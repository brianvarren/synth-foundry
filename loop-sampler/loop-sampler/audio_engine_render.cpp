/**
 * @file audio_engine_render.cpp
 * @brief Dual-voice audio rendering engine with seamless crossfading
 * 
 * Simplified architecture using two concurrent voices for clean crossfading,
 * TZFM support, and bidirectional playback.
 * 
 * Key Concepts:
 * - Q32.32 fixed-point: 32-bit integer + 32-bit fractional part for sub-sample precision
 * - Constant-power crossfading: Uses cos/sin curves to maintain consistent loudness
 * - Hardware interpolation: Leverages Pico's interpolate() for smooth sample reconstruction
 * - TZFM (Through-Zero FM): Allows negative frequencies for reverse playback
 * 
 * @author Brian Varren (rewritten)
 * @version 2.0
 * @date 2024
 */

 #include <stdint.h>
 #include <math.h>
 #include <string.h>
 #include "ADCless.h"
 #include "adc_filter.h"
 #include "audio_engine.h"
 #include "pico_interp.h"
 #include "sf_globals_bridge.h"
 #include "ui_input.h"
 #include "ladder_filter.h"
 #include <Arduino.h>
 
 // Forward declarations
 extern volatile bool g_reset_trigger_pending;
 static bool g_loop_boundaries_calculated = false;
 
 void ae_reset_loop_boundaries_flag(void) {
     g_loop_boundaries_calculated = false;
 }
 
// ── Voice Structure ──────────────────────────────────────────────────────────
// Each voice maintains its own playback state and loop boundaries
struct Voice {
    uint64_t phase_q32_32;      // Q32.32 phase accumulator - 32-bit integer + 32-bit fractional
    uint32_t loop_start;        // Loop start (samples) - where playback begins
    uint32_t loop_end;          // Loop end (samples) - where playback wraps to start
    float amplitude;            // Current amplitude (0-1) for mixing during crossfades
    bool active;                // Is this voice currently playing? (false = silent)
};
 
// ── Global State ─────────────────────────────────────────────────────────────
// Two voices for seamless crossfading - only one is "primary" at a time
static Voice voice_A = {0, 0, 0, 1.0f, true};   // Initially active
static Voice voice_B = {0, 0, 0, 0.0f, false};  // Initially silent
static Voice* primary_voice = &voice_A;         // Currently playing voice
static Voice* secondary_voice = &voice_B;       // Voice fading in during crossfade
 
// Crossfade state - tracks the transition between voices
static bool crossfading = false;                    // Are we currently crossfading?
static uint32_t crossfade_samples_total = 0;        // Total crossfade duration
static uint32_t crossfade_samples_remaining = 0;    // Samples left in current crossfade

// Pending loop parameters (calculated once per block to avoid recalculation)
static uint32_t pending_start = 0;                  // New loop start position
static uint32_t pending_end = 0;                    // New loop end position

// Zone detection state - prevents retriggering crossfade on zone entry
static bool was_in_zone_last_sample = false;
 
// Filters (mono path - applied after mixing both voices)
static Ladder8PoleLowpassFilter s_lowpass_filter;   // 8-pole ladder filter for lowpass
static SaturationEffect s_saturation_effect;        // Saturation effect for warmth and distortion

// TZFM (Through-Zero Frequency Modulation) state
static float base_ratio = 1.0f;                     // Base playback speed (1.0 = normal)
static float tzfm_depth = 0.0f;                     // FM modulation depth
static float modulator_smoothed = 0.0f;             // Smoothed modulator to prevent clicks
const float MODULATOR_SMOOTHING = 0.85f;            // One-pole smoothing coefficient
 
 // ── Helper Functions ─────────────────────────────────────────────────────────
 
// Convert Q15 signed sample to unsigned PWM value
// Q15: -32768 to +32767, PWM: 0 to PWM_RESOLUTION-1
static inline uint16_t q15_to_pwm_u(int16_t s) {
    uint32_t u = ((uint16_t)s) ^ 0x8000u;  // Convert signed to unsigned (flip MSB)
    return (uint16_t)((u * (PWM_RESOLUTION - 1u)) >> 16);  // Scale to PWM range
}
 
// Wrap phase within loop boundaries (handles both forward and reverse)
// Converts sample indices to Q32.32 for precise boundary checking
static void wrap_phase(Voice* v) {
    const int64_t start_q = ((int64_t)v->loop_start) << 32;  // Convert to Q32.32
    const int64_t end_q = ((int64_t)v->loop_end) << 32;      // Convert to Q32.32
     const int64_t span_q = end_q - start_q;  // Loop length in Q32.32
     
     if (span_q <= 0) return;  // Invalid loop boundaries
     
     int64_t phase = (int64_t)v->phase_q32_32;
     int64_t normalized = phase - start_q;  // Position relative to loop start
     
     // Modulo wrapping for both directions - handles forward and reverse playback
     if (normalized >= span_q) {
         normalized = normalized % span_q;  // Forward: wrap to start
     } else if (normalized < 0) {
         normalized = span_q - ((-normalized) % span_q);  // Reverse: wrap to end
         if (normalized == span_q) normalized = 0;  // Edge case: exactly at end
     }
     
     v->phase_q32_32 = (uint64_t)(start_q + normalized);
 }
 
// Get interpolated sample from voice using hardware interpolation
// Returns smoothly interpolated sample between two adjacent samples
static int16_t get_sample(const Voice* v, const int16_t* samples, uint32_t total_samples, bool is_reverse) {
    if (!v->active || v->amplitude <= 0.0f) return 0;  // Silent voice
    if (v->loop_end <= v->loop_start) return 0;        // Invalid loop
    
    // Extract integer sample index from Q32.32 phase
    uint32_t i = (uint32_t)(v->phase_q32_32 >> 32);

   // Special case: During crossfade, handle primary voice buffer overflow more gracefully
   // This prevents loud pops from discontinuity while still allowing fade-out
   if (crossfading && v == primary_voice) {
       if (i >= total_samples) {
           // If primary voice has significant amplitude, clamp to last sample to avoid pop
           // If amplitude is very low, allow wrap to prevent unnecessary processing
           if (v->amplitude > 0.1f) {
               i = total_samples - 1;  // Clamp to last valid sample
           } else {
               i %= total_samples;  // Safe to wrap when nearly silent
           }
       }
   } else {
       if (i >= total_samples) return 0;  // Normal case: silence if past buffer
   }
   
   // Additional safety: If we're very close to buffer end during crossfade, 
   // apply additional amplitude reduction to ensure smooth fade-out
   float additional_fade = 1.0f;  // Default: no additional fade
   if (crossfading && v == primary_voice && i >= total_samples - 8) {
       // Calculate distance from buffer end (0-7 samples)
       uint32_t distance_from_end = total_samples - 1 - i;
       // Apply additional fade factor (1.0 at distance 7, 0.0 at distance 0)
       additional_fade = (float)distance_from_end / 7.0f;
       additional_fade = fmaxf(0.0f, fminf(1.0f, additional_fade));
   }
    
    // Get second sample for interpolation (handles loop boundaries)
    uint32_t i2;
    if (is_reverse) {
        i2 = (i > v->loop_start) ? (i - 1) : (v->loop_end - 1);  // Reverse: previous sample
    } else {
        i2 = (i < v->loop_end - 1) ? (i + 1) : v->loop_start;   // Forward: next sample
    }
    
    // Extract 8-bit fractional part for interpolation (0-255)
    const uint32_t frac32 = (uint32_t)(v->phase_q32_32 & 0xFFFFFFFFull);
    const uint16_t mu8 = (uint16_t)(frac32 >> 24);  // Use upper 8 bits as interpolation weight
    
    // Convert to unsigned for hardware interpolation, then back to signed
    const uint16_t u0 = (uint16_t)((int32_t)samples[i] + 32768);   // Convert -32768..32767 to 0..65535
    const uint16_t u1 = (uint16_t)((int32_t)samples[i2] + 32768);
    const uint16_t ui = interpolate(u0, u1, mu8);  // Hardware interpolation on Pico
    int16_t sample = (int16_t)((int32_t)ui - 32768);  // Convert back to signed
    
    // Apply additional fade factor if near buffer end during crossfade
    sample = (int16_t)(sample * additional_fade);
    
    return sample;
}
 
// Calculate TZFM-modulated increment for phase accumulator
// TZFM (Through-Zero FM) allows negative frequencies for reverse playback
static int64_t calculate_increment(uint16_t adc_fm_raw, bool is_reverse) {
    // Base increment from pitch controls (in Q32.32 format)
    int64_t base_inc = (int64_t)(base_ratio * (double)(1ULL << 32));
    
    // Apply direction - negative for reverse playback
    if (is_reverse) base_inc = -base_inc;
    
    // Apply TZFM if enabled (allows through-zero modulation)
    if (tzfm_depth > 0.001f) {
        // Convert FM input to bipolar (-1 to +1) from 12-bit ADC
        float raw_mod = ((float)adc_fm_raw - 2048.0f) / 2048.0f;
        raw_mod = fmaxf(-1.0f, fminf(1.0f, raw_mod));  // Clamp to valid range
        
        // One-pole smoothing to prevent clicks from rapid FM changes
        modulator_smoothed = MODULATOR_SMOOTHING * modulator_smoothed + 
                           (1.0f - MODULATOR_SMOOTHING) * raw_mod;
        
        // Apply modulation: 1.0 + (mod * depth) allows through-zero
        float modulated = (float)base_inc * (1.0f + modulator_smoothed * tzfm_depth);
        base_inc = (int64_t)modulated;
    }
    
    // Safety limits to prevent overflow in phase accumulator
    const int64_t MAX_INC = (1LL << 37);   // Reasonable upper limit
    const int64_t MIN_INC = -(1LL << 37);  // Reasonable lower limit
    if (base_inc > MAX_INC) base_inc = MAX_INC;
    if (base_inc < MIN_INC) base_inc = MIN_INC;
    
    return base_inc;
}
 
// Check if phase is in crossfade trigger zone
// Determines when to start crossfading based on current position and crossfade length
static bool is_in_crossfade_zone(uint64_t phase, uint32_t loop_start, uint32_t loop_end, 
                                 uint32_t xfade_len, bool is_reverse) {
    uint32_t idx = (uint32_t)(phase >> 32);  // Extract sample index from Q32.32
    
    if (is_reverse) {
        // Reverse: trigger zone is at the beginning of the loop
        uint32_t zone_end = (loop_start + xfade_len < loop_end) ? 
                           (loop_start + xfade_len) : loop_end;
        return (idx >= loop_start && idx < zone_end);
    } else {
        // Forward: trigger zone is at the end of the loop
        uint32_t zone_start = (loop_end > xfade_len) ? 
                            (loop_end - xfade_len) : loop_start;
        return (idx >= zone_start && idx < loop_end);
    }
}
 
// Setup secondary voice for crossfade
// Initializes the incoming voice with new loop boundaries and position
static void setup_crossfade(uint32_t xfade_len, uint32_t xfade_samples, bool is_reverse) {
    // Secondary voice gets new loop boundaries from pending parameters
    secondary_voice->loop_start = pending_start;
    secondary_voice->loop_end = pending_end;
    secondary_voice->active = true;
    
    // Position secondary voice at the start of the new loop region
    // In reverse mode, start from the end of the loop (last sample)
    if (is_reverse) {
        secondary_voice->phase_q32_32 = ((uint64_t)(pending_end - 1)) << 32;
    } else {
        secondary_voice->phase_q32_32 = ((uint64_t)pending_start) << 32;
    }
     
     // Start crossfade
     crossfading = true;
     crossfade_samples_total = xfade_samples;
     crossfade_samples_remaining = xfade_samples;
     
     // Clear reset trigger
     g_reset_trigger_pending = false;
 }
 
// ── Main Render Function ─────────────────────────────────────────────────────
// Processes one audio block (AUDIO_BLOCK_SIZE samples) with dual-voice crossfading

void ae_render_block(const int16_t* samples,
                     uint32_t total_samples,
                     ae_state_t engine_state,
                     volatile uint64_t* io_phase_q32_32)
{
    // Early exit for silence - output center PWM value (no audio)
    if (engine_state != AE_STATE_PLAYING || !samples || total_samples < 2) {
        const uint16_t silence_pwm = PWM_RESOLUTION / 2;  // Center PWM value
        for (uint32_t i = 0; i < AUDIO_BLOCK_SIZE; ++i) {
            out_buf_ptr_L[i] = silence_pwm;
            out_buf_ptr_R[i] = silence_pwm;
        }
        return;
    }
    
    // ── Read Control Inputs ──────────────────────────────────────────────────
    // All ADC inputs are filtered except FM (needs fast response for TZFM)
    const uint16_t adc_start_q12 = adc_filter_get(ADC_LOOP_START_CH);    // Loop start position
    const uint16_t adc_len_q12 = adc_filter_get(ADC_LOOP_LEN_CH);        // Loop length
    const uint16_t adc_xfade_q12 = adc_filter_get(ADC_XFADE_LEN_CH);     // Crossfade length
    const uint16_t adc_tune_q12 = adc_filter_get(ADC_TUNE_CH);           // Fine tuning
    const uint16_t adc_tzfm_depth_q12 = adc_filter_get(ADC_TZFM_DEPTH_CH); // FM depth
    const uint16_t adc_lowpass_q12 = adc_filter_get(ADC_FX1_CH);         // Lowpass filter
    const uint16_t adc_saturation_q12 = adc_filter_get(ADC_FX2_CH);      // Saturation effect
    const uint16_t adc_fm_raw = adc_results_buf[ADC_PM_CH];  // Unfiltered for TZFM
     
    // ── Calculate Pitch Once ─────────────────────────────────────────────────
    // Convert ADC values to playback speed ratio (1.0 = normal speed)
    const uint8_t octave_pos = sf::ui_get_octave_position();
    float t_norm = ((float)adc_tune_q12 - 2048.0f) / 2048.0f;  // Convert to -1..+1
    t_norm = fmaxf(-1.0f, fminf(1.0f, t_norm));  // Clamp to valid range
    
    if (octave_pos == 0) {
        // LFO mode: very slow playback for modulation effects
        const float lfo_min = 0.001f;  // Minimum LFO speed
        const float lfo_max = 1.0f;    // Maximum LFO speed
        base_ratio = lfo_min + (1.0f - t_norm) * 0.5f * (lfo_max - lfo_min);
    } else {
        // Octave mode: musical intervals (0.5x, 1x, 2x, 4x, etc.)
        const int octave_shift = (int)octave_pos - 4;  // Center position = 1x speed
        const float octave_ratio = exp2f((float)octave_shift);  // 2^octave_shift
        const float tune_ratio = exp2f(t_norm * 0.5f);  // Fine tuning: ±50 cents
        base_ratio = octave_ratio * tune_ratio;
    }
     
    // Update TZFM depth (0.0 = no modulation, 1.0 = full depth)
    tzfm_depth = (float)adc_tzfm_depth_q12 / 4095.0f;
    
    // Get playback direction from UI state
    const ae_mode_t mode = audio_engine_get_mode();
    const bool is_reverse = (mode == AE_MODE_REVERSE);
    
    // ── Calculate Loop Boundaries ────────────────────────────────────────────
    // Lambda function to calculate new loop start/end positions from ADC values
    auto calculate_boundaries = [&]() {
        const uint32_t MIN_LOOP = 2048u;  // Minimum loop length (samples)
        const uint32_t span = (total_samples > MIN_LOOP) ? (total_samples - MIN_LOOP) : 0;
        
        // Map ADC values to sample positions within available range
        pending_start = span ? (uint32_t)(((uint64_t)adc_start_q12 * span) / 4095u) : 0u;
        uint32_t len = MIN_LOOP + (span ? (uint32_t)(((uint64_t)adc_len_q12 * span) / 4095u) : 0u);
        pending_end = pending_start + len;
        if (pending_end > total_samples) pending_end = total_samples;  // Clamp to buffer end
    };
    
    // Calculate boundaries if needed (first run or manual reset)
    if (!g_loop_boundaries_calculated || g_reset_trigger_pending) {
        calculate_boundaries();
        g_loop_boundaries_calculated = true;
        
        // Initialize primary voice if first run (cold start)
        if (primary_voice->loop_end == 0) {
            primary_voice->loop_start = pending_start;
            primary_voice->loop_end = pending_end;
            primary_voice->phase_q32_32 = ((uint64_t)pending_start) << 32;
            *io_phase_q32_32 = primary_voice->phase_q32_32;  // Sync global phase
        }
    }
     
    // ── Calculate Crossfade Length ───────────────────────────────────────────
    // Crossfade length is calculated in samples, then converted to time at current pitch
    uint32_t xfade_len = 0;
    if (primary_voice->loop_end > primary_voice->loop_start) {
        uint32_t loop_len = primary_voice->loop_end - primary_voice->loop_start;
        uint32_t max_xfade = loop_len / 2;  // Maximum crossfade is half the loop length
        xfade_len = (uint32_t)((uint64_t)max_xfade * adc_xfade_q12 >> 12);  // Scale by ADC value
        if (xfade_len < 8) xfade_len = 8;  // Minimum crossfade length
        if (xfade_len > max_xfade) xfade_len = max_xfade;  // Clamp to maximum
    }
    
   // Convert crossfade length to actual samples at current playback speed
   // This accounts for pitch changes - slower playback = longer crossfade time
   // Add safety check to prevent division by near-zero values
   float safe_ratio = fmaxf(0.0001f, fabs(base_ratio));  // Prevent near-zero division
   uint32_t xfade_samples_unclamped = (uint32_t)((((uint64_t)xfade_len) << 32) / (uint64_t)(safe_ratio * (1ULL << 32)));
   uint32_t xfade_samples = xfade_samples_unclamped;
   if (xfade_samples < 16) xfade_samples = 16;  // Minimum crossfade duration
   // Note: Upper clamp removed to allow long crossfades when needed
     
    // ── Process Audio Samples ────────────────────────────────────────────────
    // Main audio processing loop - processes one sample at a time for real-time response
    
    // Sync phase from primary voice (in case it was updated externally)
    primary_voice->phase_q32_32 = *io_phase_q32_32;
    
    for (uint32_t n = 0; n < AUDIO_BLOCK_SIZE; ++n) {
       // Calculate phase increment with TZFM modulation
       int64_t inc = calculate_increment(adc_fm_raw, is_reverse);
       
       // Get current position BEFORE advancing phase (for crossfade detection)
       uint32_t current_idx = (uint32_t)(primary_voice->phase_q32_32 >> 32);

       // Check for crossfade trigger BEFORE wrapping phase
       // This prevents premature wrapping that would interrupt crossfades
       if (!crossfading && !g_reset_trigger_pending && xfade_len > 0) {
           bool in_zone = is_in_crossfade_zone(primary_voice->phase_q32_32,
                                              primary_voice->loop_start,
                                              primary_voice->loop_end,
                                              xfade_len, is_reverse);
           
           if (in_zone && !was_in_zone_last_sample) {
               calculate_boundaries();  // Get fresh boundaries for the incoming voice
               setup_crossfade(xfade_len, xfade_samples, is_reverse);
               audio_engine_loop_led_blink();  // Visual feedback
           }
           was_in_zone_last_sample = in_zone;  // Prevent retriggering
       }

       // Advance primary voice's phase accumulator
       primary_voice->phase_q32_32 += inc;

       // Only wrap phase when not crossfading - allows primary voice to play out during fade
       if (!crossfading) {
           wrap_phase(primary_voice);
       }
        
        // Manual trigger check (user-initiated crossfade)
        if (g_reset_trigger_pending && !crossfading) {
             calculate_boundaries();
             setup_crossfade(xfade_len, xfade_samples, is_reverse);
             audio_engine_loop_led_blink();
         }
         
         // Handle crossfading between voices
         if (crossfading) {
             // Advance secondary voice with same increment as primary
             secondary_voice->phase_q32_32 += inc;
             wrap_phase(secondary_voice);  // Secondary voice always wraps normally
             
             // Calculate crossfade amplitudes using constant-power curves
             // This maintains consistent loudness during the transition
             float t = (float)(crossfade_samples_total - crossfade_samples_remaining) / 
                      (float)crossfade_samples_total;  // Progress: 0.0 to 1.0
             primary_voice->amplitude = cosf(M_PI_2 * t);    // Fade out: 1.0 → 0.0
             secondary_voice->amplitude = sinf(M_PI_2 * t);  // Fade in: 0.0 → 1.0
             
             if (--crossfade_samples_remaining == 0) {
                 // Crossfade complete - swap voices and clean up
                 crossfading = false;
                 Voice* temp = primary_voice;
                 primary_voice = secondary_voice;  // New voice becomes primary
                 secondary_voice = temp;           // Old voice becomes secondary
                 secondary_voice->active = false;  // Silence old voice
                 secondary_voice->amplitude = 0.0f;
                 primary_voice->amplitude = 1.0f;  // Full volume for new voice
                 g_loop_boundaries_calculated = false;  // Force boundary recalculation
             }
         }
         
         // Mix both voices with their current amplitudes
         // Use int32_t for accumulation to prevent overflow during crossfade
         int32_t sample = 0;
         bool is_rev_now = (inc < 0);  // Determine actual playback direction
         
         if (primary_voice->active && primary_voice->amplitude > 0.0f) {
             int16_t s = get_sample(primary_voice, samples, total_samples, is_rev_now);
             sample += (int32_t)(s * primary_voice->amplitude);
         }
         
         if (secondary_voice->active && secondary_voice->amplitude > 0.0f) {
             int16_t s = get_sample(secondary_voice, samples, total_samples, is_rev_now);
             sample += (int32_t)(s * secondary_voice->amplitude);
         }
         
         // Clamp accumulated sample to prevent int16_t overflow
         if (sample > 32767) sample = 32767;
         if (sample < -32768) sample = -32768;
         int16_t sample_clamped = (int16_t)sample;
         
        // Apply effects (mono path - both channels get same processed signal)
        // Apply saturation first to add harmonics, then lowpass to shape them
        uint16_t sat_coeff = adc_to_ladder_coefficient(adc_saturation_q12);
        sample_clamped = s_saturation_effect.process(sample_clamped, sat_coeff);
        
        uint16_t lp_coeff = adc_to_ladder_coefficient(adc_lowpass_q12);
        sample_clamped = s_lowpass_filter.process(sample_clamped, lp_coeff);
        
        // Convert final sample to PWM and output to both channels
        uint16_t pwm = q15_to_pwm_u(sample_clamped);
        out_buf_ptr_L[n] = pwm;  // Left channel
        out_buf_ptr_R[n] = pwm;  // Right channel (mono)
    }
    
    // Update global phase for external access (UI, etc.)
    *io_phase_q32_32 = primary_voice->phase_q32_32;
    
    // ── Update Display State ─────────────────────────────────────────────────
    // Prepare visualization data for the UI display
    uint32_t vis_primary = (uint32_t)(primary_voice->phase_q32_32 >> 32);
    uint32_t vis_secondary = 0;
    uint8_t vis_xfading = 0;
    
    if (crossfading) {
        vis_xfading = 1;  // Show crossfade indicator
        vis_secondary = (uint32_t)(secondary_voice->phase_q32_32 >> 32);
    }
    
    // Convert loop boundaries to 12-bit values for display scaling
    const uint16_t start_q12 = (uint16_t)(((uint64_t)primary_voice->loop_start * 4095u) / total_samples);
    const uint16_t len_q12 = (uint16_t)(((uint64_t)(primary_voice->loop_end - primary_voice->loop_start) * 4095u) / total_samples);
     
     publish_display_state2(start_q12, len_q12, vis_primary, total_samples, vis_xfading, vis_secondary);
 }
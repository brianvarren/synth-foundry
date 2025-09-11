/**
 * @file audio_engine_render.cpp
 * @brief Real-time audio rendering engine - the heart of the loop sampler
 * 
 * This file contains the most complex and critical part of the audio engine:
 * the real-time sample rendering loop. It processes audio samples asynchronously
 * as fast as possible with advanced features like seamless crossfading, pitch 
 * shifting, through-zero FM, and real-time loop parameter changes.
 * 
 * ## Key Features
 * 
 * **Real-time Crossfading**: When loop parameters change during playback,
 * the engine performs seamless constant-power crossfades between the old and 
 * new loop regions to prevent audio glitches and maintain consistent volume
 * levels. This is essential for live performance.
 * 
 * **Pitch Control**: Supports both octave switching (via rotary switch) and
 * fine tuning (via ADC knob). Special LFO mode provides ultra-slow playback
 * for creating evolving textures.
 * 
 * **Through-Zero FM**: Frequency modulation with capability to reverse playback
 * direction when modulation depth exceeds carrier frequency. Creates complex
 * timbres and metallic sounds.
 * 
 * **Loop Manipulation**: Real-time adjustment of loop start/end points while
 * playing, with automatic crossfading to maintain audio continuity.
 * 
 * **Hybrid Phase Tracking**: Uses float internally for FM precision while
 * maintaining Q32.32 interface for compatibility.
 * 
 * ## Performance Considerations
 * 
 * This code runs in the audio interrupt context and must complete within
 * the audio buffer period (typically 64 samples). The actual output rate
 * is determined by the PWM/DMA system's audio_rate setting.
 * All operations are optimized for speed and use hardware interpolation
 * where possible.
 * 
 * @author Brian Varren
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
 
 // Forward declaration for reset trigger handling
 extern volatile bool g_reset_trigger_pending;
 
 // Forward declaration for the global base increment
 extern uint64_t g_inc_base_q32_32;
 
 // Local render state (persists across calls)
 static const uint32_t MIN_LOOP_LEN_CONST = 64;  // Fixed minimum loop length
 static uint32_t g_span_start    = 0;      // = total - MIN_LOOP_LEN_CONST (precomputed)
 static uint32_t g_span_len      = 0;      // = total - MIN_LOOP_LEN_CONST (same span)
 
 static inline void loop_mapper_recalc_spans(uint32_t total) {
   uint32_t minlen = (MIN_LOOP_LEN_CONST < total) ? MIN_LOOP_LEN_CONST : (total ? total : 1);
   g_span_start = (total > minlen) ? (total - minlen) : 0;
   g_span_len   = (total > minlen) ? (total - minlen) : 0;
 }
 
 static inline uint16_t q15_to_pwm_u(int16_t s) {
   uint32_t u = ((uint16_t)s) ^ 0x8000u;        // offset-binary 0..65535
   return (uint16_t)((u * (PWM_RESOLUTION - 1u)) >> 16);
 }
 
 // ── Crossfade and Active Region State ────────────────────────────────────────
 // These variables manage the complex crossfading system that allows seamless
 // transitions when loop parameters change during playback.
 
 static uint32_t s_active_start = 0;    // Current loop start position (sample index)
 static uint32_t s_active_end   = 0;    // Current loop end position (sample index)
 
 // Crossfade state variables (cf = crossfade) - now using float for FM compatibility
 static uint32_t s_cf_steps_rem   = 0;  // Remaining crossfade steps
 static uint32_t s_cf_steps_total = 0;  // Total crossfade steps
 static float    s_cf_tail_phase  = 0.0f;  // Tail phase (old loop region)
 static float    s_cf_head_phase  = 0.0f;  // Head phase (new loop region)
 static float    s_cf_tail_inc    = 0.0f;  // Tail increment
 static float    s_cf_head_inc    = 0.0f;  // Head increment
 static uint32_t s_cf_tail_start  = 0;  // Tail region start
 static uint32_t s_cf_tail_end    = 0;  // Tail region end
 static uint32_t s_cf_head_start  = 0;  // Head region start
 static uint32_t s_cf_head_end    = 0;  // Head region end
 
 // ── 8-Pole Ladder Filters ──────────────────────────────────────────────────────
 // 8-pole ladder lowpass and highpass filters controlled by ADC5 and ADC6
  static Ladder8PoleBandpassFilter s_bandpass_filter;
 
 /**
  * @brief Hardware-accelerated sample interpolation with bi-directional support
  * 
  * Converts a floating-point phase value to an interpolated Q15 sample.
  * Handles both forward and reverse playback, bounds checking, and uses
  * hardware interpolation for smooth output.
  */
 static int16_t sample_from_float_phase(float phase, float inc, 
                                        uint32_t start, uint32_t end,
                                        const int16_t* samples) {
     if (end <= start || !samples) return 0;
     
     // Get integer and fractional parts
     int32_t idx = (int32_t)floorf(phase);
     float frac = phase - (float)idx;
     
     // Wrap index into loop region (handles negative for reverse playback)
     int32_t loop_len = (int32_t)(end - start);
     if (loop_len <= 0) return 0;
     
     // Modulo wrap for both positive and negative indices
     idx = idx - (int32_t)start;
     idx = ((idx % loop_len) + loop_len) % loop_len;
     idx = idx + (int32_t)start;
     
     // Ensure valid bounds
     if (idx < (int32_t)start) idx = start;
     if (idx >= (int32_t)end) idx = end - 1;
     
     // Get samples for interpolation
     uint32_t i0 = (uint32_t)idx;
     uint32_t i1 = (i0 + 1 < end) ? (i0 + 1) : start;  // Wrap at loop boundary
     
     // Convert Q15 samples to unsigned for hardware interpolation
     const uint16_t u0 = (uint16_t)((int32_t)samples[i0] + 32768);
     const uint16_t u1 = (uint16_t)((int32_t)samples[i1] + 32768);
     
     // Use hardware interpolation (0-255 scale)
     const uint16_t mu8 = (uint16_t)(frac * 255.0f);
     const uint16_t ui = interpolate(u0, u1, mu8);
     
     // Convert back to signed Q15 format
     return (int16_t)((int32_t)ui - 32768);
 }
 
 /**
  * @brief Main audio rendering function - processes one audio block in real-time
  * 
  * This is the heart of the audio engine, called from the audio interrupt.
  * It processes AUDIO_BLOCK_SIZE samples (typically 64) and handles:
  * - Sample playback with pitch control and through-zero FM
  * - Real-time loop parameter changes
  * - Seamless crossfading between loop regions
  * - Display state updates for the UI
  * 
  * @param samples Pointer to Q15 sample data in PSRAM
  * @param total_samples Total number of samples in the buffer
  * @param engine_state Current playback state (PLAYING, PAUSED, etc.)
  * @param io_phase_q32_32 Pointer to the phase accumulator (Q32.32 format)
  */
 void ae_render_block(const int16_t* samples,
                      uint32_t total_samples,
                      ae_state_t engine_state,
                      volatile int64_t* io_phase_q32_32)
 {
   // Early exit if not playing or no valid sample data
   if (engine_state != AE_STATE_PLAYING || !samples || total_samples < 2) {
     // Output silence and update display with current loop state
     // Use PWM midpoint for silence to prevent pops (not 0!)
     const uint16_t silence_pwm = PWM_RESOLUTION / 2;
     for (uint32_t i = 0; i < AUDIO_BLOCK_SIZE; ++i) {
       out_buf_ptr_L[i] = silence_pwm;
       out_buf_ptr_R[i] = silence_pwm;
     }
 
     const uint32_t total = total_samples ? total_samples : 1u;
     const uint16_t start_q12 = (uint16_t)(((uint64_t)s_active_start * 4095u) / (uint64_t)total);
     const uint16_t len_q12   = (uint16_t)(((uint64_t)((s_active_end > s_active_start) ? (s_active_end - s_active_start) : 0) * 4095u) / (uint64_t)total);
     publish_display_state2(start_q12, len_q12, 0, total, 0, 0);
     return;
   }
 
   // ── Convert Q32.32 phase to float for internal processing ─────────────────
   float phase = (float)(*io_phase_q32_32 / (double)(1ULL << 32));
 
   // ── Handle Reset Trigger ──────────────────────────────────────────────────────
   // Check for pending reset trigger and handle it if present
   if (g_reset_trigger_pending) {
     // Trigger LED blink on reset trigger
     audio_engine_loop_led_blink();
     
     // Force ADC input re-read/update
     adc_filter_update_from_dma();
     
     // Get current ADC values for loop parameters
     const uint16_t adc_start_q12 = adc_filter_get(ADC_LOOP_START_CH);
     const uint16_t adc_len_q12   = adc_filter_get(ADC_LOOP_LEN_CH);
     const uint16_t adc_xfade_q12 = adc_filter_get(ADC_XFADE_LEN_CH);
     
     // Recalculate loop region based on current ADC values
     const uint32_t MIN_LOOP_LEN = 64u;
     const uint32_t span_total = (total_samples > MIN_LOOP_LEN) ? (total_samples - MIN_LOOP_LEN) : 0;
     const uint32_t new_start = (span_total ? (uint32_t)(((uint64_t)adc_start_q12 * (uint64_t)span_total) / 4095u) : 0u);
     const uint32_t new_len = MIN_LOOP_LEN + (span_total ? (uint32_t)(((uint64_t)adc_len_q12 * (uint64_t)span_total) / 4095u) : 0u);
     uint32_t new_end = new_start + new_len;
     if (new_end > total_samples) new_end = total_samples;
     
     // Calculate crossfade length from XFADE knob - absolute value, not percentage
     uint32_t xfade_len = 0;
     if (adc_xfade_q12 > 0) {
       // Map XFADE knob (0-4095) to absolute crossfade length (0-1024 samples)
       // This gives about 0-23ms crossfade at 44.1kHz, independent of loop length
       const uint32_t MAX_XFADE_SAMPLES = 1024u;  // Maximum crossfade length in samples
       xfade_len = (uint32_t)(((uint64_t)MAX_XFADE_SAMPLES * (uint64_t)adc_xfade_q12) >> 12);
       if (xfade_len == 0) xfade_len = 1;  // Minimum 1 sample when knob is turned up
     }
     
     // Apply minimum crossfade length only when XFADE knob is at zero
     if (xfade_len == 0) {
       const uint32_t MIN_XF_SAMPLES = 64u;  // Minimum crossfade when knob is off
       xfade_len = MIN_XF_SAMPLES;
     }
     
     // Store current and new loop region boundaries for crossfade
     const uint32_t old_start = s_active_start;  // Current loop start
     const uint32_t old_end   = s_active_end;    // Current loop end
     
     // Update active loop region to new region
     s_active_start = new_start;
     s_active_end   = new_end;
     
     // If crossfade is specified, set up crossfade state for smooth transition
     if (xfade_len > 0) {
       // Get current playback position for "right now" crossfade
       uint32_t current_pos = (uint32_t)phase;
       
       // Elegant approach: crossfade from current playhead to loop start
       // This creates a seamless transition regardless of loop length or timing
       s_cf_tail_start = current_pos;  // Start from current position (fading out)
       s_cf_tail_end = current_pos + xfade_len;  // End after crossfade length (truly independent!)
       s_cf_head_start = new_start;    // Start from new loop start (fading in)
       s_cf_head_end = new_end;        // End at new loop end
       
       // Start both playheads from their respective start positions
       s_cf_tail_phase = phase;  // Current playhead position (continues forward)
       s_cf_head_phase = (float)new_start;  // New loop start position
       
       // Calculate number of crossfade steps based on crossfade length and current pitch
       const uint16_t adc_tune_q12 = adc_filter_get(ADC_TUNE_CH);
       const uint8_t octave_pos = sf::ui_get_octave_position();
       float t_norm = ((float)adc_tune_q12 - 2048.0f) / 2048.0f;
       if (t_norm < -1.0f) t_norm = -1.0f; if (t_norm > 1.0f) t_norm = 1.0f;
       
       float ratio_f;
       if (octave_pos == 0) {
         const float lfo_min_ratio = 0.001f;
         const float lfo_max_ratio = 1.0f;
         ratio_f = lfo_min_ratio + (1.0f - t_norm) * 0.5f * (lfo_max_ratio - lfo_min_ratio);
       } else {
         const int octave_shift = (int)octave_pos - 4;
         const float octave_ratio = exp2f((float)octave_shift);
         const float tune_ratio = exp2f(t_norm * 0.5f);
         ratio_f = octave_ratio * tune_ratio;
       }
       
       // Store increment for crossfade (will be recalculated with FM later)
       s_cf_tail_inc = ratio_f;
       s_cf_head_inc = ratio_f;
       
       uint32_t steps = xfade_len;  // Simplified: one step per sample
       if (steps == 0) steps = 1;
       
       s_cf_steps_total = steps;
       s_cf_steps_rem   = steps;
       
       // Clear the pending flag
       g_reset_trigger_pending = false;
       
       // Reset phase to start of new region
       phase = (float)new_start;
       
       // Continue with crossfade processing in the main loop below
     } else {
       // No crossfade - immediate reset
       phase = (float)new_start;
       s_cf_steps_rem = 0;  // Clear any existing crossfade
       g_reset_trigger_pending = false;
     }
   }
 
   loop_mapper_recalc_spans(total_samples);
 
   // ── Read Control Inputs (Filtered) ──────────────────────────────────────────
   // These ADC values are filtered to prevent audio artifacts from knob jitter
   const uint16_t adc_start_q12 = adc_filter_get(ADC_LOOP_START_CH);   // Loop start: 0..4095
   const uint16_t adc_len_q12   = adc_filter_get(ADC_LOOP_LEN_CH);     // Loop length: 0..4095
   const uint16_t adc_xfade_q12 = adc_filter_get(ADC_XFADE_LEN_CH);    // Crossfade length: 0..4095
   const uint16_t adc_tune_q12  = adc_filter_get(ADC_TUNE_CH);         // Pitch fine tune: 0..4095
   const uint16_t adc_fm_q12    = adc_filter_get(ADC_FM_CH);           // FM depth: 0..4095
   const uint16_t adc_pm_q12    = adc_results_buf[ADC_PM_CH];          // PM modulator (unfiltered for speed): 0..4095
   const uint16_t adc_lowpass_q12 = adc_filter_get(ADC_FX1_CH);        // Lowpass filter: 0..4095
   const uint16_t adc_highpass_q12 = adc_filter_get(ADC_FX2_CH);       // Highpass filter: 0..4095
   
 
   // ── Use Fixed Minimum Loop Length ────────────────────────────────────────────
   // Use a consistent fixed minimum loop length to ensure reticle positions
   // match actual loop bounds. This prevents the mismatch between what the user
   // sees (reticle) and what actually plays (loop bounds).
   const uint32_t MIN_LOOP_LEN = 64u;  // Fixed minimum: 64 samples
   
   const uint32_t span_total    = (total_samples > MIN_LOOP_LEN) ? (total_samples - MIN_LOOP_LEN) : 0;
   const uint32_t pending_start = (span_total ? (uint32_t)(((uint64_t)adc_start_q12 * (uint64_t)span_total) / 4095u) : 0u);
   const uint32_t pending_len   = MIN_LOOP_LEN + (span_total ? (uint32_t)(((uint64_t)adc_len_q12 * (uint64_t)span_total) / 4095u) : 0u);
   uint32_t pending_end = pending_start + pending_len;
   if (pending_end > total_samples) pending_end = total_samples;
 
   if (s_active_end <= s_active_start || s_active_end > total_samples) {
     s_active_start = pending_start;
     s_active_end   = pending_end;
 
     if (phase < (float)s_active_start || phase >= (float)s_active_end) {
       phase = (float)s_active_start;
     }
     s_cf_steps_rem = 0;
   }
 
   // ── Calculate Base Pitch Ratio ────────────────────────────────────────────────
   // The pitch control system combines two inputs:
   // 1. Octave switch (8-position rotary switch)
   // 2. Fine tune knob (ADC input)
   
   // Get octave switch position (0-7, where 0 = special LFO mode)
   const uint8_t octave_pos = sf::ui_get_octave_position();
   
   // Normalize tune knob to [-1, +1] range (centered at 2048)
   float t_norm = ((float)adc_tune_q12 - 2048.0f) / 2048.0f;
   if (t_norm < -1.0f) t_norm = -1.0f; if (t_norm > 1.0f) t_norm = 1.0f;
   
   float base_ratio;  // Base pitch ratio before FM
   
   if (octave_pos == 0) {
     // ── LFO Mode: Ultra-slow playback for evolving textures ──────────────────
     // Position 0 is special - it enables LFO mode for creating slowly evolving
     // textures. The tune knob controls the playback rate from painfully slow
     // to near-normal speed.
     const float lfo_min_ratio = 0.001f;  // ~20 minutes for a 15-second sample
     const float lfo_max_ratio = 1.0f;    // Near-normal playback speed
     base_ratio = lfo_min_ratio + (1.0f - t_norm) * 0.5f * (lfo_max_ratio - lfo_min_ratio);
   } else {
     // ── Octave Mode: Musical pitch control ───────────────────────────────────
     // Positions 1-7 map to -3 to +3 octaves (musical intervals)
     const int octave_shift = (int)octave_pos - 4;  // -3 to +3 octaves
     const float octave_ratio = exp2f((float)octave_shift);  // 0.125 to 8.0
     
     // Fine tune knob provides ±0.5 semitone adjustment on top of octave shift
     const float tune_ratio = exp2f(t_norm * 0.5f);  // ~0.7 to ~1.4
     base_ratio = octave_ratio * tune_ratio;
   }
   
   // ── Through-Zero FM Processing ────────────────────────────────────────────────
   // Normalize modulator to [-1, +1] range
   float modulator = ((float)adc_pm_q12 - 2048.0f) / 2048.0f;
   modulator = fmaxf(-1.0f, fminf(1.0f, modulator));
   
   // FM Depth: 0 to 2.0 for through-zero capability
   float fm_depth = ((float)adc_fm_q12 / 4095.0f) * 2.0f;
   
   // Apply FM: rate = base_rate * (1 + depth * modulator)
   // When depth * modulator < -1, the rate goes negative (reverse playback)
   float fm_factor = 1.0f + (fm_depth * modulator);
   float INC = base_ratio * fm_factor;
   
   // Apply safety limits to prevent extreme playback rates
   if (INC < -16.0f) INC = -16.0f;  // Maximum reverse speed
   if (INC > 16.0f) INC = 16.0f;    // Maximum forward speed
   
   // Store in Q32.32 for compatibility (if needed elsewhere)
   g_inc_base_q32_32 = (uint64_t)(fabsf(base_ratio) * (double)(1ULL << 32));
 
   // ── Calculate Crossfade Length ────────────────────────────────────────────────
   // The crossfade length determines how smoothly we transition between loop regions
   const uint32_t active_len = s_active_end - s_active_start;
   uint32_t xfade_len_user = 0;
   
   if (active_len > 1 && adc_xfade_q12 > 0) {
     // User-controlled crossfade: map knob (0-4095) to 0-50% of loop length
     // This gives musical control over crossfade length
     const uint32_t max_xfade_samples = active_len / 2;  // 50% of loop length
     xfade_len_user = (uint32_t)(((uint64_t)max_xfade_samples * (uint64_t)adc_xfade_q12) >> 12);
     if (xfade_len_user >= active_len) xfade_len_user = active_len - 1;
   }
   
   // Apply minimum crossfade length to prevent audio clicks
   const uint32_t MIN_XF_SAMPLES = 8u;
   uint32_t xfade_len = (xfade_len_user > 0) ? xfade_len_user
                        : ((active_len > MIN_XF_SAMPLES) ? MIN_XF_SAMPLES
                                                         : (active_len > 1 ? 1u : 0u));
 
   // ── Main Audio Processing Loop ─────────────────────────────────────────────────
   // Process each sample in the audio block (AUDIO_BLOCK_SIZE = 16 samples)
   for (uint32_t n = 0; n < AUDIO_BLOCK_SIZE; ++n) {
     
     // ── Crossfade Processing ──────────────────────────────────────────────────────
     // If we're currently crossfading between loop regions, handle the crossfade
     if (s_cf_steps_rem) {
       // Calculate crossfade progress (0 to N-1)
       const uint32_t k = s_cf_steps_total - s_cf_steps_rem;  // Current step
       const uint32_t N = s_cf_steps_total;                   // Total steps
       
       // Get samples from both the old (tail) and new (head) loop regions
       const int16_t a_q15 = sample_from_float_phase(s_cf_tail_phase, s_cf_tail_inc,
                                                     s_cf_tail_start, s_cf_tail_end, samples);
       const int16_t b_q15 = sample_from_float_phase(s_cf_head_phase, s_cf_head_inc,
                                                     s_cf_head_start, s_cf_head_end, samples);
       
       // Mix the two samples with constant power crossfade
       // As k increases, we fade from 'a' (old) to 'b' (new)
       // Constant power crossfade uses sine curves to maintain constant power
       const float t = (float)(k + 1u) / (float)N;  // Crossfade progress 0 to 1
       const float fade_out = sinf(M_PI_2 * (1.0f - t));  // Old sample gain
       const float fade_in = sinf(M_PI_2 * t);            // New sample gain
       
       int64_t mix_num = (int64_t)((float)a_q15 * fade_out)  // Old sample weight
                             + (int64_t)((float)b_q15 * fade_in);  // New sample weight
       int16_t crossfade_q15 = (int16_t)mix_num;
       
       // Apply 8-pole ladder filters to crossfade sample
        uint16_t cutoff_coeff = adc_to_bandpass_cutoff(adc_lowpass_q12);
        uint16_t q_coeff = adc_to_bandpass_q(adc_highpass_q12);
        crossfade_q15 = s_bandpass_filter.process(crossfade_q15, cutoff_coeff, q_coeff);
       
       const uint16_t crossfade_pwm = q15_to_pwm_u(crossfade_q15);
       out_buf_ptr_L[n] = crossfade_pwm;
       out_buf_ptr_R[n] = crossfade_pwm;
       
       // Update both crossfade phases with current FM-modulated increment
       s_cf_tail_phase += INC;
       s_cf_head_phase += INC;
       
       // Wrap crossfade phases within their regions
       float tail_len = (float)(s_cf_tail_end - s_cf_tail_start);
       float head_len = (float)(s_cf_head_end - s_cf_head_start);
       
       if (tail_len > 0) {
         while (s_cf_tail_phase >= (float)s_cf_tail_end) s_cf_tail_phase -= tail_len;
         while (s_cf_tail_phase < (float)s_cf_tail_start) s_cf_tail_phase += tail_len;
       }
       if (head_len > 0) {
         while (s_cf_head_phase >= (float)s_cf_head_end) s_cf_head_phase -= head_len;
         while (s_cf_head_phase < (float)s_cf_head_start) s_cf_head_phase += head_len;
       }
       
       // Check if crossfade is complete
       if (--s_cf_steps_rem == 0) { 
         phase = s_cf_head_phase;  // Switch to new region
       }
       continue;  // Skip normal playback processing
     }
 
     // ── Normal Playback Processing ──────────────────────────────────────────────────
     // Advance phase with FM-modulated increment
     phase += INC;
     
     // Check for loop boundary crossing (bi-directional)
     const float loop_start_f = (float)s_active_start;
     const float loop_end_f = (float)s_active_end;
     const float pre_end_f = (float)((xfade_len < active_len) 
                                     ? (s_active_end - xfade_len) 
                                     : (s_active_start + 1));
     
     bool needs_wrap = false;
     bool start_xfade = false;
     
     // Forward playback boundary check
     if (INC > 0) {
       if (xfade_len > 0 && phase >= pre_end_f && (phase - INC) < pre_end_f) {
         start_xfade = true;
       } else if (phase >= loop_end_f) {
         needs_wrap = true;
       }
     }
     // Reverse playback boundary check
     else if (INC < 0) {
       if (phase < loop_start_f) {
         needs_wrap = true;
       }
     }
 
     // ── Crossfade Initiation ────────────────────────────────────────────────────────
     // We've reached the crossfade start point - begin transitioning to new loop region
     if (start_xfade) {
       // Trigger LED blink on crossfade initiation (loop wrap with crossfade)
       audio_engine_loop_led_blink();
       
       // Store current and new loop region boundaries
       const uint32_t old_start = s_active_start;  // Current loop start
       const uint32_t old_end   = s_active_end;    // Current loop end
       const uint32_t new_start = pending_start;   // New loop start
       const uint32_t new_end   = pending_end;     // New loop end
       const uint32_t new_len   = (new_end > new_start) ? (new_end - new_start) : 0;
       
       // Calculate actual crossfade length (may be limited by new loop size)
       uint32_t xf_len = xfade_len;
       if (new_len > 0 && xf_len >= new_len) xf_len = new_len - 1;
       if (xf_len == 0) xf_len = 1;  // Minimum 1 sample crossfade
       
       // Update active loop region to new region
       s_active_start = new_start;
       s_active_end   = new_end;
       
       // Set up crossfade state for seamless transition
       s_cf_tail_start = old_start; s_cf_tail_end = old_end;  // Old region (fading out)
       s_cf_head_start = new_start; s_cf_head_end = new_end;  // New region (fading in)
       s_cf_tail_phase = (float)(old_end - xf_len);  // Start from end of old region
       s_cf_head_phase = (float)new_start;           // Start from beginning of new region
       s_cf_tail_inc = INC;  // Use current FM-modulated increment
       s_cf_head_inc = INC;
       
       // Calculate number of crossfade steps
       uint32_t steps = xf_len;
       if (steps == 0) steps = 1;  // Minimum 1 step
       s_cf_steps_total = steps;
       s_cf_steps_rem   = steps;
       
       // Generate first crossfade sample (mix of old and new regions)
       const int16_t a0 = sample_from_float_phase(s_cf_tail_phase, s_cf_tail_inc,
                                                  s_cf_tail_start, s_cf_tail_end, samples);
       const int16_t b0 = sample_from_float_phase(s_cf_head_phase, s_cf_head_inc,
                                                  s_cf_head_start, s_cf_head_end, samples);
       
       // Constant power crossfade for first sample
       const float t0 = 1.0f / (float)steps;  // First sample progress
       const float fade_out0 = sinf(M_PI_2 * (1.0f - t0));  // Old sample gain
       const float fade_in0 = sinf(M_PI_2 * t0);             // New sample gain
       
       int64_t num0 = (int64_t)((float)a0 * fade_out0) + (int64_t)((float)b0 * fade_in0);
       int16_t crossfade_q15 = (int16_t)num0;
       
       // Apply 8-pole ladder filters to crossfade sample
        uint16_t cutoff_coeff = adc_to_bandpass_cutoff(adc_lowpass_q12);
        uint16_t q_coeff = adc_to_bandpass_q(adc_highpass_q12);
        crossfade_q15 = s_bandpass_filter.process(crossfade_q15, cutoff_coeff, q_coeff);
       
       const uint16_t crossfade_pwm = q15_to_pwm_u(crossfade_q15);
       out_buf_ptr_L[n] = crossfade_pwm;
       out_buf_ptr_R[n] = crossfade_pwm;
       
       s_cf_tail_phase += INC; 
       s_cf_head_phase += INC;
       
       // Wrap phases
       float tail_len = (float)(s_cf_tail_end - s_cf_tail_start);
       float head_len = (float)(s_cf_head_end - s_cf_head_start);
       if (tail_len > 0) {
         while (s_cf_tail_phase >= (float)s_cf_tail_end) s_cf_tail_phase -= tail_len;
         while (s_cf_tail_phase < (float)s_cf_tail_start) s_cf_tail_phase += tail_len;
       }
       if (head_len > 0) {
         while (s_cf_head_phase >= (float)s_cf_head_end) s_cf_head_phase -= head_len;
         while (s_cf_head_phase < (float)s_cf_head_start) s_cf_head_phase += head_len;
       }
       
       // Check if crossfade completed in this single sample (rare but possible)
       if (--s_cf_steps_rem == 0) { 
         phase = s_cf_head_phase;  // Switch to new region
       }
       continue;  // Skip normal playback processing
     }
 
     // ── Handle Loop Wrapping ────────────────────────────────────────────────────────
     if (needs_wrap) {
       // Trigger LED blink on loop wrap
       audio_engine_loop_led_blink();
       
       // Store current and new loop region boundaries
       const uint32_t old_start = s_active_start;  // Current loop start
       const uint32_t old_end   = s_active_end;    // Current loop end
       const uint32_t new_start = pending_start;   // New loop start
       const uint32_t new_end   = pending_end;     // New loop end
       
       // Wrap phase back into loop
       float loop_len = loop_end_f - loop_start_f;
       if (loop_len > 0) {
         while (phase >= loop_end_f) phase -= loop_len;
         while (phase < loop_start_f) phase += loop_len;
       }
       
       // Use minimum crossfade length for loop end transitions
       uint32_t xf_len = (active_len > MIN_XF_SAMPLES) ? MIN_XF_SAMPLES : (active_len > 1 ? 1u : 0u);
       
       // Update active loop region to new region
       s_active_start = new_start; 
       s_active_end = new_end;
       
       // If we have a crossfade length, set up crossfade state
       if (xf_len > 0 && (old_start != new_start || old_end != new_end)) {
         // Set up crossfade between old and new regions
         s_cf_tail_start = old_start; s_cf_tail_end = old_end;  // Old region (fading out)
         s_cf_head_start = new_start; s_cf_head_end = new_end;  // New region (fading in)
         s_cf_tail_phase = (float)(old_end - xf_len);  // Start from end of old region
         s_cf_head_phase = (float)new_start;           // Start from beginning of new region
         s_cf_tail_inc = INC;  // Use current FM-modulated increment
         s_cf_head_inc = INC;
         
         // Calculate crossfade steps
         uint32_t steps = xf_len;
         if (steps == 0) steps = 1;  // Minimum 1 step
         s_cf_steps_total = steps;
         s_cf_steps_rem   = steps;
         
         // Generate first crossfade sample
         const int16_t a0 = sample_from_float_phase(s_cf_tail_phase, s_cf_tail_inc,
                                                    s_cf_tail_start, s_cf_tail_end, samples);
         const int16_t b0 = sample_from_float_phase(s_cf_head_phase, s_cf_head_inc,
                                                    s_cf_head_start, s_cf_head_end, samples);
         
         // Constant power crossfade for first sample
         const float t0 = 1.0f / (float)steps;  // First sample progress
         const float fade_out0 = sinf(M_PI_2 * (1.0f - t0));  // Old sample gain
         const float fade_in0 = sinf(M_PI_2 * t0);             // New sample gain
         
         int64_t num0 = (int64_t)((float)a0 * fade_out0) + (int64_t)((float)b0 * fade_in0);
         int16_t crossfade_q15 = (int16_t)num0;
         
         // Apply 8-pole ladder filters to crossfade sample
        uint16_t cutoff_coeff = adc_to_bandpass_cutoff(adc_lowpass_q12);
        uint16_t q_coeff = adc_to_bandpass_q(adc_highpass_q12);
        crossfade_q15 = s_bandpass_filter.process(crossfade_q15, cutoff_coeff, q_coeff);
         
         const uint16_t crossfade_pwm = q15_to_pwm_u(crossfade_q15);
         out_buf_ptr_L[n] = crossfade_pwm;
         out_buf_ptr_R[n] = crossfade_pwm;
         
         s_cf_tail_phase += INC; 
         s_cf_head_phase += INC;
         
         // Wrap phases
         float tail_len = (float)(s_cf_tail_end - s_cf_tail_start);
         float head_len = (float)(s_cf_head_end - s_cf_head_start);
         if (tail_len > 0) {
           while (s_cf_tail_phase >= (float)s_cf_tail_end) s_cf_tail_phase -= tail_len;
           while (s_cf_tail_phase < (float)s_cf_tail_start) s_cf_tail_phase += tail_len;
         }
         if (head_len > 0) {
           while (s_cf_head_phase >= (float)s_cf_head_end) s_cf_head_phase -= head_len;
           while (s_cf_head_phase < (float)s_cf_head_start) s_cf_head_phase += head_len;
         }
         
         // Check if crossfade completed
         if (--s_cf_steps_rem == 0) { 
           phase = s_cf_head_phase;  // Switch to new region
         }
         continue;  // Skip normal playback processing
       } else {
         // Simple wrap without crossfade
         phase = (float)new_start;
       }
     }
 
     // ── Normal Sample Playback ──────────────────────────────────────────────────────
     int16_t playback_q15 = sample_from_float_phase(phase, INC,
                                                    s_active_start, s_active_end, samples);
     
     // ── Apply 8-Pole Ladder Filters ──────────────────────────────────────────────
     // Apply lowpass and highpass filters controlled by ADC5 and ADC6
    uint16_t cutoff_coeff = adc_to_bandpass_cutoff(adc_lowpass_q12);
    uint16_t q_coeff = adc_to_bandpass_q(adc_highpass_q12);
    playback_q15 = s_bandpass_filter.process(playback_q15, cutoff_coeff, q_coeff);
     
     const uint16_t playback_pwm = q15_to_pwm_u(playback_q15);
     out_buf_ptr_L[n] = playback_pwm;
     out_buf_ptr_R[n] = playback_pwm;
   }
 
   // ── Update Global Phase State ────────────────────────────────────────────────────────
   // Convert float phase back to Q32.32 for external interface
   *io_phase_q32_32 = (uint64_t)(phase * (double)(1ULL << 32));
 
   // ── Update Display State ──────────────────────────────────────────────────────────────
   // Prepare visualization data for the display system (Core 1)
   uint32_t vis_primary_idx   = (uint32_t)phase;  // Main playhead position
   uint8_t  vis_xfade_active  = (s_cf_steps_rem != 0) ? 1u : 0u;  // Crossfade status
   uint32_t vis_secondary_idx = 0;  // Secondary playhead (during crossfade)
   
   // During crossfade, show both playheads for visual feedback
   if (vis_xfade_active) {
     vis_primary_idx   = (uint32_t)s_cf_head_phase;    // New region playhead
     vis_secondary_idx = (uint32_t)s_cf_tail_phase;    // Old region playhead
   }
 
   const uint16_t start_q12 = (uint16_t)(((uint64_t)s_active_start * 4095u) / (uint64_t)total_samples);
   const uint16_t len_q12   = (uint16_t)(((uint64_t)((s_active_end > s_active_start) ? (s_active_end - s_active_start) : 0) * 4095u) / (uint64_t)total_samples);
   
   // Publish state to display system for real-time visualization
   publish_display_state2(start_q12, len_q12,
                          vis_primary_idx, total_samples,
                          vis_xfade_active, vis_secondary_idx);
 }
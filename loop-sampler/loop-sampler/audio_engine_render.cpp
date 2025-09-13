/**
 * @file audio_engine_render.cpp
 * @brief Real-time audio rendering engine - the heart of the loop sampler
 * 
 * This file contains the most complex and critical part of the audio engine:
 * the real-time sample rendering loop. It processes audio samples asynchronously
 * as fast as possible with advanced features like seamless crossfading, pitch 
 * shifting, and real-time loop parameter changes.
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
 * **Loop Manipulation**: Real-time adjustment of loop start/end points while
 * playing, with automatic crossfading to maintain audio continuity.
 * 
 * **Q32.32 Phase Accumulator**: Uses 64-bit fixed-point arithmetic for
 * sub-sample precision and smooth interpolation between samples.
 * 
 * ## Performance Considerations
 * 
 * This code runs in the audio interrupt context and must complete within
 * the audio buffer period (typically 64 samples). The actual output rate
 * is determined by the PWM/DMA system's audio_rate setting.
 * All operations are optimized for speed and use fixed-point arithmetic
 * to avoid floating-point overhead.
 * 
 * @author Brian Varren
 * @version 1.0
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
 
 // Global flag to track if loop boundaries need calculation
 static bool g_loop_boundaries_calculated = false;
 
 // Function to reset loop boundaries calculation flag (called when new file is loaded)
 void ae_reset_loop_boundaries_flag(void) {
   g_loop_boundaries_calculated = false;
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

// Crossfade state variables (cf = crossfade)
static uint32_t s_cf_steps_rem   = 0;  // Remaining crossfade steps
static uint32_t s_cf_steps_total = 0;  // Total crossfade steps
static uint64_t s_cf_tail_q      = 0;  // Tail phase (old loop region)
static uint64_t s_cf_head_q      = 0;  // Head phase (new loop region)
static uint32_t s_cf_tail_start  = 0;  // Tail region start
static uint32_t s_cf_tail_end    = 0;  // Tail region end
static uint32_t s_cf_head_start  = 0;  // Head region start
static uint32_t s_cf_head_end    = 0;  // Head region end

// ── 8-Pole Ladder Filters ──────────────────────────────────────────────────────
// 8-pole ladder lowpass and highpass filters controlled by ADC5 and ADC6
static Ladder8PoleLowpassFilter s_lowpass_filter;
static Ladder8PoleHighpassFilter s_highpass_filter;

/**
 * @brief Main audio rendering function - processes one audio block in real-time
 * 
 * This is the heart of the audio engine, called from the audio interrupt.
 * It processes AUDIO_BLOCK_SIZE samples (typically 64) and handles:
 * - Sample playback with pitch control
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
                     volatile uint64_t* io_phase_q32_32)
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

  // ── Read Control Inputs (Filtered) ──────────────────────────────────────────
  // These ADC values are filtered to prevent audio artifacts from knob jitter
  // Q12 format means 12-bit fractional part, so 0..4095 maps to 0.0..0.999...
  const uint16_t adc_start_q12    = adc_filter_get(ADC_LOOP_START_CH);   // Loop start position: 0..4095
  const uint16_t adc_len_q12      = adc_filter_get(ADC_LOOP_LEN_CH);     // Loop length: 0..4095
  const uint16_t adc_xfade_q12    = adc_filter_get(ADC_XFADE_LEN_CH);    // Crossfade length: 0..4095
  const uint16_t adc_tune_q12     = adc_filter_get(ADC_TUNE_CH);         // Pitch fine tune: 0..4095
  const uint16_t adc_lowpass_q12  = adc_filter_get(ADC_FX1_CH);          // Lowpass filter cutoff: 0..4095
  const uint16_t adc_highpass_q12 = adc_filter_get(ADC_FX2_CH);         // Highpass filter cutoff: 0..4095
 
   // ── Calculate Pitch Increment Once ──────────────────────────────────────────
   // Calculate the phase increment once at the beginning and use it throughout
   // the entire audio block, regardless of state (crossfade, normal playback, etc.)
   // This ensures consistent pitch across all samples in the block.
   
   // Get octave switch position (0-7, where 0 = special LFO mode)
   const uint8_t octave_pos = sf::ui_get_octave_position();
   
   // Normalize tune knob to [-1, +1] range (centered at 2048 = 0.5 in Q12)
   // This gives us a bipolar control for fine pitch adjustment
   float t_norm = ((float)adc_tune_q12 - 2048.0f) / 2048.0f;
   if (t_norm < -1.0f) t_norm = -1.0f; if (t_norm > 1.0f) t_norm = 1.0f;
  
  float ratio_f;  // Final pitch ratio
  
  if (octave_pos == 0) {
    // ── LFO Mode: Ultra-slow playback for evolving textures ──────────────────
    // Position 0 is special - it enables LFO mode for creating slowly evolving
    // textures. The tune knob controls the playback rate from painfully slow
    // to near-normal speed. Perfect for ambient soundscapes and evolving drones.
    const float lfo_min_ratio = 0.001f;  // ~20 minutes for a 15-second sample
    const float lfo_max_ratio = 1.0f;    // Near-normal playback speed
    ratio_f = lfo_min_ratio + (1.0f - t_norm) * 0.5f * (lfo_max_ratio - lfo_min_ratio);
  } else {
    // ── Octave Mode: Musical pitch control ───────────────────────────────────
    // Positions 1-7 map to -3 to +3 octaves (musical intervals)
    // This provides standard musical pitch control for normal playback
    const int octave_shift = (int)octave_pos - 4;  // -3 to +3 octaves
    const float octave_ratio = exp2f((float)octave_shift);  // 0.125 to 8.0
    
    // Fine tune knob provides ±0.5 semitone adjustment on top of octave shift
    // This allows for subtle pitch adjustments and vibrato effects
    const float tune_ratio = exp2f(t_norm * 0.5f);  // ~0.7 to ~1.4
    ratio_f = octave_ratio * tune_ratio;
  }

  // ── Convert Pitch Ratio to Phase Increment ────────────────────────────────────
  // Convert the floating-point pitch ratio to Q32.32 fixed-point phase increment
  // Q32.32 means 32 bits integer, 32 bits fractional part for sub-sample precision
  uint64_t INC = (uint64_t)(ratio_f * (double)(1ULL << 32));
  
  // TODO: expand the limits to allow much faster playback rates
  // Apply safety limits to prevent extreme playback rates that could cause issues
  if (INC < (1ULL << 28)) INC = (1ULL << 28);  // Minimum: ~0.015x speed (very slow)
  if (INC > (1ULL << 36)) INC = (1ULL << 36);  // Maximum: ~16x speed (very fast)

   // ── Loop Boundary Variables ────────────────────────────────────────────────────
   // These variables hold the calculated loop boundaries for crossfade operations
   static uint32_t pending_start = 0;
   static uint32_t pending_len = 0;
   static uint32_t pending_end = 0;
 
    // Calculate loop boundaries based on current ADC values
    // This maps the 0..4095 ADC range to actual sample positions in the buffer
    auto calculate_loop_boundaries = [&]() -> void {
      const uint32_t MIN_LOOP_LEN = 2048u;  // Minimum loop length to prevent clicks
      const uint32_t span_total = (total_samples > MIN_LOOP_LEN) ? (total_samples - MIN_LOOP_LEN) : 0;
      
      // Map ADC start position (0..4095) to sample range (0..span_total)
      pending_start = (span_total ? (uint32_t)(((uint64_t)adc_start_q12 * (uint64_t)span_total) / 4095u) : 0u);
      
      // Map ADC length (0..4095) to loop length (MIN_LOOP_LEN..total_samples)
      pending_len = MIN_LOOP_LEN + (span_total ? (uint32_t)(((uint64_t)adc_len_q12 * (uint64_t)span_total) / 4095u) : 0u);
      
      // Calculate end position and clamp to buffer size
      pending_end = pending_start + pending_len;
      if (pending_end > total_samples) pending_end = total_samples;
    };
    
    // Setup crossfade: calculate length and initialize crossfade state
    // This function prepares all the state variables needed for seamless crossfading
    auto setup_crossfade = [&](uint64_t phase_now_q, uint64_t crossfade_start_q) -> void {
      // Calculate crossfade length based on the shorter of active or pending loop
      // This prevents the head playhead from hitting the end early and causing silence
      const uint32_t active_len = (s_active_end > s_active_start) ? (s_active_end - s_active_start) : 1;
      const uint32_t pending_len_calc = (pending_end > pending_start) ? (pending_end - pending_start) : 1;
      const uint32_t max_xfade_active = active_len / 2;    // Max 50% of current loop
      const uint32_t max_xfade_pending = pending_len_calc / 2;  // Max 50% of new loop
      const uint32_t max_xfade_both = (max_xfade_active < max_xfade_pending) ? max_xfade_active : max_xfade_pending;
      
      // Ensure we have a meaningful minimum crossfade length to prevent clicks
      const uint32_t min_xfade = (max_xfade_both >= 16) ? 8u : (max_xfade_both / 2);
      
      // Map ADC crossfade knob (0..4095) to crossfade length
      uint32_t xf_len_req = (uint32_t)((uint64_t)max_xfade_both * (uint64_t)adc_xfade_q12 >> 12);
      
      // Handle minimum knob position specially to prevent silence issues
      if (adc_xfade_q12 == 0) {
        xf_len_req = min_xfade;  // Use minimum instead of 0
      } else {
        xf_len_req = constrain(xf_len_req, min_xfade, max_xfade_both);
      }
      
      const uint32_t xf_len_eff = xf_len_req;
      
      // Calculate how many audio samples the crossfade will take
      // This accounts for the playback speed (INC) to ensure smooth crossfading
      const uint64_t num = ((uint64_t)xf_len_eff) << 32;
      uint32_t steps = (uint32_t)((num + (INC - 1)) / INC);
      if (steps == 0) steps = 1;  // Minimum 1 step
      s_cf_steps_total = steps;
      s_cf_steps_rem = steps;
      
      // Set up tail region (old loop, fading out)
      // This is the region we're transitioning FROM
      s_cf_tail_start = (uint32_t)(crossfade_start_q >> 32);
      s_cf_tail_end = s_cf_tail_start + xf_len_eff;
      s_cf_tail_q = phase_now_q;  // Start from current position
      
      // Set up head region (new loop, fading in)
      // This is the region we're transitioning TO
      s_cf_head_start = pending_start;
      s_cf_head_end = pending_end;
      
      // Calculate phase offset to keep content aligned between old and new regions
      // This prevents pitch shifts during crossfade
      uint32_t k = (uint32_t)((phase_now_q - crossfade_start_q) >> 32);  // 0..xf_len_eff-1
      if (k >= xf_len_eff) k = xf_len_eff - 1; // guard against overflow
      s_cf_head_q = (((uint64_t)pending_start + (uint64_t)k) << 32);
      
      // Clear reset trigger flag (crossfade is now initiated)
      g_reset_trigger_pending = false;
    };
   
    // ── Crossfade Length Calculation (moved after boundary calculation) ──────────────
    // This will be calculated after we determine the loop boundaries
    uint32_t xf_len = 0;
   
 
   // ── CORRECTED ORDER OF OPERATIONS ──────────────────────────────────────────────
   // First determine if we need fresh boundaries, THEN calculate them
   
   bool need_fresh_boundaries = false;
   bool should_initiate_crossfade = false;
   
    // 1. Check for reset trigger FIRST
    if (g_reset_trigger_pending) {
      audio_engine_loop_led_blink();
      adc_filter_update_from_dma();
      need_fresh_boundaries = true;
      should_initiate_crossfade = true;
    }
   
    // 2. Check if we're approaching the crossfade point (BEFORE calculating boundaries)
    // Note: This is just a preliminary check - the real detection happens in the hot loop
    if (!should_initiate_crossfade && s_cf_steps_rem == 0) {  // Not currently crossfading
      // Ensure crossfade start point is valid (not before loop start)
      const uint32_t crossfade_start_sample = (s_active_end > xf_len) ? (s_active_end - xf_len) : s_active_start;
      const uint64_t crossfade_start = ((uint64_t)crossfade_start_sample) << 32;
      const uint64_t current_phase = *io_phase_q32_32;
      const uint64_t next_phase = current_phase + INC;
      
      if ((xf_len > 0) && (current_phase < crossfade_start) && (next_phase >= crossfade_start)) {
        // We're about to enter the crossfade region - need fresh boundaries!
        audio_engine_loop_led_blink();
        need_fresh_boundaries = true;
        should_initiate_crossfade = true;
      }
    }
   
   // 3. Check if this is first run
   if (!g_loop_boundaries_calculated) {
     need_fresh_boundaries = true;
   }
   
    // 4. NOW calculate boundaries if needed
    if (need_fresh_boundaries) {
      calculate_loop_boundaries();
      g_loop_boundaries_calculated = true;
    }
    
    // 5. Calculate crossfade length for preliminary checks
    // This is a simplified version for block-level detection
    const uint32_t active_len = (s_active_end > s_active_start) ? (s_active_end - s_active_start) : 1;
    const uint32_t pending_len_calc = (pending_end > pending_start) ? (pending_end - pending_start) : 1;
    const uint32_t max_xfade_active = active_len / 2;
    const uint32_t max_xfade_pending = pending_len_calc / 2;
    const uint32_t max_xfade_both = (max_xfade_active < max_xfade_pending) ? max_xfade_active : max_xfade_pending;
    const uint32_t min_xfade = (max_xfade_both >= 16) ? 8u : (max_xfade_both / 2);
    
    uint32_t xf_len_req = (uint32_t)((uint64_t)max_xfade_both * (uint64_t)adc_xfade_q12 >> 12);
    if (adc_xfade_q12 == 0) {
      xf_len = min_xfade;
    } else {
      xf_len = constrain(xf_len_req, min_xfade, max_xfade_both);
    }
   
   // 5. Initiate crossfade if triggered
   if (should_initiate_crossfade) {
     // Ensure crossfade start point is valid (not before loop start)
     const uint32_t crossfade_start_sample = (s_active_end > xf_len) ? (s_active_end - xf_len) : s_active_start;
     const uint64_t crossfade_start = ((uint64_t)crossfade_start_sample) << 32;
     setup_crossfade(*io_phase_q32_32, crossfade_start);
   }
 
   // ── Validate Active Loop Region ────────────────────────────────────────────────
   // Ensure active loop region is valid, otherwise reset to pending region
   if (s_active_end <= s_active_start || s_active_end > total_samples) {
     s_active_start = pending_start;
     s_active_end   = pending_end;
 
     uint32_t idx0 = (uint32_t)((*io_phase_q32_32) >> 32);
     if (idx0 < s_active_start || idx0 >= s_active_end) {
       *io_phase_q32_32 = ((uint64_t)s_active_start) << 32;
     }
     s_cf_steps_rem = 0;
   }
 

  // ── Sample Interpolation Function ──────────────────────────────────────────────
  // This lambda function converts a Q32.32 phase value to an interpolated Q15 sample
  // It handles bounds checking, sample lookup, and hardware-accelerated interpolation
  auto sample_q15_from_phase = [&](uint64_t phase, uint32_t start, uint32_t end_excl) -> int16_t {
     // Early exit for invalid ranges - return silence
     if (end_excl == 0 || start >= end_excl) return 0;  // 0 is Q15 silence
    
    // Convert end position to Q32.32 format and clamp phase
    const uint64_t end_q = ((uint64_t)end_excl) << 32;
    if (phase >= end_q) phase = end_q - 1;
    
    // Extract integer sample index and clamp to valid range
    uint32_t i = (uint32_t)(phase >> 32);
    if (i < start) i = start;
    if (i >= end_excl) i = end_excl - 1;
    
      // Get second sample for interpolation (with wrapping)
    uint32_t i2 = i + 1; 
      if (i2 >= end_excl) i2 = start;  // wrap to loop start for interpolation continuity
    
    // Extract fractional part for interpolation (8-bit precision)
    const uint32_t frac32 = (uint32_t)(phase & 0xFFFFFFFFull);
    const uint16_t mu8    = (uint16_t)(frac32 >> 24);  // 0-255 interpolation factor
    
    // Convert Q15 samples to unsigned for hardware interpolation
    const uint16_t u0 = (uint16_t)((int32_t)samples[i]  + 32768);
    const uint16_t u1 = (uint16_t)((int32_t)samples[i2] + 32768);
    
    // Use hardware interpolation for smooth sample playback
    const uint16_t ui = interpolate(u0, u1, mu8);
    
    // Convert back to signed Q15 format
    return (int16_t)((int32_t)ui - 32768);
  };

  // ── Initialize Audio Processing State ──────────────────────────────────────────
  uint64_t phase_q = *io_phase_q32_32;  // Current playback position (Q32.32)
 
  // ── Unified Sample Processing Lambda ────────────────────────────────────────────
  // This lambda handles filtering and PWM conversion for both normal and crossfade samples
  auto process_sample = [&](int16_t sample_q15, uint32_t sample_index) -> void {
    // Apply 8-pole ladder filters to sample
    uint16_t lowpass_coeff = adc_to_ladder_coefficient(adc_lowpass_q12);
    sample_q15 = s_lowpass_filter.process(sample_q15, lowpass_coeff);
    
    uint16_t highpass_coeff = adc_to_ladder_coefficient(adc_highpass_q12);
    sample_q15 = s_highpass_filter.process(sample_q15, highpass_coeff);
    
    // Convert to PWM and output
    const uint16_t sample_pwm = q15_to_pwm_u(sample_q15);
    out_buf_ptr_L[sample_index] = sample_pwm;
    out_buf_ptr_R[sample_index] = sample_pwm;
  };

  // Process one crossfade step: sample processing + playhead wrapping + completion check
  // This function handles one complete sample of crossfade processing
  auto process_crossfade_step = [&](uint32_t sample_index) -> bool {
    // Calculate crossfade progress (0 to N-1)
    const uint32_t k = s_cf_steps_total - s_cf_steps_rem;  // Current step
    const uint32_t N = s_cf_steps_total;                   // Total steps
    
    // Get samples from both the old (tail) and new (head) loop regions
    // These will be mixed together with varying gains
    const int16_t a_q15 = sample_q15_from_phase(s_cf_tail_q, s_cf_tail_start, s_cf_tail_end);
    const int16_t b_q15 = sample_q15_from_phase(s_cf_head_q, s_cf_head_start, s_cf_head_end);
    
    // Mix the two samples with constant power crossfade
    // Constant power prevents volume dips during the transition
    const float t = (float)(k + 1u) / (float)N;  // Crossfade progress 0 to 1
    const float fade_out = sinf(M_PI_2 * (1.0f - t));  // Old sample gain (fading out)
    const float fade_in = sinf(M_PI_2 * t);            // New sample gain (fading in)
    
    // Mix samples with proper scaling to prevent overflow
    int64_t mix_num = (int64_t)((float)a_q15 * fade_out) + (int64_t)((float)b_q15 * fade_in);
    int16_t crossfade_q15 = (int16_t)mix_num;
    
    // Apply filtering and convert to PWM output
    process_sample(crossfade_q15, sample_index);
    
    // Advance both playheads by the phase increment
    s_cf_tail_q += INC;
    s_cf_head_q += INC;
    
    // Wrap playheads within their region boundaries to prevent silence
    // This is crucial for short loops where playheads might hit the end quickly
    const uint64_t tail_span_q = ((uint64_t)(s_cf_tail_end - s_cf_tail_start)) << 32;
    const uint64_t head_span_q = ((uint64_t)(s_cf_head_end - s_cf_head_start)) << 32;
    
    while (s_cf_tail_q >= (((uint64_t)s_cf_tail_end) << 32)) {
      s_cf_tail_q -= tail_span_q;  // Wrap to beginning of tail region
    }
    while (s_cf_head_q >= (((uint64_t)s_cf_head_end) << 32)) {
      s_cf_head_q -= head_span_q;  // Wrap to beginning of head region
    }
    
    // Check if crossfade is complete
    if (--s_cf_steps_rem == 0) { 
      // Crossfade complete - switch to new loop region
      phase_q = s_cf_head_q;  // Switch to new region
      s_active_start = pending_start;
      s_active_end = pending_end;
      g_loop_boundaries_calculated = false;  // Force recalculation next loop
      return true;  // Crossfade complete
    }
    
    return false;  // Crossfade continues
  };
 
   // ── Main Audio Processing Loop ─────────────────────────────────────────────────
   // Process each sample in the audio block (AUDIO_BLOCK_SIZE = 16 samples)
   for (uint32_t n = 0; n < AUDIO_BLOCK_SIZE; ++n) {
     
      // ── Crossfade Processing ──────────────────────────────────────────────────────
      // If we're currently crossfading between loop regions, handle the crossfade
      if (s_cf_steps_rem) {
        // Process one complete crossfade step (sample mixing + playhead wrapping + completion check)
        bool crossfade_complete = process_crossfade_step(n);
       continue;  // Skip normal playback processing
     }

      // ── Normal Playback Processing ────────────────────────────────────────────────
      // Advance phase for next sample
      phase_q += INC;
  
      // ── Per-Sample Crossfade Detection ─────────────────────────────────────────────
      // Check if we've crossed the crossfade start point (per-sample detection)
      if (s_cf_steps_rem == 0 && xf_len > 0) {  // Not currently crossfading and crossfade enabled
        // Ensure crossfade start point is valid (not before loop start)
        const uint32_t crossfade_start_sample = (s_active_end > xf_len) ? (s_active_end - xf_len) : s_active_start;
        const uint64_t crossfade_start = ((uint64_t)crossfade_start_sample) << 32;
        
        if (phase_q >= crossfade_start) {
          // We've crossed the crossfade start point! Trigger crossfade immediately
          // Calculate fresh boundaries and initiate crossfade
          calculate_loop_boundaries();
          g_loop_boundaries_calculated = true;
          
          // Set up crossfade with current phase and crossfade start
          setup_crossfade(phase_q, crossfade_start);
          
          // Process this sample as a crossfade sample
          bool crossfade_complete = process_crossfade_step(n);
        continue;  // Skip normal playback processing
        }
      }
  
      // ── Loop Wrap Detection ────────────────────────────────────────────────────────
      // If we've reached the end of the loop, we should already be in a crossfade
      // If not, something went wrong - just wrap to the beginning
      uint32_t idx = (uint32_t)(phase_q >> 32);
      if (idx >= s_active_end || idx >= total_samples) {
        // This should not happen if crossfade logic is working correctly
        // Just wrap to the beginning as a fallback
        phase_q = ((uint64_t)s_active_start) << 32;
      }

    int16_t playback_q15 = sample_q15_from_phase(phase_q, s_active_start, s_active_end);
    
     // Process the normal playback sample using unified processing
     process_sample(playback_q15, n);
  }

  // ── Update Global Phase State ────────────────────────────────────────────────────────
  // Save the final phase position for the next audio block
  *io_phase_q32_32 = phase_q;

  // ── Update Display State ──────────────────────────────────────────────────────────────
  // Prepare visualization data for the display system (Core 1)
  uint32_t vis_primary_idx   = (uint32_t)(phase_q >> 32);  // Main playhead position
  uint8_t  vis_xfade_active  = (s_cf_steps_rem != 0) ? 1u : 0u;  // Crossfade status
  uint32_t vis_secondary_idx = 0;  // Secondary playhead (during crossfade)
  
  // During crossfade, show both playheads for visual feedback
  if (vis_xfade_active) {
    vis_primary_idx   = (uint32_t)(s_cf_head_q >> 32);    // New region playhead
    vis_secondary_idx = (uint32_t)(s_cf_tail_q >> 32);    // Old region playhead
  }

  const uint16_t start_q12 = (uint16_t)(((uint64_t)s_active_start * 4095u) / (uint64_t)total_samples);
  const uint16_t len_q12   = (uint16_t)(((uint64_t)((s_active_end > s_active_start) ? (s_active_end - s_active_start) : 0) * 4095u) / (uint64_t)total_samples);
  
  // Publish state to display system for real-time visualization
  publish_display_state2(start_q12, len_q12,
                         vis_primary_idx, total_samples,
                         vis_xfade_active, vis_secondary_idx);
}
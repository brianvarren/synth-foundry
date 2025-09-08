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
#include <Arduino.h>

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

// Crossfade state variables (cf = crossfade)
static uint32_t s_cf_steps_rem   = 0;  // Remaining crossfade steps
static uint32_t s_cf_steps_total = 0;  // Total crossfade steps
static uint64_t s_cf_tail_q      = 0;  // Tail phase (old loop region)
static uint64_t s_cf_head_q      = 0;  // Head phase (new loop region)
static uint32_t s_cf_tail_start  = 0;  // Tail region start
static uint32_t s_cf_tail_end    = 0;  // Tail region end
static uint32_t s_cf_head_start  = 0;  // Head region start
static uint32_t s_cf_head_end    = 0;  // Head region end
static uint64_t s_cf_inc_q       = 0;  // Phase increment during crossfade



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
    for (uint32_t i = 0; i < AUDIO_BLOCK_SIZE; ++i) {
      out_buf_ptr_L[i] = 0;
      out_buf_ptr_R[i] = 0;
    }

    const uint32_t total = total_samples ? total_samples : 1u;
    const uint16_t start_q12 = (uint16_t)(((uint64_t)s_active_start * 4095u) / (uint64_t)total);
    const uint16_t len_q12   = (uint16_t)(((uint64_t)((s_active_end > s_active_start) ? (s_active_end - s_active_start) : 0) * 4095u) / (uint64_t)total);
    publish_display_state2(start_q12, len_q12, 0, total, 0, 0);
    return;
  }

  loop_mapper_recalc_spans(total_samples);

  // ── Read Control Inputs (Filtered) ──────────────────────────────────────────
  // These ADC values are filtered to prevent audio artifacts from knob jitter
  const uint16_t adc_start_q12 = adc_filter_get(ADC_LOOP_START_CH);   // Loop start: 0..4095
  const uint16_t adc_len_q12   = adc_filter_get(ADC_LOOP_LEN_CH);     // Loop length: 0..4095
  const uint16_t adc_xfade_q12 = adc_filter_get(ADC_XFADE_LEN_CH);    // Crossfade length: 0..4095
  const uint16_t adc_tune_q12  = adc_filter_get(ADC_TUNE_CH);         // Pitch fine tune: 0..4095
  

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

    uint32_t idx0 = (uint32_t)((*io_phase_q32_32) >> 32);
    if (idx0 < s_active_start || idx0 >= s_active_end) {
      *io_phase_q32_32 = ((uint64_t)s_active_start) << 32;
    }
    s_cf_steps_rem = 0;
  }

  // ── Pitch Control Processing ────────────────────────────────────────────────
  // The pitch control system combines two inputs:
  // 1. Octave switch (8-position rotary switch)
  // 2. Fine tune knob (ADC input)
  
  // Get octave switch position (0-7, where 0 = special LFO mode)
  const uint8_t octave_pos = sf::ui_get_octave_position();
  
  // Normalize tune knob to [-1, +1] range (centered at 2048)
  float t_norm = ((float)adc_tune_q12 - 2048.0f) / 2048.0f;
  if (t_norm < -1.0f) t_norm = -1.0f; if (t_norm > 1.0f) t_norm = 1.0f;
  
  float ratio_f;  // Final pitch ratio
  
  if (octave_pos == 0) {
    // ── LFO Mode: Ultra-slow playback for evolving textures ──────────────────
    // Position 0 is special - it enables LFO mode for creating slowly evolving
    // textures. The tune knob controls the playback rate from painfully slow
    // to near-normal speed.
    const float lfo_min_ratio = 0.001f;  // ~20 minutes for a 15-second sample
    const float lfo_max_ratio = 1.0f;    // Near-normal playback speed
    ratio_f = lfo_min_ratio + (1.0f - t_norm) * 0.5f * (lfo_max_ratio - lfo_min_ratio);
  } else {
    // ── Octave Mode: Musical pitch control ───────────────────────────────────
    // Positions 1-7 map to -3 to +3 octaves (musical intervals)
    const int octave_shift = (int)octave_pos - 4;  // -3 to +3 octaves
    const float octave_ratio = exp2f((float)octave_shift);  // 0.125 to 8.0
    
    // Fine tune knob provides ±0.5 semitone adjustment on top of octave shift
    const float tune_ratio = exp2f(t_norm * 0.5f);  // ~0.7 to ~1.4
    ratio_f = octave_ratio * tune_ratio;
  }
  

  // ── Convert Pitch Ratio to Phase Increment ────────────────────────────────────
  // Convert the floating-point pitch ratio to Q32.32 fixed-point phase increment
  uint64_t INC = (uint64_t)(ratio_f * (double)(1ULL << 32));
  
  // Apply safety limits to prevent extreme playback rates
  if (INC < (1ULL << 28)) INC = (1ULL << 28);  // Minimum: ~0.015x speed
  if (INC > (1ULL << 36)) INC = (1ULL << 36);  // Maximum: ~16x speed

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

  // ── Sample Interpolation Function ──────────────────────────────────────────────
  // This lambda function converts a Q32.32 phase value to an interpolated Q15 sample
  // It handles bounds checking, sample lookup, and hardware-accelerated interpolation
  auto sample_q15_from_phase = [&](uint64_t phase, uint32_t start, uint32_t end_excl) -> int16_t {
    // Early exit for invalid ranges
    if (end_excl == 0 || start >= end_excl) return 0;
    
    // Convert end position to Q32.32 format and clamp phase
    const uint64_t end_q = ((uint64_t)end_excl) << 32;
    if (phase >= end_q) phase = end_q - 1;
    
    // Extract integer sample index and clamp to valid range
    uint32_t i = (uint32_t)(phase >> 32);
    if (i < start) i = start;
    if (i >= end_excl) i = end_excl - 1;
    
    // Get second sample for interpolation (with bounds checking)
    uint32_t i2 = i + 1; 
    if (i2 >= end_excl) i2 = end_excl - 1;
    
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
  uint64_t last_phase_q = phase_q;      // Previous position for debugging


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
      const int16_t a_q15 = sample_q15_from_phase(s_cf_tail_q, s_cf_tail_start, s_cf_tail_end);
      const int16_t b_q15 = sample_q15_from_phase(s_cf_head_q, s_cf_head_start, s_cf_head_end);
      
      // Mix the two samples with constant power crossfade
      // As k increases, we fade from 'a' (old) to 'b' (new)
      // Constant power crossfade uses sine curves to maintain constant power
      const float t = (float)(k + 1u) / (float)N;  // Crossfade progress 0 to 1
      const float fade_out = sinf(M_PI_2 * (1.0f - t));  // Old sample gain
      const float fade_in = sinf(M_PI_2 * t);            // New sample gain
      
      const int64_t mix_num = (int64_t)((float)a_q15 * fade_out)  // Old sample weight
                            + (int64_t)((float)b_q15 * fade_in);  // New sample weight
      const int16_t crossfade_q15 = (int16_t)mix_num;
      const uint16_t crossfade_pwm = q15_to_pwm_u(crossfade_q15);
      out_buf_ptr_L[n] = crossfade_pwm;
      out_buf_ptr_R[n] = crossfade_pwm;
      
      s_cf_tail_q += s_cf_inc_q;
      s_cf_head_q += s_cf_inc_q;
      
      // Clamp playheads to their region boundaries
      const uint64_t tail_end_q = ((uint64_t)s_cf_tail_end) << 32;
      const uint64_t head_end_q = ((uint64_t)s_cf_head_end) << 32;
      if (s_cf_tail_q >= tail_end_q) s_cf_tail_q = tail_end_q - 1;
      if (s_cf_head_q >= head_end_q) s_cf_head_q = head_end_q - 1;
      
      // Check if crossfade is complete
      if (--s_cf_steps_rem == 0) { 
        phase_q = s_cf_head_q;  // Switch to new region
      }
      continue;  // Skip normal playback processing
    }

    const uint64_t end_q     = ((uint64_t)s_active_end) << 32;
    const uint64_t pre_end_q = ((uint64_t)((xfade_len < active_len)
                                ? (s_active_end - xfade_len)
                                : (s_active_start + 1))) << 32;
    const uint64_t next_phase = phase_q + INC;
    const bool start_xfade = (xfade_len > 0) && (phase_q < pre_end_q) && (next_phase >= pre_end_q);
    const bool crossed_end = (phase_q < end_q) && (next_phase >= end_q);
    phase_q = next_phase;

    // ── Crossfade Initiation ────────────────────────────────────────────────────────
    // We've reached the crossfade start point - begin transitioning to new loop region
    if (start_xfade) {
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
      s_cf_tail_q   = ((uint64_t)(old_end - xf_len)) << 32;  // Start from end of old region
      s_cf_head_q   = ((uint64_t)new_start)          << 32;  // Start from beginning of new region
      s_cf_inc_q    = INC;  // Use current pitch increment
      
      // Calculate number of crossfade steps based on crossfade length and pitch
      const uint64_t num = ((uint64_t)xf_len) << 32;
      uint32_t steps = (uint32_t)((num + (s_cf_inc_q - 1)) / s_cf_inc_q);
      if (steps == 0) steps = 1;  // Minimum 1 step
      s_cf_steps_total = steps;
      s_cf_steps_rem   = steps;
      
      // Generate first crossfade sample (mix of old and new regions)
      const int16_t a0 = sample_q15_from_phase(s_cf_tail_q, s_cf_tail_start, s_cf_tail_end);
      const int16_t b0 = sample_q15_from_phase(s_cf_head_q, s_cf_head_start, s_cf_head_end);
      
      // Constant power crossfade for first sample
      const float t0 = 1.0f / (float)steps;  // First sample progress
      const float fade_out0 = sinf(M_PI_2 * (1.0f - t0));  // Old sample gain
      const float fade_in0 = sinf(M_PI_2 * t0);             // New sample gain
      
      const int64_t num0 = (int64_t)((float)a0 * fade_out0) + (int64_t)((float)b0 * fade_in0);
      const uint16_t crossfade_pwm = q15_to_pwm_u((int16_t)num0);
      out_buf_ptr_L[n] = crossfade_pwm;
      out_buf_ptr_R[n] = crossfade_pwm;
      
       s_cf_tail_q += s_cf_inc_q; s_cf_head_q += s_cf_inc_q;
      const uint64_t tail_end_q = ((uint64_t)s_cf_tail_end) << 32;
      const uint64_t head_end_q = ((uint64_t)s_cf_head_end) << 32;
      if (s_cf_tail_q >= tail_end_q) s_cf_tail_q = tail_end_q - 1;
      if (s_cf_head_q >= head_end_q) s_cf_head_q = head_end_q - 1;
      
      // Check if crossfade completed in this single sample (rare but possible)
      if (--s_cf_steps_rem == 0) { 
        phase_q = s_cf_head_q;  // Switch to new region
      }
      continue;  // Skip normal playback processing
    }

    uint32_t idx = (uint32_t)(phase_q >> 32);
    if (crossed_end || idx >= total_samples) {
      // Store current and new loop region boundaries
      const uint32_t old_start = s_active_start;  // Current loop start
      const uint32_t old_end   = s_active_end;    // Current loop end
      const uint32_t new_start = pending_start;   // New loop start
      const uint32_t new_end   = pending_end;     // New loop end
      
      // Use minimum crossfade length for loop end transitions
      uint32_t xf_len = (active_len > MIN_XF_SAMPLES) ? MIN_XF_SAMPLES : (active_len > 1 ? 1u : 0u);
      
      // Update active loop region to new region
      s_active_start = new_start; 
      s_active_end = new_end;
      
      // If we have a crossfade length, set up crossfade state
      if (xf_len > 0) {
        // Set up crossfade between old and new regions
        s_cf_tail_start = old_start; s_cf_tail_end = old_end;  // Old region (fading out)
        s_cf_head_start = new_start; s_cf_head_end = new_end;  // New region (fading in)
        s_cf_tail_q = ((uint64_t)(old_end - xf_len)) << 32;   // Start from end of old region
        s_cf_head_q = ((uint64_t)new_start)          << 32;   // Start from beginning of new region
        s_cf_inc_q  = INC;  // Use current pitch increment
        
        // Calculate crossfade steps
        const uint64_t num = ((uint64_t)xf_len) << 32;
        uint32_t steps = (uint32_t)((num + (s_cf_inc_q - 1)) / s_cf_inc_q);
        if (steps == 0) steps = 1;  // Minimum 1 step
        s_cf_steps_total = steps;
        s_cf_steps_rem   = steps;
        
        // Generate first crossfade sample
        const int16_t a0 = sample_q15_from_phase(s_cf_tail_q, s_cf_tail_start, s_cf_tail_end);
        const int16_t b0 = sample_q15_from_phase(s_cf_head_q, s_cf_head_start, s_cf_head_end);
        
        // Constant power crossfade for first sample
        const float t0 = 1.0f / (float)steps;  // First sample progress
        const float fade_out0 = sinf(M_PI_2 * (1.0f - t0));  // Old sample gain
        const float fade_in0 = sinf(M_PI_2 * t0);             // New sample gain
        
        const int64_t num0 = (int64_t)((float)a0 * fade_out0) + (int64_t)((float)b0 * fade_in0);
        const uint16_t crossfade_pwm = q15_to_pwm_u((int16_t)num0);
        out_buf_ptr_L[n] = crossfade_pwm;
        out_buf_ptr_R[n] = crossfade_pwm;
        
        s_cf_tail_q += s_cf_inc_q; s_cf_head_q += s_cf_inc_q;
        const uint64_t tail_end_q = ((uint64_t)s_cf_tail_end) << 32;
        const uint64_t head_end_q = ((uint64_t)s_cf_head_end) << 32;
        if (s_cf_tail_q >= tail_end_q) s_cf_tail_q = tail_end_q - 1;
        if (s_cf_head_q >= head_end_q) s_cf_head_q = head_end_q - 1;
        
        // Check if crossfade completed
        if (--s_cf_steps_rem == 0) { 
          phase_q = s_cf_head_q;  // Switch to new region
        }
        continue;  // Skip normal playback processing
        } else {
          phase_q = ((uint64_t)new_start) << 32;
        }
    }

    const int16_t playback_q15 = sample_q15_from_phase(phase_q, s_active_start, s_active_end);
    const uint16_t playback_pwm = q15_to_pwm_u(playback_q15);
    out_buf_ptr_L[n] = playback_pwm;
    out_buf_ptr_R[n] = playback_pwm;
    
    last_phase_q = phase_q;
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



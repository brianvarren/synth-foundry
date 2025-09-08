/**
 * @file audio_engine.h
 * @brief Audio engine interface - real-time sample playback with crossfading and pitch control
 * 
 * This header defines the public interface for the audio engine, which is the heart
 * of the loop sampler. The audio engine provides real-time sample playback with
 * advanced features like seamless crossfading, pitch shifting, and loop manipulation.
 * 
 * ## Key Concepts
 * 
 * **Phase Accumulator**: The core of the audio engine uses a Q32.32 fixed-point
 * phase accumulator that advances through the sample data. This provides sub-sample
 * precision for smooth playback and pitch shifting.
 * 
 * **Q15 Audio Format**: All audio samples are stored in Q15 format (16-bit signed
 * integers, -32768 to +32767). This fixed-point format is used throughout the
 * audio engine for consistent performance and predictable behavior.
 * 
 * **Crossfading**: When loop parameters change during playback, the engine performs
 * seamless crossfades between the old and new loop regions to prevent audio glitches.
 * 
 * **Pitch Control**: The engine supports both octave switching (via rotary switch)
 * and fine tuning (via ADC knob), with special LFO mode for ultra-slow playback.
 * 
 * ## Transport States
 * 
 * - **IDLE**: No sample loaded, outputs silence
 * - **READY**: Sample loaded and bound, ready to play
 * - **PLAYING**: Actively rendering audio
 * - **PAUSED**: Sample bound but playback paused
 * 
 * ## Playback Modes
 * 
 * - **FORWARD**: Normal forward playback
 * - **REVERSE**: Reverse playback
 * - **ALTERNATE**: Ping-pong (forward then reverse, repeating)
 * 
 * @author Brian Varren
 * @version 1.0
 * @date 2024
 */

#pragma once
#include <stdint.h>
#include "DACless.h"

// ── Public engine state (keep small) ──────────────────────────────────────

// ── Direction / transport ─────────────────────────────────────────
typedef enum {
  AE_MODE_FORWARD   = 0,
  AE_MODE_REVERSE   = 1,
  AE_MODE_ALTERNATE = 2,   // ping-pong
} ae_mode_t;

typedef enum {
  AE_STATE_IDLE   = 0,   // no buffer bound; outputs silence
  AE_STATE_READY  = 1,   // buffer bound, but transport disarmed
  AE_STATE_PLAYING= 2,   // actively rendering
  AE_STATE_PAUSED = 3,   // buffer bound, transport paused
} ae_state_t;

// Q16.16 phase accumulator, advanced each audio sample
extern volatile uint64_t g_phase_q32_32;

// Q16.16 base increment (unity speed = src_rate / out_rate)
// Set at bind time; later multiplied by the tune ratio per block.
extern uint64_t g_inc_base_q32_32;

// Q16.16 per-ADC-unit scale for phase modulation (audio-rate PM)
//  adc(int16 centered) * g_pm_scale_q16_16 => Q16.16 delta added to phase
extern int32_t g_pm_scale_q16_16;

// Live playhead position normalized to the current loop, 0..65535.
extern volatile uint16_t g_playhead_norm_u16;

// ── Lifecycle ─────────────────────────────────────────────────────────────

// Initializes tables and PWM DMA (expects your DACless/ADCless to be linkable)
void audio_init(void);

void audio_tick(void);

// Bind the loaded sample buffer and set the base increment to unity for that file.
//  - src_sample_rate_hz: WAV/native sample rate
//  - out_sample_rate_hz: your audio engine output rate (PWM ISR rate)
//  - sample_count: number of int16 PCM samples in PSRAM
void playback_bind_loaded_buffer(uint32_t src_sample_rate_hz,
                                 uint32_t out_sample_rate_hz,
                                 uint32_t sample_count);

// ── Transport / mode control (UI calls these) ────────────────────
void audio_engine_set_mode(ae_mode_t m);      // FORWARD/REVERSE/ALTERNATE
void audio_engine_arm(bool armed);            // armed=true => READY; false => IDLE/PAUSED
void audio_engine_play(bool play);            // play=true => PLAYING, false => PAUSED
ae_state_t audio_engine_get_state(void);
ae_mode_t  audio_engine_get_mode(void);


                                 // Fill the current PWM DMA half-buffer. Assumes:
//  - out_buf_ptr points at the active half
//  - adc_results_buf[...] contains fresh ADCs
//  - channel macros (ADC_TUNE_CH, ADC_PM_CH, ADC_LOOP_START_CH, ADC_LOOP_LEN_CH, ADC_XFADE_LEN_CH) are defined
void process(void);


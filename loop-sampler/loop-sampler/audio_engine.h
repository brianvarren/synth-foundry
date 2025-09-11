#pragma once
#include <stdint.h>
#include "DACless.h"

// ── Public engine state ──────────────────────────────────────────────
typedef enum {
  AE_MODE_FORWARD   = 0,
  AE_MODE_REVERSE   = 1,
  AE_MODE_ALTERNATE = 2,
} ae_mode_t;

typedef enum {
  LOOP_WRAP = 0,
  LOOP_PINGPONG = 1
} loop_mode_t;

typedef enum {
  AE_STATE_IDLE   = 0,
  AE_STATE_READY  = 1,
  AE_STATE_PLAYING= 2,
  AE_STATE_PAUSED = 3,
} ae_state_t;

// CRITICAL CHANGE: Phase is now SIGNED for through-zero FM
extern volatile int64_t g_phase_q32_32;  // SIGNED phase accumulator

// Base increment remains unsigned (it's always positive)
extern uint64_t g_inc_base_q32_32;

// Current increment is signed for through-zero FM
extern int64_t g_inc_q32_32;

// Unused but kept for compatibility
extern int32_t g_pm_scale_q16_16;
extern volatile uint16_t g_playhead_norm_u16;

// Reset trigger state
extern volatile bool g_reset_trigger_pending;

// ── Lifecycle ────────────────────────────────────────────────────────
void audio_init(void);
void audio_tick(void);
void playback_bind_loaded_buffer(uint32_t src_sample_rate_hz,
                                 uint32_t out_sample_rate_hz,
                                 uint32_t sample_count);

// ── Transport control ────────────────────────────────────────────────
void audio_engine_set_mode(ae_mode_t m);
void audio_engine_arm(bool armed);
void audio_engine_play(bool play);
ae_state_t audio_engine_get_state(void);
ae_mode_t  audio_engine_get_mode(void);

// ── Reset trigger control ────────────────────────────────────────────
void audio_engine_reset_trigger_init(void);
void audio_engine_reset_trigger_poll(void);
void audio_engine_reset_trigger_handle(void);

// ── Loop LED control ─────────────────────────────────────────────────
void audio_engine_loop_led_init(void);
void audio_engine_loop_led_update(void);
void audio_engine_loop_led_blink(void);

void process(void);
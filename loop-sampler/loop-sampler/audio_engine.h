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


// --- Debugging -------------------------------------------------------------

// Verbosity levels
typedef enum {
  AE_DBG_OFF   = 0,
  AE_DBG_ERR   = 1,  // only problems / reasons playback is blocked
  AE_DBG_INFO  = 2,  // state transitions + once-per-second status
  AE_DBG_TRACE = 3,  // every change per block (noisy)
} ae_dbg_level_t;

// Bitmask flags describing issues/conditions in the last processed block
enum {
  AE_DIAG_NONE             = 0,
  AE_DIAG_NO_BUFFER        = 1u << 0,  // no samples bound / too short
  AE_DIAG_STATE_NOT_PLAY   = 1u << 1,  // not in PLAYING state
  AE_DIAG_LOOP_INVALID     = 1u << 2,  // LEND <= LSTART
  AE_DIAG_IDX_OOB_CLAMPED  = 1u << 3,  // index had to be clamped
  AE_DIAG_ZERO_INC         = 1u << 4,  // inc_q == 0
  AE_DIAG_XFADE_TOO_LARGE  = 1u << 5,  // xfade_len > loop_len (clamped)
  AE_DIAG_DIR_FLIPPED      = 1u << 6,  // direction flipped this block
};

// Lightweight per-block snapshot (atomically readable)
typedef struct {
  uint32_t phase_q16_16;   // phase after block
  uint32_t idx;            // integer index derived from phase
  uint32_t loop_start;     // LSTART used this block
  uint32_t loop_end;       // LEND   used this block
  uint32_t total_samples;  // bound sample count
  uint32_t inc_q16_16;     // unsigned increment (before dir)
  int32_t  inc_signed;     // signed step actually used
  uint16_t xfade_len;      // crossfade samples used
  uint16_t playhead_u16;   // 0..65535 normalized playhead
  uint8_t  mode;           // ae_mode_t
  uint8_t  state;          // ae_state_t
  int8_t   dir;            // +1 / -1
  uint32_t diag_flags;     // AE_DIAG_* bits
} ae_dbg_snapshot_t;

void audio_engine_debug_set_level(ae_dbg_level_t lvl);
ae_dbg_level_t audio_engine_debug_get_level(void);

// Poll from your main loop (~10–50 Hz). Prints serial logs according to level.
// Cheap: reads one volatile struct and rate-limits prints.
void audio_engine_debug_poll(void);

// If you prefer to consume the snapshot yourself:
ae_dbg_snapshot_t audio_engine_get_last_snapshot(void);

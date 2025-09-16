/**
 * @file sf_globals_bridge.h
 * @brief Global state management and inter-core communication bridge
 * 
 * This header manages the complex task of sharing state between the two CPU cores
 * of the RP2040. It provides thread-safe communication between the audio engine
 * (Core 0) and the display system (Core 1) using lock-free data structures.
 * 
 * ## Core Communication
 * 
 * **Audio Engine (Core 0)**: Writes audio state, loop parameters, and playback
 * position to shared variables that the display can read safely.
 * 
 * **Display System (Core 1)**: Reads audio state for real-time visualization
 * without blocking the audio engine or causing audio dropouts.
 * 
 * ## Thread Safety
 * 
 * The system uses several techniques to ensure thread-safe communication:
 * 
 * **Atomic Variables**: Simple variables (like playhead position) are marked
 * volatile and accessed atomically where possible.
 * 
 * **Sequence Locks**: Complex data structures use sequence locks (seqlocks)
 * for lock-free reads while allowing safe updates from the writer.
 * 
 * **Memory Barriers**: Compiler memory barriers ensure proper ordering of
 * memory operations across cores.
 * 
 * ## State Management
 * 
 * The bridge manages several types of shared state:
 * - Audio engine state (playing, paused, idle)
 * - Loop parameters (start, end, crossfade)
 * - Playback position and direction
 * - Crossfade state for seamless transitions
 * 
 * @author Brian Varren
 * @version 1.0
 * @date 2024
 */

#pragma once
#include <stdint.h>
#include "audio_engine.h"
#include <hardware/sync.h>

#define INTERPOLATE

// ── ADC Channel Definitions ──────────────────────────────────────────────────
// These define which ADC channels correspond to which control inputs.
// The values are used throughout the codebase for consistent control mapping.
#define ADC_LOOP_START_CH 0  // Loop start position control
#define ADC_LOOP_LEN_CH   1  // Loop length control  
#define ADC_TUNE_CH       2  // Pitch fine tune control
#define ADC_PM_CH         3  // Phase modulation (FM input signal - unfiltered)
#define ADC_XFADE_LEN_CH  4  // Crossfade length control
#define ADC_FX1_CH        5  // Effect 1 control (lowpass filter)
#define ADC_FX2_CH        6  // Effect 2 control (highpass filter)
#define ADC_TZFM_DEPTH_CH 7  // TZFM modulation depth control

// ── Crossfade Configuration ───────────────────────────────────────────────────
// These constants define the range of crossfade lengths for seamless loop transitions
#define XF_MIN_SAMPLES   16u        // Minimum crossfade length (prevents clicks)
#define XF_MAX_SAMPLES   2048u      // Maximum crossfade length (prevents excessive memory use)

// ── ADC Filtering Configuration ───────────────────────────────────────────────
// ADC filtering parameters for smooth control input processing
#define ADC_FILTER_DISPLAY_TICK_HZ (audio_rate / AUDIO_BLOCK_SIZE)  // Update rate
#define ADC_FILTER_CUTOFF_HZ 2.0f  // Low-pass filter cutoff (smooth but responsive)

// Include the actual WavInfo type from storage_wav_meta.h
#include "storage_wav_meta.h"

// ── Global Audio State (Default Namespace) ────────────────────────────────────
// These globals store the currently loaded audio sample and are shared between
// the audio engine (Core 0) and display system (Core 1). They're kept in the
// default namespace for compatibility with existing code.
extern uint8_t*  audioData;          // Q15 audio samples in PSRAM buffer
extern uint32_t  audioDataSize;      // Total bytes allocated in PSRAM
extern uint32_t  audioSampleCount;   // Number of Q15 samples in buffer
extern sf::WavInfo   currentWav;     // Original WAV file metadata

// ── Namespace Aliases ──────────────────────────────────────────────────────────
// Create sf:: aliases that point to the default namespace globals.
// This allows code within the sf namespace to access these variables
// without explicit namespace qualification.
namespace sf {
  using ::audioData;        // Q15 audio samples in PSRAM
  using ::audioDataSize;    // PSRAM buffer size in bytes
  using ::audioSampleCount; // Number of Q15 samples
  using ::currentWav;       // WAV file metadata
}

// #pragma once
// #include <stdint.h>
//#include <hardware/sync.h>   // for __compiler_memory_barrier()

// // 0..4095 for start/len (ADC-style), playhead/total are sample indices or Q16.16 — your choice
// typedef struct {
//   volatile uint32_t seq;            // even = stable, odd = being written
//   volatile uint16_t loop_start_q12; // [0..4095]
//   volatile uint16_t loop_len_q12;   // [0..4095]
//   volatile uint32_t playhead;       // sample idx or Q16.16
//   volatile uint32_t total;          // total samples (or Q16.16 length)
// } sf_display_state_t;


// Seqlock: writer flips an odd/even sequence around a field update; reader retries until it sees a stable, even sequence.
// For core1 display stability
extern volatile uint32_t g_core0_setup_done; 
//extern sf_display_state_t g_disp;   // define once in a .cpp


static inline void core0_publish_setup_done(void) {
  __compiler_memory_barrier();
  g_core0_setup_done = 1;
  __compiler_memory_barrier();
}

// static inline void disp_write_begin() {
//   g_disp.seq++;                    // make odd
//   __compiler_memory_barrier();
// }
// static inline void disp_write_end() {
//   __compiler_memory_barrier();
//   g_disp.seq++;                    // back to even
// }

// // Copy out a stable snapshot; returns when consistent.
// static inline void disp_read_snapshot(sf_display_state_t* out) {
//   while (1) {
//     uint32_t a = g_disp.seq;
//     __compiler_memory_barrier();
//     uint16_t ls = g_disp.loop_start_q12;
//     uint16_t ll = g_disp.loop_len_q12;
//     uint32_t ph = g_disp.playhead;
//     uint32_t tt = g_disp.total;
//     __compiler_memory_barrier();
//     uint32_t b = g_disp.seq;
//     if ((a == b) && ((b & 1u) == 0)) {
//       out->loop_start_q12 = ls;
//       out->loop_len_q12   = ll;
//       out->playhead       = ph;
//       out->total          = tt;
//       out->seq            = b;
//       return;
//     }
//     // else: writer in progress; try again
//   }
// }


/**
 * Visual state bridge between audio engine (writer) and display (reader).
 * - Exposes simple extern volatiles for quick reads.
 * - Also provides a tear-free snapshot API using a tiny seqlock.
 *
 * Conventions:
 *  - Functions: snake_case
 *  - Vars:      g_vis_* (extern volatile)
 */

// ─────────────────────────── Extern visual state (volatile) ─────────────────
extern volatile uint16_t g_vis_start_q12;      // loop start, 0..4095
extern volatile uint16_t g_vis_len_q12;        // loop length, 0..4095
extern volatile uint32_t g_vis_total;          // total samples (≥1)
extern volatile uint32_t g_vis_playhead_idx;   // primary playhead (head or main)
extern volatile uint8_t  g_vis_xfade_active;   // 0 or 1
extern volatile uint32_t g_vis_playhead2_idx;  // secondary playhead (tail when xfade)

// Internal writer sequence for seqlock (even = stable, odd = writing)
extern volatile uint32_t g_vis_seq;

// ───────────────────────────── Snapshot struct ──────────────────────────────
typedef struct {
  uint16_t start_q12;
  uint16_t len_q12;
  uint32_t total;
  uint32_t playhead_idx;
  uint8_t  xfade_active;
  uint32_t playhead2_idx;
} sf_vis_snapshot_t;

// ───────────────────────────── Publish APIs ─────────────────────────────────
// Back-compat single-playhead publisher.
void publish_display_state(uint16_t start_q12, uint16_t len_q12,
                           uint32_t playhead_idx, uint32_t total);

// Dual-playhead publisher (use when crossfading is possible).
void publish_display_state2(uint16_t start_q12, uint16_t len_q12,
                            uint32_t playhead_idx, uint32_t total,
                            uint8_t xfade_active, uint32_t playhead2_idx);

// ───────────────────────────── Read API (safe) ──────────────────────────────
/**
 * vis_get_snapshot(out):
 *   Copies a consistent snapshot using a seqlock. Spin-waits briefly
 *   if a write is in progress. No heap; O(1) memory; tiny code size.
 */
static inline void vis_get_snapshot(sf_vis_snapshot_t* out) {
  // Declarations of externs so this can be header-inline without extra includes
  extern volatile uint16_t g_vis_start_q12, g_vis_len_q12;
  extern volatile uint32_t g_vis_total, g_vis_playhead_idx, g_vis_playhead2_idx;
  extern volatile uint8_t  g_vis_xfade_active;
  extern volatile uint32_t g_vis_seq;

  for (;;) {
    uint32_t s0 = g_vis_seq;           // read seq (start)
    if (s0 & 1u) continue;             // writer in progress, try again

    // Read fields
    uint16_t start_q12   = g_vis_start_q12;
    uint16_t len_q12     = g_vis_len_q12;
    uint32_t total       = g_vis_total;
    uint32_t playhead    = g_vis_playhead_idx;
    uint8_t  xfade       = g_vis_xfade_active;
    uint32_t playhead2   = g_vis_playhead2_idx;

    uint32_t s1 = g_vis_seq;           // read seq (end)
    if (s0 == s1 && !(s1 & 1u)) {
      // Stable snapshot
      out->start_q12     = start_q12;
      out->len_q12       = len_q12;
      out->total         = (total == 0u) ? 1u : total; // guard divide-by-zero
      out->playhead_idx  = playhead;
      out->xfade_active  = xfade;
      out->playhead2_idx = playhead2;
      return;
    }
    // else: writer raced us; spin and try again
  }
}
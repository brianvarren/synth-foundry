#pragma once
#include <stdint.h>
#include "audio_engine.h"
#include <hardware/sync.h>

// ADC indices
#define ADC_LOOP_START_CH 0
#define ADC_LOOP_LEN_CH   1
#define ADC_TUNE_CH       2
#define ADC_PM_CH         3
#define ADC_XFADE_LEN_CH  4
#define ADC_FX1_CH        5
#define ADC_FX2_CH        6

// Tunables for variable crossfade length
#define XF_MIN_SAMPLES   16u        // keep > 1
#define XF_MAX_SAMPLES   2048u      // pick a sane upper bound for your material

#define ADC_FILTER_DISPLAY_TICK_HZ (audio_rate / AUDIO_BLOCK_SIZE)
#define ADC_FILTER_CUTOFF_HZ 5.0f

// Include the actual WavInfo type from storage_wav_meta.h
#include "storage_wav_meta.h"

// These are your CURRENT globals in the default namespace
extern uint8_t*  audioData;          // Q15 bytes in PSRAM
extern uint32_t  audioDataSize;
extern uint32_t  audioSampleCount;   // number of int16 samples
extern sf::WavInfo   currentWav;         // has sampleRate (and other metadata)

// Make sf:: aliases that point at those globals, but ONLY within any TU
// that includes this header (that's the trick).
namespace sf {
  using ::audioData;
  using ::audioDataSize;
  using ::audioSampleCount;
  using ::currentWav;
}

#pragma once
#include <stdint.h>
#include <hardware/sync.h>   // for __compiler_memory_barrier()

// 0..4095 for start/len (ADC-style), playhead/total are sample indices or Q16.16 — your choice
typedef struct {
  volatile uint32_t seq;            // even = stable, odd = being written
  volatile uint16_t loop_start_q12; // [0..4095]
  volatile uint16_t loop_len_q12;   // [0..4095]
  volatile uint32_t playhead;       // sample idx or Q16.16
  volatile uint32_t total;          // total samples (or Q16.16 length)
} sf_display_state_t;


// Seqlock: writer flips an odd/even sequence around a field update; reader retries until it sees a stable, even sequence.
// For core1 display stability

extern volatile uint32_t g_core0_setup_done; 
extern sf_display_state_t g_disp;   // define once in a .cpp


static inline void core0_publish_setup_done(void) {
  __compiler_memory_barrier();
  g_core0_setup_done = 1;
  __compiler_memory_barrier();
}

static inline void disp_write_begin() {
  g_disp.seq++;                    // make odd
  __compiler_memory_barrier();
}
static inline void disp_write_end() {
  __compiler_memory_barrier();
  g_disp.seq++;                    // back to even
}

// Copy out a stable snapshot; returns when consistent.
static inline void disp_read_snapshot(sf_display_state_t* out) {
  while (1) {
    uint32_t a = g_disp.seq;
    __compiler_memory_barrier();
    uint16_t ls = g_disp.loop_start_q12;
    uint16_t ll = g_disp.loop_len_q12;
    uint32_t ph = g_disp.playhead;
    uint32_t tt = g_disp.total;
    __compiler_memory_barrier();
    uint32_t b = g_disp.seq;
    if ((a == b) && ((b & 1u) == 0)) {
      out->loop_start_q12 = ls;
      out->loop_len_q12   = ll;
      out->playhead       = ph;
      out->total          = tt;
      out->seq            = b;
      return;
    }
    // else: writer in progress; try again
  }
}

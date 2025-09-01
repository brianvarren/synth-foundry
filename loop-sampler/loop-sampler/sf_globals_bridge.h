#pragma once
#include <stdint.h>

// If you already have a header that defines WavMeta, include it here.
// Otherwise forward-declare the struct EXACTLY matching the real one.
struct WavMeta { uint32_t sampleRate; /* ...whatever else you have... */ };

// These are your CURRENT globals in the default namespace
extern uint8_t*  audioData;          // Q15 bytes in PSRAM
extern uint32_t  audioDataSize;
extern uint32_t  audioSampleCount;   // number of int16 samples
extern WavMeta   currentWav;         // has sampleRate

// Make sf:: aliases that point at those globals, but ONLY within any TU
// that includes this header (that’s the trick).
namespace sf {
  using ::audioData;
  using ::audioDataSize;
  using ::audioSampleCount;
  using ::currentWav;
}

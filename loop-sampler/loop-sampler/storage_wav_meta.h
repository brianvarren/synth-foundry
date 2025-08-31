#pragma once
#include <stdint.h>

namespace sf {

struct WavInfo {
  uint32_t dataSize;
  uint32_t sampleRate;
  uint16_t numChannels;
  uint16_t bitsPerSample;
  uint32_t dataOffset;   // byte offset to data chunk
  bool     ok;
};

// Read RIFF/WAVE header fields for a given path.
bool wav_read_info(const char* path, WavInfo& out);

} // namespace sf

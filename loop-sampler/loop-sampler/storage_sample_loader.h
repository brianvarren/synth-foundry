#pragma once
#include <stdint.h>

namespace sf {

// Reads WAV data into preallocated buffer `dst` (PSRAM) starting at dataOffset.
// Returns MB/s; writes bytesRead. Expects dstSize >= dataSize.
float wav_load_psram(const char* path,
                     uint8_t* dst,
                     uint32_t dstSize,
                     uint32_t* bytesRead);

} // namespace sf

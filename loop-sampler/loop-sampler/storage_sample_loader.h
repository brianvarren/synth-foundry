#pragma once
#include <stdint.h>

namespace sf {

// Loads WAV file, performs two-pass normalization to -3dB, converts to mono Q15.
// 
// Process:
// - Pass 1: Scan entire file to find peak amplitude
// - Pass 2: Convert to mono (averaging stereo), normalize to -3dB, output as Q15
// 
// Input: Any standard PCM WAV (8/16/24/32-bit, mono/stereo)
// Output: Normalized mono Q15 samples in PSRAM buffer
// 
// Returns: MB/s throughput; writes actual bytes written to bytesRead
// Note: dstSize must be >= (num_samples * 2) bytes for Q15 output

float wav_load_psram(const char* path,
                     uint8_t* dst,
                     uint32_t dstSize,
                     uint32_t* bytesRead);

} // namespace sf
#pragma once
#include <stdint.h>

namespace sf {

constexpr int MAX_WAV_FILES   = 100;
constexpr int MAX_NAME_LEN    = 64;

struct FileIndex {
  char     names[MAX_WAV_FILES][MAX_NAME_LEN];
  uint32_t sizes[MAX_WAV_FILES];
  int      count;
};

// Scan root (or a folder) for *.wav (case-insensitive). Returns true if ok.
bool file_index_scan(FileIndex& idx, const char* folder = "/");

// Return name by index (or nullptr if out of range)
const char* file_index_get(const FileIndex& idx, int i);

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

// float wav_load_psram(const char* path,
//                      uint8_t* dst,
//                      uint32_t dstSize,
//                      uint32_t* bytesRead);

// new:

// Pure decode: WAV (8/16/24/32-bit PCM, mono/stereo) → mono Q15 into caller buffer.
// - No allocation, no globals, no printing.
// - dst_q15 capacity (dst_bytes) must be >= required size (2 * total_samples).
// Returns true on success. Writes bytes written and MB/s (overall decode throughput).
bool wav_decode_q15_into_buffer(const char* path,
                                int16_t* dst_q15,
                                uint32_t dst_bytes,
                                uint32_t* out_bytes_written,
                                float* out_mbps);

// High level orchestrator: allocates PSRAM, decodes, and publishes globals.
// - Computes required bytes, checks PSRAM, pmallocs, decodes, sets audioData/audioSampleCount.
// - On failure, frees any allocation and returns false.
bool storage_load_sample_q15_psram(const char* path,
                                   float* out_mbps,
                                   uint32_t* out_bytes_read,
                                   uint32_t* out_required_bytes);

} // namespace sf

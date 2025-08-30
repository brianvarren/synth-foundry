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

} // namespace sf

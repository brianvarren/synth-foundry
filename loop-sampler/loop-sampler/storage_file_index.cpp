#include <Arduino.h>
#include <SdFat.h>
#include <string.h>
#include "storage_file_index.h"

extern SdFat sd;  // defined in storage_sd_hal.cpp

namespace sf {

static bool ends_with_wav_ci(const char* s) {
  int n = strlen(s);
  if (n < 4) return false;
  const char* ext = s + (n - 4);
  return (ext[0]=='.') &&
         ((ext[1]|32)=='w') &&
         ((ext[2]|32)=='a') &&
         ((ext[3]|32)=='v');
}

bool file_index_scan(FileIndex& idx, const char* folder) {
  idx.count = 0;

  FsFile dir;
  if (!dir.open(folder)) return false;

  FsFile e;
  while (e.openNext(&dir, O_RDONLY)) {
    if (!e.isDir()) {
      char nameBuf[MAX_NAME_LEN];
      e.getName(nameBuf, sizeof(nameBuf));
      if (ends_with_wav_ci(nameBuf) && idx.count < MAX_WAV_FILES) {
        // copy name
        strncpy(idx.names[idx.count], nameBuf, MAX_NAME_LEN - 1);
        idx.names[idx.count][MAX_NAME_LEN - 1] = '\0';
        // size
        idx.sizes[idx.count] = (uint32_t)e.fileSize();
        idx.count++;
      }
    }
    e.close();
  }

  dir.close();
  return true;
}

const char* file_index_get(const FileIndex& idx, int i) {
  if (i < 0 || i >= idx.count) return nullptr;
  return idx.names[i];
}

} // namespace sf

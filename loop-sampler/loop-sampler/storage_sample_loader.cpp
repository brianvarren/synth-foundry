#include <Arduino.h>
#include <SdFat.h>
#include "storage_wav_meta.h"
#include "storage_sample_loader.h"

extern SdFat sd;

namespace sf {

float wav_load_psram(const char* path,
                     uint8_t* dst,
                     uint32_t dstSize,
                     uint32_t* bytesRead) {
  *bytesRead = 0;

  WavInfo wi;
  if (!wav_read_info(path, wi) || !wi.ok) return 0.0f;

  // Clamp to what we can actually store
  uint32_t to_copy = wi.dataSize;
  if (to_copy > dstSize) {
    to_copy = dstSize;  // avoid overflow
  }

  FsFile f = sd.open(path, O_RDONLY);
  if (!f) return 0.0f;

  uint32_t t0 = millis();
  f.seek(wi.dataOffset);

  const uint32_t CHUNK = 4096;
  uint32_t remaining = wi.dataSize;
  uint8_t* p = dst;

  while (remaining) {
    uint32_t n = remaining > CHUNK ? CHUNK : remaining;
    int r = f.read(p, n);
    if (r <= 0) break;  // hard error
    if (r == 0) break;  // EOF/stall -> prevent infinite loop
    p += r;
    remaining -= r;
    *bytesRead += r;

    // Be kind to USB/serial/etc.
    yield();
  }

  f.close();

  uint32_t dt_ms = millis() - t0;
  float mb  = (*bytesRead) / (1024.0f * 1024.0f);
  float sec = dt_ms / 1000.0f;
  return (sec > 0.0f) ? (mb / sec) : 0.0f;
}

} // namespace sf

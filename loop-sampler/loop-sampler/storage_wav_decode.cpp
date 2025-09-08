#include <SdFat.h>
#include <string.h>
#include "storage_loader.h"
#include "storage_wav_meta.h"

extern SdFat sd;  // provided by SD HAL

namespace sf {

// Utility: sign-extend 24-bit little-endian to int32
static inline int32_t le24_to_i32(const uint8_t* p) {
  int32_t v = (int32_t)((uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16));
  if (v & 0x00800000) v |= 0xFF000000; // sign extend
  return v;
}

bool wav_decode_q15_into_buffer(const char* path,
                                int16_t* dst_q15,
                                uint32_t dst_bytes,
                                uint32_t* out_bytes_written,
                                float* out_mbps)
{
  if (out_bytes_written) *out_bytes_written = 0;
  if (out_mbps)          *out_mbps = 0.0f;

  // Parse header/meta
  WavInfo wi;
  if (!wav_read_info(path, wi) || !wi.ok) return false;
  if (wi.numChannels == 0 || wi.dataSize == 0) return false;
  if (wi.bitsPerSample != 8 && wi.bitsPerSample != 16 &&
      wi.bitsPerSample != 24 && wi.bitsPerSample != 32) return false;

  const uint32_t bytes_per_in = (wi.bitsPerSample / 8u) * (uint32_t)wi.numChannels;
  if (bytes_per_in == 0) return false;

  const uint32_t total_input_samples = wi.dataSize / bytes_per_in;   // after downmix
  const uint32_t required_out_bytes  = total_input_samples * 2u;     // Q15 mono
  if (dst_bytes < required_out_bytes) return false;

  FsFile f = sd.open(path, O_RDONLY);
  if (!f) return false;

  // Chunk buffer
  static const uint32_t CHUNK_RAW = 8192;
  static uint8_t  chunk_buf[CHUNK_RAW];

  // Pass 1: find peak after downmix
  float peak = 0.0f;
  {
    f.seekSet(wi.dataOffset);
    uint32_t remaining = wi.dataSize;
    while (remaining > 0) {
      uint32_t to_read = remaining > CHUNK_RAW ? CHUNK_RAW : remaining;
      to_read = (to_read / bytes_per_in) * bytes_per_in;
      if (to_read == 0) break;
      int r = f.read(chunk_buf, to_read);
      if (r <= 0) break;

      const uint32_t frames = (uint32_t)r / bytes_per_in;
      const int ch = wi.numChannels;
      const int bps = wi.bitsPerSample;
      const uint8_t* p = chunk_buf;
      for (uint32_t i = 0; i < frames; ++i) {
        float l = 0.0f, rch = 0.0f;
        if (bps == 8) {
          uint8_t a = *p++; l = ((int)a - 128) / 128.0f;
          if (ch == 2) { uint8_t b = *p++; rch = ((int)b - 128) / 128.0f; }
        } else if (bps == 16) {
          int16_t a = (int16_t)(p[0] | (p[1] << 8)); p += 2; l = (float)a / 32768.0f;
          if (ch == 2) { int16_t b = (int16_t)(p[0] | (p[1] << 8)); p += 2; rch = (float)b / 32768.0f; }
        } else if (bps == 24) {
          int32_t a = le24_to_i32(p); p += 3; l = (float)a / 8388608.0f;
          if (ch == 2) { int32_t b = le24_to_i32(p); p += 3; rch = (float)b / 8388608.0f; }
        } else {
          int32_t a = (int32_t)(p[0] | (p[1] << 8) | (p[2] << 16) | (p[3] << 24)); p += 4; l = (float)a / 2147483648.0f;
          if (ch == 2) { int32_t b = (int32_t)(p[0] | (p[1] << 8) | (p[2] << 16) | (p[3] << 24)); p += 4; rch = (float)b / 2147483648.0f; }
        }
        float mono = (ch == 2) ? 0.5f * (l + rch) : l;
        float aabs = mono >= 0.0f ? mono : -mono;
        if (aabs > peak) peak = aabs;
      }
      remaining -= (uint32_t)r;
    }
  }

  // Target -3 dB
  float gain = 1.0f;
  if (peak > 0.000001f) {
    const float target = 0.7071f;
    const float g = target / peak;
    gain = (g > 1.0f) ? 1.0f : g;
  }

  // Pass 2: decode into Q15 with gain, measure throughput
  uint32_t written_bytes = 0;
  uint32_t t0 = millis();
  {
    f.seekSet(wi.dataOffset);
    uint32_t remaining = wi.dataSize;
    uint32_t out_index = 0; // in samples
    while (remaining > 0) {
      uint32_t to_read = remaining > CHUNK_RAW ? CHUNK_RAW : remaining;
      to_read = (to_read / bytes_per_in) * bytes_per_in;
      if (to_read == 0) break;
      int r = f.read(chunk_buf, to_read);
      if (r <= 0) break;

      const uint32_t frames = (uint32_t)r / bytes_per_in;
      const int ch = wi.numChannels;
      const int bps = wi.bitsPerSample;
      const uint8_t* p = chunk_buf;
      for (uint32_t i = 0; i < frames; ++i) {
        float l = 0.0f, rch = 0.0f;
        if (bps == 8) {
          uint8_t a = *p++; l = ((int)a - 128) / 128.0f;
          if (ch == 2) { uint8_t b = *p++; rch = ((int)b - 128) / 128.0f; }
        } else if (bps == 16) {
          int16_t a = (int16_t)(p[0] | (p[1] << 8)); p += 2; l = (float)a / 32768.0f;
          if (ch == 2) { int16_t b = (int16_t)(p[0] | (p[1] << 8)); p += 2; rch = (float)b / 32768.0f; }
        } else if (bps == 24) {
          int32_t a = le24_to_i32(p); p += 3; l = (float)a / 8388608.0f;
          if (ch == 2) { int32_t b = le24_to_i32(p); p += 3; rch = (float)b / 8388608.0f; }
        } else {
          int32_t a = (int32_t)(p[0] | (p[1] << 8) | (p[2] << 16) | (p[3] << 24)); p += 4; l = (float)a / 2147483648.0f;
          if (ch == 2) { int32_t b = (int32_t)(p[0] | (p[1] << 8) | (p[2] << 16) | (p[3] << 24)); p += 4; rch = (float)b / 2147483648.0f; }
        }
        float mono = (ch == 2) ? 0.5f * (l + rch) : l;
        float s = mono * gain * 32767.0f;
        int32_t q = (int32_t)(s + (s >= 0 ? 0.5f : -0.5f));
        if (q >  32767) q =  32767;
        if (q < -32768) q = -32768;
        dst_q15[out_index++] = (int16_t)q;
      }
      written_bytes = out_index * 2u;
      remaining -= (uint32_t)r;
    }
  }

  f.close();

  const uint32_t dt_ms = millis() - t0;
  if (out_bytes_written) *out_bytes_written = written_bytes;
  if (out_mbps) {
    float mb = (float)written_bytes / (1024.0f * 1024.0f);
    float sec = (dt_ms > 0) ? (dt_ms / 1000.0f) : 0.0f;
    *out_mbps = (sec > 0.0f) ? (mb / sec) : 0.0f;
  }

  return (written_bytes == required_out_bytes);
}

} // namespace sf



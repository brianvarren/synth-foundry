#include <SdFat.h>
#include <string.h>
#include "audio_engine.h"
#include "storage_loader.h"
#include "storage_wav_meta.h"
#include "driver_sh1122.h"
#include "driver_sdcard.h"
#include "sf_globals_bridge.h"
#include "ui_display.h"

extern SdFat sd;  // defined in storage_sd_hal.cpp

// ────────────────────────────── Pure decoder ─────────────────────────────
// Utility: sign-extend 24-bit little-endian to int32
static inline int32_t le24_to_i32(const uint8_t* p) {
  // p[0] = LSB, p[1], p[2] = MSB (sign in bit 23)
  int32_t v = (int32_t)((uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16));
  if (v & 0x00800000) v |= 0xFF000000; // sign extend
  return v;
}

namespace sf {

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

  const uint32_t total_input_samples = wi.dataSize / bytes_per_in;   // samples after mixing to mono
  const uint32_t required_out_bytes  = total_input_samples * 2u;     // Q15 mono
  if (dst_bytes < required_out_bytes) return false;

  FsFile f = sd.open(path, O_RDONLY);
  if (!f) return false;

  // Constants
  static const uint32_t CHUNK_RAW = 8192;  // safe stackless static size
  static uint8_t  chunk_buf[CHUNK_RAW];

  // Pass 1: find peak (after downmix to mono, pre-normalization)
  float peak = 0.0f;
  {
    f.seekSet(wi.dataOffset);
    uint32_t remaining = wi.dataSize;

    while (remaining > 0) {
      uint32_t to_read = remaining > CHUNK_RAW ? CHUNK_RAW : remaining;
      // align to whole samples
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
          // 8-bit PCM is unsigned (0..255)
          uint8_t a = *p++;
          float v0 = ((int)a - 128) / 128.0f;
          l = v0;
          if (ch == 2) { uint8_t b = *p++; float v1 = ((int)b - 128) / 128.0f; rch = v1; }
        } else if (bps == 16) {
          int16_t a = (int16_t)(p[0] | (p[1] << 8)); p += 2;
          float v0 = (float)a / 32768.0f;
          l = v0;
          if (ch == 2) { int16_t b = (int16_t)(p[0] | (p[1] << 8)); p += 2; float v1 = (float)b / 32768.0f; rch = v1; }
        } else if (bps == 24) {
          int32_t a = le24_to_i32(p); p += 3;
          float v0 = (float)a / 8388608.0f; // 2^23
          l = v0;
          if (ch == 2) { int32_t b = le24_to_i32(p); p += 3; float v1 = (float)b / 8388608.0f; rch = v1; }
        } else { // 32-bit PCM signed
          int32_t a = (int32_t)(p[0] | (p[1] << 8) | (p[2] << 16) | (p[3] << 24)); p += 4;
          float v0 = (float)a / 2147483648.0f; // 2^31
          l = v0;
          if (ch == 2) { int32_t b = (int32_t)(p[0] | (p[1] << 8) | (p[2] << 16) | (p[3] << 24)); p += 4; float v1 = (float)b / 2147483648.0f; rch = v1; }
        }

        float mono = (ch == 2) ? 0.5f * (l + rch) : l;
        float aabs = mono >= 0.0f ? mono : -mono;
        if (aabs > peak) peak = aabs;
      }

      remaining -= (uint32_t)r;
    }
  }

  // Target peak: -3 dB (≈ 0.7071). If peak is tiny, keep gain = 1
  float gain = 1.0f;
  if (peak > 0.000001f) {
    const float target = 0.7071f;
    const float g = target / peak;
    gain = (g > 1.0f) ? 1.0f : g; // don't amplify above unity (optional)
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
          uint8_t a = *p++;
          float v0 = ((int)a - 128) / 128.0f;
          l = v0;
          if (ch == 2) { uint8_t b = *p++; float v1 = ((int)b - 128) / 128.0f; rch = v1; }
        } else if (bps == 16) {
          int16_t a = (int16_t)(p[0] | (p[1] << 8)); p += 2;
          float v0 = (float)a / 32768.0f;
          l = v0;
          if (ch == 2) { int16_t b = (int16_t)(p[0] | (p[1] << 8)); p += 2; float v1 = (float)b / 32768.0f; rch = v1; }
        } else if (bps == 24) {
          int32_t a = le24_to_i32(p); p += 3;
          float v0 = (float)a / 8388608.0f;
          l = v0;
          if (ch == 2) { int32_t b = le24_to_i32(p); p += 3; float v1 = (float)b / 8388608.0f; rch = v1; }
        } else {
          int32_t a = (int32_t)(p[0] | (p[1] << 8) | (p[2] << 16) | (p[3] << 24)); p += 4;
          float v0 = (float)a / 2147483648.0f;
          l = v0;
          if (ch == 2) { int32_t b = (int32_t)(p[0] | (p[1] << 8) | (p[2] << 16) | (p[3] << 24)); p += 4; float v1 = (float)b / 2147483648.0f; rch = v1; }
        }

        float mono = (ch == 2) ? 0.5f * (l + rch) : l;
        float s = mono * gain * 32767.0f;

        // Clamp and store as Q15
        int32_t q = (int32_t)(s + (s >= 0 ? 0.5f : -0.5f)); // round
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

// Convert various PCM formats to float for peak detection
// data points to the start of a sample frame (all channels)
// ch_idx is the channel index (0 = left/mono, 1 = right)
static float sample_to_float(const uint8_t* data, uint16_t bits, uint16_t channels, uint16_t ch_idx) {
  // Calculate byte offset for this channel
  uint32_t bytes_per_sample = bits / 8;
  uint32_t channel_offset = ch_idx * bytes_per_sample;
  const uint8_t* ch_data = data + channel_offset;
  
  switch(bits) {
    case 8: {
      // 8-bit WAV is unsigned (0-255, center at 128)
      int32_t v = ch_data[0] - 128;
      return v / 128.0f;
    }
    case 16: {
      // 16-bit signed little-endian
      int16_t v = (int16_t)(ch_data[0] | (ch_data[1] << 8));
      return v / 32768.0f;
    }
    case 24: {
      // 24-bit signed little-endian
      int32_t v = (ch_data[0]) | (ch_data[1] << 8) | (ch_data[2] << 16);
      // Sign extend from 24 to 32 bits
      if (v & 0x800000) v |= 0xFF000000;
      return v / 8388608.0f;  // 2^23
    }
    case 32: {
      // 32-bit signed little-endian
      int32_t v = (int32_t)(ch_data[0] | (ch_data[1] << 8) | 
                            (ch_data[2] << 16) | (ch_data[3] << 24));
      return v / 2147483648.0f;  // 2^31
    }
    default:
      return 0.0f;
  }
}

// Convert and normalize to Q15 with optional stereo-to-mono
static int16_t convert_to_q15(const uint8_t* data, uint16_t bits, 
                               uint16_t channels,
                               float norm_scale) {
  float sum = 0.0f;
  
  if (channels == 1) {
    // Mono: single sample
    sum = sample_to_float(data, bits, channels, 0);
  } else {
    // Stereo: average left and right
    float left = sample_to_float(data, bits, channels, 0);
    float right = sample_to_float(data, bits, channels, 1);
    sum = (left + right) * 0.5f;
  }
  
  // Apply normalization scale BEFORE converting to Q15
  sum *= norm_scale;
  
  // Convert to Q15
  int32_t q15_raw = (int32_t)(sum * 32767.0f);
  if (q15_raw > 32767) q15_raw = 32767;
  if (q15_raw < -32768) q15_raw = -32768;
  
  return (int16_t)q15_raw;
}

// ───────────────────────────── Orchestrator ─────────────────────────────

bool storage_load_sample_q15_psram(const char* path,
                                   float* out_mbps,
                                   uint32_t* out_bytes_read,
                                   uint32_t* out_required_bytes)
{
  if (out_mbps)        *out_mbps = 0.0f;
  if (out_bytes_read)  *out_bytes_read = 0;
  if (out_required_bytes) *out_required_bytes = 0;

  // Inspect WAV to compute required size
  WavInfo wi;
  if (!wav_read_info(path, wi) || !wi.ok) return false;
  const uint32_t bytes_per_in = (wi.bitsPerSample / 8u) * (uint32_t)wi.numChannels;
  if (bytes_per_in == 0) return false;

  const uint32_t total_input_samples = wi.dataSize / bytes_per_in;
  const uint32_t required_out_bytes  = total_input_samples * 2u; // mono Q15
  if (out_required_bytes) *out_required_bytes = required_out_bytes;

  // Optional headroom check
  #ifdef ARDUINO_ARCH_RP2040
  if (required_out_bytes > rp2040.getFreePSRAMHeap()) {
    return false;
  }
  #endif

  // Drop previous buffer (if any)
  if (audioData) {
    free(audioData);
    audioData = nullptr;
    audioDataSize = 0;
    audioSampleCount = 0;
  }

  // Allocate PSRAM
  uint8_t* buf = (uint8_t*)pmalloc(required_out_bytes);
  if (!buf) return false;

  // Decode into PSRAM
  uint32_t written = 0;
  float mbps = 0.0f;
  const bool ok = wav_decode_q15_into_buffer(path,
                                             (int16_t*)buf,
                                             required_out_bytes,
                                             &written,
                                             &mbps);

  if (!ok || written != required_out_bytes) {
    free(buf);
    return false;
  }

  // Publish globals
  audioData        = buf;
  audioDataSize    = written;
  audioSampleCount = written / 2u;

  // Source (WAV) sample rate from WavInfo
  const uint32_t src_rate_hz = wi.sampleRate;

  // Your PWM/engine output rate. Replace with your symbol if different:
  // e.g., DACless_get_sample_rate_hz(), AUDIO_OUT_RATE_HZ, or PWM_SAMPLE_RATE_HZ.
  const uint32_t out_rate_hz = audio_rate;   // <-- use your project’s constant

  // Tell the audio engine about the new PSRAM buffer and rates.
  playback_bind_loaded_buffer(src_rate_hz, out_rate_hz, audioSampleCount);

  if (out_mbps)       *out_mbps = mbps;
  if (out_bytes_read) *out_bytes_read = written;

  return true;
}

// float wav_load_psram(const char* path,
//                      uint8_t* dst,
//                      uint32_t dstSize,
//                      uint32_t* bytesRead) {
//   *bytesRead = 0;
  
//   WavInfo wi;
//   if (!wav_read_info(path, wi) || !wi.ok) return 0.0f;
  
//   // We're converting to mono Q15 (2 bytes per sample)
//   uint32_t bytes_per_input_sample = (wi.bitsPerSample / 8) * wi.numChannels;
//   uint32_t total_input_samples = wi.dataSize / bytes_per_input_sample;
//   uint32_t output_size = total_input_samples * 2;  // Q15 = 2 bytes per sample
  
//   if (output_size > dstSize) {
//     // Not enough space in destination
//     char line[64];
//     snprintf(line, sizeof(line), "Need %u bytes, have %u", output_size, dstSize);
//     view_print_line(line);
//     return 0.0f;
//   }
  
//   // Stack buffer for I/O
//   const uint32_t CHUNK_SIZE = 4096;
//   uint8_t chunk_buf[CHUNK_SIZE];
  
//   char line[96];
  
//   // ═══════════════════ PASS 1: Find Peak ═══════════════════
//   view_print_line("Pass 1: Finding peak...");
//   view_flush_if_dirty();
  
//   FsFile f = sd.open(path, O_RDONLY);
//   if (!f) return 0.0f;
  
//   f.seek(wi.dataOffset);
//   float peak = 0.0f;
//   uint32_t samples_processed = 0;
//   uint32_t remaining = wi.dataSize;
  
//   while (remaining > 0) {
//     uint32_t to_read = remaining > CHUNK_SIZE ? CHUNK_SIZE : remaining;
//     // Align to sample boundary
//     to_read = (to_read / bytes_per_input_sample) * bytes_per_input_sample;
//     if (to_read == 0) break;
    
//     int r = f.read(chunk_buf, to_read);
//     if (r <= 0) break;
    
//     uint32_t samples_in_chunk = r / bytes_per_input_sample;
//     for (uint32_t i = 0; i < samples_in_chunk; i++) {
//       const uint8_t* sample_ptr = chunk_buf + i * bytes_per_input_sample;
      
//       if (wi.numChannels == 1) {
//         float v = fabsf(sample_to_float(sample_ptr, wi.bitsPerSample, wi.numChannels, 0));
//         if (v > peak) peak = v;
//       } else {
//         // For stereo, check the averaged value for peak
//         float v_left = sample_to_float(sample_ptr, wi.bitsPerSample, wi.numChannels, 0);
//         float v_right = sample_to_float(sample_ptr, wi.bitsPerSample, wi.numChannels, 1);
//         float v_avg = fabsf((v_left + v_right) * 0.5f);
//         if (v_avg > peak) peak = v_avg;
//       }
//     }
    
//     samples_processed += samples_in_chunk;
//     remaining -= r;
//     yield();
//   }
  
//   f.close();
  
//   // Calculate normalization scale for -3dB
//   // -3dB = 0.7071 of full scale
//   // Guard against silence or very quiet files
//   if (peak < 0.0001f) peak = 1.0f;
  
//   float norm_scale = 0.7071f / peak;
//   if (norm_scale > 10.0f) norm_scale = 10.0f;  // Limit gain to 20dB
  
//   snprintf(line, sizeof(line), "Peak: %.4f, Scale: %.2fx", peak, norm_scale);
//   view_print_line(line);
  
//   // Debug: show first few raw samples
//   Serial.println("=== WAV Load Debug ===");
//   Serial.print("Peak found: ");
//   Serial.println(peak, 4);
//   Serial.print("Norm scale: ");
//   Serial.println(norm_scale, 4);
  
//   // ═══════════════════ PASS 2: Convert & Normalize ═══════════════════
//   view_print_line("Pass 2: Converting to Q15...");
//   view_flush_if_dirty();
  
//   f = sd.open(path, O_RDONLY);
//   if (!f) return 0.0f;
  
//   uint32_t t0 = millis();
//   f.seek(wi.dataOffset);
  
//   int16_t* out_ptr = (int16_t*)dst;
//   uint32_t out_samples = 0;
//   remaining = wi.dataSize;
  
//   // Debug: track first few converted samples
//   int debug_count = 0;
  
//   while (remaining > 0) {
//     uint32_t to_read = remaining > CHUNK_SIZE ? CHUNK_SIZE : remaining;
//     // Align to sample boundary
//     to_read = (to_read / bytes_per_input_sample) * bytes_per_input_sample;
//     if (to_read == 0) break;
    
//     int r = f.read(chunk_buf, to_read);
//     if (r <= 0) break;
    
//     uint32_t samples_in_chunk = r / bytes_per_input_sample;
//     for (uint32_t i = 0; i < samples_in_chunk; i++) {
//       const uint8_t* sample_ptr = chunk_buf + i * bytes_per_input_sample;
      
//       // Convert to normalized Q15 (mono)
//       int16_t q15_val = convert_to_q15(sample_ptr, wi.bitsPerSample, 
//                                         wi.numChannels, norm_scale);
//       *out_ptr++ = q15_val;
//       out_samples++;
      
//       // Debug first few samples AND samples around the middle
//       if (debug_count < 10 || (out_samples > total_input_samples/2 && debug_count < 20)) {
//         // Also show the raw float value before Q15 conversion
//         float raw_val = 0.0f;
//         if (wi.numChannels == 1) {
//           raw_val = sample_to_float(sample_ptr, wi.bitsPerSample, wi.numChannels, 0);
//         } else {
//           float left = sample_to_float(sample_ptr, wi.bitsPerSample, wi.numChannels, 0);
//           float right = sample_to_float(sample_ptr, wi.bitsPerSample, wi.numChannels, 1);
//           raw_val = (left + right) * 0.5f;
//         }
        
//         Serial.print("Sample ");
//         Serial.print(out_samples);
//         Serial.print(": raw=");
//         Serial.print(raw_val, 6);
//         Serial.print(" -> Q15=");
//         Serial.print(q15_val);
//         Serial.print(" (");
//         Serial.print(q15_val / 32768.0f, 4);
//         Serial.println(")");
//         debug_count++;
//       }
//     }
    
//     remaining -= r;
//     yield();
//   }
  
//   f.close();
  
//   *bytesRead = out_samples * 2;  // Q15 samples are 2 bytes each
  
//   uint32_t dt_ms = millis() - t0;
//   float mb = (*bytesRead) / (1024.0f * 1024.0f);
//   float sec = dt_ms / 1000.0f;
  
//   // Show conversion summary
//   snprintf(line, sizeof(line), "Output: Mono Q15, %u samples", out_samples);
//   view_print_line(line);
  
//   return (sec > 0.0f) ? (mb / sec) : 0.0f;
// }


} // namespace sf

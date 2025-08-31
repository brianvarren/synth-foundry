#include <Arduino.h>
#include <SdFat.h>
#include "storage_wav_meta.h"
#include "storage_sample_loader.h"
#include "display_views.h"

extern SdFat sd;

namespace sf {

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

float wav_load_psram(const char* path,
                     uint8_t* dst,
                     uint32_t dstSize,
                     uint32_t* bytesRead) {
  *bytesRead = 0;
  
  WavInfo wi;
  if (!wav_read_info(path, wi) || !wi.ok) return 0.0f;
  
  // We're converting to mono Q15 (2 bytes per sample)
  uint32_t bytes_per_input_sample = (wi.bitsPerSample / 8) * wi.numChannels;
  uint32_t total_input_samples = wi.dataSize / bytes_per_input_sample;
  uint32_t output_size = total_input_samples * 2;  // Q15 = 2 bytes per sample
  
  if (output_size > dstSize) {
    // Not enough space in destination
    char line[64];
    snprintf(line, sizeof(line), "Need %u bytes, have %u", output_size, dstSize);
    view_print_line(line);
    return 0.0f;
  }
  
  // Stack buffer for I/O
  const uint32_t CHUNK_SIZE = 4096;
  uint8_t chunk_buf[CHUNK_SIZE];
  
  char line[96];
  
  // ═══════════════════ PASS 1: Find Peak ═══════════════════
  view_print_line("Pass 1: Finding peak...");
  view_flush_if_dirty();
  
  FsFile f = sd.open(path, O_RDONLY);
  if (!f) return 0.0f;
  
  f.seek(wi.dataOffset);
  float peak = 0.0f;
  uint32_t samples_processed = 0;
  uint32_t remaining = wi.dataSize;
  
  while (remaining > 0) {
    uint32_t to_read = remaining > CHUNK_SIZE ? CHUNK_SIZE : remaining;
    // Align to sample boundary
    to_read = (to_read / bytes_per_input_sample) * bytes_per_input_sample;
    if (to_read == 0) break;
    
    int r = f.read(chunk_buf, to_read);
    if (r <= 0) break;
    
    uint32_t samples_in_chunk = r / bytes_per_input_sample;
    for (uint32_t i = 0; i < samples_in_chunk; i++) {
      const uint8_t* sample_ptr = chunk_buf + i * bytes_per_input_sample;
      
      if (wi.numChannels == 1) {
        float v = fabsf(sample_to_float(sample_ptr, wi.bitsPerSample, wi.numChannels, 0));
        if (v > peak) peak = v;
      } else {
        // For stereo, check the averaged value for peak
        float v_left = sample_to_float(sample_ptr, wi.bitsPerSample, wi.numChannels, 0);
        float v_right = sample_to_float(sample_ptr, wi.bitsPerSample, wi.numChannels, 1);
        float v_avg = fabsf((v_left + v_right) * 0.5f);
        if (v_avg > peak) peak = v_avg;
      }
    }
    
    samples_processed += samples_in_chunk;
    remaining -= r;
    yield();
  }
  
  f.close();
  
  // Calculate normalization scale for -3dB
  // -3dB = 0.7071 of full scale
  // Guard against silence or very quiet files
  if (peak < 0.0001f) peak = 1.0f;
  
  float norm_scale = 0.7071f / peak;
  if (norm_scale > 10.0f) norm_scale = 10.0f;  // Limit gain to 20dB
  
  snprintf(line, sizeof(line), "Peak: %.4f, Scale: %.2fx", peak, norm_scale);
  view_print_line(line);
  
  // Debug: show first few raw samples
  Serial.println("=== WAV Load Debug ===");
  Serial.print("Peak found: ");
  Serial.println(peak, 4);
  Serial.print("Norm scale: ");
  Serial.println(norm_scale, 4);
  
  // ═══════════════════ PASS 2: Convert & Normalize ═══════════════════
  view_print_line("Pass 2: Converting to Q15...");
  view_flush_if_dirty();
  
  f = sd.open(path, O_RDONLY);
  if (!f) return 0.0f;
  
  uint32_t t0 = millis();
  f.seek(wi.dataOffset);
  
  int16_t* out_ptr = (int16_t*)dst;
  uint32_t out_samples = 0;
  remaining = wi.dataSize;
  
  // Debug: track first few converted samples
  int debug_count = 0;
  
  while (remaining > 0) {
    uint32_t to_read = remaining > CHUNK_SIZE ? CHUNK_SIZE : remaining;
    // Align to sample boundary
    to_read = (to_read / bytes_per_input_sample) * bytes_per_input_sample;
    if (to_read == 0) break;
    
    int r = f.read(chunk_buf, to_read);
    if (r <= 0) break;
    
    uint32_t samples_in_chunk = r / bytes_per_input_sample;
    for (uint32_t i = 0; i < samples_in_chunk; i++) {
      const uint8_t* sample_ptr = chunk_buf + i * bytes_per_input_sample;
      
      // Convert to normalized Q15 (mono)
      int16_t q15_val = convert_to_q15(sample_ptr, wi.bitsPerSample, 
                                        wi.numChannels, norm_scale);
      *out_ptr++ = q15_val;
      out_samples++;
      
      // Debug first few samples AND samples around the middle
      if (debug_count < 10 || (out_samples > total_input_samples/2 && debug_count < 20)) {
        // Also show the raw float value before Q15 conversion
        float raw_val = 0.0f;
        if (wi.numChannels == 1) {
          raw_val = sample_to_float(sample_ptr, wi.bitsPerSample, wi.numChannels, 0);
        } else {
          float left = sample_to_float(sample_ptr, wi.bitsPerSample, wi.numChannels, 0);
          float right = sample_to_float(sample_ptr, wi.bitsPerSample, wi.numChannels, 1);
          raw_val = (left + right) * 0.5f;
        }
        
        Serial.print("Sample ");
        Serial.print(out_samples);
        Serial.print(": raw=");
        Serial.print(raw_val, 6);
        Serial.print(" -> Q15=");
        Serial.print(q15_val);
        Serial.print(" (");
        Serial.print(q15_val / 32768.0f, 4);
        Serial.println(")");
        debug_count++;
      }
    }
    
    remaining -= r;
    yield();
  }
  
  f.close();
  
  *bytesRead = out_samples * 2;  // Q15 samples are 2 bytes each
  
  uint32_t dt_ms = millis() - t0;
  float mb = (*bytesRead) / (1024.0f * 1024.0f);
  float sec = dt_ms / 1000.0f;
  
  // Show conversion summary
  snprintf(line, sizeof(line), "Output: Mono Q15, %u samples", out_samples);
  view_print_line(line);
  
  return (sec > 0.0f) ? (mb / sec) : 0.0f;
}

} // namespace sf
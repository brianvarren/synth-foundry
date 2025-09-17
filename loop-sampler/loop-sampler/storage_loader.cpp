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

namespace sf {

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

  // Calculate available PSRAM after freeing current waveform
  uint32_t available_psram = rp2040.getFreePSRAMHeap();
  if (audioData && audioDataSize > 0) {
    // If there's a current waveform, we can free it and use that space too
    available_psram += audioDataSize;
  }

  // Check if new file will fit
  #ifdef ARDUINO_ARCH_RP2040
  if (required_out_bytes > available_psram) {
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
  const bool ok = wav_decode_q15_into_buffer(path, (int16_t*)buf, required_out_bytes, &written, &mbps);

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

} // namespace sf

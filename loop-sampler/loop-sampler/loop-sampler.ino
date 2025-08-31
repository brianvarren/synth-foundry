/**
 * Loop Sampler for Olimex Pico2-XXL
 * Main sketch that coordinates display, SD card, and audio playback
 * 
 * Hardware: RP2040 with PSRAM, SH1122 OLED, SD Card
 */

#include <Arduino.h>
#include "display.h"    // umbrella: driver + views
#include "storage.h"    // umbrella: sd_hal + wav_meta + file_index + sample_loader
#include "ui.h"
#include "ui_waveform.h"  // NEW: Waveform display

using namespace sf;

// ────────────────────────── Global State ───────────────────────────────────
// Made global (outside namespace) so ui_browser.cpp can access them for waveform display
uint8_t* audioData = nullptr;         // PSRAM buffer for Q15 audio samples
uint32_t audioDataSize = 0;           // Number of bytes in Q15 buffer
uint32_t audioSampleCount = 0;        // Number of Q15 samples
sf::WavInfo currentWav;                // Original WAV file metadata (pre-conversion)

// ───────────────────────── Initialize PSRAM ───────────────────────────────────
bool initPSRAM() {
  view_print_line("=== PSRAM ===");
  
  if (!rp2040.getPSRAMSize()) {
    view_print_line("❌ No PSRAM");
    return false;
  }
  
  uint32_t totalMB = rp2040.getPSRAMSize() / 1048576;
  uint32_t freeKB  = rp2040.getFreePSRAMHeap() / 1024;
  
  view_print_line((String("✅ ") + totalMB + " MB, free " + freeKB + " KB").c_str());
  return true;
}

// ───────────────────────── Load WAV Sample into PSRAM ─────────────────────
bool loadWavSample(const char* filename) {
  char line[96];

  view_print_line("");
  view_print_line("=== Loading WAV ===");
  snprintf(line, sizeof(line), "File: %s", filename);
  view_print_line(line);

  // Parse WAV header
  if (!wav_read_info(filename, currentWav) || !currentWav.ok) {
    view_print_line("❌ Can't read WAV");
    return false;
  }

  // Show original WAV properties
  {
    char sizeBuf[16];
    sd_format_size(currentWav.dataSize, sizeBuf, sizeof(sizeBuf));
    snprintf(line, sizeof(line), "Input: %uch, %u Hz",
             (unsigned)currentWav.numChannels, (unsigned)currentWav.sampleRate);
    view_print_line(line);

    snprintf(line, sizeof(line), "%u-bit, %s",
             (unsigned)currentWav.bitsPerSample, sizeBuf);
    view_print_line(line);
  }

  if (currentWav.dataSize == 0) {
    view_print_line("❌ Empty data chunk");
    return false;
  }

  // Calculate output size (mono Q15)
  uint32_t bytes_per_input_sample = (currentWav.bitsPerSample / 8) * currentWav.numChannels;
  uint32_t total_input_samples = currentWav.dataSize / bytes_per_input_sample;
  uint32_t required_output_size = total_input_samples * 2;  // Q15 = 2 bytes per sample

  // PSRAM capacity check
  uint32_t free_psram = rp2040.getFreePSRAMHeap();
  if (required_output_size > free_psram) {
    char needBuf[16], haveBuf[16];
    sd_format_size(required_output_size, needBuf, sizeof(needBuf));
    sd_format_size(free_psram, haveBuf, sizeof(haveBuf));
    snprintf(line, sizeof(line), "❌ PSRAM too small (need %s, have %s)", needBuf, haveBuf);
    view_print_line(line);
    return false;
  }

  // Free prior buffer if any
  if (audioData) {
    free(audioData);
    audioData = nullptr;
    audioDataSize = 0;
    audioSampleCount = 0;
  }

  // Allocate for Q15 output
  audioData = (uint8_t*)pmalloc(required_output_size);
  if (!audioData) {
    view_print_line("❌ Alloc failed");
    return false;
  }

  // Load, normalize, and convert to Q15
  view_print_line("Processing...");
  view_flush_if_dirty();  // show "Processing..." before long I/O

  uint32_t bytesRead = 0;
  float mbps = wav_load_psram(filename, audioData, required_output_size, &bytesRead);

  if (bytesRead == 0) {
    view_print_line("❌ Load failed");
    free(audioData);
    audioData = nullptr;
    return false;
  }

  audioDataSize = bytesRead;
  audioSampleCount = bytesRead / 2;  // Q15 samples are 2 bytes each

  // Success summary
  {
    char sizeBuf[16];
    sd_format_size(bytesRead, sizeBuf, sizeof(sizeBuf));
    snprintf(line, sizeof(line), "Speed: %.2f MB/s", mbps);
    view_print_line(line);
    snprintf(line, sizeof(line), "✅ Loaded %s (%u samples)", sizeBuf, audioSampleCount);
    view_print_line(line);
  }

  return true;
}

// ───────────────────────── Display Audio Data Sample ──────────────────────
void showAudioDataSample() {
  if (!audioData || audioDataSize == 0) {
    view_print_line("No audio loaded");
    return;
  }
  
  view_print_line("");
  view_print_line("=== Q15 Samples (hex) ===");
  
  // Show first 16 Q15 samples as hex
  int16_t* samples = (int16_t*)audioData;
  uint32_t samples_to_show = min(16u, audioSampleCount);
  
  String hexLine = "";
  for (uint32_t i = 0; i < samples_to_show; i++) {
    char hex[8];
    sprintf(hex, "%04X ", (uint16_t)samples[i]);
    hexLine += hex;
    
    if ((i + 1) % 4 == 0) {
      view_print_line(hexLine.c_str());
      hexLine = "";
    }
  }
  if (hexLine.length() > 0) view_print_line(hexLine.c_str());
  
  // Show first few samples as decimal values
  view_print_line("");
  view_print_line("=== Q15 Values ===");
  for (uint32_t i = 0; i < min(8u, audioSampleCount); i++) {
    char line[32];
    float normalized = samples[i] / 32768.0f;
    snprintf(line, sizeof(line), "[%u]: %d (%.4f)", i, samples[i], normalized);
    view_print_line(line);
  }
}

// ───────────────────────── List and Count WAV Files ───────────────────────
int listWavFiles() {
  view_print_line("");
  view_print_line("=== WAV Files ===");

  FileIndex idx;
  if (!file_index_scan(idx)) {
    view_print_line("❌ SD scan failed");
    return 0;
  }

  if (idx.count == 0) {
    view_print_line("No WAV files found");
    return 0;
  }

  for (int i = 0; i < idx.count && i < 20; ++i) {
    char sizeBuf[16];
    sd_format_size(idx.sizes[i], sizeBuf, sizeof(sizeBuf));
    view_print_line((String(i + 1) + ". " + idx.names[i] + " (" + sizeBuf + ")").c_str());
  }
  view_print_line((String("Total: ") + idx.count + " files").c_str());
  return idx.count;
}

// ───────────────────────── Arduino Setup ──────────────────────────────────────
void setup() {
  Serial.begin(115200);
  while(!Serial);
  delay(1000);
  Serial.println("\n=== Olimex Pico2-XXL Loop Sampler ===\n");
  
  // Display first (feedback)
  display_init();
  view_show_status("Loop Sampler", "Initializing");
  view_clear_log();
  view_print_line("=== Loop Sampler ===");
  view_print_line("Q15 DSP Mode");
  view_print_line("Initializing...");
  view_flush_if_dirty();
  delay(500);
  
  // PSRAM
  if (!initPSRAM()) {
    view_flush_if_dirty();
    while (1) delay(100);
  }
  view_flush_if_dirty();
  delay(500);
  
  // SD card
  if (!sd_begin()) {
    view_print_line("❌ SD Card failed!");
    view_flush_if_dirty();
    while (1) delay(100);
  }
  {
    float mb = sd_card_size_mb();
    view_print_line("✅ SD Card ready");
    view_print_line((String("Size: ") + String(mb, 1) + " MB").c_str());
    view_flush_if_dirty();
  }
  delay(500);
  
  ui_input_init();
  browser_init(loadWavSample);

  //view_set_auto_scroll(false);
}

// ───────────────────────── Arduino Loop ───────────────────────────────────
void loop() {

  ui_input_update();

  browser_tick();

  view_handle_scroll(millis());
  view_flush_if_dirty();

  // TODO: Add button handling for sample triggering
  // TODO: Add UART MIDI handling
  // TODO: Add audio playback via PWM/I2S (Q15 format ready)
  // TODO: Add effects processing (Q15 DSP chain)
  
  //delay(10);
}
/**
 * Loop Sampler for Olimex Pico2-XXL
 * Main sketch that coordinates display, SD card, and audio playback
 * 
 * Hardware: RP2040 with PSRAM, SH1122 OLED, SD Card
 */

#include <Arduino.h>
#include "display.h"    // umbrella: driver + views
#include "storage.h"    // umbrella: sd_hal + wav_meta + file_index + sample_loader

using namespace sf;

// ────────────────────────── Global State ───────────────────────────────────
uint8_t* audioData = nullptr;         // PSRAM buffer for audio sample
uint32_t audioDataSize = 0;           // Number of sample bytes
WavInfo  currentWav;                  // WAV file metadata

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
  view_print_line("");
  view_print_line("=== Loading WAV ===");
  view_print_line((String("File: ") + filename).c_str());
  
  // Read WAV header
  if (!wav_read_info(filename, currentWav) || !currentWav.ok) {
    view_print_line("❌ Can't read WAV");
    return false;
  }
  
  // Show WAV props
  {
    char sizeBuf[16];
    sd_format_size(currentWav.dataSize, sizeBuf, sizeof(sizeBuf));
    view_print_line((String("Ch: ") + currentWav.numChannels +
                     ", Rate: " + currentWav.sampleRate + "Hz").c_str());
    view_print_line((String("Bits: ") + currentWav.bitsPerSample +
                     ", Size: " + sizeBuf).c_str());
  }

  // Check PSRAM availability
  if (currentWav.dataSize > rp2040.getFreePSRAMHeap()) {
    view_print_line("❌ PSRAM too small!");
    return false;
  }
  
  // Free previous allocation if exists
  if (audioData) {
    free(audioData);
    audioData = nullptr;
  }
  
  // Allocate PSRAM
  audioData = (uint8_t*)pmalloc(currentWav.dataSize);
  if (!audioData) {
    view_print_line("❌ Alloc failed");
    return false;
  }
  audioDataSize = currentWav.dataSize;
  
  // Load the audio data
  view_print_line("Reading...");
  view_flush_if_dirty();  // show progress before long I/O
  
  uint32_t bytesRead = 0;
  float readSpeedMBs = wav_load_psram(filename, audioData, audioDataSize, &bytesRead);
  
  if (bytesRead != audioDataSize) {
    view_print_line("❌ Read error");
    free(audioData);
    audioData = nullptr;
    audioDataSize = 0;
    return false;
  }
  
  {
    char sizeBuf[16];
    sd_format_size(bytesRead, sizeBuf, sizeof(sizeBuf));
    view_print_line((String("Speed: ") + String(readSpeedMBs, 2) + " MB/s").c_str());
    view_print_line((String("✅ Loaded ") + sizeBuf).c_str());
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
  view_print_line("=== First 32 bytes ===");
  
  String hexLine = "";
  for (uint32_t i = 0; i < min(32u, audioDataSize); i++) {
    char hex[4];
    sprintf(hex, "%02X ", audioData[i]);
    hexLine += hex;
    
    if ((i + 1) % 8 == 0) {
      view_print_line(hexLine.c_str());
      hexLine = "";
    }
  }
  if (hexLine.length() > 0) view_print_line(hexLine.c_str());
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
  delay(1000);
  Serial.println("\n=== Olimex Pico2-XXL Loop Sampler ===\n");
  
  // Display first (feedback)
  display_init();
  view_show_status("Loop Sampler", "Initializing");
  view_clear_log();
  view_print_line("=== Loop Sampler ===");
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
  
  // List WAVs
  int wavCount = listWavFiles();
  view_flush_if_dirty();
  if (wavCount == 0) {
    view_print_line("");
    view_print_line("No samples to load!");
    view_flush_if_dirty();
    return;
  }
  
  delay(1000);
  
  // Load first WAV
  {
    FileIndex idx;
    if (file_index_scan(idx) && idx.count > 0) {
      if (loadWavSample(idx.names[0])) {
        showAudioDataSample();
      }
    }
  }
  
  view_print_line("");
  view_print_line("=== Ready! ===");
  view_print_line("↑/↓ to scroll");
  view_flush_if_dirty();
}

// ───────────────────────── Arduino Loop ───────────────────────────────────
void loop() {
  view_handle_scroll(millis());
  view_flush_if_dirty();

  // TODO: Add button handling for sample triggering
  // TODO: Add UART MIDI handling
  // TODO: Add audio playback via PWM
  // TODO: Add effects processing
  
  delay(10);
}

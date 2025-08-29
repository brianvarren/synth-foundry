

/**
 * Loop Sampler for Olimex Pico2-XXL
 * Main sketch that coordinates display, SD card, and audio playback
 * 
 * Hardware: RP2040 with PSRAM, SH1122 OLED, SD Card
 */

#include <Arduino.h>
#include "display.h"   // umbrella: driver + views
#include "sdcard.h"

using namespace sf;

// ────────────────────────── Global State ───────────────────────────────────
uint8_t* audioData = nullptr;         // Pointer to allocated PSRAM for audio sample
uint32_t audioDataSize = 0;           // Number of sample bytes
WavInfo currentWav;                   // Current WAV file metadata

// ───────────────────────── Initialize PSRAM ───────────────────────────────────
bool initPSRAM() {
  view_print_line("=== PSRAM ===");
  
  if (!rp2040.getPSRAMSize()) {
    view_print_line("❌ No PSRAM");
    return false;
  }
  
  uint32_t totalMB = rp2040.getPSRAMSize() / 1048576;
  uint32_t freeKB  = rp2040.getFreePSRAMHeap() / 1024;
  
  view_print_line(("✅ " + String(totalMB) + " MB, free " + String(freeKB) + " KB").c_str());
  return true;
}

// ───────────────────────── Load WAV Sample into PSRAM ─────────────────────
bool loadWavSample(const char* filename) {
  view_print_line("");
  view_print_line("=== Loading WAV ===");
  view_print_line(("File: " + String(filename)).c_str());
  
  // Get WAV info first
  if (!SDCard.getWavInfo(filename, currentWav)) {
    view_print_line("❌ Can't read WAV");
    return false;
  }
  
  // Display WAV properties
  view_print_line((String("Ch: ") + currentWav.numChannels +
                   ", Rate: " + currentWav.sampleRate + "Hz").c_str());
  view_print_line((String("Bits: ") + currentWav.bitsPerSample +
                   ", Size: " + SDCard.formatSize(currentWav.dataSize)).c_str());
  
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
  view_flush_if_dirty();  // Force display update before long operation
  
  uint32_t bytesRead;
  float readSpeed = SDCard.loadWavData(filename, audioData, audioDataSize, &bytesRead);
  
  if (bytesRead != audioDataSize) {
    view_print_line("❌ Read error");
    free(audioData);
    audioData = nullptr;
    audioDataSize = 0;
    return false;
  }
  
  view_print_line((String("Speed: ") + String(readSpeed, 2) + " MB/s").c_str());
  view_print_line((String("✅ Loaded ") + SDCard.formatSize(bytesRead)).c_str());
  
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
  
  if (hexLine.length() > 0) {
    view_print_line(hexLine.c_str());
  }
}

// ───────────────────────── List and Count WAV Files ───────────────────────
int listWavFiles() {
  view_print_line("");
  view_print_line("=== WAV Files ===");
  
  String files[20];
  uint32_t sizes[20];
  int count = SDCard.listWavFiles(files, sizes, 20);
  
  if (count == 0) {
    view_print_line("No WAV files found");
  } else {
    for (int i = 0; i < count; i++) {
      view_print_line((String(i + 1) + ". " + files[i] + 
                      " (" + SDCard.formatSize(sizes[i]) + ")").c_str());
    }
    view_print_line((String("Total: ") + count + " files").c_str());
  }
  
  return count;
}

// ───────────────────────── Arduino Setup ──────────────────────────────────────
void setup() {
  Serial.begin(115200);
  delay(1000); // Give time for serial monitor to connect
  
  Serial.println("\n=== Olimex Pico2-XXL Loop Sampler ===\n");
  
  // Initialize display first for visual feedback
  display_init();
  view_show_status("Loop Sampler", "Initializing");
  view_clear_log();
  view_print_line("=== Loop Sampler ===");
  view_print_line("Initializing...");
  view_flush_if_dirty();
  delay(500);
  
  // Initialize PSRAM
  if (!initPSRAM()) {
    view_flush_if_dirty();
    while(1) delay(100); // Halt if no PSRAM
  }
  view_flush_if_dirty();
  delay(500);
  
  // Initialize SD Card
  if (!SDCard.begin()) {
    view_print_line("❌ SD Card failed!");
    view_flush_if_dirty();
    while(1) delay(100); // Halt if no SD
  }
  
  view_print_line((String("✅ SD Card ready")).c_str());
  view_print_line((String("Size: ") + String(SDCard.getCardSizeMB(), 1) + " MB").c_str());
  view_flush_if_dirty();
  delay(500);
  
  // List available WAV files
  int wavCount = listWavFiles();
  view_flush_if_dirty();
  
  if (wavCount == 0) {
    view_print_line("");
    view_print_line("No samples to load!");
    view_flush_if_dirty();
    return;
  }
  
  delay(1000);
  
  // Load the first WAV file found
  String firstWav = SDCard.getFirstWavFile();
  if (firstWav.length() > 0) {
    if (loadWavSample(firstWav.c_str())) {
      showAudioDataSample();
    }
  }
  
  view_print_line("");
  view_print_line("=== Ready! ===");
  view_print_line("↑/↓ to scroll");
  view_flush_if_dirty();
}

// ───────────────────────── Arduino Loop ───────────────────────────────────
void loop() {
  // Handle display scrolling & redraw if needed
  view_handle_scroll(millis());
  view_flush_if_dirty();

  // TODO: Add button handling for sample triggering
  // TODO: Add UART MIDI handling
  // TODO: Add audio playback via PWM
  // TODO: Add effects processing
  
  delay(10);
}

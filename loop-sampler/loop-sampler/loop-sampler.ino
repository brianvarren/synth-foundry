/**
 * Loop Sampler for Olimex Pico2-XXL
 * Main sketch that coordinates display, SD card, and audio playback
 * 
 * Hardware: RP2040 with PSRAM, SH1122 OLED, SD Card
 */

#include <Arduino.h>
#include "display.h"
#include "sdcard.h"

// ────────────────────────── Global State ───────────────────────────────────
uint8_t* audioData = nullptr;         // Pointer to allocated PSRAM for audio sample
uint32_t audioDataSize = 0;           // Number of sample bytes
WavInfo currentWav;                   // Current WAV file metadata

// ───────────────────────── Initialize PSRAM ───────────────────────────────────
bool initPSRAM() {
  Display.addLine("=== PSRAM ===");
  
  if (!rp2040.getPSRAMSize()) {
    Display.addLine("❌ No PSRAM");
    return false;
  }
  
  uint32_t totalMB = rp2040.getPSRAMSize() / 1048576;
  uint32_t freeKB = rp2040.getFreePSRAMHeap() / 1024;
  
  Display.addLine("✅ " + String(totalMB) + " MB, free " + String(freeKB) + " KB");
  return true;
}

// ───────────────────────── Load WAV Sample into PSRAM ─────────────────────
bool loadWavSample(const char* filename) {
  Display.addLine("");
  Display.addLine("=== Loading WAV ===");
  Display.addLine("File: " + String(filename));
  
  // Get WAV info first
  if (!SDCard.getWavInfo(filename, currentWav)) {
    Display.addLine("❌ Can't read WAV");
    return false;
  }
  
  // Display WAV properties
  Display.addLine("Ch: " + String(currentWav.numChannels) + 
                 ", Rate: " + String(currentWav.sampleRate) + "Hz");
  Display.addLine("Bits: " + String(currentWav.bitsPerSample) + 
                 ", Size: " + SDCard.formatSize(currentWav.dataSize));
  
  // Check PSRAM availability
  if (currentWav.dataSize > rp2040.getFreePSRAMHeap()) {
    Display.addLine("❌ PSRAM too small!");
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
    Display.addLine("❌ Alloc failed");
    return false;
  }
  
  audioDataSize = currentWav.dataSize;
  
  // Load the audio data
  Display.addLine("Reading...");
  Display.update();  // Force display update before long operation
  
  uint32_t bytesRead;
  float readSpeed = SDCard.loadWavData(filename, audioData, audioDataSize, &bytesRead);
  
  if (bytesRead != audioDataSize) {
    Display.addLine("❌ Read error");
    free(audioData);
    audioData = nullptr;
    audioDataSize = 0;
    return false;
  }
  
  Display.addLine("Speed: " + String(readSpeed, 2) + " MB/s");
  Display.addLine("✅ Loaded " + SDCard.formatSize(bytesRead));
  
  return true;
}

// ───────────────────────── Display Audio Data Sample ──────────────────────
void showAudioDataSample() {
  if (!audioData || audioDataSize == 0) {
    Display.addLine("No audio loaded");
    return;
  }
  
  Display.addLine("");
  Display.addLine("=== First 32 bytes ===");
  
  String hexLine = "";
  for (int i = 0; i < min(32u, audioDataSize); i++) {
    char hex[4];
    sprintf(hex, "%02X ", audioData[i]);
    hexLine += hex;
    
    if ((i + 1) % 8 == 0) {
      Display.addLine(hexLine);
      hexLine = "";
    }
  }
  
  if (hexLine.length() > 0) {
    Display.addLine(hexLine);
  }
}

// ───────────────────────── List and Count WAV Files ───────────────────────
int listWavFiles() {
  Display.addLine("");
  Display.addLine("=== WAV Files ===");
  
  String files[20];
  uint32_t sizes[20];
  int count = SDCard.listWavFiles(files, sizes, 20);
  
  if (count == 0) {
    Display.addLine("No WAV files found");
  } else {
    for (int i = 0; i < count; i++) {
      Display.addLine(String(i + 1) + ". " + files[i] + 
                     " (" + SDCard.formatSize(sizes[i]) + ")");
    }
    Display.addLine("Total: " + String(count) + " files");
  }
  
  return count;
}

// ───────────────────────── Arduino Setup ──────────────────────────────────────
void setup() {
  Serial.begin(115200);
  delay(1000); // Give time for serial monitor to connect
  
  Serial.println("\n=== Olimex Pico2-XXL Loop Sampler ===\n");
  
  // Initialize display first for visual feedback
  Display.begin();
  Display.addLine("=== Loop Sampler ===");
  Display.addLine("Initializing...");
  Display.update();
  delay(500);
  
  // Initialize PSRAM
  if (!initPSRAM()) {
    Display.update();
    while(1) delay(100); // Halt if no PSRAM
  }
  Display.update();
  delay(500);
  
  // Initialize SD Card
  if (!SDCard.begin()) {
    Display.addLine("❌ SD Card failed!");
    Display.update();
    while(1) delay(100); // Halt if no SD
  }
  
  Display.addLine("✅ SD Card ready");
  Display.addLine("Size: " + String(SDCard.getCardSizeMB(), 1) + " MB");
  Display.update();
  delay(500);
  
  // List available WAV files
  int wavCount = listWavFiles();
  Display.update();
  
  if (wavCount == 0) {
    Display.addLine("");
    Display.addLine("No samples to load!");
    Display.update();
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
  
  Display.addLine("");
  Display.addLine("=== Ready! ===");
  Display.addLine("↑/↓ to scroll");
  Display.update();
}

// ───────────────────────── Arduino Loop ───────────────────────────────────
void loop() {
  // Handle display scrolling
  Display.handleScroll();
  
  // Update display if needed
  if (Display.needsRedraw()) {
    Display.update();
  }
  
  // TODO: Add button handling for sample triggering
  // TODO: Add UART MIDI handling
  // TODO: Add audio playback via PWM
  // TODO: Add effects processing
  
  delay(10);
}
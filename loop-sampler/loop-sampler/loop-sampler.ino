/**
 * Loop Sampler for Olimex Pico2-XXL
 * Main sketch that coordinates display, SD card, and audio playback
 * 
 * Hardware: RP2040 with PSRAM, SH1122 OLED, SD Card
 */

#include <Arduino.h>
#include "driver_sh1122.h"
#include "driver_sdcard.h"
#include "ui_display.h"
#include "ui_input.h"
#include "storage_loader.h"
#include "storage_wav_meta.h"

using namespace sf;

// ────────────────────────── Global State ───────────────────────────────────
// Made global (outside namespace) so ui_browser.cpp can access them for waveform display
uint8_t* audioData = nullptr;         // PSRAM buffer for Q15 audio samples
uint32_t audioDataSize = 0;           // Number of bytes in Q15 buffer
uint32_t audioSampleCount = 0;        // Number of Q15 samples
WavInfo currentWav;                    // Original WAV file metadata (pre-conversion)

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

// ───────────────────────── Arduino Setup ──────────────────────────────────────
void setup() {
  Serial.begin(115200);
  while(!Serial);
  delay(1000);
  Serial.println("\n=== Olimex Pico2-XXL Loop Sampler ===\n");
  
  // Initialize display hardware ONLY (stays in DS_SETUP state)
  display_init();
  
  // Now we can show status messages
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
  
  // Initialize input system
  ui_input_init();
  
  // Start display timer at 30 FPS (you can adjust this)
  if (!display_timer_begin(30)) {
    Serial.println("Warning: Failed to start display timer");
    // Not fatal - display_tick() can still be called manually from loop()
  }
  
  // Signal that setup is complete - this will scan files and enter browser
  display_setup_complete();
}

// ───────────────────────── Arduino Loop ───────────────────────────────────
void loop() {
  // Update input system
  ui_input_update();

  // Display system tick - returns immediately if no update needed
  display_tick();

  // Legacy scroll handling (if still needed)
  view_handle_scroll(millis());
  view_flush_if_dirty();

  // Small delay to prevent CPU hogging
  // (can be removed if timer ISR is handling all timing)
  delay(1);
}
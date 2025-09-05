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
#include "sf_globals_bridge.h"
#include "audio_engine.h"

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
  
  // Initialize audio engine
  audio_init();

  // Set debug level for audio engine
  audio_engine_debug_set_level(AE_DBG_INFO);  // or AE_DBG_ERR, AE_DBG_TRACE, AE_DBG_OFF
  
  // Signal that setup is complete - this will scan files and enter browser
  core0_publish_setup_done();
}

static uint32_t next_ms = 0;
static const uint32_t kFrameIntervalMs = 16; // ~60 Hz

void setup1(){
  display_init();
}

// ───────────────────────── Arduino Loop ───────────────────────────────────
void loop() {

  audio_tick();
  
  static uint32_t last = 0;
  if (millis() - last >= 250) {
    last += 250;
    Serial.print('.');
    audio_engine_debug_poll();  // must be reachable
    Serial.flush();
  }
}

void loop1() {

  static bool s_boot_done = false;

  // Wait until core0 finished setup(), then enter browser once
  if (!s_boot_done) {
    if (g_core0_setup_done) {
      display_setup_complete();              // calls file_index_scan + first render
      s_boot_done = true;                    // guard: call only once
    } else {
      // keep core1 gentle while waiting
      delayMicroseconds(200);
    }
    return;
  }

  // Update input system
  ui_input_update();

  display_tick();
}
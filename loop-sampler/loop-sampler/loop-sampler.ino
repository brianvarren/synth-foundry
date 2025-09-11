/**
 * @file loop-sampler.ino
 * @brief Main Arduino sketch for the Loop Sampler - a real-time audio looper and sampler
 * 
 * This is the central orchestrator for a sophisticated loop sampler built on the Olimex Pico2-XXL.
 * The system uses the RP2040's dual cores to achieve real-time audio processing while maintaining
 * a responsive user interface.
 * 
 * ## Architecture Overview
 * 
 * **Core 0 (Main Core):**
 * - Handles audio processing and real-time sample playback
 * - Manages SD card operations and sample loading
 * - Processes ADC inputs for real-time control
 * - Runs the main audio engine with crossfading and pitch shifting
 * 
 * **Core 1 (Secondary Core):**
 * - Manages the OLED display (SH1122) and user interface
 * - Handles file browsing and waveform visualization
 * - Processes encoder and button inputs
 * - Updates display at ~60Hz independently of audio processing
 * 
 * ## Key Features
 * 
 * - **Real-time Loop Manipulation**: Adjust loop start/end points while playing
 * - **Crossfading**: Seamless transitions when changing loop parameters
 * - **Pitch Shifting**: Octave switching + fine tune control
 * - **LFO Mode**: Ultra-slow playback for creating evolving textures
 * - **Reset Trigger**: GPIO18 input for tempo sync and loop reset with crossfading
 * - **PSRAM Storage**: Large sample buffers (up to 8MB) for long recordings
 * - **Q15 Audio Processing**: Fixed-point DSP for consistent performance
 * - **Linear FM**: Real-time frequency modulation with external modulator input
 * 
 * ## Hardware Requirements
 * 
 * - Olimex Pico2-XXL (RP2040 with PSRAM)
 * - SH1122 OLED display (256x64)
 * - SD card for sample storage
 * - Rotary encoder with button
 * - 8-position rotary switch for octave selection
 * - 8 analog control inputs (pots/knobs + FM modulator input)
 * - Reset trigger input (GPIO18) for tempo sync
 * - Loop LED output (GPIO15) for visual feedback
 * 
 * ## Audio Engine Details
 * 
 * The audio engine uses a phase accumulator approach for sample playback:
 * - Q32.32 fixed-point phase for sub-sample precision
 * - Linear interpolation between samples for smooth playback
 * - Real-time crossfading when loop parameters change
 * - PWM output at configurable sample rate for high-quality audio
 * 
 * ## Reset Trigger System
 * 
 * The reset trigger on GPIO18 provides tempo sync functionality:
 * - Active-high trigger (rising edge to reset)
 * - Resets phase accumulator to loop start position
 * - Forces re-read of all ADC inputs for current loop parameters
 * - Recalculates loop region based on current knob positions
 * - Initiates seamless crossfade from current playhead to loop start
 * - Enables smooth tempo-synchronized loop resets without audio glitches
 * - Crossfade quality is independent of loop length or timing
 * 
 * ## Loop LED System
 * 
 * The loop LED on GPIO15 provides visual feedback:
 * - Blinks for 50ms when the loop wraps (reaches loop end)
 * - Blinks for 50ms when reset trigger is received (tempo sync)
 * - Provides visual tempo sync feedback for live performance
 * - Works with both crossfaded and immediate loop transitions
 * 
 * @author Brian Varren
 * @version 1.0
 * @date 2024
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
// These globals store the currently loaded audio sample and are shared between
// the audio engine (Core 0) and display system (Core 1). They're kept outside
// the sf namespace so the UI can access them directly for waveform display.

uint8_t* audioData = nullptr;         // PSRAM buffer containing Q15 audio samples
                                    // Q15 = 16-bit signed integers, -32768 to +32767
                                    // This format is used throughout the audio engine
                                    // for consistent fixed-point arithmetic

uint32_t audioDataSize = 0;           // Total bytes allocated in PSRAM buffer
                                    // Should equal audioSampleCount * 2 (2 bytes per Q15 sample)

uint32_t audioSampleCount = 0;        // Number of Q15 samples in the buffer
                                    // This is the length of the loaded audio file
                                    // after conversion to mono and normalization

WavInfo currentWav;                    // Metadata from the original WAV file
                                    // Contains sample rate, bit depth, channels, etc.
                                    // Used for proper playback speed calculation

// ───────────────────────── Initialize PSRAM ───────────────────────────────────
/**
 * @brief Initialize and verify PSRAM (Pseudo Static RAM) availability
 * 
 * PSRAM is external memory that acts like RAM but is slower to access.
 * It's essential for storing large audio samples that wouldn't fit in the
 * RP2040's internal RAM (264KB). The Olimex Pico2-XXL has 8MB of PSRAM.
 * 
 * @return true if PSRAM is available and working, false otherwise
 */
bool initPSRAM() {
  view_print_line("=== PSRAM ===");
  
  // Check if PSRAM is detected by the hardware
  if (!rp2040.getPSRAMSize()) {
    view_print_line("❌ No PSRAM");
    return false;
  }
  
  // Calculate and display PSRAM statistics
  uint32_t totalMB = rp2040.getPSRAMSize() / 1048576;  // Convert bytes to MB
  uint32_t freeKB  = rp2040.getFreePSRAMHeap() / 1024; // Convert bytes to KB
  
  view_print_line((String("✅ ") + totalMB + " MB, free " + freeKB + " KB").c_str());
  return true;
}

// ───────────────────────── Arduino Setup ──────────────────────────────────────
/**
 * @brief Main setup function - initializes all hardware and systems
 * 
 * This function runs on Core 0 and performs the critical initialization sequence:
 * 1. Serial communication for debugging
 * 2. Display system initialization
 * 3. PSRAM verification (critical for sample storage)
 * 4. SD card initialization (for sample file access)
 * 5. Input system setup (encoders, switches, ADC)
 * 6. Audio engine initialization (PWM, DMA, interpolation)
 * 
 * The function uses a "fail-fast" approach - if any critical component
 * fails to initialize, the system halts with an error message.
 */
void setup() {
  // Initialize serial communication for debugging - minimal to prevent pops
  Serial.begin(115200);
  // while(!Serial);  // DISABLED - don't wait for serial connection to prevent boot hang
  delay(1000);
  Serial.println("Boot: Starting..."); // Minimal debug for boot diagnosis

  // Initialize display system and show startup status
  Serial.println("Boot: Initializing display...");
  view_show_status("Loop Sampler", "Initializing");
  view_clear_log();
  view_print_line("=== Loop Sampler ===");
  view_print_line("Q15 DSP Mode");  // Q15 = 16-bit fixed-point audio format
  view_print_line("Initializing...");
  view_flush_if_dirty();
  delay(500);

  // Initialize PSRAM - CRITICAL for sample storage
  Serial.println("Boot: Checking PSRAM...");
  if (!initPSRAM()) {
    Serial.println("Boot: PSRAM FAILED!");
    view_flush_if_dirty();
    while (1) delay(100);  // Halt if PSRAM fails - system cannot function without it
  }
  Serial.println("Boot: PSRAM OK");
  view_flush_if_dirty();
  delay(500);
  
  // Initialize SD card for sample file access
  Serial.println("Boot: Initializing SD card...");
  if (!sd_begin()) {
    Serial.println("Boot: SD Card FAILED!");
    view_print_line("❌ SD Card failed!");
    view_flush_if_dirty();
    while (1) delay(100);  // Halt if SD card fails - no samples to load
  }
  Serial.println("Boot: SD Card OK");
  {
    float mb = sd_card_size_mb();
    view_print_line("✅ SD Card ready");
    view_print_line((String("Size: ") + String(mb, 1) + " MB").c_str());
    view_flush_if_dirty();
  }
  delay(500);
  
  // Initialize input system (encoders, rotary switch, ADC filtering)
  Serial.println("Boot: Initializing inputs...");
  ui_input_init();
  
  // Initialize audio engine (PWM output, DMA, interpolation tables)
  Serial.println("Boot: Initializing audio engine...");
  audio_init();
  
  // Initialize reset trigger on GPIO18
  Serial.println("Boot: Initializing reset trigger...");
  audio_engine_reset_trigger_init();
  
  // Initialize loop LED on GPIO15
  Serial.println("Boot: Initializing loop LED...");
  audio_engine_loop_led_init();

  // Signal to Core 1 that setup is complete - this triggers file scanning
  // and browser initialization on the display core
  Serial.println("Boot: Signaling Core 1...");
  core0_publish_setup_done();
  Serial.println("Boot: Core 0 setup complete!");
}

// ───────────────────────── Core 1 Setup (Display Core) ────────────────────────
/**
 * @brief Setup function for Core 1 - initializes display hardware
 * 
 * This runs on Core 1 and initializes the SH1122 OLED display.
 * Core 1 is dedicated to display and UI tasks to keep them separate
 * from the real-time audio processing on Core 0.
 */
void setup1(){
  Serial.println("Core1: Starting display init...");
  display_init();
  Serial.println("Core1: Display init complete");
}

// ───────────────────────── Core 0 Main Loop (Audio Core) ──────────────────────
/**
 * @brief Main loop for Core 0 - handles real-time audio processing
 * 
 * This is the heart of the audio engine. It runs continuously and:
 * - Processes audio samples in real-time
 * - Handles ADC input filtering and control processing
 * - Manages the audio engine state machine
 * - Maintains timing for audio callbacks
 * 
 * The loop is intentionally simple to maintain real-time performance.
 * All heavy processing is done in the audio_tick() function.
 */
void loop() {
  // Process audio engine - this handles the main audio processing loop
  // including sample playback, crossfading, and control input processing
  audio_tick();
  
  // Poll for reset trigger on GPIO18
  audio_engine_reset_trigger_poll();
  
  // Update loop LED state
  audio_engine_loop_led_update();
  
  // Debug output (currently disabled to maintain real-time performance)
  static uint32_t last = 0;
  static uint32_t last_debug_report = 0;
  // if (millis() - last >= 250) {
  //   last += 250;
  //   Serial.print('.');
  //   audio_engine_debug_poll();  // must be reachable
  //   Serial.flush();
  // }
  
  // // Print comprehensive debug report every 5 seconds
  // if (millis() - last_debug_report >= 5000) {
  //   last_debug_report = millis();
  //   audio_engine_debug_print_quick_status();
  // }
}

// ───────────────────────── Core 1 Main Loop (Display Core) ────────────────────
/**
 * @brief Main loop for Core 1 - handles display and UI updates
 * 
 * This loop manages the user interface and display updates:
 * - Waits for Core 0 to complete initialization
 * - Scans SD card for WAV files and enters browser mode
 * - Updates input handling (encoders, buttons, switches)
 * - Refreshes display at ~60Hz
 * 
 * The two-phase approach (boot wait + main loop) ensures proper
 * initialization order between the cores.
 */
void loop1() {
  static bool s_boot_done = false;
  static uint32_t last_debug = 0;

  // Phase 1: Wait for Core 0 to complete setup, then initialize browser
  if (!s_boot_done) {
    if (g_core0_setup_done) {
      Serial.println("Core1: Core 0 setup complete, initializing browser...");
      // Core 0 is ready - scan SD card and enter file browser
      display_setup_complete();              // calls file_index_scan + first render
      s_boot_done = true;                    // guard: call only once
      Serial.println("Core1: Browser initialization complete");
    } else {
      // Keep Core 1 gentle while waiting for Core 0
      delayMicroseconds(200);
      
      // Debug output every 5 seconds
      if (millis() - last_debug > 5000) {
        Serial.println("Core1: Waiting for Core 0...");
        last_debug = millis();
      }
    }
    return;
  }

  // Phase 2: Main UI loop - update inputs and display
  ui_input_update();  // Process encoders, buttons, rotary switch
  display_tick();     // Update display at ~60Hz
}
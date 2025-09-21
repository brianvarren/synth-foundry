/**
 * @file xylem_debug.ino
 * @brief XYLEM Synthesizer - Debug Display Test
 * 
 * A debug version to troubleshoot display connection issues.
 * 
 * @author Brian Varren
 * @version 1.0
 * @date 2024
 */

#include "ui_DisplayAdafruit.h"
#include "LocalMetronome.h"
#include "audio_engine.h"
#include "DACless.h"
#include "context_params.h"
#include "command_parser.h"

// ── Global Objects ───────────────────────────────────────────────────────────────
// DisplayAdafruit display; // Defined in ui_DisplayAdafruit.cpp
LocalMetronome metro;

// ── LED Configuration ────────────────────────────────────────────────────────────
#define LED_PIN 4
#define LED_PULSE_MS 5

// LED pulse state - using hardware timing for precision
static volatile bool ledPulseActive = false;
static volatile uint32_t ledPulseStartTime = 0;
static volatile bool ledPulseScheduled = false;

// ── Setup Function (Core 0) ──────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  
  // Wait for serial or timeout - but don't block forever
  unsigned long startMs = millis();
  while (!Serial && millis() - startMs < 3000) {
    delay(10);
  }
  
  // Always print startup message, even if serial not connected
  delay(100); // Give serial time to initialize
  Serial.println("XYLEM Metronome Test");
  Serial.println("Serial initialized");
  
  // Initialize LED
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);
  
  // Initialize metronome
  MetronomeConfig metroConfig;
  metroConfig.bpm = 120.0f;
  metroConfig.ppqn = 96;
  metroConfig.beatsPerBar = 4;
  metroConfig.beatUnit = 4;
  metroConfig.startRunning = true;
  metroConfig.dispatchInIrq = false;
  metroConfig.maxEventQueue = 256;
  
  if (!metro.begin(metroConfig)) {
    Serial.println("ERROR: Metronome init failed!");
    while (1) { delay(1000); }
  }
  
  // Setup metronome callbacks
  setupMetronomeCallbacks();
  
  // Initialize context parameters
  initContextParams();
  
  // Initialize command parser
  initCommandParser();
  
  // Set metronome reference for tempo control
  setMetronomeReference(&metro);
  
  // Initialize audio engine
  if (!audioEngineInit()) {
    Serial.println("ERROR: Audio engine init failed!");
    while (1) { delay(1000); }
  }
  
  // Configure PWM DMA for audio output
  configurePWM_DMA();
  
  // Start audio engine
  audioEngineStart();
  
  Serial.println("Metronome initialized successfully!");
  Serial.println("Audio engine initialized successfully!");
  Serial.println("Command parser initialized successfully!");
  Serial.println("Core 0 setup complete - Core 1 will handle display");
  Serial.println("Type 'help' for available commands or 'test' to verify communication");
}

// ── Main Loop (Core 0) ────────────────────────────────────────────────────────────
void loop() {
  // Update metronome (required for event dispatch)
  metro.update();
  
  // Handle LED pulse - robust timing with overflow protection
  if (ledPulseActive && ledPulseScheduled) {
    uint32_t now = millis();
    uint32_t elapsed = now - ledPulseStartTime;
    
    // Check for overflow (millis() wraps every ~49 days)
    if (elapsed >= LED_PULSE_MS) {
      digitalWrite(LED_PIN, LOW);
      ledPulseActive = false;
      ledPulseScheduled = false;
    }
  }
  
  
  // Process serial commands
  processSerialCommands();
  
  // Process audio engine callbacks
  audioEngineProcessCallback();
  
  delay(10);
}

// ── Setup Function (Core 1) ──────────────────────────────────────────────────────
void setup1() {
  // Initialize display on core 1
  if (!display.begin()) {
    Serial.println("ERROR: Display init failed on core 1!");
    return;
  }
  
  Serial.println("Display initialized on core 1");
  
  // Show initial message
  display.showMessage("Metronome Ready!");
  delay(2000);
}

// ── Main Loop (Core 1) ────────────────────────────────────────────────────────────
void loop1() {
  updateMetronomeDisplay();
  delay(100); // Update display every 100ms for smooth updates
}

// ── Metronome Callback Functions ─────────────────────────────────────────────────
void setupMetronomeCallbacks() {
  // Beat callback - flash LED on every beat
  metro.onBeat([](uint64_t ticks, uint32_t bar, uint16_t beat) {
    // Start LED pulse immediately (in interrupt context)
    digitalWrite(LED_PIN, HIGH);
    ledPulseActive = true;
    ledPulseStartTime = millis();
    ledPulseScheduled = true;
    
    if (shouldShowBeatUpdates()) {
      Serial.printf("Beat %d.%d (tick %llu)\n", bar + 1, beat + 1, ticks);
    }
  });
  
  // Bar callback - show bar info
  metro.onBar([](uint64_t ticks, uint32_t bar) {
    if (shouldShowBeatUpdates()) {
      Serial.printf("=== BAR %d ===\n", bar + 1);
    }
  });
}

// ── Display Update Functions ─────────────────────────────────────────────────────
void updateMetronomeDisplay() {
  display.clearDisplay();
  
  // Show context parameters in compact format
  display.setCursor(0, 0);
  display.print("XYLEM Context");
  
  // Row 1: Consonance and Precision
  display.setCursor(0, 12);
  char line1[32];
  snprintf(line1, sizeof(line1), "Cons:%d Prec:%d", contextParams.consonance, contextParams.precision);
  display.print(line1);
  
  // Row 2: Pace and Density  
  display.setCursor(0, 24);
  char line2[32];
  snprintf(line2, sizeof(line2), "Pace:%d Dens:%d", contextParams.pace, contextParams.density);
  display.print(line2);
  
  // Row 3: Tempo
  display.setCursor(0, 36);
  char line3[32];
  snprintf(line3, sizeof(line3), "Tempo: %.1f BPM", getTempo());
  display.print(line3);
  
  // Row 4: Root Note and Status
  display.setCursor(0, 48);
  char line4[32];
  snprintf(line4, sizeof(line4), "%s %s", getNoteName(contextParams.root_note), metro.isRunning() ? "RUN" : "STOP");
  display.print(line4);
  
  display.display();
}

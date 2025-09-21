// BasicMetronome.ino - Example usage of LocalMetronome library
// For Raspberry Pi Pico with Arduino-Pico core

#include <LocalMetronome.h>

// Create metronome instance
LocalMetronome metro;

// LED pins for visual feedback
const int LED_TICK = 2;   // Blinks on every tick (rapid)
const int LED_BEAT = 3;   // Blinks on beat
const int LED_BAR = 4;    // Blinks on bar

// Track last print time to avoid flooding serial
unsigned long lastPrintMs = 0;

void setup() {
  Serial.begin(115200);
  
  // Wait for serial or timeout
  unsigned long startMs = millis();
  while (!Serial && millis() - startMs < 3000) {
    delay(10);
  }
  
  Serial.println("LocalMetronome Example");
  Serial.println("----------------------");
  
  // Setup LED pins
  pinMode(LED_BUILTIN, OUTPUT);
  pinMode(LED_TICK, OUTPUT);
  pinMode(LED_BEAT, OUTPUT);
  pinMode(LED_BAR, OUTPUT);
  
  // Configure metronome
  MetronomeConfig cfg;
  cfg.bpm = 120.0f;          // 120 BPM
  cfg.ppqn = 96;              // 96 ticks per quarter note
  cfg.beatsPerBar = 4;        // 4/4 time
  cfg.beatUnit = 4;           // quarter note gets the beat
  cfg.startRunning = true;    // Start immediately
  cfg.dispatchInIrq = false;  // Use deferred dispatch (safer)
  cfg.maxEventQueue = 256;    // Event queue size
  
  // Initialize metronome
  if (!metro.begin(cfg)) {
    Serial.println("Failed to initialize metronome!");
    while (1) { delay(100); }
  }
  
  Serial.println("Metronome initialized!");
  Serial.printf("BPM: %.1f, Time Sig: %d/%d, PPQN: %d\n", 
                cfg.bpm, cfg.beatsPerBar, cfg.beatUnit, cfg.ppqn);
  
  // Register callback handlers
  setupCallbacks();
  
  Serial.println("\nCommands:");
  Serial.println("  +/- : Increase/decrease BPM by 5");
  Serial.println("  s   : Stop");
  Serial.println("  g   : Start (go)");
  Serial.println("  z   : Zero position");
  Serial.println("  t   : Change time signature");
  Serial.println("  b   : Set specific BPM");
  Serial.println("  j   : Jump to specific bar");
  Serial.println("  x   : Simulate external sync");
  Serial.println();
}

void setupCallbacks() {
  // Bar callback - flash LED and print
  metro.onBar([](uint64_t ticks, uint32_t bar) {
    digitalWrite(LED_BAR, HIGH);
    
    // Print bar info (rate limited)
    if (millis() - lastPrintMs > 100) {
      Serial.printf("=== BAR %d === (ticks: %llu)\n", bar + 1, ticks);
      lastPrintMs = millis();
    }
  });
  
  // Beat callback
  metro.onBeat([](uint64_t ticks, uint32_t bar, uint16_t beat) {
    digitalWrite(LED_BEAT, HIGH);
    
    // Print beat info
    if (millis() - lastPrintMs > 100) {
      Serial.printf("  Beat %d.%d\n", bar + 1, beat + 1);
      lastPrintMs = millis();
    }
  });
  
  // Tick callback (use sparingly - high frequency!)
  // We'll just blink an LED briefly
  static uint8_t tickCounter = 0;
  metro.onTick([](uint64_t ticks) {
    // Only blink every 24th tick (1/4 of a beat at 96 PPQN)
    tickCounter++;
    if (tickCounter >= 24) {
      digitalWrite(LED_TICK, HIGH);
      tickCounter = 0;
    }
  });
}

void loop() {
  // IMPORTANT: Must call update() regularly to dispatch events
  metro.update();
  
  // Turn off LEDs after brief flash
  static unsigned long ledOffTime = 0;
  if (millis() > ledOffTime) {
    digitalWrite(LED_TICK, LOW);
    digitalWrite(LED_BEAT, LOW);
    digitalWrite(LED_BAR, LOW);
    ledOffTime = millis() + 20; // LEDs off after 20ms
  }
  
  // Handle serial commands
  handleSerialCommands();
  
  // Print status periodically
  static unsigned long lastStatusMs = 0;
  if (millis() - lastStatusMs > 5000) {
    printStatus();
    lastStatusMs = millis();
  }
}

void handleSerialCommands() {
  if (!Serial.available()) return;
  
  char cmd = Serial.read();
  // Clear any extra characters
  while (Serial.available()) Serial.read();
  
  switch (cmd) {
    case '+': {
      float bpm = metro.getBPM();
      metro.setBPM(bpm + 5, true); // Slew to new tempo
      Serial.printf("BPM increased to %.1f (slewing)\n", bpm + 5);
      break;
    }
    
    case '-': {
      float bpm = metro.getBPM();
      if (bpm > 30) {
        metro.setBPM(bpm - 5, true);
        Serial.printf("BPM decreased to %.1f (slewing)\n", bpm - 5);
      }
      break;
    }
    
    case 's':
      metro.stop();
      Serial.println("Metronome stopped");
      break;
      
    case 'g':
      metro.start();
      Serial.println("Metronome started");
      break;
      
    case 'z':
      metro.zero();
      Serial.println("Position reset to 0:0:0");
      break;
      
    case 't':
      changeTimeSignature();
      break;
      
    case 'b':
      setBPMFromSerial();
      break;
      
    case 'j':
      jumpToBar();
      break;
      
    case 'x':
      simulateExternalSync();
      break;
      
    default:
      Serial.println("Unknown command");
      break;
  }
}

void changeTimeSignature() {
  Serial.println("Enter time signature (e.g., 3/4, 6/8, 7/8): ");
  
  // Simple blocking read for demo
  while (!Serial.available()) { delay(10); }
  
  int numerator = Serial.parseInt();
  Serial.read(); // consume '/'
  int denominator = Serial.parseInt();
  
  if (numerator > 0 && numerator <= 16 && 
      (denominator == 2 || denominator == 4 || denominator == 8 || denominator == 16)) {
    metro.setTimeSignature(numerator, denominator);
    Serial.printf("Time signature changed to %d/%d\n", numerator, denominator);
  } else {
    Serial.println("Invalid time signature");
  }
}

void setBPMFromSerial() {
  Serial.print("Enter BPM (20-300): ");
  
  while (!Serial.available()) { delay(10); }
  
  float bpm = Serial.parseFloat();
  if (bpm >= 20 && bpm <= 300) {
    Serial.print("\nSlew (y/n)? ");
    while (!Serial.available()) { delay(10); }
    char slew = Serial.read();
    
    metro.setBPM(bpm, slew == 'y');
    Serial.printf("\nBPM set to %.1f %s\n", bpm, slew == 'y' ? "(slewing)" : "(immediate)");
  } else {
    Serial.println("\nInvalid BPM");
  }
}

void jumpToBar() {
  Serial.print("Jump to bar number: ");
  
  while (!Serial.available()) { delay(10); }
  
  int bar = Serial.parseInt();
  if (bar > 0) {
    uint64_t ticks = (bar - 1) * metro.ticksPerBar.load();
    metro.setSongTicks(ticks);
    Serial.printf("\nJumped to bar %d\n", bar);
  }
}

void simulateExternalSync() {
  Serial.println("Simulating external sync packet...");
  
  // Simulate receiving a sync packet from another device
  // This would normally come from UART/LoRa
  uint64_t remoteTicks = metro.getSongTicks() + 48; // Remote is 1/2 beat ahead
  uint64_t remoteTimeUs = metro.microsNow() - 1000; // 1ms ago
  float remoteBPM = 125.0f; // Slightly faster
  
  metro.externalSyncUpdate(remoteTicks, remoteTimeUs, remoteBPM, 4, 4);
  
  Serial.println("External sync applied (should slew to match)");
}

void printStatus() {
  if (!metro.isRunning()) {
    Serial.println("[Status] Stopped");
    return;
  }
  
  uint32_t bar, beat, tick;
  metro.getBBT(bar, beat, tick);
  
  Serial.printf("[Status] Bar %d, Beat %d, Tick %d | BPM: %.1f | Song time: %.2fs\n",
                bar + 1, beat + 1, tick,
                metro.getBPM(),
                metro.getSongSeconds());
}
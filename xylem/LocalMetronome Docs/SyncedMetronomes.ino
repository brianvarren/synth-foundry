// SyncedMetronomes.ino - Example of synchronizing multiple devices
// Connect multiple Picos via UART (TX1->RX1, RX1->TX1)
// One device acts as master, others as slaves

#include <LocalMetronome.h>
#include "SyncPacket.h"

// Configuration
#define IS_MASTER false  // Set to true for master device
#define SYNC_UART Serial1
#define SYNC_BAUD 115200
#define SYNC_INTERVAL_MS 1000  // Send sync every second
#define ENABLE_LATENCY_COMP true

// Metronome instance
LocalMetronome metro;

// Sync state
bool isMaster = IS_MASTER;
unsigned long lastSyncMs = 0;
unsigned long lastPingMs = 0;
SyncProtocol::LatencyEstimate latency = {0};
uint32_t pingStartTime = 0;
bool waitingForPong = false;

// Visual feedback
const int LED_MASTER = 5;  // Red LED for master
const int LED_SYNC = 6;    // Green LED for sync status

void setup() {
  Serial.begin(115200);
  SYNC_UART.begin(SYNC_BAUD);
  
  // Wait for serial
  unsigned long startMs = millis();
  while (!Serial && millis() - startMs < 3000) {
    delay(10);
  }
  
  // Setup LEDs
  pinMode(LED_BUILTIN, OUTPUT);
  pinMode(LED_MASTER, OUTPUT);
  pinMode(LED_SYNC, OUTPUT);
  
  // Visual indication of role
  digitalWrite(LED_MASTER, isMaster ? HIGH : LOW);
  
  Serial.println("=================================");
  Serial.println("LocalMetronome Multi-Device Sync");
  Serial.println("=================================");
  Serial.printf("Role: %s\n", isMaster ? "MASTER" : "SLAVE");
  Serial.println();
  
  // Configure metronome
  MetronomeConfig cfg;
  cfg.bpm = 120.0f;
  cfg.ppqn = 96;
  cfg.beatsPerBar = 4;
  cfg.beatUnit = 4;
  cfg.startRunning = isMaster;  // Only master starts immediately
  cfg.maxPPMAdjust = 5000.0f;   // Allow larger corrections for sync
  cfg.resyncHoldUs = 100000;    // 100ms between resyncs
  
  // Initialize
  if (!metro.begin(cfg)) {
    Serial.println("Failed to initialize metronome!");
    while (1) { delay(100); }
  }
  
  // Setup callbacks
  setupMetronomeCallbacks();
  
  Serial.println("Metronome initialized!");
  Serial.println("\nCommands:");
  Serial.println("  m : Toggle master/slave role");
  Serial.println("  + : Increase BPM (master only)");
  Serial.println("  - : Decrease BPM (master only)");
  Serial.println("  s : Stop all devices");
  Serial.println("  g : Start all devices");
  Serial.println("  l : Show latency stats");
  Serial.println();
  
  if (!isMaster) {
    Serial.println("Waiting for sync from master...");
  }
}

void setupMetronomeCallbacks() {
  // Visual feedback on beats
  metro.onBeat([](uint64_t ticks, uint32_t bar, uint16_t beat) {
    // Blink on beat 1
    if (beat == 0) {
      digitalWrite(LED_BUILTIN, HIGH);
    }
    
    // Print beat info
    Serial.printf("%s Beat: %d.%d (tick %llu)\n", 
                  isMaster ? "[M]" : "[S]", bar + 1, beat + 1, ticks);
  });
  
  // Bar callback for master to send sync
  if (isMaster) {
    metro.onBar([](uint64_t ticks, uint32_t bar) {
      // Send sync on every bar
      sendSyncPacket();
    });
  }
}

void loop() {
  // Dispatch metronome events
  metro.update();
  
  // Turn off LED after flash
  static unsigned long ledOffMs = 0;
  if (millis() > ledOffMs) {
    digitalWrite(LED_BUILTIN, LOW);
    ledOffMs = millis() + 50;
  }
  
  // Handle serial commands
  handleSerialCommands();
  
  // Process incoming sync packets
  processSyncPackets();
  
  // Master: send periodic sync
  if (isMaster && millis() - lastSyncMs > SYNC_INTERVAL_MS) {
    sendSyncPacket();
    lastSyncMs = millis();
  }
  
  // Periodic latency measurement
  if (ENABLE_LATENCY_COMP && millis() - lastPingMs > 5000) {
    sendPing();
    lastPingMs = millis();
  }
}

void sendSyncPacket() {
  if (!metro.isRunning()) return;
  
  // Create timing packet
  SyncPacketWithTime pktWithTime;
  pktWithTime.packet = SyncProtocol::createTimingPacket(
    metro.getBPM(),
    metro.ppqn_.load(),
    metro.beatsPerBar_.load(),
    metro.beatUnit_.load(),
    (uint32_t)metro.getSongTicks(),
    metro.isRunning()
  );
  
  // Add timestamp
  pktWithTime.timestamp_us = (uint32_t)metro.microsNow();
  
  // Send packet
  SYNC_UART.write((uint8_t*)&pktWithTime, sizeof(pktWithTime));
  
  // Calculate and send checksum
  uint8_t checksum = SyncProtocol::calculateChecksum(
    (uint8_t*)&pktWithTime, sizeof(pktWithTime)
  );
  SYNC_UART.write(checksum);
  
  // Visual feedback
  digitalWrite(LED_SYNC, HIGH);
  delay(1);
  digitalWrite(LED_SYNC, LOW);
}

void processSyncPackets() {
  // Check for incoming data
  if (SYNC_UART.available() < sizeof(SyncPacketWithTime) + 1) {
    return; // Not enough data yet
  }
  
  // Peek at magic byte
  if (SYNC_UART.peek() != SYNC_MAGIC_BYTE) {
    // Discard invalid data
    SYNC_UART.read();
    return;
  }
  
  // Read packet
  SyncPacketWithTime pktWithTime;
  SYNC_UART.readBytes((uint8_t*)&pktWithTime, sizeof(pktWithTime));
  
  // Read and verify checksum
  uint8_t receivedChecksum = SYNC_UART.read();
  uint8_t calculatedChecksum = SyncProtocol::calculateChecksum(
    (uint8_t*)&pktWithTime, sizeof(pktWithTime)
  );
  
  if (receivedChecksum != calculatedChecksum) {
    Serial.println("Checksum error in sync packet!");
    return;
  }
  
  // Validate packet
  if (!SyncProtocol::isValidPacket(pktWithTime.packet)) {
    Serial.println("Invalid sync packet!");
    return;
  }
  
  // Process based on type
  switch (pktWithTime.packet.type) {
    case SYNC_TIMING:
      handleTimingPacket(pktWithTime);
      break;
      
    case SYNC_TRANSPORT:
      handleTransportPacket(pktWithTime.packet);
      break;
      
    case SYNC_PING:
      handlePing(pktWithTime.timestamp_us);
      break;
      
    case SYNC_PONG:
      handlePong(pktWithTime.timestamp_us);
      break;
  }
}

void handleTimingPacket(const SyncPacketWithTime& pktWithTime) {
  if (isMaster) return; // Master doesn't sync to others
  
  const SyncPacket& pkt = pktWithTime.packet;
  
  // Extract timing info
  float bpm = SyncProtocol::getBPMFromPacket(pkt);
  bool shouldRun = (pkt.flags & 0x01) != 0;
  
  // Start/stop if needed
  if (shouldRun && !metro.isRunning()) {
    metro.start();
    Serial.println("Started by sync");
  } else if (!shouldRun && metro.isRunning()) {
    metro.stop();
    Serial.println("Stopped by sync");
  }
  
  if (!shouldRun) return;
  
  // Calculate receive time
  uint64_t now = metro.microsNow();
  uint32_t now32 = (uint32_t)now;
  
  // Estimate transmission delay
  uint32_t estimatedDelay = 500; // Default 500us
  if (latency.samples > 0) {
    estimatedDelay = latency.avg_us;
  }
  
  // Compensated sender time
  uint64_t compensatedSenderTime = pktWithTime.timestamp_us + estimatedDelay;
  
  // Apply sync update
  metro.externalSyncUpdate(
    pkt.songTicks,
    compensatedSenderTime,
    bpm,
    pkt.beatsPerBar,
    pkt.beatUnit
  );
  
  // Visual feedback
  digitalWrite(LED_SYNC, HIGH);
  delay(1);
  digitalWrite(LED_SYNC, LOW);
  
  // Debug output
  static unsigned long lastDebugMs = 0;
  if (millis() - lastDebugMs > 2000) {
    Serial.printf("[Sync] BPM:%.1f Pos:%d Delay:%dus\n", 
                  bpm, pkt.songTicks, estimatedDelay);
    lastDebugMs = millis();
  }
}

void handleTransportPacket(const SyncPacket& pkt) {
  bool shouldStart = (pkt.flags & 0x01) != 0;
  
  if (shouldStart && !metro.isRunning()) {
    metro.start();
    Serial.println("Transport: START");
  } else if (!shouldStart && metro.isRunning()) {
    metro.stop();
    Serial.println("Transport: STOP");
  }
}

void sendPing() {
  if (isMaster || waitingForPong) return;
  
  SyncPacketWithTime pkt = {0};
  pkt.packet.magic = SYNC_MAGIC_BYTE;
  pkt.packet.version = SYNC_VERSION;
  pkt.packet.type = SYNC_PING;
  pkt.timestamp_us = (uint32_t)metro.microsNow();
  
  pingStartTime = pkt.timestamp_us;
  waitingForPong = true;
  
  SYNC_UART.write((uint8_t*)&pkt, sizeof(pkt));
  uint8_t checksum = SyncProtocol::calculateChecksum((uint8_t*)&pkt, sizeof(pkt));
  SYNC_UART.write(checksum);
}

void handlePing(uint32_t senderTime) {
  if (!isMaster) return; // Only master responds to pings
  
  // Send pong with original timestamp
  SyncPacketWithTime pkt = {0};
  pkt.packet.magic = SYNC_MAGIC_BYTE;
  pkt.packet.version = SYNC_VERSION;
  pkt.packet.type = SYNC_PONG;
  pkt.timestamp_us = senderTime; // Echo back original time
  
  SYNC_UART.write((uint8_t*)&pkt, sizeof(pkt));
  uint8_t checksum = SyncProtocol::calculateChecksum((uint8_t*)&pkt, sizeof(pkt));
  SYNC_UART.write(checksum);
}

void handlePong(uint32_t echoedTime) {
  if (!waitingForPong || echoedTime != pingStartTime) return;
  
  uint32_t now = (uint32_t)metro.microsNow();
  uint32_t rtt = now - pingStartTime;
  
  SyncProtocol::updateLatencyEstimate(latency, rtt);
  waitingForPong = false;
  
  Serial.printf("Latency: %dus (RTT: %dus)\n", latency.avg_us, rtt);
}

void handleSerialCommands() {
  if (!Serial.available()) return;
  
  char cmd = Serial.read();
  while (Serial.available()) Serial.read(); // Clear buffer
  
  switch (cmd) {
    case 'm':
      toggleMasterSlave();
      break;
      
    case '+':
      if (isMaster) {
        float bpm = metro.getBPM();
        metro.setBPM(bpm + 5);
        Serial.printf("BPM -> %.1f\n", bpm + 5);
      }
      break;
      
    case '-':
      if (isMaster) {
        float bpm = metro.getBPM();
        if (bpm > 40) {
          metro.setBPM(bpm - 5);
          Serial.printf("BPM -> %.1f\n", bpm - 5);
        }
      }
      break;
      
    case 's':
      metro.stop();
      if (isMaster) {
        // Send stop to all slaves
        SyncPacket pkt = SyncProtocol::createTransportPacket(false);
        SYNC_UART.write((uint8_t*)&pkt, sizeof(pkt));
      }
      Serial.println("STOP");
      break;
      
    case 'g':
      metro.zero();
      metro.start();
      if (isMaster) {
        // Send start to all slaves
        SyncPacket pkt = SyncProtocol::createTransportPacket(true);
        SYNC_UART.write((uint8_t*)&pkt, sizeof(pkt));
      }
      Serial.println("START");
      break;
      
    case 'l':
      if (latency.samples > 0) {
        Serial.printf("Latency Stats:\n");
        Serial.printf("  Min: %dus\n", latency.min_us);
        Serial.printf("  Avg: %dus\n", latency.avg_us);
        Serial.printf("  Max: %dus\n", latency.max_us);
        Serial.printf("  Samples: %d\n", latency.samples);
      } else {
        Serial.println("No latency data yet");
      }
      break;
  }
}

void toggleMasterSlave() {
  isMaster = !isMaster;
  digitalWrite(LED_MASTER, isMaster ? HIGH : LOW);
  
  Serial.printf("\nSwitched to %s mode\n", isMaster ? "MASTER" : "SLAVE");
  
  if (isMaster) {
    metro.start();
  } else {
    Serial.println("Waiting for sync...");
  }
}
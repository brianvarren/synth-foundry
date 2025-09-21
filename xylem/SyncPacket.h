// SyncPacket.h - Protocol for synchronizing multiple LocalMetronome instances
#pragma once
#include <stdint.h>

// Magic bytes for packet identification
#define SYNC_MAGIC_BYTE 0xA7
#define SYNC_VERSION 0x01

// Packet types
enum SyncPacketType : uint8_t {
  SYNC_TIMING = 0x01,    // Regular timing sync
  SYNC_TRANSPORT = 0x02, // Transport control (start/stop)
  SYNC_PING = 0x03,      // Latency measurement
  SYNC_PONG = 0x04       // Ping response
};

// Compact 16-byte sync packet for UART/LoRa transmission
struct __attribute__((packed)) SyncPacket {
  uint8_t  magic;          // 0xA7
  uint8_t  version;        // 0x01
  uint8_t  type;           // SyncPacketType
  uint8_t  flags;          // Bit 0: isRunning, Bit 1: forceSnap
  
  uint16_t ppqn;           
  uint16_t bpm_q8;         // BPM * 256 (Q8.8 fixed point)
  
  uint8_t  beatsPerBar;
  uint8_t  beatUnit;
  uint16_t reserved;       // Padding/future use
  
  uint32_t songTicks;      // Current position (32-bit for compactness)
  
  // Note: timestamp handled separately for flexibility
};

// Extended packet with timestamp (20 bytes)
struct __attribute__((packed)) SyncPacketWithTime {
  SyncPacket packet;
  uint32_t   timestamp_us; // Sender's local time (truncated to 32-bit)
};

// Helper class for sync protocol
class SyncProtocol {
public:
  // Packet creation
  static SyncPacket createTimingPacket(float bpm, uint16_t ppqn, 
                                       uint8_t beatsPerBar, uint8_t beatUnit,
                                       uint32_t songTicks, bool isRunning) {
    SyncPacket pkt;
    pkt.magic = SYNC_MAGIC_BYTE;
    pkt.version = SYNC_VERSION;
    pkt.type = SYNC_TIMING;
    pkt.flags = isRunning ? 0x01 : 0x00;
    pkt.ppqn = ppqn;
    pkt.bpm_q8 = (uint16_t)(bpm * 256.0f); // Convert to Q8.8
    pkt.beatsPerBar = beatsPerBar;
    pkt.beatUnit = beatUnit;
    pkt.reserved = 0;
    pkt.songTicks = songTicks;
    return pkt;
  }
  
  static SyncPacket createTransportPacket(bool start) {
    SyncPacket pkt = {0};
    pkt.magic = SYNC_MAGIC_BYTE;
    pkt.version = SYNC_VERSION;
    pkt.type = SYNC_TRANSPORT;
    pkt.flags = start ? 0x01 : 0x00;
    return pkt;
  }
  
  // Packet validation
  static bool isValidPacket(const SyncPacket& pkt) {
    return pkt.magic == SYNC_MAGIC_BYTE && pkt.version == SYNC_VERSION;
  }
  
  // BPM conversion
  static float getBPMFromPacket(const SyncPacket& pkt) {
    return (float)pkt.bpm_q8 / 256.0f;
  }
  
  // CRC calculation (simple XOR checksum for speed)
  static uint8_t calculateChecksum(const uint8_t* data, size_t len) {
    uint8_t checksum = 0;
    for (size_t i = 0; i < len; i++) {
      checksum ^= data[i];
    }
    return checksum;
  }
  
  // Latency estimation helpers
  struct LatencyEstimate {
    uint32_t min_us;
    uint32_t avg_us;
    uint32_t max_us;
    uint32_t samples;
  };
  
  static void updateLatencyEstimate(LatencyEstimate& est, uint32_t rtt_us) {
    uint32_t one_way = rtt_us / 2;
    
    if (est.samples == 0) {
      est.min_us = est.max_us = est.avg_us = one_way;
      est.samples = 1;
    } else {
      est.min_us = min(est.min_us, one_way);
      est.max_us = max(est.max_us, one_way);
      
      // Simple moving average
      est.avg_us = ((est.avg_us * est.samples) + one_way) / (est.samples + 1);
      est.samples++;
      
      // Reset after many samples to adapt to changing conditions
      if (est.samples > 1000) {
        est.samples = 100; // Keep some history
      }
    }
  }
};
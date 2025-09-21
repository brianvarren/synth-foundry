# LocalMetronome Library for RP2040

A high-precision, low-jitter musical timing library for Raspberry Pi Pico (RP2040) that provides an authoritative local clock with bar/beat/tick tracking, external synchronization support, and deterministic timing using hardware timers.

## Features

### Core Timing
- **Hardware-timer precision**: Uses RP2040 hardware alarms for microsecond-accurate timing
- **Zero drift**: Fixed-point math eliminates floating-point accumulation errors
- **Low jitter**: ISR-based tick generation with optional deferred callback dispatch
- **Deterministic scheduling**: Absolute deadline-based timing, not sleep-based

### Musical Features
- **BBT tracking**: Bar, beat, and tick position tracking
- **Flexible time signatures**: Support for any meter (4/4, 3/4, 6/8, 7/8, etc.)
- **Variable PPQN**: Configurable ticks-per-quarter-note resolution
- **Tempo changes**: Smooth slewing or immediate BPM changes
- **Transport control**: Start, stop, and position reset

### Synchronization
- **External sync support**: Accept timing updates from UART/LoRa/network
- **Latency compensation**: Built-in RTT measurement and compensation
- **Bounded corrections**: Configurable PPM limits prevent instability
- **Phase alignment**: Automatic slewing to maintain sync

### Callbacks
- **Event handlers**: Register callbacks for ticks, beats, and bars
- **Deferred dispatch**: Safe event handling outside ISR context
- **Low latency**: Optional direct ISR callbacks for advanced users

## Installation

1. Copy the library files to your Arduino libraries folder:
```
Arduino/libraries/LocalMetronome/
├── LocalMetronome.h
├── LocalMetronome.cpp
├── SyncPacket.h
└── examples/
    ├── BasicMetronome/
    └── SyncedMetronomes/
```

2. Include in your sketch:
```cpp
#include <LocalMetronome.h>
```

## Basic Usage

```cpp
#include <LocalMetronome.h>

LocalMetronome metro;

void setup() {
  // Configure metronome
  MetronomeConfig cfg;
  cfg.bpm = 120.0f;
  cfg.ppqn = 96;
  cfg.beatsPerBar = 4;
  cfg.beatUnit = 4;
  cfg.startRunning = true;
  
  // Initialize
  metro.begin(cfg);
  
  // Register callbacks
  metro.onBeat([](uint64_t ticks, uint32_t bar, uint16_t beat) {
    // Trigger drum sample, flash LED, etc.
    Serial.printf("Beat %d.%d\n", bar + 1, beat + 1);
  });
  
  metro.onBar([](uint64_t ticks, uint32_t bar) {
    // Change pattern, update display, etc.
    Serial.printf("=== Bar %d ===\n", bar + 1);
  });
}

void loop() {
  // IMPORTANT: Must call update() to dispatch events
  metro.update();
  
  // Your application code here
}
```

## Advanced Features

### Tempo Changes

```cpp
// Smooth tempo change (slewing)
metro.setBPM(140.0f, true);

// Immediate tempo change
metro.setBPM(140.0f, false);

// Get current tempo
float currentBPM = metro.getBPM();
```

### Position Control

```cpp
// Jump to specific tick position
metro.setSongTicks(384);  // Jump to bar 2 (at 96 PPQN, 4/4)

// Nudge position (phase adjustment)
metro.nudgeSongTicks(24);  // Advance by 1/4 beat

// Get current position
uint32_t bar, beat, tick;
metro.getBBT(bar, beat, tick);

// Get song time in seconds
double seconds = metro.getSongSeconds();
```

### External Synchronization

```cpp
// Receive sync from another device
void handleSyncPacket(SyncData data) {
  metro.externalSyncUpdate(
    data.songTicks,      // Remote position
    data.timestamp_us,   // When sent
    data.bpm,           // Remote tempo
    data.beatsPerBar,   // Remote meter
    data.beatUnit
  );
}
```

## Multi-Device Synchronization

The library includes a complete sync protocol for coordinating multiple devices:

### Master Device
```cpp
// Master sends timing packets periodically
metro.onBar([](uint64_t ticks, uint32_t bar) {
  SyncPacket pkt = createSyncPacket();
  Serial1.write((uint8_t*)&pkt, sizeof(pkt));
});
```

### Slave Device
```cpp
// Slave receives and syncs
void processSyncPacket(SyncPacket pkt) {
  metro.externalSyncUpdate(
    pkt.songTicks,
    pkt.timestamp_us,
    pkt.bpm,
    pkt.beatsPerBar,
    pkt.beatUnit
  );
}
```

## Configuration Options

```cpp
MetronomeConfig cfg;
cfg.bpm = 120.0f;           // Initial tempo
cfg.ppqn = 96;              // Resolution (ticks per quarter)
cfg.beatsPerBar = 4;        // Time signature numerator
cfg.beatUnit = 4;           // Time signature denominator
cfg.startRunning = true;    // Auto-start on begin()
cfg.dispatchInIrq = false;  // Deferred callback dispatch
cfg.maxEventQueue = 128;    // Ring buffer size
cfg.maxPPMAdjust = 2000.0f; // Max tempo correction (PPM)
cfg.resyncHoldUs = 5000;    // Min time between resyncs
```

## Performance Specifications

Based on the design goals and RP2040 capabilities:

- **Timing accuracy**: < 1 tick drift over 10 minutes
- **Jitter**: < 10μs typical (hardware timer limited)
- **Callback latency**: < 100μs with deferred dispatch
- **Sync convergence**: < 4 bars for ±2ms phase error
- **CPU overhead**: < 1% at 120 BPM, 96 PPQN

## Thread Safety

- The library is **core-agnostic** - runs on whichever core calls `begin()`
- All callbacks execute on the same core as `update()`
- Atomic operations ensure safe cross-core state access
- Ring buffer allows lock-free ISR→main communication

## Best Practices

1. **Keep callbacks lightweight**: Don't do heavy processing in callbacks
2. **Call update() regularly**: Required for deferred event dispatch
3. **Use appropriate PPQN**: 96 for general use, 480 for high precision
4. **Monitor queue depth**: Size buffer for worst-case event bursts
5. **Test sync latency**: Measure actual UART/LoRa delays for your setup

## Limitations

- Maximum reliable BPM: ~999 (limited by timer resolution)
- Minimum reliable BPM: ~20 (limited by 32-bit tick counter)
- Maximum PPQN: 960 (practical limit for performance)
- Queue overflow: Events lost if buffer full (size appropriately)

## Example Applications

- **Synchronized LED strips**: Multiple modules with coordinated animations
- **Distributed drum machines**: Perfectly synced rhythm across devices
- **Live looping systems**: Precise loop recording and playback
- **MIDI clock generation**: Stable clock source for MIDI devices
- **Audiovisual installations**: Frame-accurate sync between sound and visuals
- **Wireless metronomes**: Musicians with synchronized click tracks

## Testing

The library includes test cases for:
1. **Drift test**: 10-minute accuracy verification
2. **Jitter measurement**: Statistical analysis of timing
3. **Tempo ramping**: Smooth BPM transitions
4. **Sync convergence**: Phase error correction
5. **Load testing**: Performance under callback stress

## License

This library is provided as-is for use in musical and artistic projects. See LICENSE file for details.

## Contributing

Contributions welcome! Please ensure any changes maintain:
- Hardware timer usage for precision
- Fixed-point math for drift prevention
- Lock-free ring buffer for ISR safety
- Comprehensive callback error handling
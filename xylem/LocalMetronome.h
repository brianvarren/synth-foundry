// LocalMetronome.h
#pragma once
#include <stdint.h>
#include <functional>
#include <atomic>
#include <cstdint>

struct MetronomeConfig {
  float     bpm         = 120.0f;
  uint16_t  ppqn        = 96;          // ticks per quarter note
  uint8_t   beatsPerBar = 4;           // time signature numerator
  uint8_t   beatUnit    = 4;           // denominator (4 = quarter)
  bool      startRunning = true;
  bool      dispatchInIrq = false;     // if true, call user handlers in IRQ (advanced)
  uint32_t  maxEventQueue = 128;       // ring buffer depth for deferred dispatch
  // Slew behavior for external sync
  float     maxPPMAdjust = 2000.0f;    // max parts-per-million correction per resync
  uint32_t  resyncHoldUs = 5000;       // ignore resyncs closer than this
};

class LocalMetronome {
public:
  // Event types for ring buffer
  enum EventType : uint8_t {
    EVENT_TICK = 0,
    EVENT_BEAT = 1,
    EVENT_BAR  = 2
  };

  struct Event {
    EventType type;
    uint64_t songTicks;
    uint32_t bar;
    uint16_t beat;
  };

  // Lifecycle
  bool begin(const MetronomeConfig& cfg = MetronomeConfig());
  void end();

  // Transport
  void start();
  void stop();
  void zero();
  bool isRunning() const { return running.load(); }

  // Tempo / meter
  void setBPM(float bpm, bool slew = true);
  float getBPM() const;
  void setTimeSignature(uint8_t beatsPerBar, uint8_t beatUnit);
  void setPPQN(uint16_t ppqn);

  // Position (absolute)
  uint64_t getSongTicks() const;
  void     setSongTicks(uint64_t ticks);
  void     nudgeSongTicks(int64_t deltaTicks);

  // Human-friendly getters
  void getBBT(uint32_t& bar, uint16_t& beat, uint16_t& tickWithinBeat) const;

  // Timecode helpers
  double   getSongSeconds() const;
  uint64_t microsNow() const;

  // External sync
  void externalSyncUpdate(uint64_t refTicks,
                          uint64_t sender_time_us,
                          float    refBPM,
                          uint8_t  refBeatsPerBar,
                          uint8_t  refBeatUnit);

  // Handlers (deferred by default)
  void onTick(std::function<void(uint64_t songTicks)> cb);
  void onBeat(std::function<void(uint64_t songTicks, uint32_t bar, uint16_t beat)> cb);
  void onBar(std::function<void(uint64_t songTicks, uint32_t bar)> cb);

  // Pump deferred events
  void update();

private:
  // Configuration
  MetronomeConfig config;
  
  // State
  std::atomic<bool> running{false};
  std::atomic<bool> initialized{false};
  
  // Timing
  std::atomic<uint64_t> songTicks_{0};
  std::atomic<uint64_t> nextDeadlineUs{0};
  std::atomic<uint64_t> tickIntervalUs{500000}; // default 120bpm @ 96ppqn
  std::atomic<uint64_t> tickIntervalTarget{500000};
  
  // Tempo/meter
  std::atomic<uint32_t> currentBPM_q16{7864320}; // 120 BPM in Q16.16
  std::atomic<uint16_t> ppqn_{96};
  std::atomic<uint8_t> beatsPerBar_{4};
  std::atomic<uint8_t> beatUnit_{4};
  
  // Calculated values
  std::atomic<uint32_t> ticksPerBeat{96};
  std::atomic<uint32_t> ticksPerBar{384};
  
  // Slewing
  std::atomic<int32_t> slewRatePPM{0};
  
  // External sync
  uint64_t lastResyncUs{0};
  
  // Callbacks
  std::function<void(uint64_t)> tickCallback;
  std::function<void(uint64_t, uint32_t, uint16_t)> beatCallback;
  std::function<void(uint64_t, uint32_t)> barCallback;
  
  // Ring buffer for deferred dispatch - using static allocation
  static constexpr uint32_t MAX_EVENT_QUEUE = 256;
  struct RingBuffer {
    Event buffer[MAX_EVENT_QUEUE];
    uint32_t size;
    std::atomic<uint32_t> head{0};
    std::atomic<uint32_t> tail{0};
    
    bool push(const Event& e);
    bool pop(Event& e);
    bool empty() const;
  } eventQueue;
  
  // Hardware alarm handle
  int alarmNum = -1;
  
  // Private methods
  void updateDerivedValues();
  void scheduleNextTick();
  static void alarmCallback(unsigned int alarm_num);
  void processTickInISR();
  uint64_t bpmToTickInterval(float bpm) const;
  void applySlewCorrection();
};
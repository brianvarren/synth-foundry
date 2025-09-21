// LocalMetronome.cpp
#include "LocalMetronome.h"
#include "hardware/timer.h"
#include "hardware/irq.h"
#include "hardware/sync.h"
#include "pico/time.h"
#include <cstring>

// ── Embedded-friendly min/max replacements ────────────────────────────────────────
static inline int32_t min_int32(int32_t a, int32_t b) { return (a < b) ? a : b; }
static inline int32_t max_int32(int32_t a, int32_t b) { return (a > b) ? a : b; }
static inline int64_t abs_int64(int64_t a) { return (a < 0) ? -a : a; }

// Fixed-point math helpers
#define Q16_ONE (1 << 16)
#define MICROS_PER_MINUTE 60000000ULL

// Global instance for alarm callback
LocalMetronome* g_metro_instance = nullptr;

// Ring buffer implementation
bool LocalMetronome::RingBuffer::push(const Event& e) {
    uint32_t next_head = (head.load() + 1) % size;
    if (next_head == tail.load()) {
        return false; // Full
    }
    buffer[head.load()] = e;
    head.store(next_head);
    return true;
}

bool LocalMetronome::RingBuffer::pop(Event& e) {
    if (empty()) {
        return false;
    }
    e = buffer[tail.load()];
    tail.store((tail.load() + 1) % size);
    return true;
}

bool LocalMetronome::RingBuffer::empty() const {
    return head.load() == tail.load();
}

// Static callback for hardware alarm
void LocalMetronome::alarmCallback(unsigned int alarm_num) {
    // For now, we'll use a global instance approach
    // In a real implementation, you'd need a way to get the instance
    // This is a simplified version for the basic display test
    extern LocalMetronome* g_metro_instance;
    if (g_metro_instance) {
        g_metro_instance->processTickInISR();
    }
}

bool LocalMetronome::begin(const MetronomeConfig& cfg) {
    if (initialized.load()) {
        return false;
    }
    
    config = cfg;
    
    // Initialize ring buffer (static allocation)
    eventQueue.size = (config.maxEventQueue > MAX_EVENT_QUEUE) ? MAX_EVENT_QUEUE : config.maxEventQueue;
    eventQueue.head = 0;
    eventQueue.tail = 0;
    
    // Set initial values
    ppqn_.store(config.ppqn);
    beatsPerBar_.store(config.beatsPerBar);
    beatUnit_.store(config.beatUnit);
    
    // Calculate derived values
    updateDerivedValues();
    
    // Convert BPM to tick interval
    tickIntervalUs.store(bpmToTickInterval(config.bpm));
    tickIntervalTarget.store(tickIntervalUs.load());
    
    // Store BPM as Q16.16 fixed point
    currentBPM_q16.store((uint32_t)(config.bpm * Q16_ONE));
    
    initialized.store(true);
    
    // Set global instance for alarm callback
    g_metro_instance = this;
    
    if (config.startRunning) {
        start();
    }
    
    return true;
}

void LocalMetronome::end() {
    if (!initialized.load()) {
        return;
    }
    
    stop();
    
    // Cancel any pending alarms
    if (alarmNum >= 0) {
        cancel_alarm(alarmNum);
        alarmNum = -1;
    }
    
    // No need to free static buffer
    
    // Clear global instance
    g_metro_instance = nullptr;
    
    initialized.store(false);
}

void LocalMetronome::start() {
    if (!initialized.load() || running.load()) {
        return;
    }
    
    running.store(true);
    
    // Schedule first tick
    nextDeadlineUs.store(time_us_64() + tickIntervalUs.load());
    scheduleNextTick();
}

void LocalMetronome::stop() {
    if (!running.load()) {
        return;
    }
    
    running.store(false);
    
    // Cancel pending alarm
    if (alarmNum >= 0) {
        cancel_alarm(alarmNum);
        alarmNum = -1;
    }
}

void LocalMetronome::zero() {
    uint32_t save = save_and_disable_interrupts();
    songTicks_.store(0);
    restore_interrupts(save);
}

void LocalMetronome::setBPM(float bpm, bool slew) {
    if (bpm < 20.0f || bpm > 999.0f) {
        return; // Sanity check
    }
    
    uint64_t newInterval = bpmToTickInterval(bpm);
    tickIntervalTarget.store(newInterval);
    currentBPM_q16.store((uint32_t)(bpm * Q16_ONE));
    
    if (!slew) {
        // Immediate change
        tickIntervalUs.store(newInterval);
        slewRatePPM.store(0);
    } else {
        // Calculate slew rate
        int64_t diff = (int64_t)newInterval - (int64_t)tickIntervalUs.load();
        int32_t ppm = (diff * 1000000LL) / (int64_t)tickIntervalUs.load();
        
        // Clamp to max adjustment
        ppm = max_int32(-(int32_t)config.maxPPMAdjust, 
                       min_int32((int32_t)config.maxPPMAdjust, ppm));
        
        slewRatePPM.store(ppm);
    }
}

float LocalMetronome::getBPM() const {
    return (float)currentBPM_q16.load() / Q16_ONE;
}

void LocalMetronome::setTimeSignature(uint8_t beatsPerBar, uint8_t beatUnit) {
    beatsPerBar_.store(beatsPerBar);
    beatUnit_.store(beatUnit);
    updateDerivedValues();
}

void LocalMetronome::setPPQN(uint16_t ppqn) {
    ppqn_.store(ppqn);
    updateDerivedValues();
    
    // Recalculate tick interval
    float bpm = getBPM();
    tickIntervalUs.store(bpmToTickInterval(bpm));
    tickIntervalTarget.store(tickIntervalUs.load());
}

uint64_t LocalMetronome::getSongTicks() const {
    return songTicks_.load();
}

void LocalMetronome::setSongTicks(uint64_t ticks) {
    uint32_t save = save_and_disable_interrupts();
    songTicks_.store(ticks);
    restore_interrupts(save);
}

void LocalMetronome::nudgeSongTicks(int64_t deltaTicks) {
    uint32_t save = save_and_disable_interrupts();
    uint64_t current = songTicks_.load();
    if (deltaTicks < 0 && (uint64_t)(-deltaTicks) > current) {
        songTicks_.store(0);
    } else {
        songTicks_.store(current + deltaTicks);
    }
    restore_interrupts(save);
}

void LocalMetronome::getBBT(uint32_t& bar, uint16_t& beat, uint16_t& tickWithinBeat) const {
    uint64_t ticks = songTicks_.load();
    uint32_t tpBar = ticksPerBar.load();
    uint32_t tpBeat = ticksPerBeat.load();
    
    bar = ticks / tpBar;
    uint64_t ticksInBar = ticks % tpBar;
    beat = ticksInBar / tpBeat;
    tickWithinBeat = ticksInBar % tpBeat;
}

double LocalMetronome::getSongSeconds() const {
    uint64_t ticks = songTicks_.load();
    float bpm = getBPM();
    uint16_t ppqn = ppqn_.load();
    
    // seconds = (ticks / ppqn) * (60 / bpm)
    return (double)ticks * 60.0 / (ppqn * bpm);
}

uint64_t LocalMetronome::microsNow() const {
    return time_us_64();
}

void LocalMetronome::externalSyncUpdate(uint64_t refTicks,
                                        uint64_t sender_time_us,
                                        float    refBPM,
                                        uint8_t  refBeatsPerBar,
                                        uint8_t  refBeatUnit) {
    uint64_t now = time_us_64();
    
    // Rate limit resyncs
    if (now - lastResyncUs < config.resyncHoldUs) {
        return;
    }
    lastResyncUs = now;
    
    // Update tempo and meter if provided
    if (refBPM > 0) {
        setBPM(refBPM, true); // Use slewing
    }
    if (refBeatsPerBar > 0 && refBeatUnit > 0) {
        setTimeSignature(refBeatsPerBar, refBeatUnit);
    }
    
    // Calculate expected local time for refTicks
    // Account for transmission delay (simplified - could add RTT measurement)
    uint64_t expectedLocalTime = sender_time_us + (now - sender_time_us) / 2;
    
    // Calculate phase error
    uint64_t currentTicks = songTicks_.load();
    int64_t tickError = (int64_t)refTicks - (int64_t)currentTicks;
    
    // Convert tick error to time error
    int64_t phaseErrorUs = tickError * (int64_t)tickIntervalUs.load();
    
    // Apply correction
    if (abs_int64(phaseErrorUs) > (int64_t)tickIntervalUs.load()) {
        // Large error - snap to position
        setSongTicks(refTicks);
        nextDeadlineUs.store(expectedLocalTime);
    } else {
        // Small error - apply gradual correction via slewing
        int32_t ppm = (phaseErrorUs * 1000000LL) / (int64_t)tickIntervalUs.load();
        ppm = max_int32(-(int32_t)config.maxPPMAdjust, 
                       min_int32((int32_t)config.maxPPMAdjust, ppm));
        
        // Add to existing slew rate
        int32_t currentSlew = slewRatePPM.load();
        slewRatePPM.store(currentSlew + ppm);
    }
}

void LocalMetronome::onTick(std::function<void(uint64_t)> cb) {
    tickCallback = cb;
}

void LocalMetronome::onBeat(std::function<void(uint64_t, uint32_t, uint16_t)> cb) {
    beatCallback = cb;
}

void LocalMetronome::onBar(std::function<void(uint64_t, uint32_t)> cb) {
    barCallback = cb;
}

void LocalMetronome::update() {
    Event e;
    while (eventQueue.pop(e)) {
        switch (e.type) {
            case EVENT_TICK:
                if (tickCallback) {
                    tickCallback(e.songTicks);
                }
                break;
                
            case EVENT_BEAT:
                if (beatCallback) {
                    beatCallback(e.songTicks, e.bar, e.beat);
                }
                break;
                
            case EVENT_BAR:
                if (barCallback) {
                    barCallback(e.songTicks, e.bar);
                }
                break;
        }
    }
}

void LocalMetronome::updateDerivedValues() {
    uint16_t ppqn = ppqn_.load();
    uint8_t beatUnit = beatUnit_.load();
    uint8_t beatsPerBar = beatsPerBar_.load();
    
    // Calculate ticks per beat (accounting for beat unit)
    // If beat unit is 4 (quarter note), ticks per beat = ppqn
    // If beat unit is 8 (eighth note), ticks per beat = ppqn / 2
    uint32_t tpb = ppqn * 4 / beatUnit;
    ticksPerBeat.store(tpb);
    
    // Calculate ticks per bar
    ticksPerBar.store(tpb * beatsPerBar);
}

void LocalMetronome::scheduleNextTick() {
    if (!running.load()) {
        return;
    }
    
    // Schedule hardware alarm
    alarmNum = hardware_alarm_set_target(0, nextDeadlineUs.load());
    hardware_alarm_set_callback(0, alarmCallback);
}

void LocalMetronome::processTickInISR() {
    if (!running.load()) {
        return;
    }
    
    // Increment song position
    uint64_t ticks = songTicks_.fetch_add(1) + 1;
    
    // Check for beat/bar boundaries
    uint32_t tpBeat = ticksPerBeat.load();
    uint32_t tpBar = ticksPerBar.load();
    
    bool isBeat = (ticks % tpBeat) == 0;
    bool isBar = (ticks % tpBar) == 0;
    
    // Queue events or dispatch immediately
    if (config.dispatchInIrq) {
        // Direct dispatch in IRQ (advanced mode)
        if (tickCallback) {
            tickCallback(ticks);
        }
        if (isBeat) {
            uint32_t bar = ticks / tpBar;
            uint16_t beat = (ticks % tpBar) / tpBeat;
            if (beatCallback) {
                beatCallback(ticks, bar, beat);
            }
            if (isBar && barCallback) {
                barCallback(ticks, bar);
            }
        }
    } else {
        // Queue for deferred dispatch
        Event e;
        e.songTicks = ticks;
        
        if (isBar) {
            e.type = EVENT_BAR;
            e.bar = ticks / tpBar;
            eventQueue.push(e);
        }
        if (isBeat) {
            e.type = EVENT_BEAT;
            e.bar = ticks / tpBar;
            e.beat = (ticks % tpBar) / tpBeat;
            eventQueue.push(e);
        }
        if (tickCallback) {
            e.type = EVENT_TICK;
            eventQueue.push(e);
        }
    }
    
    // Apply slewing if needed
    applySlewCorrection();
    
    // Schedule next tick
    nextDeadlineUs.fetch_add(tickIntervalUs.load());
    scheduleNextTick();
}

uint64_t LocalMetronome::bpmToTickInterval(float bpm) const {
    // tickIntervalUs = 60,000,000 / (BPM * PPQN * (beatUnit/4))
    // Adjusted for beat unit
    uint16_t ppqn = ppqn_.load();
    uint8_t beatUnit = beatUnit_.load();
    
    // Calculate effective PPQN based on beat unit
    float effectivePPQN = ppqn * (4.0f / beatUnit);
    
    return (uint64_t)(MICROS_PER_MINUTE / (bpm * effectivePPQN));
}

void LocalMetronome::applySlewCorrection() {
    int32_t ppm = slewRatePPM.load();
    if (ppm == 0) {
        return;
    }
    
    uint64_t current = tickIntervalUs.load();
    uint64_t target = tickIntervalTarget.load();
    
    if (current == target) {
        slewRatePPM.store(0);
        return;
    }
    
    // Apply PPM adjustment
    int64_t adjustment = (current * ppm) / 1000000LL;
    
    if (current < target) {
        current = min_int32(current + adjustment, target);
    } else {
        current = max_int32(current + adjustment, target);
    }
    
    tickIntervalUs.store(current);
    
    // Reduce slew rate as we approach target
    if (abs_int64((int64_t)current - (int64_t)target) < abs_int64(adjustment)) {
        slewRatePPM.store(ppm / 2);
    }
}
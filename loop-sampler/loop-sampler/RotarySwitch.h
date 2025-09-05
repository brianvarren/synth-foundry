/*
 * RotarySwitch - Handler for rotary switches using 74HC165 shift register
 * Part of the EEncoder library family
 * 
 * Uses a 74HC165 parallel-in/serial-out shift register to read
 * rotary switch positions using only 3 GPIO pins
 */

#ifndef ROTARYSWITCH_H
#define ROTARYSWITCH_H

#include <Arduino.h>

class RotarySwitch {
public:
    // Constructor
    // numPositions: Number of switch positions (2-12)
    // plPin: Parallel Load pin (active low)
    // cpPin: Clock Pulse pin
    // q7Pin: Serial data output from shift register
    RotarySwitch(uint8_t numPositions, uint8_t plPin, uint8_t cpPin, uint8_t q7Pin);
    
    // Callback type - called when position changes
    typedef void (*PositionChangeHandler)(RotarySwitch& sw);
    
    // Must be called in loop()
    void update();
    
    // Set callback handler
    void setChangeHandler(PositionChangeHandler handler) { _changeHandler = handler; }
    
    // Get current position (1 to numPositions, 0 if no position detected)
    uint8_t getPosition() const { return _currentPosition; }
    
    // Get previous position
    uint8_t getPreviousPosition() const { return _previousPosition; }
    
    // Check if position has changed since last update
    bool hasChanged() const { return _changed; }
    
    // Configuration
    void setDebounceDuration(uint32_t ms) { _debounceDuration = ms; }
    void setEnabled(bool enabled) { _enabled = enabled; }
    bool isEnabled() const { return _enabled; }
    
    // Get number of positions this switch supports
    uint8_t getNumPositions() const { return _numPositions; }
    
    // Utility - get position name (can be overridden by user)
    virtual const char* getPositionName(uint8_t position) const;
    
private:
    // Pin assignments
    uint8_t _plPin;      // Parallel Load (active low)
    uint8_t _cpPin;      // Clock Pulse
    uint8_t _q7Pin;      // Serial Data Out
    
    // Configuration
    uint8_t _numPositions;
    uint32_t _debounceDuration = 20;  // 20ms default for rotary switches
    bool _enabled = true;
    
    // State
    uint8_t _currentPosition = 0;
    uint8_t _previousPosition = 0;
    uint8_t _lastStablePosition = 0;
    bool _changed = false;
    
    // Timing
    uint32_t _lastChangeTime = 0;
    uint32_t _lastReadTime = 0;
    
    // Shift register data
    uint8_t _shiftData = 0xFF;  // All high when no position selected
    uint8_t _lastShiftData = 0xFF;
    
    // Callback
    PositionChangeHandler _changeHandler = nullptr;
    
    // Private methods
    uint8_t readShiftRegister();
    uint8_t decodePosition(uint8_t data);
};

#endif // ROTARYSWITCH_H
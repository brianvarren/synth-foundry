/*
 * RotarySwitch - Handler for rotary switches using 74HC165 shift register
 * Implementation file
 */

#include "RotarySwitch.h"

RotarySwitch::RotarySwitch(uint8_t numPositions, uint8_t plPin, uint8_t cpPin, uint8_t q7Pin)
    : _numPositions(numPositions), _plPin(plPin), _cpPin(cpPin), _q7Pin(q7Pin) {
    
    // Validate number of positions
    if (_numPositions < 2) _numPositions = 2;
    if (_numPositions > 12) _numPositions = 12;
    
    // Configure pins
    pinMode(_plPin, OUTPUT);
    pinMode(_cpPin, OUTPUT);
    pinMode(_q7Pin, INPUT);
    
    // Set initial states
    digitalWrite(_plPin, HIGH);  // PL is active low
    digitalWrite(_cpPin, LOW);
    
    // Read initial position
    _shiftData = readShiftRegister();
    _lastShiftData = _shiftData;
    _currentPosition = decodePosition(_shiftData);
    _previousPosition = _currentPosition;
    _lastStablePosition = _currentPosition;
}

void RotarySwitch::update() {
    if (!_enabled) return;
    
    uint32_t now = millis();
    
    // Read the shift register
    uint8_t newData = readShiftRegister();
    
    // Check if data has changed
    if (newData != _lastShiftData) {
        _lastChangeTime = now;
        _lastShiftData = newData;
    }
    
    // If data has been stable for debounce period
    if ((now - _lastChangeTime) >= _debounceDuration) {
        uint8_t newPosition = decodePosition(newData);
        
        // Check if position has actually changed
        if (newPosition != _lastStablePosition && newPosition != 0) {
            _previousPosition = _currentPosition;
            _currentPosition = newPosition;
            _lastStablePosition = newPosition;
            _changed = true;
            
            // Fire callback
            if (_changeHandler) {
                _changeHandler(*this);
            }
        } else {
            _changed = false;
        }
    }
}

uint8_t RotarySwitch::readShiftRegister() {
    uint8_t data = 0;
    
    // Pulse PL low to load parallel data
    digitalWrite(_plPin, LOW);
    delayMicroseconds(5);  // 74HC165 needs min 5ns, we give it 5us to be safe
    digitalWrite(_plPin, HIGH);
    delayMicroseconds(5);
    
    // Read first bit (already available on Q7 after PL)
    data = digitalRead(_q7Pin) ? 0x80 : 0x00;
    
    // Clock in the remaining 7 bits
    for (int i = 6; i >= 0; i--) {
        digitalWrite(_cpPin, HIGH);
        delayMicroseconds(5);
        
        if (digitalRead(_q7Pin)) {
            data |= (1 << i);
        }
        
        digitalWrite(_cpPin, LOW);
        delayMicroseconds(5);
    }
    
    return data;
}

uint8_t RotarySwitch::decodePosition(uint8_t data) {
    // Look for a single low bit (switch position pulls input low)
    // The position corresponds to which bit is low
    
    for (uint8_t i = 0; i < 8; i++) {
        if (!(data & (1 << i))) {
            // Found a low bit - this is our position
            // But we need to check it's within our valid range
            uint8_t position = i + 1;  // Convert to 1-based position
            
            if (position <= _numPositions) {
                return position;
            }
        }
    }
    
    // No valid position found (either no switch connected or between positions)
    return 0;
}

const char* RotarySwitch::getPositionName(uint8_t position) const {
    static char buffer[16];
    
    if (position == 0) {
        return "None";
    } else if (position > _numPositions) {
        return "Invalid";
    } else {
        snprintf(buffer, sizeof(buffer), "Pos %d", position);
        return buffer;
    }
}
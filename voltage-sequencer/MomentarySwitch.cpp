// ========== MomentarySwitch.cpp ==========
#include "MomentarySwitch.h"

MomentarySwitch::MomentarySwitch(uint8_t pin, bool activeLow)
    : _pin(pin), _activeLow(activeLow) {
    pinMode(_pin, INPUT_PULLUP);
    
    // Read initial state
    _lastReading = digitalRead(_pin);
}

void MomentarySwitch::update() {
    if (!_enabled) return;
    
    bool currentReading = digitalRead(_pin);
    uint32_t now = millis();
    
    // Check if reading has changed
    if (currentReading != _lastReading) {
        _lastChangeTime = now;
    }
    
    // If reading has been stable for debounce period
    if ((now - _lastChangeTime) > _debounceDuration) {
        bool isPressed = _activeLow ? !currentReading : currentReading;
        
        // Button press detected
        if (isPressed && !_pressed) {
            handlePress();
        }
        // Button release detected
        else if (!isPressed && _pressed) {
            handleRelease();
        }
        
        // Check for long press
        if (_pressed && !_longPressFired && _longPressHandler) {
            if (now - _pressStartTime >= _longPressDuration) {
                _longPressFired = true;
                _longPressHandler(*this);
            }
        }
    }
    
    // Check for double-click timeout
    if (_clickCount > 0 && (now - _lastClickTime) > _doubleClickWindow) {
        // Single click detected (no second click within window)
        if (_clickCount == 1 && _clickHandler) {
            _clickHandler(*this);
        }
        _clickCount = 0;
    }
    
    _lastReading = currentReading;
}

void MomentarySwitch::handlePress() {
    _pressed = true;
    _pressStartTime = millis();
    _longPressFired = false;
    
    if (_pressHandler) {
        _pressHandler(*this);
    }
}

void MomentarySwitch::handleRelease() {
    _pressed = false;
    uint32_t now = millis();
    
    if (_releaseHandler) {
        _releaseHandler(*this);
    }
    
    // Only count as click if it wasn't a long press
    if (!_longPressFired) {
        _clickCount++;
        _lastClickTime = now;
        
        // Double click detected
        if (_clickCount == 2) {
            if (_doubleClickHandler) {
                _doubleClickHandler(*this);
            }
            _clickCount = 0;  // Reset after double click
        }
    }
}

uint32_t MomentarySwitch::getPressedDuration() const {
    if (!_pressed) return 0;
    return millis() - _pressStartTime;
}
/*
  EEncoder - A clean rotary encoder library for RP2040
  Implementation file
  
  v1.3.0 - Improved sensitivity and no missed detents
*/

#include "EEncoder.h"

// Constructor with button
EEncoder::EEncoder(uint8_t pinA, uint8_t pinB, uint8_t buttonPin, uint8_t countsPerDetent) :
    _pinA(pinA),
    _pinB(pinB),
    _buttonPin(buttonPin),
    _hasButton(true),
    _lastEncoderState(0),
    _encoderState(0),
    _increment(0),
    _absolutePosition(0),
    _lastCallbackPosition(0),
    _lastValidState(0),
    _stateSequence(0),
    _lastStateChangeTime(0),
    _countsPerDetent(countsPerDetent),
    _buttonState(HIGH),
    _lastButtonState(HIGH),
    _buttonStateChangeTime(0),
    _buttonPressTime(0),
    _longPressHandled(false),
    _debounceInterval(DEFAULT_DEBOUNCE_MS),
    _longPressDuration(DEFAULT_LONG_PRESS_MS),
    _accelerationEnabled(false),
    _accelerationRate(DEFAULT_ACCELERATION_RATE),
    _lastRotationTime(0),
    _encoderCallback(nullptr),
    _buttonCallback(nullptr),
    _longPressCallback(nullptr),
    _enabled(true)
{
    // Configure pins with INPUT_PULLUP
    pinMode(_pinA, INPUT_PULLUP);
    pinMode(_pinB, INPUT_PULLUP);
    pinMode(_buttonPin, INPUT_PULLUP);
    
    // Read initial encoder state
    _lastEncoderState = getEncoderState();
    _lastValidState = _lastEncoderState;
    _lastStateChangeTime = millis();
}

// Constructor without button
EEncoder::EEncoder(uint8_t pinA, uint8_t pinB, uint8_t countsPerDetent) :
    _pinA(pinA),
    _pinB(pinB),
    _buttonPin(0),
    _hasButton(false),
    _lastEncoderState(0),
    _encoderState(0),
    _increment(0),
    _absolutePosition(0),
    _lastCallbackPosition(0),
    _lastValidState(0),
    _stateSequence(0),
    _lastStateChangeTime(0),
    _countsPerDetent(countsPerDetent),
    _buttonState(HIGH),
    _lastButtonState(HIGH),
    _buttonStateChangeTime(0),
    _buttonPressTime(0),
    _longPressHandled(false),
    _debounceInterval(DEFAULT_DEBOUNCE_MS),
    _longPressDuration(DEFAULT_LONG_PRESS_MS),
    _accelerationEnabled(false),
    _accelerationRate(DEFAULT_ACCELERATION_RATE),
    _lastRotationTime(0),
    _encoderCallback(nullptr),
    _buttonCallback(nullptr),
    _longPressCallback(nullptr),
    _enabled(true)
{
    // Configure pins
    pinMode(_pinA, INPUT_PULLUP);
    pinMode(_pinB, INPUT_PULLUP);
    
    // Read initial encoder state
    _lastEncoderState = getEncoderState();
    _lastValidState = _lastEncoderState;
    _lastStateChangeTime = millis();
}

// Main update function - must be called frequently in loop()
void EEncoder::update() {
    if (!_enabled) return;
    
    readEncoder();
    
    if (_hasButton) {
        readButton();
    }
}

// Read current encoder state
uint8_t EEncoder::getEncoderState() {
    return (digitalRead(_pinA) << 1) | digitalRead(_pinB);
}

// Read and process encoder
void EEncoder::readEncoder() {
    _encoderState = getEncoderState();
    
    // Only process if state changed
    if (_encoderState != _lastEncoderState) {
        uint32_t currentTime = millis();
        
        // Create a 4-bit value from old and new states
        uint8_t combined = (_lastEncoderState << 2) | _encoderState;
        
        // State transition table for quadrature decoding
        // This table is more forgiving and catches all valid transitions
        static const int8_t transitionTable[16] = {
             0,  // 0000: no change
             1,  // 0001: CW
            -1,  // 0010: CCW
             0,  // 0011: invalid (skip)
            -1,  // 0100: CCW
             0,  // 0101: no change
             0,  // 0110: invalid (skip)
             1,  // 0111: CW
             1,  // 1000: CW
             0,  // 1001: invalid (skip)
             0,  // 1010: no change
            -1,  // 1011: CCW
             0,  // 1100: invalid (skip)
            -1,  // 1101: CCW
             1,  // 1110: CW
             0   // 1111: no change
        };
        
        int8_t direction = transitionTable[combined];
        
        // Update position for any valid transition
        if (direction != 0) {
            _absolutePosition += direction;
            _lastStateChangeTime = currentTime;
            
            // Calculate how many detents we've moved since last callback
            int32_t positionDelta = _absolutePosition - _lastCallbackPosition;
            
            // Check if we've moved enough for a complete detent
            if (abs(positionDelta) >= _countsPerDetent) {
                // Calculate how many full detents we've moved
                int8_t detents = positionDelta / _countsPerDetent;
                
                // Update the last callback position by the number of full detents
                // This preserves any fractional detent for next time
                _lastCallbackPosition += detents * _countsPerDetent;
                
                // Set increment for this callback
                _increment = detents;
                
                // Apply acceleration if enabled
                if (_accelerationEnabled && abs(detents) == 1) {
                    uint32_t timeSinceLastRotation = currentTime - _lastRotationTime;
                    
                    // If rotating quickly, multiply increment
                    if (timeSinceLastRotation < ACCELERATION_THRESHOLD_MS) {
                        _increment *= _accelerationRate;
                    }
                }
                
                _lastRotationTime = currentTime;
                
                // Fire callback
                if (_encoderCallback != nullptr) {
                    _encoderCallback(*this);
                }
            }
        }
        
        // Track valid states for idle detection
        if (_encoderState == 0b00 || _encoderState == 0b11) {
            _lastValidState = _encoderState;
        }
        
        _lastEncoderState = _encoderState;
    }
    // Handle idle recalibration
    else {
        uint32_t currentTime = millis();
        
        // If encoder has been idle at a detent position, recalibrate
        if ((currentTime - _lastStateChangeTime) > ENCODER_IDLE_TIMEOUT_MS) {
            // Only recalibrate if we're at a natural detent position
            if (_encoderState == 0b00 || _encoderState == 0b11) {
                // Round position to nearest detent
                int32_t nearestDetent = ((_absolutePosition + _countsPerDetent/2) / _countsPerDetent) * _countsPerDetent;
                
                // Only adjust if we're close to a detent (within 1 count)
                if (abs(_absolutePosition - nearestDetent) <= 1) {
                    _absolutePosition = nearestDetent;
                    _lastCallbackPosition = nearestDetent;
                }
            }
        }
    }
}

// Read and process button
void EEncoder::readButton() {
    bool currentState = digitalRead(_buttonPin);
    
    // Check if state changed
    if (currentState != _lastButtonState) {
        _buttonStateChangeTime = millis();
    }
    
    // Check if we've passed the debounce interval
    if ((millis() - _buttonStateChangeTime) >= _debounceInterval) {
        // State has been stable for debounce interval
        if (currentState != _buttonState) {
            _buttonState = currentState;
            
            // Button pressed (transition to LOW)
            if (_buttonState == LOW) {
                _buttonPressTime = millis();
                _longPressHandled = false;
                
                // Fire regular press callback
                if (_buttonCallback != nullptr) {
                    _buttonCallback(*this);
                }
            }
            // Button released
            else {
                // Reset long press flag
                _longPressHandled = false;
            }
        }
    }
    
    // Check for long press while button is held
    if (_buttonState == LOW && !_longPressHandled && _longPressCallback != nullptr) {
        if ((millis() - _buttonPressTime) >= _longPressDuration) {
            _longPressHandled = true;
            _longPressCallback(*this);
        }
    }
    
    _lastButtonState = currentState;
}

// Get button state
bool EEncoder::getButton() const {
    return _hasButton && (_buttonState == LOW);
}

// Set encoder rotation callback
void EEncoder::setEncoderHandler(EncoderCallback callback) {
    _encoderCallback = callback;
}

// Set button press callback
void EEncoder::setButtonHandler(ButtonCallback callback) {
    _buttonCallback = callback;
}

// Set long press callback
void EEncoder::setLongPressHandler(ButtonCallback callback) {
    _longPressCallback = callback;
}

// Set debounce interval in milliseconds (applies to button only)
void EEncoder::setDebounceInterval(uint16_t intervalMs) {
    _debounceInterval = intervalMs;
}

// Set long press duration in milliseconds
void EEncoder::setLongPressDuration(uint16_t durationMs) {
    _longPressDuration = durationMs;
}

// Enable or disable acceleration
void EEncoder::setAcceleration(bool enabled) {
    _accelerationEnabled = enabled;
}

// Set acceleration multiplier
void EEncoder::setAccelerationRate(uint8_t rate) {
    _accelerationRate = rate;
}

// Enable or disable the encoder
void EEncoder::enable(bool enabled) {
    _enabled = enabled;
    
    // Reset state when disabled
    if (!_enabled) {
        _increment = 0;
        // Don't reset absolute position - preserve it
    }
}
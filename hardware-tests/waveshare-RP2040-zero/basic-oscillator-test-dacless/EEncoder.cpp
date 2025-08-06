/*
  EEncoder - A clean rotary encoder library for RP2040
  Implementation file
  
  v1.2.0 - Robust state machine for reliable detent detection
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
    _position(0),
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
    // Configure pins with INPUT_PULLUP (though external pullups are expected)
    pinMode(_pinA, INPUT_PULLUP);
    pinMode(_pinB, INPUT_PULLUP);
    pinMode(_buttonPin, INPUT_PULLUP);
    
    // Read initial encoder state
    _lastEncoderState = getEncoderState();
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
    _position(0),
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
        
        // Determine direction based on state transition
        int8_t direction = 0;
        
        // Valid CW transitions
        if (combined == 0b0001 || combined == 0b0111 || 
            combined == 0b1110 || combined == 0b1000) {
            direction = 1;
        }
        // Valid CCW transitions  
        else if (combined == 0b0010 || combined == 0b1011 || 
                 combined == 0b1101 || combined == 0b0100) {
            direction = -1;
        }
        
        // Update position if valid transition
        if (direction != 0) {
            _position += direction;
            _lastStateChangeTime = currentTime;
            
            // Check if we've completed a detent
            if (abs(_position) >= _countsPerDetent) {
                // We've moved one full detent
                _increment = (_position > 0) ? 1 : -1;
                
                // Reset position for next detent
                _position = 0;
                
                // Apply acceleration if enabled
                if (_accelerationEnabled) {
                    uint32_t timeSinceLastRotation = currentTime - _lastRotationTime;
                    
                    // If rotating quickly, multiply increment
                    if (timeSinceLastRotation < ACCELERATION_THRESHOLD_MS) {
                        _increment *= _accelerationRate;
                    }
                    
                    _lastRotationTime = currentTime;
                }
                
                // Fire callback
                if (_encoderCallback != nullptr) {
                    _encoderCallback(*this);
                }
            }
        }
        
        _lastEncoderState = _encoderState;
    }
    // Check for idle timeout to resynchronize
    else if (_position != 0) {
        uint32_t currentTime = millis();
        
        // If encoder has been idle, reset position
        // This prevents drift from missed counts
        if ((currentTime - _lastStateChangeTime) > ENCODER_IDLE_TIMEOUT_MS) {
            _position = 0;
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
        _position = 0;
    }
}
/*
  EEncoder - A clean rotary encoder library for RP2040
  Optimized for digital synthesizer UI controls
  
  v1.3.0 Features:
  - Improved sensitivity - no missed detents
  - Robust state machine with absolute position tracking
  - Hardware normalization (handles 1, 2, or 4 counts per detent)
  - Callback-based event handling
  - No encoder debouncing (uses quadrature decoding)
  - Normalized output (always ±1 per physical detent)
  - Long press detection
  - Acceleration support
  - Intelligent idle recalibration
  - Simple, clean API
*/

#ifndef EEncoder_h
#define EEncoder_h

#include <Arduino.h>

// Default debounce time in milliseconds for button
#define DEFAULT_DEBOUNCE_MS 10

// Default long press duration in milliseconds
#define DEFAULT_LONG_PRESS_MS 500

// Idle timeout for position recalibration
#define ENCODER_IDLE_TIMEOUT_MS 100

// Default counts per detent - most common encoders produce 4 counts per physical click
#define DEFAULT_COUNTS_PER_DETENT 4

// Default acceleration threshold - turns faster than this trigger acceleration
#define ACCELERATION_THRESHOLD_MS 100

// Default acceleration multiplier
#define DEFAULT_ACCELERATION_RATE 5

// Forward declaration
class EEncoder;

// Callback function types
typedef void (*EncoderCallback)(EEncoder& encoder);
typedef void (*ButtonCallback)(EEncoder& encoder);

class EEncoder {
public:
    // Constructor for encoder with button
    EEncoder(uint8_t pinA, uint8_t pinB, uint8_t buttonPin, uint8_t countsPerDetent = DEFAULT_COUNTS_PER_DETENT);
    
    // Constructor for encoder without button
    EEncoder(uint8_t pinA, uint8_t pinB, uint8_t countsPerDetent = DEFAULT_COUNTS_PER_DETENT);
    
    // Must be called in loop() as often as possible
    void update();
    
    // Set callback handlers
    void setEncoderHandler(EncoderCallback callback);
    void setButtonHandler(ButtonCallback callback);
    void setLongPressHandler(ButtonCallback callback);
    
    // Get the increment value since last callback
    // Returns ±1 per physical detent (normalized from hardware counts)
    // With acceleration enabled, returns ±accelerationRate when turning fast
    int8_t getIncrement() const { return _increment; }
    
    // Get button state (true if pressed)
    bool getButton() const;
    
    // Configuration methods
    void setDebounceInterval(uint16_t intervalMs);  // Button only - encoder doesn't need debouncing
    void setLongPressDuration(uint16_t durationMs);
    void setAcceleration(bool enabled);
    void setAccelerationRate(uint8_t rate);
    
    // Enable/disable the encoder
    void enable(bool enabled = true);
    bool isEnabled() const { return _enabled; }
    
private:
    // Pin assignments
    uint8_t _pinA;
    uint8_t _pinB;
    uint8_t _buttonPin;
    bool _hasButton;
    
    // State tracking for encoder
    uint8_t _lastEncoderState;
    uint8_t _encoderState;
    int8_t _increment;
    
    // Improved position tracking
    int32_t _absolutePosition;       // Total accumulated position
    int32_t _lastCallbackPosition;   // Position at last callback
    uint8_t _lastValidState;         // Last known good detent state
    uint8_t _stateSequence;          // For tracking state machine
    uint32_t _lastStateChangeTime;   // For idle detection
    uint8_t _countsPerDetent;        // Hardware counts per physical detent
    
    // State tracking for button
    bool _buttonState;
    bool _lastButtonState;
    uint32_t _buttonStateChangeTime;
    uint32_t _buttonPressTime;
    bool _longPressHandled;
    
    // Debouncing
    uint16_t _debounceInterval;
    
    // Long press
    uint16_t _longPressDuration;
    
    // Acceleration
    bool _accelerationEnabled;
    uint8_t _accelerationRate;
    uint32_t _lastRotationTime;
    
    // Callbacks
    EncoderCallback _encoderCallback;
    ButtonCallback _buttonCallback;
    ButtonCallback _longPressCallback;
    
    // Enable state
    bool _enabled;
    
    // Internal methods
    void readEncoder();
    void readButton();
    uint8_t getEncoderState();
};

#endif // EEncoder_h
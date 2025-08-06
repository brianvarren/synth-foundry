// ========== MomentarySwitch.h ==========
/*
 * MomentarySwitch - Handler for momentary push buttons
 * Part of the EEncoder library family
 */

#ifndef MOMENTARYSWITCH_H
#define MOMENTARYSWITCH_H

#include <Arduino.h>

class MomentarySwitch {
public:
    // Constructor
    MomentarySwitch(uint8_t pin, bool activeLow = true);
    
    // Callback types
    typedef void (*ButtonHandler)(MomentarySwitch& sw);
    
    // Must be called in loop()
    void update();
    
    // Set callback handlers
    void setPressHandler(ButtonHandler handler) { _pressHandler = handler; }
    void setReleaseHandler(ButtonHandler handler) { _releaseHandler = handler; }
    void setClickHandler(ButtonHandler handler) { _clickHandler = handler; }
    void setLongPressHandler(ButtonHandler handler) { _longPressHandler = handler; }
    void setDoubleClickHandler(ButtonHandler handler) { _doubleClickHandler = handler; }
    
    // Get state
    bool isPressed() const { return _pressed; }
    uint32_t getPressedDuration() const;
    uint8_t getClickCount() const { return _clickCount; }
    
    // Configuration
    void setDebounceDuration(uint32_t ms) { _debounceDuration = ms; }
    void setLongPressDuration(uint32_t ms) { _longPressDuration = ms; }
    void setDoubleClickWindow(uint32_t ms) { _doubleClickWindow = ms; }
    void setEnabled(bool enabled) { _enabled = enabled; }
    bool isEnabled() const { return _enabled; }
    
private:
    uint8_t _pin;
    bool _activeLow;
    bool _pressed = false;
    bool _lastReading = false;
    bool _enabled = true;
    bool _longPressFired = false;
    
    uint32_t _lastChangeTime = 0;
    uint32_t _pressStartTime = 0;
    uint32_t _lastClickTime = 0;
    uint8_t _clickCount = 0;
    
    uint32_t _debounceDuration = 10;
    uint32_t _longPressDuration = 500;
    uint32_t _doubleClickWindow = 300;
    
    ButtonHandler _pressHandler = nullptr;
    ButtonHandler _releaseHandler = nullptr;
    ButtonHandler _clickHandler = nullptr;
    ButtonHandler _longPressHandler = nullptr;
    ButtonHandler _doubleClickHandler = nullptr;
    
    void handlePress();
    void handleRelease();
};

#endif // MOMENTARYSWITCH_H
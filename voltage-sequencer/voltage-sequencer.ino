/*
 * Voltage Sequencer for RP2040
 * Uses DACless for PWM audio output and EEncoder for UI control
 * Core0: Real-time control (encoder, triggers, PWM)
 * Core1: Display updates (OLED via I2C)
 */

#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "DACless.h"
#include "EEncoder.h"
#include "MomentarySwitch.h"

// Pin Definitions
#define PIN_ENC_CLK    1   // Encoder clock (DT on silkscreen)
#define PIN_ENC_DT     0   // Encoder data (CLK on silkscreen)
#define PIN_ENC_SW     2   // Encoder switch
#define PIN_TRIG_LED   4
#define PIN_PWM_OUT    6
#define PIN_RESET_IN   8
#define PIN_TRIG_IN    28
#define PIN_MANUAL_TRIG 26
#define PIN_I2C_SDA    14
#define PIN_I2C_SCL    15

// Display Settings
#define SCREEN_WIDTH   128
#define SCREEN_HEIGHT  64
#define OLED_RESET     -1
#define OLED_ADDRESS   0x3C

// Sequencer Settings
#define MAX_STEPS      16
#define DEFAULT_VOLTAGE 1.65
#define MIN_VOLTAGE    0.0
#define MAX_VOLTAGE    3.3
#define VOLTAGE_INCREMENT_NORMAL 0.005  // 5mV steps (~6 cents in 1V/oct)
#define VOLTAGE_INCREMENT_FAST   0.020  // 20mV steps (~24 cents in 1V/oct)
#define LONG_PRESS_DURATION 5000  // 5 seconds for reset

// Calibration - adjust if your output doesn't match display
// Measure actual output voltage and compare to display to determine these values
// OFFSET: If output is consistently low, increase this value
// SCALE: If error varies with voltage level, adjust this multiplier
#define VOLTAGE_CALIBRATION_OFFSET  0.050  // Add 50mV to compensate for RC filter loss
#define VOLTAGE_CALIBRATION_SCALE   1.000  // Fine-tune scaling if needed

// PWM Settings
#define PWM_BITS       13  // 13-bit: 0.4mV PWM resolution, 15.3kHz carrier
                           // This gives ~0.5 cent pitch resolution for 1V/oct
#define PWM_MAX_VALUE  ((1 << PWM_BITS) - 1)
#define BLOCK_SIZE     16

// UI States
enum UIState {
    STATE_PLAYBACK,
    STATE_EDIT
};

// Global Objects
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire1, OLED_RESET);
EEncoder encoder(PIN_ENC_CLK, PIN_ENC_DT, PIN_ENC_SW, 4);  // 4 counts per detent (standard encoder)
MomentarySwitch manualTrigger(PIN_MANUAL_TRIG, true);  // Active low (pulls to ground)
DAClessAudio* audio;

// Sequencer State (volatile for inter-core access)
struct Sequencer {
    volatile float voltages[MAX_STEPS];
    volatile uint8_t numSteps;
    volatile uint8_t currentStep;
    volatile UIState uiState;
    volatile uint8_t editStep;
    volatile bool encoderPressed;
    volatile uint32_t lastTrigTime;
    volatile uint16_t currentPwmValue;
    volatile bool displayNeedsUpdate;
} seq;

// LED Flash State (non-blocking)
struct LEDFlash {
    bool active;
    uint32_t startTime;
    uint32_t duration;
    uint8_t pattern;  // 0=single, 1=double, 2=triple
    uint8_t flashCount;
    bool ledState;
    uint32_t lastToggle;
} ledFlash = {false, 0, 0, 0, 0, false, 0};

// Core synchronization
volatile bool core1Ready = false;

// Trigger Detection
volatile bool triggerPending = false;
volatile bool resetPending = false;
bool lastTrigState = HIGH;
bool lastResetState = HIGH;

// Function Prototypes
void initSequencer();
void updateDisplay();
void handleEncoder(EEncoder& enc);
void handleEncoderButton(EEncoder& enc);
void handleEncoderLongPress(EEncoder& enc);
void handleManualTrigger(MomentarySwitch& sw);
void processTriggers();
void advanceStep();
void resetSequence();
float constrainVoltage(float v);
uint16_t voltageToPWM(float voltage);
void audioBlockCallback(void* userdata, uint16_t* buffer);
void updateLEDFlash();
void startLEDFlash(uint8_t pattern, uint32_t duration);

void setup() {
    Serial.begin(115200);
    
    // Initialize sequencer state
    initSequencer();
    
    // Initialize hardware (except display - that's on core1)
    // Configure trigger inputs with pullups
    pinMode(PIN_TRIG_IN, INPUT_PULLUP);
    pinMode(PIN_RESET_IN, INPUT_PULLUP);
    
    // Configure trigger LED
    pinMode(PIN_TRIG_LED, OUTPUT);
    digitalWrite(PIN_TRIG_LED, LOW);
    
    // Setup encoder callbacks
    encoder.setEncoderHandler(handleEncoder);
    encoder.setButtonHandler(handleEncoderButton);
    encoder.setLongPressHandler(handleEncoderLongPress);
    encoder.setLongPressDuration(LONG_PRESS_DURATION);
    encoder.setDebounceInterval(5);  // Minimal debounce for fast response
    encoder.setAcceleration(true);
    encoder.setAccelerationRate(4);  // 4x speed when turning fast (more reasonable)
    
    // Setup manual trigger button - use press handler for immediate response
    manualTrigger.setPressHandler(handleManualTrigger);
    manualTrigger.setDebounceDuration(5);  // Minimal debounce for fast response
    
    // Setup DACless audio
    DAClessConfig audioConfig;
    audioConfig.pinPWM = PIN_PWM_OUT;
    audioConfig.pwmBits = PWM_BITS;
    audioConfig.blockSize = BLOCK_SIZE;
    audioConfig.nAdcInputs = 0;  // No ADC inputs needed
    
    audio = new DAClessAudio(audioConfig);
    audio->setBlockCallback(audioBlockCallback, &seq);
    audio->begin();
    
    // Wait for core1 to be ready
    while (!core1Ready) {
        delay(1);
    }
    
    Serial.println("Voltage Sequencer Ready!");
    Serial.print("PWM Resolution: ");
    Serial.print(PWM_BITS);
    Serial.println(" bits");
    Serial.print("Voltage Resolution: ");
    Serial.print(3.3 / PWM_MAX_VALUE * 1000, 3);
    Serial.println(" mV/step (PWM)");
    Serial.print("Encoder Resolution: ");
    Serial.print(VOLTAGE_INCREMENT_NORMAL * 1000, 1);
    Serial.println(" mV/click (5mV)");
    Serial.print("Calibration Offset: +");
    Serial.print(VOLTAGE_CALIBRATION_OFFSET * 1000, 1);
    Serial.println(" mV");
    Serial.println("\nControls:");
    Serial.println("  Playback Mode:");
    Serial.println("    - Turn encoder: Cycle through steps");
    Serial.println("    - Press+Turn: Add/remove steps");
    Serial.println("    - Click encoder: Enter edit mode");
    Serial.println("    - Manual trigger: Advance to next step");
    Serial.println("  Edit Mode:");
    Serial.println("    - Turn encoder: Adjust voltage");
    Serial.println("        Slow: 5mV steps");
    Serial.println("        Medium: 10mV steps");
    Serial.println("        Fast: 20mV steps");
    Serial.println("    - Press+Turn: Add/remove steps");
    Serial.println("    - Click encoder: Return to playback");
    Serial.println("    - Manual trigger: Edit next step");
    Serial.println("    - Long press (5s): Reset all voltages to 1.65V");
    Serial.println("\nDisplay running on Core1");
}

// Core1 setup - handles display
void setup1() {
    // Configure I2C for display (400kHz fast mode)
    Wire1.setSDA(PIN_I2C_SDA);
    Wire1.setSCL(PIN_I2C_SCL);
    Wire1.begin();
    Wire1.setClock(400000);  // 400kHz I2C for faster display updates
    
    // Initialize display
    if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDRESS, &Wire1)) {
        Serial.println("SSD1306 allocation failed");
    }
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.display();
    
    core1Ready = true;
}

void loop() {
    // Core0: Handle all real-time tasks
    // Update encoder and manual trigger
    encoder.update();
    manualTrigger.update();
    
    // Update LED flash state (non-blocking)
    updateLEDFlash();
    
    // Process external triggers (always active for trig/reset inputs)
    processTriggers();
}

// Core1 loop - handles display updates
void loop1() {
    static uint32_t lastDisplayUpdate = 0;
    uint32_t now = millis();
    
    // Update display when needed or periodically
    if (seq.displayNeedsUpdate || (now - lastDisplayUpdate > 100)) {
        updateDisplay();
        lastDisplayUpdate = now;
        seq.displayNeedsUpdate = false;
    }
    
    // Small delay to prevent core1 from hogging resources
    delay(10);
}

void initSequencer() {
    seq.numSteps = 8;  // Default to 8 steps
    seq.currentStep = 0;
    seq.uiState = STATE_PLAYBACK;
    seq.editStep = 0;
    seq.encoderPressed = false;
    seq.lastTrigTime = 0;
    seq.displayNeedsUpdate = true;
    
    // Initialize all steps to default voltage
    for (int i = 0; i < MAX_STEPS; i++) {
        seq.voltages[i] = DEFAULT_VOLTAGE;
    }
    
    // Set initial PWM value
    seq.currentPwmValue = voltageToPWM(seq.voltages[0]);
}

void updateDisplay() {
    display.clearDisplay();
    
    // Title bar
    display.setTextSize(1);
    display.setCursor(0, 0);
    
    if (seq.uiState == STATE_EDIT) {
        display.print("EDIT STEP ");
        display.print(seq.editStep + 1);
        display.print("/");
        display.print(seq.numSteps);
        
        // Show press+turn hint
        if (seq.encoderPressed) {
            display.setCursor(100, 0);
            display.print("+/-");
        }
    } else {
        display.print("PLAY ");
        display.print(seq.currentStep + 1);
        display.print("/");
        display.print(seq.numSteps);
        display.print(" steps");
        
        // Show press+turn hint
        if (seq.encoderPressed) {
            display.setCursor(100, 0);
            display.print("+/-");
        }
    }
    
    // Voltage display (extra large)
    display.setTextSize(3);  // Larger text
    display.setCursor(10, 18);  // Better centered
    
    float displayVoltage;
    if (seq.uiState == STATE_EDIT) {
        displayVoltage = seq.voltages[seq.editStep];
    } else {
        displayVoltage = seq.voltages[seq.currentStep];
    }
    
    // Display with 2 decimal places (hundredths)
    display.print(displayVoltage, 2);
    display.setTextSize(2);  // Smaller 'V'
    display.print("V");
    
    // Step visualization (bottom) - taller bars
    display.setTextSize(1);
    int barWidth = SCREEN_WIDTH / seq.numSteps;
    int barY = 46;  // Moved up slightly
    int barHeight = 16;  // Taller bars (was 10)
    
    for (int i = 0; i < seq.numSteps; i++) {
        int x = i * barWidth;
        
        // Highlight current/edit step
        if ((seq.uiState == STATE_PLAYBACK && i == seq.currentStep) ||
            (seq.uiState == STATE_EDIT && i == seq.editStep)) {
            display.fillRect(x, barY, barWidth - 1, barHeight, SSD1306_WHITE);
            // Draw voltage level in black (inverted)
            int level = (seq.voltages[i] / MAX_VOLTAGE) * (barHeight - 2);
            display.fillRect(x + 1, barY + barHeight - level - 1, barWidth - 3, level, SSD1306_BLACK);
        } else {
            // Draw voltage level in white
            int level = (seq.voltages[i] / MAX_VOLTAGE) * (barHeight - 2);
            display.drawRect(x, barY, barWidth - 1, barHeight, SSD1306_WHITE);
            display.fillRect(x + 1, barY + barHeight - level - 1, barWidth - 3, level, SSD1306_WHITE);
        }
    }
    
    display.display();
}

void handleEncoder(EEncoder& enc) {
    int8_t increment = enc.getIncrement();
    if (increment == 0) return;
    
    // Check if button is pressed for adding/removing steps
    bool buttonPressed = (digitalRead(PIN_ENC_SW) == LOW);
    seq.encoderPressed = buttonPressed;
    
    if (buttonPressed) {
        // Press+turn: Add/remove steps (works in both modes)
        int newSteps = seq.numSteps + increment;
        if (newSteps > MAX_STEPS) newSteps = MAX_STEPS;
        if (newSteps < 2) newSteps = 2;
        
        if (newSteps != seq.numSteps) {
            seq.numSteps = newSteps;
            
            // Wrap current/edit step if needed
            if (seq.currentStep >= seq.numSteps) {
                seq.currentStep = 0;
                seq.currentPwmValue = voltageToPWM(seq.voltages[0]);
            }
            if (seq.editStep >= seq.numSteps) {
                seq.editStep = seq.numSteps - 1;
            }
            
            Serial.print("Steps: ");
            Serial.println(seq.numSteps);
        }
    } else {
        // Normal turning: Navigate through steps or adjust voltage
        if (seq.uiState == STATE_EDIT) {
            // Edit mode: adjust voltage of current step
            // Progressive acceleration: the faster you turn, the bigger the steps
            float delta;
            uint8_t absInc = abs(increment);
            
            if (absInc == 1) {
                // Single click - finest control (5mV)
                delta = VOLTAGE_INCREMENT_NORMAL;
            } else if (absInc <= 4) {
                // Moderate speed - medium steps (10mV)
                delta = VOLTAGE_INCREMENT_NORMAL * 2;
            } else {
                // Fast turning - larger steps (20mV)
                delta = VOLTAGE_INCREMENT_FAST;
            }
            
            // Apply direction
            delta *= (increment > 0) ? 1 : -1;
            
            seq.voltages[seq.editStep] = constrainVoltage(seq.voltages[seq.editStep] + delta);
            
            // Update PWM if we're editing the current playing step
            if (seq.editStep == seq.currentStep) {
                seq.currentPwmValue = voltageToPWM(seq.voltages[seq.currentStep]);
            }
        } else {
            // Playback mode: cycle through steps
            int newStep = (int)seq.currentStep + increment;
            while (newStep >= seq.numSteps) newStep -= seq.numSteps;
            while (newStep < 0) newStep += seq.numSteps;
            seq.currentStep = newStep;
            seq.currentPwmValue = voltageToPWM(seq.voltages[seq.currentStep]);
            
            // Flash LED briefly to indicate step change
            startLEDFlash(0, 15);  // Very brief flash
            
            Serial.print("Manual step ");
            Serial.print(seq.currentStep + 1);
            Serial.print(": ");
            Serial.print(seq.voltages[seq.currentStep], 2);
            Serial.println("V");
        }
    }
    
    seq.displayNeedsUpdate = true;
}

void handleEncoderButton(EEncoder& enc) {
    // Toggle between playback and edit mode
    if (seq.uiState == STATE_PLAYBACK) {
        seq.uiState = STATE_EDIT;
        seq.editStep = seq.currentStep;  // Start editing current step
        Serial.println("Entering EDIT mode");
    } else {
        seq.uiState = STATE_PLAYBACK;
        seq.encoderPressed = false;
        // When returning to playback, go to the step we were editing
        seq.currentStep = seq.editStep;
        seq.currentPwmValue = voltageToPWM(seq.voltages[seq.currentStep]);
        Serial.println("Entering PLAYBACK mode");
    }
    seq.displayNeedsUpdate = true;
}

void handleEncoderLongPress(EEncoder& enc) {
    Serial.println("Long press detected - resetting all voltages to 1.65V");
    
    // Reset all voltages
    for (int i = 0; i < MAX_STEPS; i++) {
        seq.voltages[i] = DEFAULT_VOLTAGE;
    }
    
    // Update current PWM value
    seq.currentPwmValue = voltageToPWM(seq.voltages[seq.currentStep]);
    
    // Start triple flash to indicate reset
    startLEDFlash(2, 600);  // Triple flash over 600ms
    
    seq.displayNeedsUpdate = true;
}

void handleManualTrigger(MomentarySwitch& sw) {
    if (seq.uiState == STATE_PLAYBACK) {
        // In playback mode: trigger advance
        triggerPending = true;
    } else {
        // In edit mode: advance to next step for editing
        seq.editStep++;
        if (seq.editStep >= seq.numSteps) {
            seq.editStep = 0;
        }
        
        // Flash LED to indicate step change
        startLEDFlash(0, 20);  // Single 20ms flash
        
        seq.displayNeedsUpdate = true;
        
        Serial.print("Edit step ");
        Serial.print(seq.editStep + 1);
        Serial.println();
    }
}

void processTriggers() {
    // Read external trigger inputs with debouncing
    bool trigState = digitalRead(PIN_TRIG_IN);
    bool resetState = digitalRead(PIN_RESET_IN);
    
    // Detect falling edge on trigger input (active low)
    if (trigState == LOW && lastTrigState == HIGH) {
        // Only advance in playback mode from external trigger
        if (seq.uiState == STATE_PLAYBACK) {
            triggerPending = true;
        }
    }
    lastTrigState = trigState;
    
    // Detect falling edge on reset input (works in both modes)
    if (resetState == LOW && lastResetState == HIGH) {
        resetPending = true;
    }
    lastResetState = resetState;
    
    // Process reset (higher priority)
    if (resetPending) {
        resetSequence();
        resetPending = false;
    }
    // Process trigger (playback mode only)
    else if (triggerPending && seq.uiState == STATE_PLAYBACK) {
        // Minimal debounce for fast response
        if (millis() - seq.lastTrigTime > 5) {
            advanceStep();
            seq.lastTrigTime = millis();
        }
        triggerPending = false;
    }
}

void advanceStep() {
    seq.currentStep++;
    if (seq.currentStep >= seq.numSteps) {
        seq.currentStep = 0;
    }
    
    // Update PWM value
    seq.currentPwmValue = voltageToPWM(seq.voltages[seq.currentStep]);
    
    Serial.print("Step ");
    Serial.print(seq.currentStep + 1);
    Serial.print(": ");
    Serial.print(seq.voltages[seq.currentStep], 2);
    Serial.println("V");
}

void resetSequence() {
    seq.currentStep = 0;
    seq.currentPwmValue = voltageToPWM(seq.voltages[0]);
    
    Serial.println("Sequence reset to step 1");
    
    // Double flash LED for reset
    for (int i = 0; i < 2; i++) {
        digitalWrite(PIN_TRIG_LED, HIGH);
        delay(20);
        digitalWrite(PIN_TRIG_LED, LOW);
        delay(20);
    }
}

float constrainVoltage(float v) {
    if (v < MIN_VOLTAGE) return MIN_VOLTAGE;
    if (v > MAX_VOLTAGE) return MAX_VOLTAGE;
    return v;
}

uint16_t voltageToPWM(float voltage) {
    // Apply calibration to compensate for RC filter and other losses
    voltage = constrainVoltage(voltage);
    
    // Apply calibration: scale first, then add offset
    float calibratedVoltage = (voltage * VOLTAGE_CALIBRATION_SCALE) + VOLTAGE_CALIBRATION_OFFSET;
    
    // Ensure calibrated voltage doesn't exceed PWM range
    if (calibratedVoltage > MAX_VOLTAGE) {
        calibratedVoltage = MAX_VOLTAGE;
    }
    
    // Convert to PWM value
    return (uint16_t)((calibratedVoltage / MAX_VOLTAGE) * PWM_MAX_VALUE);
}

void audioBlockCallback(void* userdata, uint16_t* buffer) {
    Sequencer* s = (Sequencer*)userdata;
    
    // Fill buffer with current PWM value
    for (int i = 0; i < BLOCK_SIZE; i++) {
        buffer[i] = s->currentPwmValue;
    }
}

// Non-blocking LED flash functions
void startLEDFlash(uint8_t pattern, uint32_t duration) {
    ledFlash.pattern = pattern;
    ledFlash.duration = duration;
    ledFlash.startTime = millis();
    ledFlash.active = true;
    ledFlash.flashCount = 0;
    ledFlash.ledState = true;
    ledFlash.lastToggle = millis();
    digitalWrite(PIN_TRIG_LED, HIGH);
}

void updateLEDFlash() {
    if (!ledFlash.active) return;
    
    uint32_t now = millis();
    uint32_t elapsed = now - ledFlash.startTime;
    
    // Check if flash sequence is complete
    if (elapsed >= ledFlash.duration) {
        digitalWrite(PIN_TRIG_LED, LOW);
        ledFlash.active = false;
        return;
    }
    
    // Calculate flash timing based on pattern
    uint32_t flashInterval;
    uint8_t maxFlashes;
    
    switch (ledFlash.pattern) {
        case 0: // Single flash - just turn off after duration
            if (elapsed >= ledFlash.duration) {
                digitalWrite(PIN_TRIG_LED, LOW);
                ledFlash.active = false;
            }
            return;
            
        case 1: // Double flash
            flashInterval = 40;
            maxFlashes = 4; // ON-OFF-ON-OFF
            break;
            
        case 2: // Triple flash
            flashInterval = 100;
            maxFlashes = 6; // ON-OFF-ON-OFF-ON-OFF
            break;
            
        default:
            ledFlash.active = false;
            return;
    }
    
    // Toggle LED at intervals
    if ((now - ledFlash.lastToggle) >= flashInterval && ledFlash.flashCount < maxFlashes) {
        ledFlash.ledState = !ledFlash.ledState;
        digitalWrite(PIN_TRIG_LED, ledFlash.ledState ? HIGH : LOW);
        ledFlash.lastToggle = now;
        ledFlash.flashCount++;
    }
}
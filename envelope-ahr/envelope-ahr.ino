#include "DACless.h"
#include <hardware/pwm.h>

// Pin definitions
const uint PIN_TRIGGER = 4;      // SW_INVERT trigger input
const uint PIN_LED_PWM = 6;      // LED output (visual feedback)
const uint PIN_CV_PWM = 0;       // CV output (main envelope output)
const uint PIN_OUTPUT_INVERT = 3; // Output invert switch (GP10)
const uint PIN_TRIGGER_LED = 2;  // Trigger indicator LED (GP2)

// ADC channel mapping
const uint ADC_ATTACK = 0;       // GP26
const uint ADC_HOLD = 1;         // GP27  
const uint ADC_RELEASE = 2;      // GP28
const uint ADC_TIME_CV = 3;      // GP29

// Envelope states
enum EnvelopeState {
  ENV_IDLE,
  ENV_ATTACK,
  ENV_HOLD,
  ENV_RELEASE
};

// Global variables
EnvelopeState envState = ENV_IDLE;
uint32_t envTimer = 0;
uint32_t holdTimer = 0;
bool triggerInvert = false;  // Set based on switch position
bool outputInvert = false;   // Output inversion flag
volatile bool triggerReceived = false;  // Flag set by interrupt

// Trigger LED timing
uint32_t triggerLedTimer = 0;    // Millisecond counter for LED pulse
const uint32_t TRIGGER_LED_PULSE_MS = 5;  // 5ms pulse duration

// Fixed-point envelope level (16.16 format: upper 16 bits integer, lower 16 bits fraction)
uint32_t envLevelFixed = 0;
const uint32_t FIXED_ONE = 65536;  // 1.0 in 16.16 fixed point
const uint32_t FIXED_MAX_12BIT = 4095 << 16;  // 4095.0 in fixed point

// Pre-calculated increment values for efficiency
uint32_t attackIncrement = 1;    // Fixed-point increment per sample
uint32_t releaseIncrement = 1;   // Fixed-point increment per sample

// Timing parameters (in samples)
uint32_t attackTime = 44100;   // 1 second at 44.1kHz
uint32_t holdTime = 44100;
uint32_t releaseTime = 44100;
float timeScale = 1.0f;

// DACless configuration for CV output
DAClessConfig config;
DAClessAudio* audio;

// LED PWM slice
uint ledSlice;

// Interrupt handler for trigger input
void triggerISR() {
  // Set flag for main loop to process
  triggerReceived = true;
}

// Audio callback - called at sample rate
void audioCallback(void* userdata, uint16_t* buffer) {
  for (int i = 0; i < config.blockSize; i++) {
    // Process envelope using fixed-point math
    switch (envState) {
      case ENV_IDLE:
        envLevelFixed = 0;
        break;
        
      case ENV_ATTACK:
        envLevelFixed += attackIncrement;
        if (envLevelFixed >= FIXED_ONE) {
          envLevelFixed = FIXED_ONE;
          envState = ENV_HOLD;
          holdTimer = 0;
        }
        break;
        
      case ENV_HOLD:
        envLevelFixed = FIXED_ONE;
        holdTimer++;
        if (holdTimer >= holdTime) {
          envState = ENV_RELEASE;
        }
        break;
        
      case ENV_RELEASE:
        if (envLevelFixed > releaseIncrement) {
          envLevelFixed -= releaseIncrement;
        } else {
          envLevelFixed = 0;
          envState = ENV_IDLE;
        }
        break;
    }
    
    // Convert fixed-point to 12-bit PWM value
    // Apply output inversion if needed
    uint32_t outputLevel = envLevelFixed;
    if (outputInvert) {
      outputLevel = FIXED_ONE - envLevelFixed;
    }
    
    // Fast conversion: multiply by 4095 and shift right 16
    buffer[i] = (uint16_t)((outputLevel * 4095) >> 16);
  }
  
  // Update LED PWM (less frequently, just use last envelope value)
  // LED always shows non-inverted envelope
  pwm_set_gpio_level(PIN_LED_PWM, (uint16_t)((envLevelFixed * 4095) >> 16));
}

void setup() {
  Serial.begin(115200);
  while(!Serial);
  
  // Configure trigger input with pullup and attach interrupt
  pinMode(PIN_TRIGGER, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(PIN_TRIGGER), triggerISR, FALLING);  // Trigger on falling edge (HIGH to LOW)
  
  // Configure output invert switch with pullup
  pinMode(PIN_OUTPUT_INVERT, INPUT_PULLUP);
  
  // Configure trigger LED as output
  pinMode(PIN_TRIGGER_LED, OUTPUT);
  digitalWrite(PIN_TRIGGER_LED, LOW);  // Start with LED off
  
  // Configure LED PWM (separate from DACless)
  gpio_set_function(PIN_LED_PWM, GPIO_FUNC_PWM);
  ledSlice = pwm_gpio_to_slice_num(PIN_LED_PWM);
  pwm_set_wrap(ledSlice, 4095);  // 12-bit resolution to match DACless
  pwm_set_enabled(ledSlice, true);
  
  // Configure DACless for CV output
  config.pinPWM = PIN_CV_PWM;
  config.pwmBits = 12;          // 12-bit resolution
  config.blockSize = 128;       // Small block size for responsive envelope
  config.nAdcInputs = 4;        // Read all 4 ADC inputs
  
  // Create and initialize DACless
  audio = new DAClessAudio(config);
  audio->setBlockCallback(audioCallback);
  audio->begin();
  
  Serial.println("Envelope Generator Started");
  Serial.print("Sample Rate: ");
  Serial.println(audio->getSampleRate());
}

void loop() {
  // Read ADC values and calculate timing parameters
  // INVERT the readings so knobs work in expected direction
  uint16_t attackRaw = 4095 - audio->getADC(ADC_ATTACK);
  uint16_t holdRaw = 4095 - audio->getADC(ADC_HOLD);
  uint16_t releaseRaw = 4095 - audio->getADC(ADC_RELEASE);
  uint16_t timeCvRaw = 4095 - audio->getADC(ADC_TIME_CV);

  // Convert ADC readings to time values
  // Map 0-4095 to useful time ranges
  float sampleRate = audio->getSampleRate();
  
  // Attack: 1ms to 2 seconds
  float attackMs = 1.0f + (attackRaw / 4095.0f) * 1999.0f;
  attackTime = (uint32_t)(attackMs * sampleRate / 1000.0f);
  
  // Hold: 0ms to 2 seconds  
  float holdMs = (holdRaw / 4095.0f) * 2000.0f;
  holdTime = (uint32_t)(holdMs * sampleRate / 1000.0f);
  
  // Release: 1ms to 3 seconds
  float releaseMs = 1.0f + (releaseRaw / 4095.0f) * 2999.0f;
  releaseTime = (uint32_t)(releaseMs * sampleRate / 1000.0f);
  
  // Time CV scaling: 0.1x to 10x (exponential)
  float cvNorm = timeCvRaw / 4095.0f;
  //timeScale = pow(100.0f, cvNorm - 0.5f);  // 0.1 to 10 range, 1.0 at center
  timeScale = 1.0f;
  
  // Apply time scaling to all parameters
  attackTime = (uint32_t)(attackTime * timeScale);
  holdTime = (uint32_t)(holdTime * timeScale);
  releaseTime = (uint32_t)(releaseTime * timeScale);
  
  // Clamp to reasonable ranges and ensure minimum of 1 sample
  attackTime = max((uint32_t)1, min(attackTime, (uint32_t)(sampleRate * 10)));  // 1 sample to 10s
  holdTime = max((uint32_t)0, min(holdTime, (uint32_t)(sampleRate * 10)));      // 0 to 10s
  releaseTime = max((uint32_t)1, min(releaseTime, (uint32_t)(sampleRate * 10))); // 1 sample to 10s
  
  // Pre-calculate fixed-point increments for attack and release
  // These are calculated here (outside audio callback) to avoid division in the hot path
  if (attackTime > 0) {
    attackIncrement = FIXED_ONE / attackTime;  // Fixed-point increment per sample
  } else {
    attackIncrement = FIXED_ONE;  // Instant attack
  }
  
  if (releaseTime > 0) {
    releaseIncrement = FIXED_ONE / releaseTime;  // Fixed-point increment per sample
  } else {
    releaseIncrement = FIXED_ONE;  // Instant release
  }
  
  // Read output invert switch (active low - pulled up internally)
  outputInvert = !digitalRead(PIN_OUTPUT_INVERT);
  
  // Check if trigger was received (set by interrupt)
  if (triggerReceived) {
    // Clear the flag
    triggerReceived = false;
    
    // ALWAYS reset the envelope on trigger, regardless of current state
    envState = ENV_ATTACK;
    envLevelFixed = 0;  // Start from zero
    envTimer = 0;
    holdTimer = 0;  // Also reset hold timer
    
    // Start trigger LED pulse
    digitalWrite(PIN_TRIGGER_LED, HIGH);
    triggerLedTimer = millis();  // Record when we turned it on
    
    // Serial.println("TRIGGER! Envelope reset.");
    // Serial.print("State now: ");
    Serial.println(envState);
  }
  
  // Handle trigger LED pulse timing
  if (digitalRead(PIN_TRIGGER_LED) == HIGH) {
    // Check if pulse duration has elapsed
    if (millis() - triggerLedTimer >= TRIGGER_LED_PULSE_MS) {
      digitalWrite(PIN_TRIGGER_LED, LOW);  // Turn off the LED
    }
  }
  
  // Debug output periodically
  static uint32_t debugCounter = 0;
  if (debugCounter++ % 2000 == 0) {  // Print every 2 seconds or so
    // Serial.print("Envelope State: ");
    // Serial.print(envState);
    // Serial.print(" Level: ");
    // Serial.print((float)envLevelFixed / FIXED_ONE);  // Convert back to float for display
    // Serial.print(" Trigger Pin: ");
    // Serial.println(digitalRead(PIN_TRIGGER));
  }
  
  // Small delay to not flood the system
  delay(1);
}
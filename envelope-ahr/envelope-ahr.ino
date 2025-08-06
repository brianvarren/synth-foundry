#include "DACless.h"
#include <hardware/pwm.h>

// Pin definitions
const uint PIN_TRIGGER = 3;      // SW_INVERT trigger input
const uint PIN_LED_PWM = 6;      // LED output (visual feedback)
const uint PIN_CV_PWM = 8;       // CV output (main envelope output)

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
float envLevel = 0.0f;
uint32_t envTimer = 0;
uint32_t holdTimer = 0;
bool lastTriggerState = false;
bool triggerInvert = false;  // Set based on switch position

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

// Audio callback - called at sample rate
void audioCallback(void* userdata, uint16_t* buffer) {
  for (int i = 0; i < config.blockSize; i++) {
    // Process envelope
    switch (envState) {
      case ENV_IDLE:
        envLevel = 0.0f;
        break;
        
      case ENV_ATTACK:
        if (envTimer < attackTime) {
          envLevel = (float)envTimer / (float)attackTime;
          envTimer++;
        } else {
          envState = ENV_HOLD;
          envTimer = 0;
          holdTimer = 0;
        }
        break;
        
      case ENV_HOLD:
        envLevel = 1.0f;
        holdTimer++;
        if (holdTimer >= holdTime) {
          envState = ENV_RELEASE;
          envTimer = 0;
        }
        break;
        
      case ENV_RELEASE:
        if (envTimer < releaseTime) {
          envLevel = 1.0f - ((float)envTimer / (float)releaseTime);
          envTimer++;
        } else {
          envState = ENV_IDLE;
          envLevel = 0.0f;
        }
        break;
    }
    
    // Convert envelope level to PWM value (12-bit)
    buffer[i] = (uint16_t)(envLevel * 4095.0f);
  }
  
  // Update LED PWM (less frequently, just use last envelope value)
  pwm_set_gpio_level(PIN_LED_PWM, (uint16_t)(envLevel * 4095.0f));
}

void setup() {
  Serial.begin(115200);
  
  // Configure trigger input with pullup
  pinMode(PIN_TRIGGER, INPUT_PULLUP);
  
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
  uint16_t attackRaw = audio->getADC(ADC_ATTACK);
  uint16_t holdRaw = audio->getADC(ADC_HOLD);
  uint16_t releaseRaw = audio->getADC(ADC_RELEASE);
  uint16_t timeCvRaw = audio->getADC(ADC_TIME_CV);
  
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
  timeScale = pow(100.0f, cvNorm - 0.5f);  // 0.1 to 10 range, 1.0 at center
  
  // Apply time scaling to all parameters
  attackTime = (uint32_t)(attackTime * timeScale);
  holdTime = (uint32_t)(holdTime * timeScale);
  releaseTime = (uint32_t)(releaseTime * timeScale);
  
  // Clamp to reasonable ranges
  attackTime = max((uint32_t)44, min(attackTime, (uint32_t)(sampleRate * 10)));  // 1ms to 10s
  holdTime = max((uint32_t)0, min(holdTime, (uint32_t)(sampleRate * 10)));      // 0 to 10s
  releaseTime = max((uint32_t)44, min(releaseTime, (uint32_t)(sampleRate * 10))); // 1ms to 10s
  
  // Check trigger input
  bool currentTrigger = digitalRead(PIN_TRIGGER);
  
  // Apply inversion if switch is set (you might want to read this from hardware)
  if (triggerInvert) {
    currentTrigger = !currentTrigger;
  }
  
  // Detect rising edge
  if (currentTrigger && !lastTriggerState) {
    // Trigger the envelope
    if (envState == ENV_IDLE || envState == ENV_RELEASE) {
      envState = ENV_ATTACK;
      envTimer = 0;
    }
  }
  
  lastTriggerState = currentTrigger;
  
  // Small delay to not flood the system
  delay(1);
}
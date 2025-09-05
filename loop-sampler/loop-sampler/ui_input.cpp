#include <Arduino.h>
#include "EEncoder.h"
#include "config_pins.h"        // ENC_A_PIN, ENC_B_PIN, ENC_BTN_PIN, ENC_COUNTS_PER_DETENT
#include "ui_input.h"
#include "ui_display.h"
#include "ADCless.h"
#include "adc_filter.h"
#include "RotarySwitch.h"
#include "sf_globals_bridge.h"

namespace sf {

// Define pins for rotary switch shift register
#define PL_PIN  13   // GP_PL (Parallel Load)
#define CP_PIN  12   // GP_CP (Clock Pulse)
#define Q7_PIN  14   // GP_Q7 (Serial Data)

#define ENC_COUNTS_PER_DETENT 4

// Create 8-position rotary switch
RotarySwitch octave(8, PL_PIN, CP_PIN, Q7_PIN);

// Keep the encoder object file-local and static
static EEncoder s_enc(ENC_A_PIN, ENC_B_PIN, ENC_BTN_PIN, ENC_COUNTS_PER_DETENT);

void ui_octave_change_callback(RotarySwitch& oct) {
  Serial.print("Octave changed to: "); Serial.println(oct.getPosition());
}

// These two *global* functions are what your driver expects to find and call.
void ui_encoder_turn_callback(EEncoder& enc) {
  int8_t inc = enc.getIncrement();   // normalized ±1, or ±N with accel
  display_on_turn(inc);
}

void ui_encoder_button_press_callback(EEncoder& /*enc*/) {
  display_on_button();
}

// Call this once from setup() after display/SD init
void ui_input_init() {
  configureADC_DMA();

  // Enable median-of-3 on all 8 channels: mask = 0xFF (adjust as you like)
  adc_filter_init(ADC_FILTER_DISPLAY_TICK_HZ, ADC_FILTER_CUTOFF_HZ, 0xFFu);
  
  octave.setChangeHandler(ui_octave_change_callback);
  s_enc.setEncoderHandler(ui_encoder_turn_callback);
  s_enc.setButtonHandler(ui_encoder_button_press_callback);
  s_enc.setAcceleration(false); // if your lib supports it and you want it
  // Nothing else here; onEncoderTurn/onButtonPress do the routing.
}

void ui_input_update() {
    octave.update();
    s_enc.update();
}

} // namespace sf
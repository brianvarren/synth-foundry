#include <Arduino.h>
#include "EEncoder.h"
#include "config_pins.h"        // ENC_A_PIN, ENC_B_PIN, ENC_BTN_PIN, ENC_COUNTS_PER_DETENT
#include "ui_input.h"
#include "ui_browser.h"

namespace sf {

#define ENC_COUNTS_PER_DETENT 4

// Keep the encoder object file-local and static
static EEncoder s_enc(ENC_A_PIN, ENC_B_PIN, ENC_BTN_PIN, ENC_COUNTS_PER_DETENT);

// These two *global* functions are what your driver expects to find and call.
void ui_encoder_turn_callback(EEncoder& enc)
{
  int8_t inc = enc.getIncrement();   // normalized ±1, or ±N with accel
  browser_on_turn(inc);
}

void ui_encoder_button_press_callback(EEncoder& /*enc*/)
{
  browser_on_button();
}

// Call this once from setup() after display/SD init
void ui_input_init()
{
  s_enc.setEncoderHandler(ui_encoder_turn_callback);
  s_enc.setButtonHandler(ui_encoder_button_press_callback);
  s_enc.setAcceleration(false); // if your lib supports it and you want it
  // Nothing else here; onEncoderTurn/onButtonPress do the routing.
}

void ui_input_update()
{
    s_enc.update();
}

} // namespace sf
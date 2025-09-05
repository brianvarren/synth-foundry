#pragma once

// Forward-declare to keep this header light.
// (We include <EEncoder.h> in the .cpp.)
class RotarySwitch;
class EEncoder;

namespace sf {

// Global callbacks the EEncoder driver will call.
// Define them in ui_input.cpp (one translation unit only).
void ui_octave_change_callback(RotarySwitch& oct);
void ui_encoder_turn_callback(EEncoder& enc);
void ui_encoder_button_press_callback(EEncoder& enc);

void ui_input_init();
void ui_input_update();

} // namespace ui_input

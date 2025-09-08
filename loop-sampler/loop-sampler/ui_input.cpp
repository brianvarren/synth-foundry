/**
 * @file ui_input.cpp
 * @brief User input handling system - encoders, switches, and ADC processing
 * 
 * This file manages all user input devices for the loop sampler:
 * - Rotary encoder with button (for file browsing and navigation)
 * - 8-position rotary switch (for octave selection)
 * - 7 analog control inputs (pots/knobs for real-time control)
 * - ADC filtering to prevent audio artifacts from knob jitter
 * 
 * ## Input Devices
 * 
 * **Rotary Encoder**: Used for file browsing and menu navigation.
 * Supports acceleration for faster scrolling through long file lists.
 * 
 * **Rotary Switch**: 8-position switch for octave selection:
 * - Position 0: LFO mode (ultra-slow playback)
 * - Positions 1-7: -3 to +3 octaves
 * 
 * **Analog Controls**: 7 ADC channels for real-time parameter control:
 * - Loop start position
 * - Loop length
 * - Pitch fine tune
 * - Phase modulation (future use)
 * - Crossfade length
 * - FX1 (future use)
 * - FX2 (future use)
 * 
 * ## ADC Filtering
 * 
 * All analog inputs are filtered to prevent audio artifacts from knob jitter.
 * The filter cutoff is set to 2Hz to provide smooth control while maintaining
 * responsiveness for live performance.
 * 
 * @author Brian Varren
 * @version 1.0
 * @date 2024
 */

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

// ── Hardware Pin Definitions ────────────────────────────────────────────────
// Pins for the 8-position rotary switch (connected via shift register)
#define PL_PIN  13   // GP_PL (Parallel Load) - loads switch state into register
#define CP_PIN  12   // GP_CP (Clock Pulse) - shifts data out
#define Q7_PIN  14   // GP_Q7 (Serial Data) - reads switch position


#define ENC_COUNTS_PER_DETENT 4  // Encoder detent resolution

// ── Input Device Objects ────────────────────────────────────────────────────
// Create 8-position rotary switch for octave selection
RotarySwitch octave(8, PL_PIN, CP_PIN, Q7_PIN);

// Rotary encoder for file browsing and navigation (kept static for encapsulation)
static EEncoder s_enc(ENC_A_PIN, ENC_B_PIN, ENC_BTN_PIN, ENC_COUNTS_PER_DETENT);


void ui_octave_change_callback(RotarySwitch& oct) {
  Serial.print("Octave changed to: "); Serial.println(oct.getPosition());
}

// Function to get current octave switch position for audio engine
uint8_t ui_get_octave_position() {
  return octave.getPosition();
}


// These two *global* functions are what your driver expects to find and call.
void ui_encoder_turn_callback(EEncoder& enc) {
  int8_t inc = enc.getIncrement();   // normalized ±1, or ±N with accel
  display_on_turn(inc);
}

void ui_encoder_button_press_callback(EEncoder& /*enc*/) {
  display_on_button();
}

/**
 * @brief Initialize the input system - ADC, encoders, and switches
 * 
 * This function sets up all input devices and their filtering:
 * - Configures ADC DMA for continuous sampling of control inputs
 * - Initializes ADC filtering to prevent audio artifacts from knob jitter
 * - Sets up encoder and switch callback handlers
 * - Enables median-of-3 filtering on all ADC channels for noise reduction
 */
void ui_input_init() {
  // Configure ADC DMA for continuous sampling of control inputs
  configureADC_DMA();

  // Initialize ADC filtering system
  // - Display tick rate: matches audio engine update rate
  // - Cutoff frequency: 2Hz (smooth control, responsive performance)
  // - Median filter: enabled on all 8 channels (0xFF mask) for noise reduction
  adc_filter_init(ADC_FILTER_DISPLAY_TICK_HZ, ADC_FILTER_CUTOFF_HZ, 0xFFu);
  
  
  // Set up callback handlers for input devices
  octave.setChangeHandler(ui_octave_change_callback);      // Octave switch changes
  s_enc.setEncoderHandler(ui_encoder_turn_callback);       // Encoder rotation
  s_enc.setButtonHandler(ui_encoder_button_press_callback); // Encoder button press
  s_enc.setAcceleration(false); // Disable acceleration for precise control
}

/**
 * @brief Update all input devices - called from main loop
 * 
 * This function polls all input devices and processes their state changes.
 * It should be called regularly from the main loop to maintain responsive
 * user interface behavior.
 */
void ui_input_update() {
    octave.update();  // Poll rotary switch for position changes
    s_enc.update();   // Poll encoder for rotation and button events
}

} // namespace sf
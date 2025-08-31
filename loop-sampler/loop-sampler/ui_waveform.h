#pragma once
#include <stdint.h>

namespace sf {

// Initialize waveform display with Q15 audio buffer
// samples: pointer to Q15 audio data in PSRAM
// count: number of Q15 samples
// sampleRate: original sample rate (for time display)
void waveform_init(const int16_t* samples, uint32_t count, uint32_t sampleRate);

// Draw the waveform (call after init)
// Clears screen and renders waveform scaled to fill display
void waveform_draw(void);

// Check if waveform view is active
bool waveform_is_active(void);

// Exit waveform view (called on encoder turn)
void waveform_exit(void);

// Handle encoder events while in waveform view
// Returns true if event was handled, false if should exit to browser
bool waveform_on_turn(int8_t inc);
bool waveform_on_button(void);

} // namespace sf
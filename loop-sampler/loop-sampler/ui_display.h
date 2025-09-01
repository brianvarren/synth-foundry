#pragma once
#include <stdint.h>

typedef bool (*UiLoadFn)(const char* filename);  // app-provided loader (keeps coupling low)

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

// Initialize the browser (scans SD and renders the first page)
void browser_init(UiLoadFn onLoad);

// Call from loop() so long I/O happens outside encoder callbacks
void browser_tick(void);

// Encoder events
void browser_on_turn(int8_t inc);   // ±1 … ±5 per your library’s acceleration
void browser_on_button(void);       // request load of selected item

} // namespace ui

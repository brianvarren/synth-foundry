#pragma once
#include <stdint.h>

// App-provided loader (keeps coupling low). Must perform the actual sample load.
// Returns true on success, false on failure.
typedef bool (*UiLoadFn)(const char* filename);

namespace sf {

// ─────────────────────────── Display FSM ────────────────────────────────
enum DisplayState : uint8_t {
  DS_BROWSER = 0,          // File list visible
  DS_LOADING,              // One-shot load work happens in tick()
  DS_DELAY_TO_WAVEFORM,    // 1s “Loaded” message before drawing waveform
  DS_WAVEFORM              // Waveform view (grayscale)
};

// Optional: query current state (handy for debug/telemetry)
DisplayState display_state(void);

// ────────────────────── Waveform subview (internal-ish) ─────────────────
// Bob keeps these public to avoid breaking existing includes.
// You don’t need to call them directly from the sketch.
void waveform_init(const int16_t* samples, uint32_t count, uint32_t sampleRate);
void waveform_draw(void);
bool waveform_on_turn(int8_t inc);     // returns false to request exit to browser
bool waveform_on_button(void);         // returns false to request exit to browser
void waveform_exit(void);              // force-exit to browser

// ───────────────────────── Browser / Top-level API ──────────────────────
// Initialize the UI (build index, show list). Provide your loader callback.
void browser_init(UiLoadFn onLoad);

// Pump the FSM from loop(). Does any deferred work (load, delay, waveform show).
void browser_tick(void);

// Forward your encoder/button events here.
void browser_on_turn(int8_t inc);      // ±1 … ±5 (acceleration tolerated)
void browser_on_button(void);          // select/load or exit waveform

} // namespace sf

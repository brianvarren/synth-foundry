#pragma once
#include <stdint.h>

// Global ISR entry you can call from ANY timer/ISR source
extern "C" void displayTimerCallback(void);

namespace sf {

// ─────────────────────────── Display FSM ────────────────────────────────
enum DisplayState : uint8_t {
  DS_BOOT = 0,
  DS_SETUP,                    // NEW: showing setup status messages
  DS_BROWSER,
  DS_LOADING,
  DS_DELAY_TO_WAVEFORM,
  DS_WAVEFORM
};

DisplayState display_state(void);

// Waveform subview (kept public; you don't call these from the sketch)
void waveform_init(const int16_t* samples, uint32_t count, uint32_t sampleRate);
void waveform_draw(void);
bool waveform_on_turn(int8_t inc);
bool waveform_on_button(void);
void waveform_exit(void);

// ───────────────────────── Top-level Display API ────────────────────────
// Init hardware and prepare for setup messages
void display_init(void);

// Signal that setup is complete and enter browser
void display_setup_complete(void);

// Call this from loop(). It returns immediately unless an ISR set a flag.
void display_tick(void);

// Forward encoder/button events
void display_on_turn(int8_t inc);
void display_on_button(void);

// Optional: start/stop an internal timer that calls the ISR at 'fps'
bool display_timer_begin(uint32_t fps);   // returns true if started
void display_timer_end(void);

// ───────────────────────── Debug helpers (optional) ─────────────────────
void display_debug_list_files(void);        // prints a simple, non-interactive list
void display_debug_dump_q15(uint32_t n);    // prints first N Q15 samples (max clamps)

} // namespace sf
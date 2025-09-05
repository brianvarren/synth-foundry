#pragma once
#include <stdint.h>

// #define ADC_DEBUG

// Global ISR entry you can call from ANY timer/ISR source
extern "C" void displayTimerCallback(void);

namespace sf {

// // ─────────────────────────── Display FSM ────────────────────────────────
// enum DisplayState : uint8_t {
//   DS_BOOT = 0,
//   DS_SETUP,                    // NEW: showing setup status messages
//   DS_BROWSER,
//   DS_LOADING,
//   DS_DELAY_TO_WAVEFORM,
//   DS_WAVEFORM
// };

// Add ADC_DEBUG state to your enum
enum DisplayState : uint8_t {
  DS_BOOT = 0,
  DS_SETUP,
  DS_BROWSER,
  DS_LOADING,
  DS_DELAY_TO_WAVEFORM,
  DS_WAVEFORM
#ifdef ADC_DEBUG
  ,DS_ADC_DEBUG         // Add ADC debug state
#endif
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

// Add ADC debug functions
#ifdef ADC_DEBUG
void adc_debug_init(void);
void adc_debug_draw(void);
bool adc_debug_on_turn(int8_t inc);
bool adc_debug_on_button(void);
void adc_debug_exit(void);
bool adc_debug_is_active(void);
#endif

} // namespace sf
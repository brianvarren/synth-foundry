#pragma once
#include <stdint.h>
#include <math.h>   // only used by setCutoffHz / setTauMs (not in hot path)

/**
 * AdcEmaFilter
 * - EMA with alpha = 1 / (2^smoothing_shift)  -> y += (x - y) >> shift
 * - Optional median-of-3 prefilter to kill single-sample spikes
 * - All integer math in process(); no heap; O(1)
 *
 * Conventions:
 *  - Class/methods: camelCase
 *  - Variables:     lowercase_underscores
 */

class AdcEmaFilter {
public:
  AdcEmaFilter(uint8_t shift = 3, bool enable_median3 = false)
  : smoothing_shift(shift), use_median3(enable_median3) {
    if (smoothing_shift > 15) smoothing_shift = 15;
    y = 0;
    initialized = false;
    m0 = m1 = 0;
  }

  // Process one 12-bit ADC sample (0..4095). Returns filtered sample (0..4095).
  inline uint16_t process(uint16_t x) {
    uint16_t in = x;
    if (use_median3) {
      // median-of-3 on {x, m1, m0}
      uint16_t a = x, b = m1, c = m0;
      if (a > b) { uint16_t t=a; a=b; b=t; }   // a <= b
      if (b > c) { uint16_t t=b; b=c; c=t; }   // b <= c
      if (a > b) { uint16_t t=a; a=b; b=t; }   // a <= b
      in = b; // median
      m0 = m1;
      m1 = x;
    }

    if (!initialized) {
      y = in;
      initialized = true;
      return in;
    }

    // EMA: y += (x - y) >> shift
    int32_t delta = (int32_t)in - (int32_t)y;
    y += (delta >> smoothing_shift);
    return (uint16_t)y;
  }

  inline void setSmoothingShift(uint8_t shift) {
    smoothing_shift = (shift > 15) ? 15 : shift;
  }

  // Convenience: pick a shift from desired cutoff vs. call frequency (ticks/sec)
  // alpha_float = 1 - exp(-2*pi*fc/fs)  ~ 2^-shift  ->  shift ~ log2(1/alpha)
  inline void setCutoffHz(float tick_rate_hz, float cutoff_hz) {
    if (cutoff_hz <= 0.f || tick_rate_hz <= 0.f) return;
    float alpha = 1.f - expf(-2.f * 3.14159265f * cutoff_hz / tick_rate_hz);
    if (alpha < 1e-6f) alpha = 1e-6f;
    int s = (int)lroundf(log2f(1.0f / alpha));
    if (s < 0) s = 0; if (s > 15) s = 15;
    smoothing_shift = (uint8_t)s;
  }

  inline void setTauMs(float tick_rate_hz, float tau_ms) {
    if (tau_ms <= 0.f || tick_rate_hz <= 0.f) return;
    float fc = 1.0f / (2.f * 3.14159265f * (tau_ms / 1000.f)); // 1/(2π τ)
    setCutoffHz(tick_rate_hz, fc);
  }

  inline void enableMedian3(bool on) { use_median3 = on; }

  inline uint16_t value() const { return (uint16_t)y; }

private:
  uint8_t  smoothing_shift;   // 0 = no smoothing, 15 = heavy smoothing
  bool     use_median3;
  bool     initialized;

  // EMA state
  int32_t  y;                 // current filtered value (0..4095 range)

  // median-of-3 state
  uint16_t m0, m1;            // last two raw samples
};

/* ─────────────────────── Centralized filter API ──────────────────────────
   One module-level bank of filters for every ADC channel.
   Call adc_filter_update_from_dma() at a fixed cadence (e.g., your display
   timer ISR or once-per-audio-block) to update all channels from adc_results_buf[].
   Then read with adc_filter_get(ch) anywhere (audio/display).
*/

// Configure all channels at once.
// median3_mask: bit i enables median-of-3 on channel i (1=on).
void adc_filter_init(float update_rate_hz, float cutoff_hz, uint32_t median3_mask);

// Optional runtime re-tune helpers (all channels).
void adc_filter_set_cutoff_all(float update_rate_hz, float cutoff_hz);
void adc_filter_set_shift_all(uint8_t shift);
void adc_filter_enable_median3_mask(uint32_t median3_mask);

// Update bank from adc_results_buf[]. O(NUM_ADC_INPUTS), no heap.
void adc_filter_update_from_dma(void);

// Read the latest filtered sample (0..4095). Safe across cores.
uint16_t adc_filter_get(uint8_t ch);

// Bulk snapshot into caller-provided buffer; copies min(n, NUM_ADC_INPUTS).
void adc_filter_snapshot(uint16_t* dst, uint32_t n);

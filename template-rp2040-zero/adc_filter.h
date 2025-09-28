/**
 * @file adc_filter.h
 * @brief ADC input filtering system for smooth control processing
 * 
 * This header provides a sophisticated filtering system for analog control inputs
 * to prevent audio artifacts from knob jitter and electrical noise. The system
 * uses exponential moving average (EMA) filters with optional median-of-3
 * prefiltering for robust, real-time control processing.
 * 
 * ## Filtering Features
 * 
 * **Exponential Moving Average (EMA)**: Smooths control inputs using integer-only
 * arithmetic for consistent performance. The smoothing amount is controlled by
 * a shift parameter (0 = no smoothing, 15 = heavy smoothing).
 * 
 * **Median-of-3 Prefiltering**: Optional spike removal that eliminates single-sample
 * noise spikes that can cause audio artifacts. Uses a 3-sample median filter
 * on each channel independently.
 * 
 * **Real-time Performance**: All filtering operations use integer arithmetic and
 * are optimized for real-time audio processing. No heap allocation or floating-point
 * operations in the hot path.
 * 
 * ## Filter Configuration
 * 
 * The system supports both simple shift-based configuration and frequency-based
 * configuration using cutoff frequencies and time constants. This allows for
 * precise tuning of filter characteristics for different control types.
 * 
 * @author Brian Varren
 * @version 1.0
 * @date 2024
 */

#pragma once
#include <stdint.h>
#include <math.h>   // only used by setCutoffHz / setTauMs (not in hot path)

/**
 * @class AdcEmaFilter
 * @brief Exponential Moving Average filter for ADC input smoothing
 * 
 * This class implements an EMA filter with the formula:
 * y += (x - y) >> shift
 * 
 * Where:
 * - x is the input sample (0..4095)
 * - y is the filtered output
 * - shift controls smoothing (0 = no smoothing, 15 = heavy smoothing)
 * 
 * The filter also supports optional median-of-3 prefiltering to eliminate
 * single-sample noise spikes that can cause audio artifacts.
 * 
 * All operations use integer arithmetic for consistent real-time performance.
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

// ── Centralized Filter API ────────────────────────────────────────────────────
// The system provides a centralized bank of filters for all ADC channels.
// This allows consistent filtering across the entire system while maintaining
// real-time performance.
//
// Usage Pattern:
// 1. Call adc_filter_update_from_dma() at regular intervals (e.g., audio block rate)
// 2. Read filtered values with adc_filter_get(ch) from anywhere in the system
// 3. All filtering is done in the background with no blocking operations

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

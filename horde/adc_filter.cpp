#include "adc_filter.h"
#include "ADCless.h"   // NUM_ADC_INPUTS, adc_results_buf[]
#include <stddef.h>

// ───────────────────────── Module state ───────────────────────────────────
static AdcEmaFilter       s_filters[NUM_ADC_INPUTS];           // per-channel EMA
static volatile uint16_t  s_filtered[NUM_ADC_INPUTS];          // published values
static volatile uint8_t   s_inited = 0;

// ──────────────────────── Small helpers (no heap) ─────────────────────────
static inline uint8_t clamp_u8(uint8_t v, uint8_t hi) { return (v > hi) ? hi : v; }

void adc_filter_init(float update_rate_hz, float cutoff_hz, uint32_t median3_mask) {
  // Default construct then configure
  for (uint32_t i = 0; i < NUM_ADC_INPUTS; ++i) {
    s_filters[i].setCutoffHz(update_rate_hz, cutoff_hz);
    s_filters[i].enableMedian3((median3_mask >> i) & 1u);
    // Prime outputs with current raw (prevents initial jump on first read)
    uint16_t raw = adc_results_buf[i];
    (void)s_filters[i].process(raw);
    s_filtered[i] = s_filters[i].value();
  }
  s_inited = 1;
}

void adc_filter_set_cutoff_all(float update_rate_hz, float cutoff_hz) {
  for (uint32_t i = 0; i < NUM_ADC_INPUTS; ++i) {
    s_filters[i].setCutoffHz(update_rate_hz, cutoff_hz);
  }
}

void adc_filter_set_shift_all(uint8_t shift) {
  uint8_t s = clamp_u8(shift, 15);
  for (uint32_t i = 0; i < NUM_ADC_INPUTS; ++i) {
    s_filters[i].setSmoothingShift(s);
  }
}

void adc_filter_enable_median3_mask(uint32_t median3_mask) {
  for (uint32_t i = 0; i < NUM_ADC_INPUTS; ++i) {
    s_filters[i].enableMedian3((median3_mask >> i) & 1u);
  }
}

void adc_filter_update_from_dma(void) {
  if (!s_inited) return;
  // Update each channel once from the latest DMA result
  for (uint32_t i = 0; i < NUM_ADC_INPUTS; ++i) {
    uint16_t raw = adc_results_buf[i];           // 12-bit 0..4095
    uint16_t f   = s_filters[i].process(raw);    // EMA (+ optional median3)
    s_filtered[i] = f;                           // publish (16-bit write)
  }
}

uint16_t adc_filter_get(uint8_t ch) {
  if (ch >= NUM_ADC_INPUTS) return 0;
  // 16-bit read; on RP2040 this is a single aligned access
  return s_filtered[ch];
}

void adc_filter_snapshot(uint16_t* dst, uint32_t n) {
  if (!dst) return;
  uint32_t count = (n < NUM_ADC_INPUTS) ? n : NUM_ADC_INPUTS;
  for (uint32_t i = 0; i < count; ++i) {
    dst[i] = s_filtered[i];
  }
}

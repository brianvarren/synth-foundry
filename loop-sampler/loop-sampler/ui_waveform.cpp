#include <Arduino.h>
#include <U8g2lib.h>
#include "ui_waveform.h"
#include "display_driver.h"   // for grayscale functions
#include "display_views.h"    // for view_clear_log(), view_flush_if_dirty()

namespace sf {

// SH1122 OLED dimensions
static const uint16_t SCREEN_WIDTH = 256;
static const uint16_t SCREEN_HEIGHT = 64;

// Grayscale levels (0=black, 15=white)
static const uint8_t SHADE_BACKGROUND = 0;   // Black background
static const uint8_t SHADE_WAVEFORM = 12;    // 75% brightness (12/16)
static const uint8_t SHADE_DIM = 4;          // For future loop zone dimming
static const uint8_t SHADE_CENTERLINE = 6;   // Optional center line

// Waveform state
static const int16_t* s_samples = nullptr;
static uint32_t s_sampleCount = 0;
static uint32_t s_sampleRate = 0;
static bool s_active = false;

// Helper: Find min/max in a range of Q15 samples
static void find_min_max_range(const int16_t* samples, 
                                uint32_t start, uint32_t end,
                                int16_t& min_out, int16_t& max_out) {
  if (start >= end) {
    min_out = 0;
    max_out = 0;
    return;
  }
  
  int16_t min_val = samples[start];
  int16_t max_val = samples[start];
  
  for (uint32_t i = start + 1; i < end; i++) {
    if (samples[i] < min_val) min_val = samples[i];
    if (samples[i] > max_val) max_val = samples[i];
  }
  
  min_out = min_val;
  max_out = max_val;
}

void waveform_init(const int16_t* samples, uint32_t count, uint32_t sampleRate) {
  s_samples = samples;
  s_sampleCount = count;
  s_sampleRate = sampleRate;
  s_active = true;
}

void waveform_draw(void) {
  // Clear to black
  gray4_clear(SHADE_BACKGROUND);

  const int W = 256;
  const int H = 64;
  const int mid = H / 2;

  if (!s_samples || s_sampleCount == 0) {
    // Draw just a center line for empty waveform
    gray4_draw_hline(0, W - 1, mid, SHADE_CENTERLINE);
    gray4_send_buffer();
    return;
  }

  // 1) Compute samples-per-pixel in double to avoid truncation
  const double spp = (double)s_sampleCount / (double)W;

  // 2) Global peak for robust integer scaling (Q15)
  int16_t gmin =  32767;
  int16_t gmax = -32768;
  for (uint32_t i = 0; i < s_sampleCount; ++i) {
    int16_t v = s_samples[i];
    if (v < gmin) gmin = v;
    if (v > gmax) gmax = v;
  }
  int32_t peak = gmax;
  if (-gmin > peak) peak = -gmin;
  
  if (peak < 1) {
    // Silence - just draw center line
    gray4_draw_hline(0, W - 1, mid, SHADE_CENTERLINE);
    gray4_send_buffer();
    return;
  }

  // Optional: Draw subtle center line first
  // gray4_draw_hline(0, W - 1, mid, SHADE_CENTERLINE);

  // 3) Draw vertical min/max "envelope" per column
  for (int x = 0; x < W; ++x) {
    uint32_t start = (uint32_t)floor((double)x * spp);
    uint32_t end   = (uint32_t)floor((double)(x + 1) * spp);

    if (start >= s_sampleCount) break;
    if (end <= start) end = start + 1;
    if (end > s_sampleCount) end = s_sampleCount;

    int16_t cmin =  32767;
    int16_t cmax = -32768;
    for (uint32_t i = start; i < end; ++i) {
      int16_t v = s_samples[i];
      if (v < cmin) cmin = v;
      if (v > cmax) cmax = v;
    }

    // Map Q15 -> screen with integer math (top=0, bottom=H-1)
    int yMin = mid - ((int32_t)cmax * (H / 2)) / peak;
    int yMax = mid - ((int32_t)cmin * (H / 2)) / peak;

    if (yMin < 0)      yMin = 0;
    if (yMin > H - 1)  yMin = H - 1;
    if (yMax < 0)      yMax = 0;
    if (yMax > H - 1)  yMax = H - 1;

    // Draw waveform column at 75% brightness
    if (yMin == yMax) {
      gray4_set_pixel(x, yMin, SHADE_WAVEFORM);
    } else {
      gray4_draw_vline(x, yMin, yMax, SHADE_WAVEFORM);
    }
  }

  // Send the grayscale buffer to display
  gray4_send_buffer();
}

bool waveform_is_active(void) {
  return s_active;
}

void waveform_exit(void) {
  s_active = false;
  // Don't clear the sample pointers - they might still be valid
  // Just mark as inactive so browser can take over
}

bool waveform_on_turn(int8_t inc) {
  // Any encoder turn exits waveform view
  if (inc != 0) {
    waveform_exit();
    return false;  // Let browser handle the turn
  }
  return true;
}

bool waveform_on_button(void) {
  // Button press also exits back to browser
  waveform_exit();
  return false;  // Let browser re-render
}

// === Future functions for loop zone visualization ===

void waveform_set_loop_zone(uint32_t startSample, uint32_t endSample) {
  // Future: Store loop points and redraw with dimmed areas outside
  // Areas outside loop zone would be drawn at SHADE_DIM instead of SHADE_WAVEFORM
}

void waveform_draw_with_antialiasing(void) {
  // Future: Wu's line algorithm for smoother diagonal waveforms
  // Would blend adjacent pixels with intermediate shade values
}

} // namespace sf
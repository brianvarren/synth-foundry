#include <Arduino.h>
#include "driver_sh1122.h"
#include "adc_filter.h"
#include "audio_engine.h"
#include "sf_globals_bridge.h"
#include "ui_display.h"

namespace sf {

// Use public FSM accessors instead of extern globals
DisplayState display_state(void);
void display_set_state(DisplayState st);
void browser_render_sample_list(void);         // implemented in ui_display.cpp

// ─────────────────────── Display constants (no heap) ─────────────────────
static const uint8_t SHADE_BACKGROUND   = 0;   // 0..15 (SH1122 grayscale)
static const uint8_t SHADE_WAVEFORM     = 12;
static const uint8_t SHADE_CENTERLINE   = 0;
static const uint8_t SHADE_WAVEFORM_DIM = 4;

// ───────────────────────── Waveform cache & overlay helpers ──────────────
static uint8_t s_wave_ymin[256];
static uint8_t s_wave_ymax[256];
static int     s_lastStartPx    = -1;
static int     s_lastEndPx      = -1;
static int     s_lastPlayheadPx = -1;
bool           s_wave_ready     = false;

// Waveform state
static const int16_t* s_samples     = 0;    // Q15 pointer in PSRAM
static uint32_t       s_sampleCount = 0;
static uint32_t       s_sampleRate  = 0;

static inline int adc12ToPx256(uint16_t v) {
  return (v * 256) >> 12;
}

static inline void drawWaveColumn(int x, uint8_t y_min, uint8_t y_max, uint8_t shade) {
  if (y_min > y_max) { uint8_t t = y_min; y_min = y_max; y_max = t; }
  if (y_min == y_max) {
    gray4_set_pixel(x, y_min, shade);
  } else {
    gray4_draw_vline(x, y_min, y_max, shade);
  }
}

static inline void clearColumnToBackground(int x) {
  gray4_draw_vline(x, 0, 64 - 1, SHADE_BACKGROUND);
}

static inline void restoreWaveColumn(int x, uint8_t shade) {
  clearColumnToBackground(x);
  drawWaveColumn(x, s_wave_ymin[x], s_wave_ymax[x], shade);
}

static inline void restoreWaveSpan(int x0, int x1, uint8_t shade) {
  if (x0 > x1) return;
  for (int x = x0; x <= x1; ++x) restoreWaveColumn(x, shade);
}

static inline void draw_center_line_if_empty(void) {
  const int W = 256;
  const int H = 64;
  gray4_draw_hline(0, W - 1, H / 2, SHADE_CENTERLINE);
}

// ───────────────────────────── Waveform view ─────────────────────────────
void waveform_init(const int16_t* samples, uint32_t count, uint32_t sampleRate) {
  s_samples     = samples;
  s_sampleCount = count;
  s_sampleRate  = sampleRate;
}

void waveform_draw(void) {
  gray4_clear(SHADE_BACKGROUND);

  const int W = 256;
  const int H = 64;
  const int mid = H / 2;

  if (!s_samples || s_sampleCount == 0) {
    gray4_draw_hline(0, W - 1, mid, SHADE_WAVEFORM);
    s_wave_ready = false;
    gray4_send_buffer();
    return;
  }

  // Peak estimate for normalization
  int16_t peak = 1;
  {
    const uint32_t step = (s_sampleCount > 4096u) ? (s_sampleCount / 4096u) : 1u;
    int16_t maxabs = 1;
    for (uint32_t i = 0; i < s_sampleCount; i += step) {
      int16_t v = s_samples[i];
      int16_t a = (v < 0) ? (int16_t)-v : v;
      if (a > maxabs) maxabs = a;
    }
    peak = (maxabs < 128) ? 128 : maxabs;
  }

  // samples-per-pixel
  const double spp = (double)s_sampleCount / (double)W;

  // Build envelope cache and draw the waveform at full shade
  for (int x = 0; x < W; ++x) {
    uint32_t start = (uint32_t)floor((double)x * spp);
    uint32_t end   = (uint32_t)floor((double)(x + 1) * spp);
    if (start >= s_sampleCount) break;
    if (end <= start) end = start + 1;
    if (end > s_sampleCount) end = s_sampleCount;

    int16_t cmin =  32767, cmax = -32768;
    for (uint32_t i = start; i < end; ++i) {
      int16_t v = s_samples[i];
      if (v < cmin) cmin = v;
      if (v > cmax) cmax = v;
    }

    int yMin = mid - ((int32_t)cmax * (H / 2)) / peak;
    int yMax = mid - ((int32_t)cmin * (H / 2)) / peak;

    if (yMin < 0)      yMin = 0;
    if (yMin > H - 1)  yMin = H - 1;
    if (yMax < 0)      yMax = 0;
    if (yMax > H - 1)  yMax = H - 1;

    s_wave_ymin[x] = (uint8_t)yMin;
    s_wave_ymax[x] = (uint8_t)yMax;

    drawWaveColumn(x, s_wave_ymin[x], s_wave_ymax[x], SHADE_WAVEFORM);
  }

  // reset loop overlay history so the first overlay tick repaints cleanly
  s_lastStartPx = -1;
  s_lastEndPx   = -1;
  s_wave_ready  = true;

  gray4_send_buffer();
}

bool waveform_on_turn(int8_t /*inc*/) {
  // Any input exits to browser in current UX
  waveform_exit();
  return false;
}

bool waveform_on_button(void) {
  waveform_exit();
  return false;
}

bool waveform_is_active(void) { return display_state() == DS_WAVEFORM; }

void waveform_exit(void) { display_set_state(DS_BROWSER); browser_render_sample_list(); }

// ───────────────────────── Overlay (loop shading and playheads) ──────────
void waveform_overlay_tick(void) {
  if (!s_wave_ready) return;

  // 1) ACTIVE loop from audio (for shading; contiguous, no wrap)
  sf_vis_snapshot_t snap;
  vis_get_snapshot(&snap);

  int act_start_px  = (int)((snap.start_q12 * 256u + 2047u) / 4095u);
  int act_length_px = (int)((snap.len_q12   * 256u + 2047u) / 4095u);
  int act_end_px    = act_start_px + act_length_px;
  if (act_start_px < 0)   act_start_px = 0;
  if (act_start_px > 255) act_start_px = 255;
  if (act_end_px   < 0)   act_end_px   = 0;
  if (act_end_px   > 255) act_end_px   = 255;

  // 2) RETICLE from live filtered ADC (shows real-time knob positions)
  // This shows where the user is trying to set the loop, using the same
  // calculation as the audio engine to ensure reticle matches actual bounds
  const uint16_t loop_start_adc  = adc_filter_get(ADC_LOOP_START_CH);
  const uint16_t loop_length_adc = adc_filter_get(ADC_LOOP_LEN_CH);

  // Use the same minimum loop length calculation as the audio engine
  const uint32_t MIN_LOOP_LEN = 64u;  // Fixed minimum: 64 samples
  const uint32_t span_total = (snap.total > MIN_LOOP_LEN) ? (snap.total - MIN_LOOP_LEN) : 0;
  
  // Calculate reticle positions using the same logic as audio engine
  const uint32_t ret_start_sample = (span_total ? (uint32_t)(((uint64_t)loop_start_adc * (uint64_t)span_total) / 4095u) : 0u);
  const uint32_t ret_len_samples = MIN_LOOP_LEN + (span_total ? (uint32_t)(((uint64_t)loop_length_adc * (uint64_t)span_total) / 4095u) : 0u);
  const uint32_t ret_end_sample = ret_start_sample + ret_len_samples;
  
  // Convert sample positions to pixel positions
  int ret_start_px = (snap.total > 0) ? (int)(((uint64_t)ret_start_sample * 256u) / (uint64_t)snap.total) : 0;
  int ret_end_px = (snap.total > 0) ? (int)(((uint64_t)ret_end_sample * 256u) / (uint64_t)snap.total) : 0;
  
  // Clamp to valid pixel range
  if (ret_start_px < 0) ret_start_px = 0;
  if (ret_start_px > 255) ret_start_px = 255;
  if (ret_end_px < 0) ret_end_px = 0;
  if (ret_end_px > 255) ret_end_px = 255;

  // 3) Playheads
  int ph1_px = -1, ph2_px = -1;
  if (snap.total > 0) {
    ph1_px = (int)(((uint64_t)snap.playhead_idx  * 256u) / (uint64_t)snap.total);
    if (ph1_px > 255) ph1_px = 255;
    if (snap.xfade_active) {
      ph2_px = (int)(((uint64_t)snap.playhead2_idx * 256u) / (uint64_t)snap.total);
      if (ph2_px > 255) ph2_px = 255;
    }
  }

  auto shade_at_active = [&](int x) -> uint8_t {
    if (x < 0 || x > 255) return SHADE_WAVEFORM_DIM;
    return (x >= act_start_px && x <= act_end_px) ? SHADE_WAVEFORM : SHADE_WAVEFORM_DIM;
  };

  // Clear previous playhead cursors by restoring waveform underneath
  if (s_lastPlayheadPx >= 0 && s_lastPlayheadPx < 256) {
    restoreWaveColumn(s_lastPlayheadPx, shade_at_active(s_lastPlayheadPx));
  }
  static int s_lastPlayheadPx2 = -1;
  if (s_lastPlayheadPx2 >= 0 && s_lastPlayheadPx2 < 256) {
    restoreWaveColumn(s_lastPlayheadPx2, shade_at_active(s_lastPlayheadPx2));
  }

  // Paint ACTIVE loop shading
  if (act_start_px <= act_end_px) {
    restoreWaveSpan(act_start_px, act_end_px, SHADE_WAVEFORM);
    if (act_start_px > 0)   restoreWaveSpan(0,              act_start_px - 1, SHADE_WAVEFORM_DIM);
    if (act_end_px   < 255) restoreWaveSpan(act_end_px + 1, 255,              SHADE_WAVEFORM_DIM);
  } else {
    restoreWaveSpan(0, 255, SHADE_WAVEFORM_DIM);
  }

  // Boundary markers: draw the RETICLE
  if (ret_start_px >= 0 && ret_start_px < 256) gray4_draw_vline(ret_start_px, 0, 64 - 1, 15);
  if (ret_end_px   >= 0 && ret_end_px   < 256) gray4_draw_vline(ret_end_px,   0, 64 - 1, 15);

  // Draw playhead cursors
  if (ph1_px >= 0) {
    gray4_draw_vline(ph1_px, 0, 64 - 1, 15);
  }
  if (snap.xfade_active && ph2_px >= 0 && ph2_px != ph1_px) {
    gray4_draw_vline(ph2_px, 0, 64 - 1, 8);
  }

  s_lastPlayheadPx  = ph1_px;
  s_lastPlayheadPx2 = ph2_px;

  s_lastStartPx = act_start_px;
  s_lastEndPx   = act_end_px;
  gray4_send_buffer();
}

} // namespace sf



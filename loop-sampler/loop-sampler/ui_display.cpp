#include <Arduino.h>
#include "driver_sh1122.h"     // gray4_* API
#include "driver_sdcard.h"     // FileIndex + file_index_scan(), sd_format_size()
#include "ADCless.h"
#include "ui_display.h"
#include "storage_loader.h"    // extern audioData/audioSampleCount, etc.
#include "storage_wav_meta.h"  // extern currentWav (for sampleRate)
#include "sf_globals_bridge.h"
#include <pico/time.h>         // pico-sdk timer API
#include "adc_filter.h"
#include "audio_engine.h"

namespace sf {

// ─────────────────────────── Browser state (UI) ──────────────────────────
static FileIndex  s_idx;                // names[], sizes[], count
static int        s_sel  = 0;           // selected row
static int        s_top  = 0;           // top row of the current page
static int        s_pendingIdx = -1;    // index captured on "load" press

// ─────────────────────────── Waveform state (UI) ─────────────────────────
static const int16_t* s_samples     = 0;    // Q15 pointer in PSRAM
static uint32_t       s_sampleCount = 0;
static uint32_t       s_sampleRate  = 0;

// ─────────────────────────────── FSM state ───────────────────────────────
static DisplayState s_state = DS_SETUP;  // START IN SETUP MODE
static uint32_t     s_tDelayUntil = 0;   // millis() deadline for DS_DELAY_TO_WAVEFORM

// ──────────────────────────── Timer ISR state ────────────────────────────
static volatile bool     s_pendingUpdate = false;  // ISR sets, display_tick clears
static repeating_timer_t s_displayTimer;           // pico-sdk timer handle
static bool              s_timerActive = false;    // track if timer is running

// ────────────────────────── Forward declarations ─────────────────────────
static void browser_render_sample_list(void);

// ─────────────────────── Display constants (no heap) ─────────────────────
static const uint8_t SHADE_BACKGROUND  = 0;   // 0..15 (SH1122 grayscale)
static const uint8_t SHADE_WAVEFORM    = 12;
static const uint8_t SHADE_DIM         = 4;   // reserved for future loop zone
static const uint8_t SHADE_CENTERLINE  = 0;
static const uint8_t SHADE_WAVEFORM_DIM = 4;  // NEW: dim waveform outside loop

// ───────────────────────── Waveform cache & overlay helpers ───────────────
// The waveform is drawn once; shading is overlaid on top each tick.
static uint8_t s_wave_ymin[256];
static uint8_t s_wave_ymax[256];
static bool    s_wave_ready   = false;
static int     s_lastStartPx  = -1;
static int     s_lastEndPx    = -1;
static int     s_lastPlayheadPx = -1; // Track previous cursor position

static inline int adc12ToPx256(uint16_t v) {
  // 12-bit ADC → 0..255 screen coordinate
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

static inline void drawPlayheadCursor(int x) {
  if (x >= 0 && x < 256) {
    // Draw a bright vertical line for the playhead
    gray4_draw_vline(x, 0, 64 - 1, 15);  // max brightness (15)
  }
}

static inline void drawBoundaryLines(int start_px, int end_px) {
  if (start_px >= 0 && start_px < 256) gray4_draw_vline(start_px, 0, 64 - 1, 8);
  if (end_px   >= 0 && end_px   < 256) gray4_draw_vline(end_px,   0, 64 - 1, 8);
}

static void overlayLoopShadingTick() {
  if (!s_wave_ready) return;

  // Read the latest loop params
  uint16_t loop_start_adc  = adc_filter_get(ADC_LOOP_START_CH);
  uint16_t loop_length_adc = adc_filter_get(ADC_LOOP_LEN_CH);

  int start_px  = adc12ToPx256(loop_start_adc);   // 0..255
  int length_px = adc12ToPx256(loop_length_adc);  // 0..255
  int end_px    = start_px + length_px;
  if (end_px >= 256) end_px -= 256; // wrap to 0..255

  // Calculate playhead position from phase (display-rate math, not audio-rate!)
  int playhead_px = -1;
  if (s_sampleCount > 0) {
    uint64_t phase_snapshot = g_phase_q32_32;  // atomic read
    uint64_t idx = phase_snapshot >> 32;       // get integer part
    // Normalize to 0..255 pixels
    playhead_px = (int)((idx * 256u) / s_sampleCount);
    if (playhead_px > 255) playhead_px = 255;  // safety clamp
  }

  // Clear previous playhead cursor (restore the waveform at that column)
  if (s_lastPlayheadPx >= 0 && s_lastPlayheadPx < 256) {
    // Determine what shade this column should have based on loop region
    uint8_t shade = SHADE_WAVEFORM_DIM;  // default to dim (outside loop)
    
    // Check if this column is inside the loop
    if (end_px >= start_px) {
      if (s_lastPlayheadPx >= start_px && s_lastPlayheadPx <= end_px) {
        shade = SHADE_WAVEFORM;
      }
    } else {
      // wrapped loop
      if (s_lastPlayheadPx >= start_px || s_lastPlayheadPx <= end_px) {
        shade = SHADE_WAVEFORM;
      }
    }
    
    restoreWaveColumn(s_lastPlayheadPx, shade);
  }

  // 1) Paint INSIDE the loop region at full shade (always)
  if (end_px >= start_px) {
    // contiguous
    restoreWaveSpan(start_px, end_px, SHADE_WAVEFORM);
  } else {
    // wrap: [start..255] U [0..end]
    restoreWaveSpan(start_px, 255, SHADE_WAVEFORM);
    restoreWaveSpan(0, end_px,   SHADE_WAVEFORM);
  }

  // 2) Paint OUTSIDE the loop region at dim shade (always)
  if (end_px >= start_px) {
    if (start_px > 0)   restoreWaveSpan(0, start_px - 1, SHADE_WAVEFORM_DIM);
    if (end_px < 255)   restoreWaveSpan(end_px + 1, 255, SHADE_WAVEFORM_DIM);
  } else {
    // wrap outside: (end+1 .. start-1) if there is space
    if (end_px + 1 <= start_px - 1)
      restoreWaveSpan(end_px + 1, start_px - 1, SHADE_WAVEFORM_DIM);
  }

  // 3) Boundary markers
  drawBoundaryLines(start_px, end_px);

  // 4) Draw playhead cursor (on top of everything)
  if (playhead_px >= 0) {
    drawPlayheadCursor(playhead_px);
  }
  s_lastPlayheadPx = playhead_px;

  // 5) Flush
  s_lastStartPx = start_px;
  s_lastEndPx   = end_px;
  gray4_send_buffer();
}

// ───────────────────────────── Timer ISR ─────────────────────────────────
// Global ISR entry point (extern "C" so it's callable from anywhere)
extern "C" void displayTimerCallback(void) {
  s_pendingUpdate = true;
}

// Pico-SDK timer callback (returns true to keep timer running)
static bool display_timer_isr(repeating_timer_t* /*rt*/) {
  displayTimerCallback();
  return true;  // keep timer running
}

// ───────────────────────────── Small helpers ─────────────────────────────
static void draw_center_line_if_empty(void) {
  const int W = 256;
  const int H = 64;
  gray4_draw_hline(0, W - 1, H / 2, SHADE_CENTERLINE);
}

static void render_status_line(const char* msg) {
  // View layer is assumed to be text-mode (u8g2) underneath
  view_clear_log();
  view_print_line(msg);
  view_flush_if_dirty();
}

// ───────────────────────────── FSM accessors ─────────────────────────────
DisplayState display_state(void) { return s_state; }

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

bool waveform_is_active(void) {
  return s_state == DS_WAVEFORM;
}

void waveform_exit(void) {
  s_state = DS_BROWSER;
  browser_render_sample_list();  // switch back to text view and redraw
}

// ─────────────────────────── Browser rendering ───────────────────────────
static void browser_render_sample_list() {


  Serial.print("browser_render_sample_list from core ");
  Serial.println(get_core_num());   // 0 or 1

  view_set_auto_scroll(false); // stop auto-scrolling while browsing

  view_clear_log();
  // Header
  {
    char title[40];
    snprintf(title, sizeof(title), "Files on SD (%d)", s_idx.count);
    view_print_line(title);
  }

  // Body (paged list)
  const int visible = 7; // rows that fit your font/height
  const int end     = (s_top + visible <= s_idx.count) ? (s_top + visible) : s_idx.count;

  for (int i = s_top; i < end; ++i) {
    char line[64];
    char sizeStr[16];
    sd_format_size(s_idx.sizes[i], sizeStr, sizeof(sizeStr));
    const char marker = (i == s_sel) ? '>' : ' ';
    snprintf(line, sizeof(line), "%c %s (%s)", marker, s_idx.names[i], sizeStr);
    view_print_line(line);
  }

  // Footer (selection position)
  {
    char footer[16];
    snprintf(footer, sizeof(footer), "%d/%d", (s_sel + 1), s_idx.count);
    view_print_line(footer);
  }

  view_flush_if_dirty();
}

// ──────────────────────── Top-level Display API ──────────────────────────
void display_init(void) {
  // Initialize hardware
  sh1122_init();
  
  // Initialize state variables
  s_sel  = 0;
  s_top  = 0;
  s_pendingIdx = -1;
  s_pendingUpdate = false;

  // Stay in DS_SETUP state - don't scan files or show browser yet
  s_state = DS_SETUP;

  // Start display timer at 30 FPS (you can adjust this)
  if (!display_timer_begin(30)) {
    Serial.println("Warning: Failed to start display timer");
    // Not fatal - display_tick() can still be called manually from loop()
  }
}

void display_setup_complete(void) {
  // Build the index (blocking scan)
  if (!file_index_scan(s_idx)) {
    render_status_line("SD scan failed");
    // Even on failure, enter browser (will show 0 files)
  }
  
  // Transition to browser and render
  s_state = DS_BROWSER;
  browser_render_sample_list();
}

void display_tick(void) {
  // Quick exit if no update needed (this is the common case)
  if (!s_pendingUpdate) {
    return;
  }
  
  // Clear the flag atomically
  s_pendingUpdate = false;
  
  // Process FSM based on current state
  switch (s_state) {
    
    case DS_SETUP:
      // In setup mode, just allow status messages to be shown
      // Don't do anything special here
      break;

    case DS_LOADING: {
      if (s_pendingIdx < 0 || s_pendingIdx >= s_idx.count) {
        s_pendingIdx = -1;
        s_state = DS_BROWSER;
        //browser_render_sample_list();
        return;
      }

      view_set_auto_scroll(true);
      view_clear_log();
      {
        char line[64];
        snprintf(line, sizeof(line), "Loading: %s", s_idx.names[s_pendingIdx]);
        view_print_line(line);
      }
      view_flush_if_dirty();

      // Do the actual load (no callback now)
      const char* path = s_idx.names[s_pendingIdx];
      uint32_t bytesRead = 0, required = 0;
      float mbps = 0.0f;

      const bool ok = storage_load_sample_q15_psram(path, &mbps, &bytesRead, &required);

      // Status lines (keep it text-only here)
      {
        char line[64];
        if (ok) {
          char sizeBuf[16];
          sd_format_size(bytesRead, sizeBuf, sizeof(sizeBuf));
          snprintf(line, sizeof(line), "Speed: %.2f MB/s", mbps);
          view_print_line(line);
          snprintf(line, sizeof(line), "✓ Loaded %s (%u samples)", sizeBuf, (unsigned)(bytesRead / 2));
          view_print_line(line);
        } else {
          view_print_line("✗ Load failed");
        }
      }
      view_flush_if_dirty();

      if (ok && audioData && audioSampleCount > 0u) {
        s_tDelayUntil = millis() + 1000u;   // UX: small pause before waveform
        s_state = DS_DELAY_TO_WAVEFORM;
      } else {
        s_state = DS_BROWSER;               // back to list on failure
      }

      s_pendingIdx = -1;
    } break;

    case DS_DELAY_TO_WAVEFORM: {
      if (millis() >= s_tDelayUntil) {
        // Clear text view before next view
        view_clear_log();
        view_flush_if_dirty();

    #ifdef ADC_DEBUG
        // Show ADC debug instead of waveform
        adc_debug_init();
        adc_debug_draw();
        s_state = DS_ADC_DEBUG;
    #else
        // Normal waveform behavior
        if (audioData && audioSampleCount > 0u) {
          waveform_init((const int16_t*)audioData, audioSampleCount, currentWav.sampleRate);
          waveform_draw();
          s_state = DS_WAVEFORM;
        } else {
          s_state = DS_BROWSER;
          browser_render_sample_list();
        }
    #endif
      }
    } break;

    case DS_WAVEFORM:
      overlayLoopShadingTick();
      audio_engine_arm(true);
      audio_engine_play(true);
      break;

    case DS_BROWSER:
    default:
      // Browser view is static until user interaction
      break;

#ifdef ADC_DEBUG
    case DS_ADC_DEBUG:
      // Refresh ADC values periodically
      adc_debug_draw();
      break;
#endif
  }
}

void display_on_turn(int8_t inc) {
  switch (s_state) {
    case DS_WAVEFORM: {
      if (!waveform_on_turn(inc)) {
        // waveform requested exit; browser is already re-rendered in waveform_exit()
      }
    } break;

    case DS_BROWSER: {
      if (s_idx.count == 0) return;

      int next = s_sel + (int)inc;
      if (next < 0) next = 0;
      if (next >= s_idx.count) next = s_idx.count - 1;

      if (next != s_sel) {
        s_sel = next;

        // Keep selection within the page window
        const int visible = 7;
        if (s_sel < s_top) s_top = s_sel;
        if (s_sel >= s_top + visible) s_top = s_sel - (visible - 1);

        browser_render_sample_list();
      }
    } break;

    // Ignore input during load/delay/setup
    case DS_SETUP:
    case DS_LOADING:
    case DS_DELAY_TO_WAVEFORM:
    default:
      break;
  }
}

void display_on_button(void) {
  switch (s_state) {
    case DS_WAVEFORM: {
      if (!waveform_on_button()) {
        // exit handled inside
      }
    } break;

    case DS_BROWSER: {
      if (s_idx.count == 0) return;

      // Capture selection and transition to LOADING
      s_pendingIdx = s_sel;
      s_state = DS_LOADING;
      
      // Force an immediate update to show loading status
      s_pendingUpdate = true;
    } break;

    // Ignore extra presses during load/delay/setup
    case DS_SETUP:
    case DS_LOADING:
    case DS_DELAY_TO_WAVEFORM:
    default:
      break;
  }
}

// ────────────────────────── Timer Management ─────────────────────────────
bool display_timer_begin(uint32_t fps) {
  if (s_timerActive) {
    return false;  // already running
  }
  
  if (fps == 0 || fps > 1000) {
    return false;  // sanity check
  }
  
  // Calculate period in microseconds
  uint32_t period_us = 1000000u / fps;
  
  // Start repeating timer
  if (!add_repeating_timer_us((int32_t)period_us, display_timer_isr, nullptr, &s_displayTimer)) {
    return false;
  }
  
  s_timerActive = true;
  return true;
}

void display_timer_end(void) {
  if (s_timerActive) {
    cancel_repeating_timer(&s_displayTimer);
    s_timerActive = false;
    s_pendingUpdate = false;
  }
}

void display_debug_list_files(void) {
  FileIndex idx;
  view_clear_log();
  view_print_line("=== WAV Files ===");
  if (!file_index_scan(idx)) {
    view_print_line("SD scan failed");
    view_flush_if_dirty();
    return;
  }
  if (idx.count == 0) {
    view_print_line("No WAV files found");
    view_flush_if_dirty();
    return;
  }
  const int max_print = (idx.count < 20) ? idx.count : 20;
  for (int i = 0; i < max_print; ++i) {
    char sizeBuf[16], line[96];
    sd_format_size(idx.sizes[i], sizeBuf, sizeof(sizeBuf));
    snprintf(line, sizeof(line), "%2d: %s  (%s)", i + 1, idx.names[i], sizeBuf);
    view_print_line(line);
  }
  view_flush_if_dirty();
}

void display_debug_dump_q15(uint32_t n) {
  if (!audioData || audioSampleCount == 0) {
    view_print_line("No audio loaded");
    view_flush_if_dirty();
    return;
  }
  const int16_t* samples = (const int16_t*)audioData;
  const uint32_t count   = (n > audioSampleCount) ? audioSampleCount : n;
  char line[48];
  view_print_line("=== Q15 Values ===");
  for (uint32_t i = 0; i < count && i < 16u; ++i) {   // hard cap for safety
    float normalized = samples[i] / 32768.0f;
    snprintf(line, sizeof(line), "[%u]: %d (%.4f)", (unsigned)i, samples[i], normalized);
    view_print_line(line);
  }
  view_flush_if_dirty();
}

#ifdef ADC_DEBUG
// ─────────────────────────── ADC Debug view ──────────────────────────────
void adc_debug_init(void) {
    // Nothing special needed for initialization
}

void adc_debug_draw(void) {
    view_clear_log();
    view_print_line("=== ADC Debug ===");
    
    // Display raw and normalized values for each ADC channel
    for (int i = 0; i < NUM_ADC_INPUTS; i++) {
        char line[64];
        uint16_t raw_value = adc_results_buf[i];
        snprintf(line, sizeof(line), "CH%d: %4d", i, raw_value);
        view_print_line(line);
    }
    
    view_print_line("Press button to exit");
    view_flush_if_dirty();
}

bool adc_debug_on_turn(int8_t inc) {
    // Ignore encoder input in ADC debug mode, or use it to exit
    adc_debug_exit();
    return false;
}

bool adc_debug_on_button(void) {
    adc_debug_exit();
    return false;
}

bool adc_debug_is_active(void) {
    return s_state == DS_ADC_DEBUG;
}

void adc_debug_exit(void) {
    s_state = DS_BROWSER;
    browser_render_sample_list();  // switch back to browser view
}
#endif

} // namespace sf
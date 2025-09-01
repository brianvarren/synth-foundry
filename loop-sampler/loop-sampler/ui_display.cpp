#include <Arduino.h>
#include "driver_sh1122.h"
#include "driver_sdcard.h"
#include "ui_display.h"
#include "storage_loader.h"
#include "storage_wav_meta.h"  // for WavInfo

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

// Global variables from loop-sampler.ino (outside sf namespace)
extern uint8_t* audioData;          // PSRAM buffer (from loop-sampler.ino)
extern uint32_t audioSampleCount;   // Number of Q15 samples
extern sf::WavInfo currentWav;      // For sample rate

namespace sf {

static FileIndex s_idx;                // names[], sizes[], count
static int       s_sel  = 0;           // selected row
static int       s_top  = 0;           // top row of the current page
static UiLoadFn  s_load = 0;           // app-provided loader

// Defer loads out of callbacks (in case the driver fires them in ISR/context)
static volatile bool s_pendingLoad = false;
static int           s_pendingIdx  = -1;

// Track post-load waveform display timing
static bool s_pendingWaveform = false;
static uint32_t s_waveformShowTime = 0;

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

static void browser_render_sample_list()
{
  view_set_auto_scroll(false); // Return view control to the user

  view_clear_log();
  view_print_line("=== WAV Files ===");

  if (s_idx.count == 0) {
    view_print_line("No WAV files found");
    view_flush_if_dirty();
    return;
  }

  const int visible = LINES_PER_SCREEN;     // from display.h (e.g., 7)
  const int end     = (s_top + visible <= s_idx.count) ? (s_top + visible) : s_idx.count;

  for (int i = s_top; i < end; ++i) {
    char sizeStr[16];
    char line[64];

    sd_format_size(s_idx.sizes[i], sizeStr, sizeof(sizeStr));
    // Prefix ">" for selected; keep it short & deterministic
    const char marker = (i == s_sel) ? '>' : ' ';

    // Ensure truncation-safe formatting
    snprintf(line, sizeof(line), "%c %s (%s)", marker, s_idx.names[i], sizeStr);
    view_print_line(line);
  }

  // Optional footer
  {
    char footer[32];
    snprintf(footer, sizeof(footer), "%d/%d", (s_sel + 1), s_idx.count);
    view_print_line(footer);
  }

  view_flush_if_dirty();
}

void browser_init(UiLoadFn onLoad) {
  s_load = onLoad;
  s_sel  = 0;
  s_top  = 0;

  // Build the index
  if (!file_index_scan(s_idx)) {
    view_clear_log();
    view_print_line("SD scan failed");
    view_flush_if_dirty();
    return;
  }

  browser_render_sample_list();
}

void browser_on_turn(int8_t inc)
{
  // Check if waveform view is active
  if (waveform_is_active()) {
    if (!waveform_on_turn(inc)) {
      // Waveform view exited, redraw browser
      // Important: browser uses U8G2 text mode, not grayscale
      browser_render_sample_list();
    }
    return;
  }

  if (s_idx.count == 0) return;

  // Clamp selection
  int next = s_sel + (int)inc;
  if (next < 0) next = 0;
  if (next >= s_idx.count) next = s_idx.count - 1;

  if (next != s_sel) {
    s_sel = next;

    // Keep selection in view
    const int visible = LINES_PER_SCREEN - 1;
    if (s_sel < s_top) s_top = s_sel;
    if (s_sel >= s_top + visible) s_top = s_sel - (visible - 1);

    browser_render_sample_list();
  }
}

void browser_on_button(void)
{
  // Check if waveform view is active
  if (waveform_is_active()) {
    if (!waveform_on_button()) {
      // Waveform view exited, redraw browser
      // Important: browser uses U8G2 text mode, not grayscale
      browser_render_sample_list();
    }
    return;
  }

  if (s_idx.count == 0 || s_load == 0) return;
  s_pendingIdx  = s_sel;
  s_pendingLoad = true;
}

void browser_tick(void)
{
  // Handle deferred waveform display
  if (s_pendingWaveform) {
    if (millis() >= s_waveformShowTime) {
      s_pendingWaveform = false;
      
      // Now show the waveform (uses grayscale mode)
      if (::audioData && ::audioSampleCount > 0) {
        // Clear display before drawing waveform
        view_clear_log();
        view_flush_if_dirty();
        
        // Initialize and draw waveform with 4-bit grayscale
        waveform_init((const int16_t*)::audioData, ::audioSampleCount, ::currentWav.sampleRate);
        waveform_draw();
      }
    }
    return;
  }
  
  if (!s_pendingLoad) return;

  view_set_auto_scroll(true); // Auto-scroll during file load messages
  s_pendingLoad = false;

  const int idx = s_pendingIdx;
  s_pendingIdx  = -1;
  if (idx < 0 || idx >= s_idx.count) return;

  const char* path = s_idx.names[idx];

  view_clear_log();
  {
    char line[64];
    snprintf(line, sizeof(line), "Loading: %s", path);
    view_print_line(line);
  }
  view_flush_if_dirty();

  const bool ok = s_load(path);  // This does all the loading work and prints messages

  // Add final status message
  view_print_line(ok ? "✓ Loaded" : "✗ Load failed");
  view_flush_if_dirty();  // Make sure the final message is displayed
  
  if (ok && ::audioData && ::audioSampleCount > 0) {
    // Schedule waveform display for 1 second from now
    s_pendingWaveform = true;
    s_waveformShowTime = millis() + 1000;  // Show waveform 1 second after "Loaded" message
  } else if (!ok) {
    // Failed load - return to browser after delay
    delay(1000);
    browser_render_sample_list();
  }
}

} // namespace sf;
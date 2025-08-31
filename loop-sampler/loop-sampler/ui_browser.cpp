#include <Arduino.h>
#include "ui_browser.h"
#include "ui_waveform.h"       // NEW: waveform display
#include "display.h"           // for LINES_PER_SCREEN
#include "display_views.h"     // view_* APIs
#include "storage_file_index.h"
#include "storage_sd_hal.h"    // sd_format_size()
#include "storage_wav_meta.h"  // for WavInfo

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
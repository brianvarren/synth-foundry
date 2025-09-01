#include "driver_sh1122.h"     // gray4_* API
#include "driver_sdcard.h"     // FileIndex + file_index_scan(), sd_format_size()
#include "ui_display.h"
#include "storage_loader.h"    // extern audioData/audioSampleCount, etc. (your existing)
#include "storage_wav_meta.h"  // extern currentWav (for sampleRate)
#include "sf_globals_bridge.h"

namespace sf {

// ─────────────────────── Display constants (no heap) ─────────────────────
static const uint8_t SHADE_BACKGROUND  = 0;   // 0..15 (SH1122 grayscale)
static const uint8_t SHADE_WAVEFORM    = 12;
static const uint8_t SHADE_DIM         = 4;   // reserved for future loop zone
static const uint8_t SHADE_CENTERLINE  = 6;

// ─────────────────────────── Browser state (UI) ──────────────────────────
static FileIndex  s_idx;                // names[], sizes[], count
static int        s_sel  = 0;           // selected row
static int        s_top  = 0;           // top row of the current page
static UiLoadFn   s_load = 0;           // app-provided loader
static int        s_pendingIdx = -1;    // index captured on “load” press

// ─────────────────────────── Waveform state (UI) ─────────────────────────
static const int16_t* s_samples     = 0;    // Q15 pointer in PSRAM
static uint32_t       s_sampleCount = 0;
static uint32_t       s_sampleRate  = 0;

// ─────────────────────────────── FSM state ───────────────────────────────
static DisplayState s_state = DS_BROWSER;
static uint32_t         s_tDelayUntil = 0;   // millis() deadline for DS_DELAY_TO_WAVEFORM

// ────────────────────────── Forward declarations ─────────────────────────
static void browser_render_sample_list(void);

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
    draw_center_line_if_empty();
    gray4_send_buffer();
    return;
  }

  // Peak estimate for vertical scale (simple clamp avoids divide-by-zero)
  int16_t peak = 1;
  {
    // Very light scan for peak (every Nth sample) — no dynamic memory
    const uint32_t step = (s_sampleCount > 4096u) ? (s_sampleCount / 4096u) : 1u;
    int16_t maxabs = 1;
    for (uint32_t i = 0; i < s_sampleCount; i += step) {
      int16_t v = s_samples[i];
      int16_t a = (v < 0) ? (int16_t)-v : v;
      if (a > maxabs) maxabs = a;
    }
    peak = (maxabs < 128) ? 128 : maxabs;  // keep scale sane
  }

  // Horizontal downsampling: samples-per-pixel
  const double spp = (double)s_sampleCount / (double)W;

  // Column-wise min/max envelope
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

    // Map Q15 → screen (top=0, bottom=H-1)
    int yMin = mid - ((int32_t)cmax * (H / 2)) / peak;
    int yMax = mid - ((int32_t)cmin * (H / 2)) / peak;

    if (yMin < 0)      yMin = 0;
    if (yMin > H - 1)  yMin = H - 1;
    if (yMax < 0)      yMax = 0;
    if (yMax > H - 1)  yMax = H - 1;

    if (yMin == yMax) {
      gray4_set_pixel(x, yMin, SHADE_WAVEFORM);
    } else {
      gray4_draw_vline(x, yMin, yMax, SHADE_WAVEFORM);
    }
  }

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
static void browser_render_sample_list()
{
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
    // "›" and "•" are fine too; stick to ASCII for safety
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

// ───────────────────────────── Browser / API ─────────────────────────────
void browser_init(UiLoadFn onLoad) {
  s_load = onLoad;
  s_sel  = 0;
  s_top  = 0;
  s_pendingIdx = -1;

  // Build the index (blocking scan, same as before)
  if (!file_index_scan(s_idx)) {
    render_status_line("SD scan failed");
    s_state = DS_BROWSER; // still in browser; list is just empty
    return;
  }

  s_state = DS_BROWSER;
  browser_render_sample_list();
}

void browser_on_turn(int8_t inc)
{
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

    // Ignore input during load/delay
    case DS_LOADING:
    case DS_DELAY_TO_WAVEFORM:
    default:
      break;
  }
}

void browser_on_button(void)
{
  switch (s_state) {
    case DS_WAVEFORM: {
      if (!waveform_on_button()) {
        // exit handled inside
      }
    } break;

    case DS_BROWSER: {
      if (s_idx.count == 0 || s_load == 0) return;

      // Capture selection and transition to LOADING; the actual work happens in tick()
      s_pendingIdx = s_sel;
      s_state = DS_LOADING;

      // Status will be printed in tick(); keep callbacks snappy
    } break;

    // Ignore extra presses during load/delay
    case DS_LOADING:
    case DS_DELAY_TO_WAVEFORM:
    default:
      break;
  }
}

void browser_tick(void)
{
  switch (s_state) {
    case DS_LOADING: {
      // Defensive checks
      if (s_pendingIdx < 0 || s_pendingIdx >= s_idx.count || s_load == 0) {
        s_pendingIdx = -1;
        s_state = DS_BROWSER;
        browser_render_sample_list();
        return;
      }

      // Show “Loading …” line (auto-scroll for multi-line loader messages)
      view_set_auto_scroll(true);
      view_clear_log();
      {
        char line[64];
        snprintf(line, sizeof(line), "Loading: %s", s_idx.names[s_pendingIdx]);
        view_print_line(line);
      }
      view_flush_if_dirty();

      // Do the actual load (app’s callback prints its own details)
      const char* path = s_idx.names[s_pendingIdx];
      const bool ok = s_load(path);

      // Final status line
      view_print_line(ok ? "✓ Loaded" : "✗ Load failed");
      view_flush_if_dirty();

      if (ok && audioData && audioSampleCount > 0u) {
        // Defer waveform display for ~1s (UX match)
        s_tDelayUntil = millis() + 1000u;
        s_state = DS_DELAY_TO_WAVEFORM;
      } else {
        // Failure: brief pause to let user read, then back to browser
        delay(1000);
        s_state = DS_BROWSER;
        browser_render_sample_list();
      }

      // Consume the pending index
      s_pendingIdx = -1;

    } break;

    case DS_DELAY_TO_WAVEFORM: {
      if (millis() >= s_tDelayUntil) {
        // Clear text view before grayscale draw
        view_clear_log();
        view_flush_if_dirty();

        // Initialize and draw waveform
        if (audioData && audioSampleCount > 0u) {
          waveform_init((const int16_t*)audioData, audioSampleCount, currentWav.sampleRate);
          waveform_draw();
          s_state = DS_WAVEFORM;
        } else {
          // Safety net: nothing to draw; go back
          s_state = DS_BROWSER;
          browser_render_sample_list();
        }
      }
    } break;

    case DS_WAVEFORM:
      // No periodic work right now
      break;

    case DS_BROWSER:
    default:
      // Nothing to do unless user turns/presses
      break;
  }
}

} // namespace sf

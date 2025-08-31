#include <Arduino.h>
#include <U8g2lib.h>
#include <string.h>       // for strncpy
#include "display_driver.h"
#include "display_views.h"

namespace sf {

// --- Fixed storage (policy-compliant) ---
static char  s_lines[MAX_DISPLAY_LINES][MAX_LINE_CHARS + 1];
static int   s_line_count      = 0;
static int   s_scroll_offset   = 0;
static bool  s_dirty           = true;
static bool  s_auto_scroll     = true;
static uint32_t s_last_scroll  = 0;

void view_clear_log() {
  for (int i = 0; i < MAX_DISPLAY_LINES; ++i) s_lines[i][0] = '\0';
  s_line_count    = 0;
  s_scroll_offset = 0;
  s_dirty         = true;
}

void view_print_line(const char* s) {
  if (!s) return;
  if (s_line_count < MAX_DISPLAY_LINES) {
    // append
    int idx = s_line_count++;
    strncpy(s_lines[idx], s, MAX_LINE_CHARS);
    s_lines[idx][MAX_LINE_CHARS] = '\0';
  } else {
    // shift up, drop oldest (simple ring without modulo)
    for (int i = 1; i < MAX_DISPLAY_LINES; ++i) {
      memcpy(s_lines[i - 1], s_lines[i], MAX_LINE_CHARS + 1);
    }
    strncpy(s_lines[MAX_DISPLAY_LINES - 1], s, MAX_LINE_CHARS);
    s_lines[MAX_DISPLAY_LINES - 1][MAX_LINE_CHARS] = '\0';
  }
  s_dirty = true;
}

void view_redraw_log(U8G2& g) {
  g.clearBuffer();
  g.setFont(u8g2_font_5x7_tf);

  const int start = s_scroll_offset;
  const int end   = (start + LINES_PER_SCREEN <= s_line_count)
                      ? (start + LINES_PER_SCREEN)
                      : s_line_count;

  int y = 8; // baseline for first row (font dependent)
  for (int i = start; i < end; ++i, y += LINE_HEIGHT) {
    g.drawStr(0, y, s_lines[i]);
  }

  g.sendBuffer();
  s_dirty = false;
}

void view_set_auto_scroll(bool enabled){
  s_auto_scroll = enabled;
}

void view_handle_scroll(uint32_t now_ms) {
  if (s_auto_scroll) {
    if (now_ms - s_last_scroll < SCROLL_DELAY_MS) return;
    s_last_scroll = now_ms;

    // Auto-scroll only if content exceeds a page
    if (s_line_count > LINES_PER_SCREEN) {
      if (s_scroll_offset + LINES_PER_SCREEN < s_line_count) {
        ++s_scroll_offset;
        s_dirty = true;
      }
    }
  }
}

bool view_needs_redraw() { return s_dirty; }

void view_flush_if_dirty() {
  if (!s_dirty) return;
  auto& g = display_gfx();
  view_redraw_log(g);
}

void view_show_status(const char* title, const char* line2) {
  auto& g = display_gfx();
  g.clearBuffer();
  g.setFont(u8g2_font_6x12_tf);
  if (title) g.drawStr(0, 14, title);
  if (line2) g.drawStr(0, 30, line2);
  g.sendBuffer();
  s_dirty = false; // status renders immediately
}

} // namespace sf

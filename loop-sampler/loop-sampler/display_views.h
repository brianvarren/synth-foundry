#pragma once
#include <stdint.h>

class U8G2;

namespace sf {

// --- Config for text/scroll view (constexprs: no RAM) ---
constexpr int DISPLAY_WIDTH        = 256;
constexpr int DISPLAY_HEIGHT       = 64;
constexpr int MAX_DISPLAY_LINES    = 20;
constexpr int MAX_LINE_CHARS       = 42;   // adjust for chosen font/width
constexpr int LINES_PER_SCREEN     = 7;
constexpr int LINE_HEIGHT          = 8;
constexpr uint32_t SCROLL_DELAY_MS = 500;

// Clear the log/view model
void view_clear_log();

// Append one line (truncates to MAX_LINE_CHARS)
void view_print_line(const char* s);

// Manual redraw of the current text model
void view_redraw_log(U8G2& g);

// Enable/disable automatic line scrolling for status/log views
void view_set_auto_scroll(bool enabled);

// Simple auto-scroll tick; pass millis()
void view_handle_scroll(uint32_t now_ms);

// Dirty flag if something changed
bool view_needs_redraw();

// If dirty, redraw + flush
void view_flush_if_dirty();

// Quick status card (title + line2)
void view_show_status(const char* title, const char* line2);

// Clear full screen for graphics (not the scrolling log)
void view_clear_screen();

// Draw a mono 16-bit waveform scaled to fill the screen.
// - data: interleaved if channels==2 (we average L/R on the fly)
// - frames: number of sample frames (per channel group)
// - channels: 1 or 2
void view_draw_waveform_16(const int16_t* data, uint32_t frames, uint8_t channels);


} // namespace sf

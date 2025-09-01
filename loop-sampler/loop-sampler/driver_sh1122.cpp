#include <Arduino.h>
#include <SPI.h>
#include <U8g2lib.h>
#include <string.h>
#include "config_pins.h"
#include "driver_sh1122.h"

namespace sf {

// Single, private instance – no dynamic allocation
static U8G2_SH1122_256X64_F_4W_HW_SPI u8g2(U8G2_R0, DISP_CS, DISP_DC, DISP_RST);

// 4-bit grayscale buffer (256x64 pixels, 2 pixels per byte)
// Total: 128 bytes/row * 64 rows = 8192 bytes
static uint8_t gray4_buffer[8192];

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
  auto& g = sh1122_gfx();
  view_redraw_log(g);
}

void view_show_status(const char* title, const char* line2) {
  auto& g = sh1122_gfx();
  g.clearBuffer();
  g.setFont(u8g2_font_6x12_tf);
  if (title) g.drawStr(0, 14, title);
  if (line2) g.drawStr(0, 30, line2);
  g.sendBuffer();
  s_dirty = false; // status renders immediately
}

void sh1122_init() {
  // Hardware reset (safe on SH1122 boards)
  pinMode(DISP_RST, OUTPUT);
  digitalWrite(DISP_RST, HIGH); delay(5);
  digitalWrite(DISP_RST, LOW);  delay(20);
  digitalWrite(DISP_RST, HIGH); delay(50);

  // SPI mapping (if your core doesn't auto-map)
  SPI.setSCK(DISP_SCL);
  SPI.setTX(DISP_SDA);
  SPI.begin();

  u8g2.begin();
  u8g2.setBusClock(8000000UL);
  u8g2.setContrast(127);
  u8g2.setFont(u8g2_font_5x7_tf);
  u8g2.clearBuffer();
  u8g2.sendBuffer();
  
  // Clear grayscale buffer
  gray4_clear(0);
}

// SH1122 is 256x64, 4-bit (two pixels per byte)
static inline void sh1122_set_col0(U8G2& u8) {
  // Column address = 0: lower 4 bits then higher 3 bits
  u8.sendF("c", 0x00);   // Set Column Address low nibble (0x00..0x0F)
  u8.sendF("c", 0x10);   // Set Column Address high (0x10..0x17) -> 0
}

static inline void sh1122_set_row(U8G2& u8, uint8_t row) {
  // Row Address Set is a double-byte command: 0xB0, <row>
  u8.sendF("ca", 0xB0, row);
}

void sh1122_send_gray4(const uint8_t* buf_256x64_gray4) {
  // Push one row (128 bytes) at a time
  U8G2& u8 = sh1122_gfx();

  for (uint8_t y = 0; y < 64; ++y) {
    sh1122_set_row(u8, y);
    sh1122_set_col0(u8);

    const uint8_t* row = buf_256x64_gray4 + (uint16_t)y * 128u;
    // A0=1 (data) is implied by 'd' below; write 128 bytes (256 pixels)
    for (uint16_t i = 0; i < 128; ++i) {
      u8.sendF("d", row[i]);   // each byte: high nibble = x&~1, low nibble = x|1
    }
  }
}

// === 4-bit Grayscale Drawing Functions ===

void gray4_clear(uint8_t shade) {
  // Shade should be 0-15
  if (shade > 15) shade = 15;
  uint8_t byte_val = (shade << 4) | shade;  // Both pixels same shade
  memset(gray4_buffer, byte_val, sizeof(gray4_buffer));
}

void gray4_set_pixel(int16_t x, int16_t y, uint8_t shade) {
  // Bounds check
  if (x < 0 || x >= 256 || y < 0 || y >= 64) return;
  if (shade > 15) shade = 15;
  
  // Calculate byte position
  // Each row is 128 bytes, with 2 pixels per byte
  uint16_t byte_idx = y * 128 + (x / 2);
  
  // SH1122 packing: high nibble = even x, low nibble = odd x
  if (x & 1) {
    // Odd x - low nibble
    gray4_buffer[byte_idx] = (gray4_buffer[byte_idx] & 0xF0) | shade;
  } else {
    // Even x - high nibble
    gray4_buffer[byte_idx] = (gray4_buffer[byte_idx] & 0x0F) | (shade << 4);
  }
}

uint8_t gray4_get_pixel(int16_t x, int16_t y) {
  if (x < 0 || x >= 256 || y < 0 || y >= 64) return 0;
  
  uint16_t byte_idx = y * 128 + (x / 2);
  
  if (x & 1) {
    // Odd x - low nibble
    return gray4_buffer[byte_idx] & 0x0F;
  } else {
    // Even x - high nibble
    return gray4_buffer[byte_idx] >> 4;
  }
}

void gray4_draw_hline(int16_t x0, int16_t x1, int16_t y, uint8_t shade) {
  if (y < 0 || y >= 64) return;
  if (shade > 15) shade = 15;
  
  // Ensure x0 <= x1
  if (x0 > x1) {
    int16_t tmp = x0;
    x0 = x1;
    x1 = tmp;
  }
  
  // Clamp to screen bounds
  if (x0 < 0) x0 = 0;
  if (x1 >= 256) x1 = 255;
  
  // Draw pixel by pixel (can be optimized for byte-aligned runs)
  for (int16_t x = x0; x <= x1; x++) {
    gray4_set_pixel(x, y, shade);
  }
}

void gray4_draw_vline(int16_t x, int16_t y0, int16_t y1, uint8_t shade) {
  if (x < 0 || x >= 256) return;
  if (shade > 15) shade = 15;
  
  // Ensure y0 <= y1
  if (y0 > y1) {
    int16_t tmp = y0;
    y0 = y1;
    y1 = tmp;
  }
  
  // Clamp to screen bounds
  if (y0 < 0) y0 = 0;
  if (y1 >= 64) y1 = 63;
  
  // Optimized for vertical lines - we can set pixels more efficiently
  for (int16_t y = y0; y <= y1; y++) {
    gray4_set_pixel(x, y, shade);
  }
}

void gray4_draw_line(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint8_t shade) {
  // Special cases for horizontal and vertical lines
  if (y0 == y1) {
    gray4_draw_hline(x0, x1, y0, shade);
    return;
  }
  if (x0 == x1) {
    gray4_draw_vline(x0, y0, y1, shade);
    return;
  }
  
  // Bresenham's line algorithm
  int16_t dx = abs(x1 - x0);
  int16_t dy = abs(y1 - y0);
  int16_t sx = (x0 < x1) ? 1 : -1;
  int16_t sy = (y0 < y1) ? 1 : -1;
  int16_t err = dx - dy;
  
  while (true) {
    gray4_set_pixel(x0, y0, shade);
    
    if (x0 == x1 && y0 == y1) break;
    
    int16_t e2 = 2 * err;
    if (e2 > -dy) {
      err -= dy;
      x0 += sx;
    }
    if (e2 < dx) {
      err += dx;
      y0 += sy;
    }
  }
}

void gray4_draw_rect(int16_t x, int16_t y, int16_t w, int16_t h, uint8_t shade) {
  gray4_draw_hline(x, x + w - 1, y, shade);
  gray4_draw_hline(x, x + w - 1, y + h - 1, shade);
  gray4_draw_vline(x, y, y + h - 1, shade);
  gray4_draw_vline(x + w - 1, y, y + h - 1, shade);
}

void gray4_fill_rect(int16_t x, int16_t y, int16_t w, int16_t h, uint8_t shade) {
  // Clamp bounds
  if (x < 0) { w += x; x = 0; }
  if (y < 0) { h += y; y = 0; }
  if (x + w > 256) w = 256 - x;
  if (y + h > 64) h = 64 - y;
  if (w <= 0 || h <= 0) return;
  
  for (int16_t dy = 0; dy < h; dy++) {
    gray4_draw_hline(x, x + w - 1, y + dy, shade);
  }
}

void gray4_send_buffer() {
  sh1122_send_gray4(gray4_buffer);
}

uint8_t* gray4_get_buffer() {
  return gray4_buffer;
}

void sh1122_set_contrast(uint8_t v) { u8g2.setContrast(v); }
U8G2& sh1122_gfx()                  { return u8g2; }
void  sh1122_clear_buffer()         { u8g2.clearBuffer(); }
void  sh1122_send_buffer()          { u8g2.sendBuffer(); }

} // namespace sf
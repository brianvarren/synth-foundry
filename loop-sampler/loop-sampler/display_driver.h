#pragma once
#include <stdint.h>

// Forward declaration; keep U8g2 headers out of most TUs
class U8G2;

namespace sf {

// One-time hardware init (pins, reset, SPI, u8g2.begin, defaults)
void  display_init();

// Optional tweak
void  display_set_contrast(uint8_t v);

// Access to the single global U8g2 object (owned in .cpp)
U8G2& display_gfx();

// Convenience passthroughs (optional)
void  display_clear_buffer();
void  display_send_buffer();

// Send raw 4-bit grayscale buffer (128 bytes per row, 64 rows = 8192 bytes)
void  display_send_gray4(const uint8_t* buf_256x64_gray4);

// === 4-bit Grayscale Drawing API ===
// All shade values are 0-15 (0=black, 15=white)

// Clear entire grayscale buffer to a shade
void gray4_clear(uint8_t shade);

// Set/get individual pixel
void gray4_set_pixel(int16_t x, int16_t y, uint8_t shade);
uint8_t gray4_get_pixel(int16_t x, int16_t y);

// Drawing primitives
void gray4_draw_hline(int16_t x0, int16_t x1, int16_t y, uint8_t shade);
void gray4_draw_vline(int16_t x, int16_t y0, int16_t y1, uint8_t shade);
void gray4_draw_line(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint8_t shade);
void gray4_draw_rect(int16_t x, int16_t y, int16_t w, int16_t h, uint8_t shade);
void gray4_fill_rect(int16_t x, int16_t y, int16_t w, int16_t h, uint8_t shade);

// Send grayscale buffer to display
void gray4_send_buffer();

// Direct access to grayscale buffer (256x64 pixels, 2 pixels per byte = 8192 bytes)
uint8_t* gray4_get_buffer();

} // namespace sf
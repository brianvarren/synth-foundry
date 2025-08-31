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

} // namespace sf

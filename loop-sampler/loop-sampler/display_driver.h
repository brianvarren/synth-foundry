// #pragma once

// #include <Arduino.h>
// #include <U8g2lib.h>
// #include <SPI.h>
// #include "config/pins.h"

// #define DISPLAY_WIDTH 256
// #define DISPLAY_HEIGHT 64
// #define MAX_DISPLAY_LINES 20
// #define LINES_PER_SCREEN 7
// #define LINE_HEIGHT 8
// #define FONT_START_Y 10
// #define SCROLL_DELAY_MS 500

// class DisplayClass {
// private:
//   U8G2_SH1122_256X64_F_4W_HW_SPI* u8g2;
  
//   // Text buffer management
//   String displayLines[MAX_DISPLAY_LINES];
//   int lineCount;
//   int scrollOffset;
//   bool _needsRedraw;

//   // Scrolling
//   unsigned long lastScrollTime;
//   bool autoScrollEnabled;
  
// public:
//   DisplayClass() : 
//     u8g2(nullptr),
//     lineCount(0),
//     scrollOffset(0),
//     _needsRedraw(true),
//     lastScrollTime(0),
//     autoScrollEnabled(true) {}
  
//   ~DisplayClass() {
//     if (u8g2) delete u8g2;
//   }
  
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

//   // ───────────────── Initialization ─────────────────
//   bool begin() {
//     Serial.println("Initializing SH1122 display...");
    
//     // Hardware reset sequence
//     pinMode(DISP_RST, OUTPUT);
//     digitalWrite(DISP_RST, HIGH);
//     delay(5);
//     digitalWrite(DISP_RST, LOW);
//     delay(20);
//     digitalWrite(DISP_RST, HIGH);
//     delay(50);
    
//     // Configure SPI0 for display
//     SPI.setSCK(DISP_SCL);
//     SPI.setTX(DISP_SDA);
//     SPI.begin();
    
//     // Initialize U8G2
//     u8g2 = new U8G2_SH1122_256X64_F_4W_HW_SPI(U8G2_R0, DISP_CS, DISP_DC, DISP_RST);
    
//     if (!u8g2) {
//       Serial.println("Failed to allocate display object");
//       return false;
//     }
    
//     u8g2->begin();
//     u8g2->setBusClock(8000000UL); // 8 MHz for stability
//     u8g2->setContrast(180);
//     u8g2->setFont(u8g2_font_5x7_tf); // Small font for more text
    
//     clear();
//     Serial.println("Display initialized");
//     return true;
//   }
  
//   // ───────────────── Display Clear ─────────────────
//     void clear() {
//     lineCount = 0;
//     scrollOffset = 0;
//     _needsRedraw = true;
//     for (int i = 0; i < MAX_DISPLAY_LINES; i++) {
//       displayLines[i] = "";
//     }
//   }
  

#include <Arduino.h>
#include <SPI.h>
#include <U8g2lib.h>
#include "config_pins.h"
#include "display_driver.h"

namespace sf {

// Single, private instance — no dynamic allocation
static U8G2_SH1122_256X64_F_4W_HW_SPI u8g2(U8G2_R0, DISP_CS, DISP_DC, DISP_RST);

void display_init() {
  // Hardware reset (safe on SH1122 boards)
  pinMode(DISP_RST, OUTPUT);
  digitalWrite(DISP_RST, HIGH); delay(5);
  digitalWrite(DISP_RST, LOW);  delay(20);
  digitalWrite(DISP_RST, HIGH); delay(50);

  // SPI mapping (if your core doesn’t auto-map)
  SPI.setSCK(DISP_SCL);
  SPI.setTX(DISP_SDA);
  SPI.begin();

  u8g2.begin();
  u8g2.setBusClock(8000000UL);
  u8g2.setContrast(180);
  u8g2.setFont(u8g2_font_5x7_tf);
  u8g2.clearBuffer();
  u8g2.sendBuffer();
}

void display_set_contrast(uint8_t v) { u8g2.setContrast(v); }
U8G2& display_gfx()                  { return u8g2; }
void  display_clear_buffer()         { u8g2.clearBuffer(); }
void  display_send_buffer()          { u8g2.sendBuffer(); }

} // namespace sf

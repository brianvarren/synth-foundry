#include <Arduino.h>
#include <SPI.h>
#include <U8g2lib.h>

#define DISP_DC   2
#define DISP_RST  3
#define DISP_CS   5
#define DISP_SCL  6
#define DISP_SDA  7

U8G2_SH1122_256X64_F_4W_HW_SPI u8g2(U8G2_R0, DISP_CS, DISP_DC, DISP_RST);

void setup() {
  delay(100);
  pinMode(DISP_RST, OUTPUT);
  digitalWrite(DISP_RST, HIGH);
  delay(5);
  digitalWrite(DISP_RST, LOW);
  delay(20);
  digitalWrite(DISP_RST, HIGH);
  delay(50);

  SPI.setSCK(DISP_SCL);
  SPI.setTX(DISP_SDA);
  SPI.begin();
  u8g2.begin();
  u8g2.setBusClock(4000000UL); // start at 4 MHz; raise later
  u8g2.setContrast(180);
  u8g2.setFont(u8g2_font_6x12_tf);
}

void loop() {
  static uint8_t x = 0;
  u8g2.clearBuffer();
  u8g2.drawFrame(0,0,256,64);
  u8g2.drawStr(6,14,"U8G2 SH1122 - RP2350");
  u8g2.drawBox(x, 48, 24, 8);
  x = (x + 4) % 256;
  u8g2.sendBuffer();
  delay(40);
}

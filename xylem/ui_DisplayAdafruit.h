/**
 * @file ui_DisplayAdafruit.h
 * @brief Adafruit SSD1306 OLED display driver for XYLEM synthesizer
 * 
 * @author Brian Varren
 * @version 1.0
 * @date 2024
 */

#pragma once

#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <stdint.h>

// ── Display Configuration ────────────────────────────────────────────────────────
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
#define SCREEN_ADDRESS 0x3C
#define PIN_WIRE_SDA 14
#define PIN_WIRE_SCL 15

// ── Simple Display Driver Class ──────────────────────────────────────────────────
class DisplayAdafruit {
public:
  DisplayAdafruit();
  ~DisplayAdafruit();
  
  // Lifecycle
  bool begin();
  void end();
  
  // Basic display functions
  void clear();
  void showText(const char* text, uint8_t x = 0, uint8_t y = 10);
  void showMessage(const char* msg);
  void update();
  
  // Adafruit GFX methods
  void clearDisplay();
  void setCursor(int16_t x, int16_t y);
  void print(const char* text);
  void print(int value);
  void print(float value, int digits = 2);
  void println(const char* text);
  void display();
  
private:
  Adafruit_SSD1306 ssd1306;
  bool initialized;
};

// ── Global Display Instance ──────────────────────────────────────────────────────
extern DisplayAdafruit display;


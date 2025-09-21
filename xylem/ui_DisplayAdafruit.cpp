/**
 * @file ui_DisplayAdafruit.cpp
 * @brief Adafruit SSD1306 OLED display driver implementation
 * 
 * @author Brian Varren
 * @version 1.0
 * @date 2024
 */

#include "ui_DisplayAdafruit.h"
#include <Wire.h>

// ── Global Display Instance ──────────────────────────────────────────────────────
DisplayAdafruit display;

// ── Constructor/Destructor ───────────────────────────────────────────────────────
DisplayAdafruit::DisplayAdafruit() : ssd1306(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire1, OLED_RESET), initialized(false) {
}

DisplayAdafruit::~DisplayAdafruit() {
  end();
}

// ── Lifecycle Methods ────────────────────────────────────────────────────────────
bool DisplayAdafruit::begin() {
  // Initialize I2C
  Wire1.setSDA(PIN_WIRE_SDA);
  Wire1.setSCL(PIN_WIRE_SCL);
  Wire1.begin();
  
  // Initialize display
  if (!ssd1306.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
    return false;
  }
  
  // Clear and set up display
  ssd1306.clearDisplay();
  ssd1306.setTextSize(1);
  ssd1306.setTextColor(SSD1306_WHITE);
  ssd1306.setCursor(0, 0);
  
  // Show startup message
  ssd1306.println("XYLEM");
  ssd1306.println("Synth Foundry");
  ssd1306.println("Initializing...");
  ssd1306.display();
  
  initialized = true;
  return true;
}

void DisplayAdafruit::end() {
  if (initialized) {
    ssd1306.clearDisplay();
    ssd1306.display();
  }
  initialized = false;
}

// ── Basic Display Functions ──────────────────────────────────────────────────────
void DisplayAdafruit::clear() {
  if (!initialized) return;
  
  ssd1306.clearDisplay();
  ssd1306.display();
}

void DisplayAdafruit::showText(const char* text, uint8_t x, uint8_t y) {
  if (!initialized) return;
  
  ssd1306.clearDisplay();
  ssd1306.setCursor(x, y);
  ssd1306.print(text);
  ssd1306.display();
}

void DisplayAdafruit::showMessage(const char* msg) {
  if (!initialized) return;
  
  ssd1306.clearDisplay();
  ssd1306.setCursor(0, 10);
  ssd1306.print(msg);
  ssd1306.display();
}

void DisplayAdafruit::update() {
  // Simple display doesn't need continuous updates
  // All drawing is immediate
}

// ── Adafruit GFX Methods ─────────────────────────────────────────────────────────
void DisplayAdafruit::clearDisplay() {
  if (!initialized) return;
  ssd1306.clearDisplay();
}

void DisplayAdafruit::setCursor(int16_t x, int16_t y) {
  if (!initialized) return;
  ssd1306.setCursor(x, y);
}

void DisplayAdafruit::print(const char* text) {
  if (!initialized) return;
  ssd1306.print(text);
}

void DisplayAdafruit::print(int value) {
  if (!initialized) return;
  ssd1306.print(value);
}

void DisplayAdafruit::print(float value, int digits) {
  if (!initialized) return;
  ssd1306.print(value, digits);
}

void DisplayAdafruit::println(const char* text) {
  if (!initialized) return;
  ssd1306.println(text);
}

void DisplayAdafruit::display() {
  if (!initialized) return;
  ssd1306.display();
}


/**
 * Display Module for SH1122 OLED
 * Handles all display operations including text buffering and scrolling
 */

#ifndef DISPLAY_H
#define DISPLAY_H

#include <Arduino.h>
#include <U8g2lib.h>
#include <SPI.h>

// ─────────────────────── Display Configuration ───────────────────────
#define DISP_DC 2
#define DISP_RST 3
#define DISP_CS 5
#define DISP_SCL 6
#define DISP_SDA 7

#define DISPLAY_WIDTH 256
#define DISPLAY_HEIGHT 64
#define MAX_DISPLAY_LINES 20
#define LINES_PER_SCREEN 7
#define LINE_HEIGHT 8
#define FONT_START_Y 10
#define SCROLL_DELAY_MS 500

class DisplayClass {
private:
  U8G2_SH1122_256X64_F_4W_HW_SPI* u8g2;
  
  // Text buffer management
  String displayLines[MAX_DISPLAY_LINES];
  int lineCount;
  int scrollOffset;
  bool _needsRedraw;
  
  // Scrolling
  unsigned long lastScrollTime;
  bool autoScrollEnabled;
  
public:
  DisplayClass() : 
    u8g2(nullptr),
    lineCount(0),
    scrollOffset(0),
    _needsRedraw(true),
    lastScrollTime(0),
    autoScrollEnabled(true) {}
  
  ~DisplayClass() {
    if (u8g2) delete u8g2;
  }
  
  // ───────────────── Initialization ─────────────────
  bool begin() {
    Serial.println("Initializing SH1122 display...");
    
    // Hardware reset sequence
    pinMode(DISP_RST, OUTPUT);
    digitalWrite(DISP_RST, HIGH);
    delay(5);
    digitalWrite(DISP_RST, LOW);
    delay(20);
    digitalWrite(DISP_RST, HIGH);
    delay(50);
    
    // Configure SPI0 for display
    SPI.setSCK(DISP_SCL);
    SPI.setTX(DISP_SDA);
    SPI.begin();
    
    // Initialize U8G2
    u8g2 = new U8G2_SH1122_256X64_F_4W_HW_SPI(U8G2_R0, DISP_CS, DISP_DC, DISP_RST);
    
    if (!u8g2) {
      Serial.println("Failed to allocate display object");
      return false;
    }
    
    u8g2->begin();
    u8g2->setBusClock(8000000UL); // 8 MHz for stability
    u8g2->setContrast(180);
    u8g2->setFont(u8g2_font_5x7_tf); // Small font for more text
    
    clear();
    Serial.println("Display initialized");
    return true;
  }
  
  // ───────────────── Text Buffer Management ─────────────────
  void addLine(const String& line) {
    Serial.println(line); // Mirror to serial
    
    if (lineCount < MAX_DISPLAY_LINES) {
      displayLines[lineCount++] = line;
    } else {
      // Shift lines up
      for (int i = 0; i < MAX_DISPLAY_LINES - 1; i++) {
        displayLines[i] = displayLines[i + 1];
      }
      displayLines[MAX_DISPLAY_LINES - 1] = line;
    }
    _needsRedraw = true;
  }
  
  void clear() {
    lineCount = 0;
    scrollOffset = 0;
    _needsRedraw = true;
    for (int i = 0; i < MAX_DISPLAY_LINES; i++) {
      displayLines[i] = "";
    }
  }
  
  // ───────────────── Display Update ─────────────────
  void update() {
    if (!u8g2) return;
    
    u8g2->clearBuffer();
    
    // Draw border
    u8g2->drawFrame(0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT);
    
    // Display lines with scrolling
    int displayStart = scrollOffset;
    int displayEnd = min(displayStart + LINES_PER_SCREEN, lineCount);
    
    for (int i = displayStart; i < displayEnd; i++) {
      int y = FONT_START_Y + ((i - displayStart) * LINE_HEIGHT);
      u8g2->drawStr(4, y, displayLines[i].c_str());
    }
    
    // Draw scroll indicator if needed
    if (lineCount > LINES_PER_SCREEN) {
      int barHeight = max(2, (LINES_PER_SCREEN * (DISPLAY_HEIGHT - 4)) / lineCount);
      int barY = 2 + ((scrollOffset * (DISPLAY_HEIGHT - 4 - barHeight)) / 
                      (lineCount - LINES_PER_SCREEN));
      u8g2->drawBox(DISPLAY_WIDTH - 6, barY, 4, barHeight);
    }
    
    u8g2->sendBuffer();
    _needsRedraw = false;
  }
  
  // ───────────────── Scrolling Control ─────────────────
  void handleScroll() {
    if (!autoScrollEnabled || lineCount <= LINES_PER_SCREEN) return;
    
    unsigned long currentTime = millis();
    if (currentTime - lastScrollTime > SCROLL_DELAY_MS) {
      scrollOffset++;
      if (scrollOffset > lineCount - LINES_PER_SCREEN) {
        scrollOffset = 0; // Wrap around
      }
      _needsRedraw = true;
      lastScrollTime = currentTime;
    }
  }
  
  void scrollUp() {
    if (scrollOffset > 0) {
      scrollOffset--;
      _needsRedraw = true;
    }
  }
  
  void scrollDown() {
    if (scrollOffset < lineCount - LINES_PER_SCREEN) {
      scrollOffset++;
      _needsRedraw = true;
    }
  }
  
  void scrollToTop() {
    scrollOffset = 0;
    _needsRedraw = true;
  }
  
  void scrollToBottom() {
    if (lineCount > LINES_PER_SCREEN) {
      scrollOffset = lineCount - LINES_PER_SCREEN;
      _needsRedraw = true;
    }
  }
  
  void setAutoScroll(bool enabled) {
    autoScrollEnabled = enabled;
  }
  
  // ───────────────── Status Methods ─────────────────
  bool needsRedraw() const { return _needsRedraw; }
  int getLineCount() const { return lineCount; }
  int getScrollOffset() const { return scrollOffset; }
  bool isAutoScrolling() const { return autoScrollEnabled; }
  
  // ───────────────── Utility Methods ─────────────────
  void showProgressBar(const String& label, int percent) {
    if (!u8g2) return;
    
    u8g2->clearBuffer();
    
    // Draw label
    u8g2->drawStr(10, 25, label.c_str());
    
    // Draw progress bar
    int barWidth = 200;
    int barHeight = 10;
    int barX = (DISPLAY_WIDTH - barWidth) / 2;
    int barY = 35;
    
    u8g2->drawFrame(barX, barY, barWidth, barHeight);
    u8g2->drawBox(barX + 2, barY + 2, 
                  (barWidth - 4) * percent / 100, 
                  barHeight - 4);
    
    // Draw percentage
    String percentStr = String(percent) + "%";
    u8g2->drawStr(barX + barWidth/2 - 10, barY + barHeight + 12, 
                  percentStr.c_str());
    
    u8g2->sendBuffer();
  }
  
  void showSplash(const String& title, const String& subtitle = "") {
    if (!u8g2) return;
    
    u8g2->clearBuffer();
    u8g2->setFont(u8g2_font_9x15_tf); // Bigger font for splash
    
    int titleWidth = u8g2->getStrWidth(title.c_str());
    u8g2->drawStr((DISPLAY_WIDTH - titleWidth) / 2, 28, title.c_str());
    
    if (subtitle.length() > 0) {
      u8g2->setFont(u8g2_font_5x7_tf); // Smaller font for subtitle
      int subtitleWidth = u8g2->getStrWidth(subtitle.c_str());
      u8g2->drawStr((DISPLAY_WIDTH - subtitleWidth) / 2, 45, subtitle.c_str());
    }
    
    u8g2->setFont(u8g2_font_5x7_tf); // Reset to default font
    u8g2->sendBuffer();
  }
};

// Global display instance
extern DisplayClass Display;
DisplayClass Display;

#endif // DISPLAY_H
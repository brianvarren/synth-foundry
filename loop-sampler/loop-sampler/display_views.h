//   // ───────────────── Text Buffer Management ─────────────────
//   void addLine(const String& line) {
//     Serial.println(line); // Mirror to serial
    
//     if (lineCount < MAX_DISPLAY_LINES) {
//       displayLines[lineCount++] = line;
//     } else {
//       // Shift lines up
//       for (int i = 0; i < MAX_DISPLAY_LINES - 1; i++) {
//         displayLines[i] = displayLines[i + 1];
//       }
//       displayLines[MAX_DISPLAY_LINES - 1] = line;
//     }
//     _needsRedraw = true;
//   }
  


//   // ───────────────── Display Update ─────────────────
//   void update() {
//     if (!u8g2) return;
    
//     u8g2->clearBuffer();
    
//     // Draw border
//     u8g2->drawFrame(0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT);
    
//     // Display lines with scrolling
//     int displayStart = scrollOffset;
//     int displayEnd = min(displayStart + LINES_PER_SCREEN, lineCount);
    
//     for (int i = displayStart; i < displayEnd; i++) {
//       int y = FONT_START_Y + ((i - displayStart) * LINE_HEIGHT);
//       u8g2->drawStr(4, y, displayLines[i].c_str());
//     }

//     // Draw scroll indicator if needed
//     if (lineCount > LINES_PER_SCREEN) {
//       int barHeight = max(2, (LINES_PER_SCREEN * (DISPLAY_HEIGHT - 4)) / lineCount);
//       int barY = 2 + ((scrollOffset * (DISPLAY_HEIGHT - 4 - barHeight)) / 
//                       (lineCount - LINES_PER_SCREEN));
//       u8g2->drawBox(DISPLAY_WIDTH - 6, barY, 4, barHeight);
//     }
    
//     u8g2->sendBuffer();
//     _needsRedraw = false;
//   }
  


//   // ───────────────── Scrolling Control ─────────────────
//   void handleScroll() {
//     if (!autoScrollEnabled || lineCount <= LINES_PER_SCREEN) return;
    
//     unsigned long currentTime = millis();
//     if (currentTime - lastScrollTime > SCROLL_DELAY_MS) {
//       scrollOffset++;
//       if (scrollOffset > lineCount - LINES_PER_SCREEN) {
//         scrollOffset = 0; // Wrap around
//       }
//       _needsRedraw = true;
//       lastScrollTime = currentTime;
//     }
//   }
  
//   void scrollUp() {
//     if (scrollOffset > 0) {
//       scrollOffset--;
//       _needsRedraw = true;
//     }
//   }
  
//   void scrollDown() {
//     if (scrollOffset < lineCount - LINES_PER_SCREEN) {
//       scrollOffset++;
//       _needsRedraw = true;
//     }
//   }
  
//   void scrollToTop() {
//     scrollOffset = 0;
//     _needsRedraw = true;
//   }
  
//   void scrollToBottom() {
//     if (lineCount > LINES_PER_SCREEN) {
//       scrollOffset = lineCount - LINES_PER_SCREEN;
//       _needsRedraw = true;
//     }
//   }
  
//   void setAutoScroll(bool enabled) {
//     autoScrollEnabled = enabled;
//   }
  


//   // ───────────────── Status Methods ─────────────────
//   bool needsRedraw() const { return _needsRedraw; }
//   int getLineCount() const { return lineCount; }
//   int getScrollOffset() const { return scrollOffset; }
//   bool isAutoScrolling() const { return autoScrollEnabled; }
  


//   // ───────────────── Utility Methods ─────────────────
//   void showProgressBar(const String& label, int percent) {
//     if (!u8g2) return;
    
//     u8g2->clearBuffer();
    
//     // Draw label
//     u8g2->drawStr(10, 25, label.c_str());
    
//     // Draw progress bar
//     int barWidth = 200;
//     int barHeight = 10;
//     int barX = (DISPLAY_WIDTH - barWidth) / 2;
//     int barY = 35;
    
//     u8g2->drawFrame(barX, barY, barWidth, barHeight);
//     u8g2->drawBox(barX + 2, barY + 2, 
//                   (barWidth - 4) * percent / 100, 
//                   barHeight - 4);
    
//     // Draw percentage
//     String percentStr = String(percent) + "%";
//     u8g2->drawStr(barX + barWidth/2 - 10, barY + barHeight + 12, 
//                   percentStr.c_str());
    
//     u8g2->sendBuffer();
//   }
  
//   void showSplash(const String& title, const String& subtitle = "") {
//     if (!u8g2) return;
    
//     u8g2->clearBuffer();
//     u8g2->setFont(u8g2_font_9x15_tf); // Bigger font for splash
    
//     int titleWidth = u8g2->getStrWidth(title.c_str());
//     u8g2->drawStr((DISPLAY_WIDTH - titleWidth) / 2, 28, title.c_str());
    
//     if (subtitle.length() > 0) {
//       u8g2->setFont(u8g2_font_5x7_tf); // Smaller font for subtitle
//       int subtitleWidth = u8g2->getStrWidth(subtitle.c_str());
//       u8g2->drawStr((DISPLAY_WIDTH - subtitleWidth) / 2, 45, subtitle.c_str());
//     }
    
//     u8g2->setFont(u8g2_font_5x7_tf); // Reset to default font
//     u8g2->sendBuffer();
//   }





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

// Simple auto-scroll tick; pass millis()
void view_handle_scroll(uint32_t now_ms);

// Dirty flag if something changed
bool view_needs_redraw();

// If dirty, redraw + flush
void view_flush_if_dirty();

// Quick status card (title + line2)
void view_show_status(const char* title, const char* line2);

} // namespace sf

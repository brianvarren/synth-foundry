#include <Arduino.h>
#include <SPI.h>
#include <SdFat.h>
#include "driver_sdcard.h"
#include "driver_sh1122.h"
#include "config_pins.h"

#define SD_SPI_SPEED 60000000

SdFat sd;

namespace sf {

bool initialized = false;
float cardSizeMB = 0.0f;

static void log_err(const char* tag) {
  char buf[64];
  snprintf(buf, sizeof(buf), "%s err=%u data=%u",
           tag, (unsigned)sd.sdErrorCode(), (unsigned)sd.sdErrorData());
  view_print_line(buf);
}

  // ───────────────── Initialization ─────────────────
  bool sd_begin() {
    Serial.println("Initializing SD card on SPI1...");
    
    // Configure SPI1 pins
    SPI1.setSCK(SD_SCK_PIN);
    SPI1.setTX(SD_MOSI_PIN);
    SPI1.setRX(SD_MISO_PIN);
    SPI1.begin();
    
    // Create SdFat configuration for SPI1
    static SdSpiConfig cfg(SD_CS_PIN, DEDICATED_SPI, SD_SPI_SPEED, &SPI1);
    //sd = new SdFat();
    
    // if (!sd || !cfg) {
    //   Serial.println("Failed to allocate SD objects");
    //   return false;
    // }
    
    // Initialize SD card
    if (!sd.begin(cfg)) {
      Serial.println("SD card initialization failed");
      return false;
    }
    
    // Get card size
    cardSizeMB = sd.card()->sectorCount() / 2048.0;
    initialized = true;
    
    Serial.printf("SD card initialized. Size: %.1f MB\n", cardSizeMB);
    return true;
  }

float sd_card_size_mb() {
  uint64_t sectors = sd.card() ? sd.card()->sectorCount() : 0;
  if (!sectors) return 0.0f;
  double bytes = (double)sectors * 512.0;
  return (float)(bytes / (1024.0 * 1024.0));
}

void sd_format_size(uint32_t bytes, char* out, int out_len) {
  const char* unit = "B";
  double v = (double)bytes;
  if (bytes >= 1024u) { v = bytes / 1024.0; unit = "KB"; }
  if (bytes >= 1024u*1024u) { v = bytes / (1024.0*1024.0); unit = "MB"; }
  if (bytes >= 1024u*1024u*1024u) { v = bytes / (1024.0*1024.0*1024.0); unit = "GB"; }
  snprintf(out, out_len, "%.1f %s", v, unit);
}

} // namespace sf
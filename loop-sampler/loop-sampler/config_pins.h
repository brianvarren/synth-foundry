#pragma once

// ─────────────────────── Encoder ───────────────────────
#define ENC_A_PIN   23
#define ENC_B_PIN   22
#define ENC_BTN_PIN 19

// ─────────────────────── SD Card ───────────────────────
#define SD_CS_PIN   9   // Chip Select pin for the SD card
#define SD_SCK_PIN  10  // Clock pin
#define SD_MOSI_PIN 11  // Master Out Slave In
#define SD_MISO_PIN 24  // Master In Slave Out

// ─────────────────────── Display ───────────────────────
#define DISP_DC     2
#define DISP_RST    3 
#define DISP_CS     5
#define DISP_SCL    6
#define DISP_SDA    7

// ─────────────────────── Reset Trigger ───────────────────────
#define RESET_TRIGGER_PIN 18  // GPIO18 for tempo sync reset trigger
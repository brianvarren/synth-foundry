/**
 * @file driver_sdcard.h
 * @brief SD card driver interface for sample file storage
 * 
 * This header provides the interface for SD card operations, including
 * initialization, file system access, and storage capacity reporting.
 * The SD card is used to store WAV sample files that can be loaded
 * into PSRAM for real-time playback.
 * 
 * ## Features
 * 
 * **SPI Interface**: Uses SPI communication for fast file access
 * **SdFat Library**: Leverages the efficient SdFat library for file operations
 * **Capacity Reporting**: Provides storage size information for user feedback
 * **Error Handling**: Robust initialization with proper error reporting
 * 
 * ## Usage
 * 
 * The SD card must be initialized before any file operations can be performed.
 * The system scans the SD card for WAV files and maintains an index for
 * fast browsing and selection.
 * 
 * @author Brian Varren
 * @version 1.0
 * @date 2024
 */

#pragma once
#include <stdint.h>

namespace sf {

// ── Global SD Card State ──────────────────────────────────────────────────────
extern bool initialized;  // SD card initialization status
extern float cardSizeMB;  // SD card capacity in megabytes

// ── SD Card Interface Functions ───────────────────────────────────────────────

/**
 * @brief Initialize SD card and file system
 * 
 * Sets up SPI communication and initializes the SdFat library.
 * Must be called before any file operations.
 * 
 * @return true if initialization successful, false otherwise
 */
bool sd_begin();

/**
 * @brief Get SD card capacity in megabytes
 * 
 * @return Card size in MB as a float for convenience
 */
float sd_card_size_mb();

/**
 * @brief Format byte count into human-readable string
 * 
 * Converts raw byte count to a formatted string like "1.2 MB"
 * for display purposes.
 * 
 * @param bytes Raw byte count
 * @param out Output buffer for formatted string
 * @param out_len Maximum length of output buffer
 */
void sd_format_size(uint32_t bytes, char* out, int out_len);

} // namespace sf

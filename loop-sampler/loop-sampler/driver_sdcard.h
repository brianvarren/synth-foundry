#pragma once
#include <stdint.h>

namespace sf {

extern bool initialized;
extern float cardSizeMB;

// Initialize SD (SPI pins + SdFat begin). Returns true if ok.
bool sd_begin();

// Card size in MB (float for convenience).
float sd_card_size_mb();

// Format bytes into a short human string, e.g. "1.2 MB".
void sd_format_size(uint32_t bytes, char* out, int out_len);

} // namespace sf

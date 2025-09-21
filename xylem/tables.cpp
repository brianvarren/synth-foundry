/**
 * @file tables.cpp
 * @brief Lookup tables for audio synthesis
 * 
 * Implements pre-computed lookup tables for efficient audio synthesis.
 * All tables use Q15 fixed-point format for optimal performance.
 * 
 * @author Brian Varren
 * @version 1.0
 * @date 2024
 */

#include "tables.h"
#include <math.h>

// ── Sine Lookup Table ──────────────────────────────────────────────────────────────

/**
 * @brief Sine wave lookup table in Q15 format
 * 
 * 2048-point sine table covering one complete cycle (0 to 2π).
 * Values are in Q15 format: -32768 to +32767 representing -1.0 to +1.0.
 * 
 * Generated at compile time for maximum efficiency.
 */
const int16_t SINE_Q15[2048] = {
    #include "sine_table_data.h"  // Generated sine table data
};

// ── Table Access Functions ─────────────────────────────────────────────────────────

/**
 * @brief Interpolated sine lookup in Q15 format
 * 
 * Performs linear interpolation between adjacent table entries for smooth
 * sine wave generation. Uses 32-bit phase accumulator for high precision.
 * 
 * Phase accumulator layout:
 * - Bits 31-21: Table index (11 bits, 0-2047)
 * - Bits 20-0:  Fractional part for interpolation (21 bits)
 * 
 * @param phase 32-bit phase accumulator (0 to 2^32-1 represents 0 to 2π)
 * @return Interpolated sine value in Q15 format (-32768 to +32767)
 */
int16_t interp_sine_q15(uint32_t phase) {
    // Extract table index (top 11 bits)
    uint32_t index = phase >> 21;
    
    // Extract fractional part (bottom 21 bits)
    uint32_t frac = phase & 0x1FFFFF;
    
    // Get adjacent table entries
    int16_t y0 = SINE_Q15[index];
    int16_t y1 = SINE_Q15[(index + 1) & 0x7FF];  // Wrap around for last entry
    
    // Linear interpolation
    // y = y0 + (y1 - y0) * frac / (2^21)
    int32_t diff = (int32_t)y1 - (int32_t)y0;
    int32_t interp = (int32_t)y0 + ((diff * (int32_t)frac) >> 21);
    
    return (int16_t)interp;
}

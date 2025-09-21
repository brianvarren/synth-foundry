/**
 * @file tables.h
 * @brief Lookup tables for audio synthesis
 * 
 * Provides pre-computed lookup tables for efficient audio synthesis.
 * All tables use Q15 fixed-point format for optimal performance.
 * 
 * @author Brian Varren
 * @version 1.0
 * @date 2024
 */

#pragma once
#include <stdint.h>

// ── Sine Lookup Table ──────────────────────────────────────────────────────────────

/**
 * @brief Sine wave lookup table in Q15 format
 * 
 * 2048-point sine table covering one complete cycle (0 to 2π).
 * Values are in Q15 format: -32768 to +32767 representing -1.0 to +1.0.
 * 
 * Usage:
 * - Phase accumulator: 32-bit unsigned (0 to 2^32-1 represents 0 to 2π)
 * - Table index: phase >> 21 (extract top 11 bits for 2048 entries)
 * - Interpolation: Use lower 21 bits for linear interpolation
 */
extern const int16_t SINE_Q15[2048];

// ── Table Access Functions ─────────────────────────────────────────────────────────

/**
 * @brief Interpolated sine lookup in Q15 format
 * 
 * Performs linear interpolation between adjacent table entries for smooth
 * sine wave generation. Uses 32-bit phase accumulator for high precision.
 * 
 * @param phase 32-bit phase accumulator (0 to 2^32-1 represents 0 to 2π)
 * @return Interpolated sine value in Q15 format (-32768 to +32767)
 */
int16_t interp_sine_q15(uint32_t phase);

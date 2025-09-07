/**
 * @file pico_interp.h
 * @brief Hardware-accelerated interpolation for smooth audio playback
 * 
 * This header provides access to the RP2040's hardware interpolation units
 * for high-performance sample interpolation. The RP2040 includes dedicated
 * interpolation hardware that can perform linear interpolation between
 * two 16-bit values with 8-bit fractional precision.
 * 
 * ## Hardware Interpolation
 * 
 * The RP2040's interpolation units provide:
 * - Linear interpolation between two 16-bit values
 * - 8-bit fractional precision (256 steps between samples)
 * - Hardware acceleration for consistent performance
 * - No CPU overhead for interpolation calculations
 * 
 * ## Usage in Audio Engine
 * 
 * The interpolation system is used in the audio engine for:
 * - Smooth sample playback with sub-sample precision
 * - Pitch shifting without aliasing artifacts
 * - Crossfading between different loop regions
 * - Real-time audio processing with minimal CPU load
 * 
 * ## Performance Benefits
 * 
 * Using hardware interpolation instead of software interpolation:
 * - Reduces CPU load in the audio interrupt
 * - Provides consistent timing regardless of CPU load
 * - Enables higher quality audio processing
 * - Allows for more complex audio algorithms
 * 
 * @author Brian Varren
 * @version 1.0
 * @date 2024
 */

#pragma once

// ── Hardware Interpolation Interface ──────────────────────────────────────────

/**
 * @brief Initialize RP2040 interpolation hardware
 * 
 * Sets up the hardware interpolation units for use by the audio engine.
 * Must be called during system initialization.
 */
void setupInterpolators();

/**
 * @brief Perform hardware-accelerated linear interpolation
 * 
 * Uses the RP2040's interpolation hardware to interpolate between two values.
 * 
 * @param x First value (16-bit)
 * @param y Second value (16-bit) 
 * @param mu_scaled Interpolation factor (0-255, where 0 = x, 255 = y)
 * @return Interpolated result (16-bit)
 */
uint16_t interpolate(uint16_t x, uint16_t y, uint16_t mu_scaled);

/**
 * @brief Alternative interpolation function (if multiple units available)
 * 
 * @param x First value (16-bit)
 * @param y Second value (16-bit)
 * @param mu_scaled Interpolation factor (0-255)
 * @return Interpolated result (16-bit)
 */
uint16_t interpolate1(uint16_t x, uint16_t y, uint16_t mu_scaled);
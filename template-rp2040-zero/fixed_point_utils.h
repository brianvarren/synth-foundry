/**
 * @file fixed_point_utils.h
 * @brief Fixed-point arithmetic utilities for real-time audio processing
 * 
 * This header provides essential fixed-point math functions optimized for
 * real-time audio synthesis on the RP2040. All functions use integer
 * arithmetic to avoid floating-point operations in audio processing paths.
 * 
 * ## Fixed-Point Formats
 * 
 * **Q1.15 Audio Signals**: 16-bit signed integers (-32768..+32767 ≈ -1.0..+1.0)
 * **Q0.32 Phase**: 32-bit unsigned integers for oscillator phase accumulation
 * **Q24.8 Sample Phase**: 32-bit signed integers for sample playback with reverse/TZFM
 * **Q5.27 Mix Bus**: 32-bit accumulators with headroom for polyphony
 * 
 * ## Key Features
 * 
 * **No Floating-Point**: All hot-path functions use integer arithmetic
 * **Saturation**: Proper overflow protection for all operations
 * **Rounding**: Consistent rounding for downcasts
 * **Performance**: Optimized for ARM Cortex-M0+ instruction set
 * 
 * @author Brian Varren
 * @version 1.0
 * @date 2024
 */

#pragma once
#include <stdint.h>
#include <stdbool.h>

// ── Fixed-Point Type Definitions ───────────────────────────────────────────────
typedef int16_t   audio_sample_t;    // Q1.15 audio samples (-32768..+32767)
typedef uint32_t  phase_q32_t;       // Q0.32 oscillator phase (0..4294967295)
typedef int32_t   sample_phase_t;    // Q24.8 sample phase (signed, enables reverse)
typedef int32_t   mix_accum_t;       // Q5.27 mix bus accumulator

// ── Q1.15 Audio Sample Math ────────────────────────────────────────────────────

/**
 * @brief Multiply two Q1.15 values with proper rounding and saturation
 * 
 * Performs 16×16→32 multiplication, rounds to 15 bits, then saturates.
 * This is the fundamental operation for audio signal processing.
 * 
 * @param a First Q1.15 operand
 * @param b Second Q1.15 operand
 * @return Q1.15 result: sat_q15(rshift_round((int32_t)a * (int32_t)b, 15))
 */
static inline int16_t mul_q15(int16_t a, int16_t b) {
    int32_t product = (int32_t)a * (int32_t)b;
    int32_t rounded = (product + 0x4000) >> 15;  // Round to 15 bits
    if (rounded > 32767) return 32767;
    if (rounded < -32768) return -32768;
    return (int16_t)rounded;
}

/**
 * @brief Add two Q1.15 values with saturation
 * 
 * @param a First Q1.15 operand
 * @param b Second Q1.15 operand
 * @return Q1.15 result with overflow protection
 */
static inline int16_t add_q15(int16_t a, int16_t b) {
    int32_t sum = (int32_t)a + (int32_t)b;
    if (sum > 32767) return 32767;
    if (sum < -32768) return -32768;
    return (int16_t)sum;
}

/**
 * @brief Saturate 32-bit value to Q1.15 range
 * 
 * @param x 32-bit value to saturate
 * @return Q1.15 saturated result
 */
static inline int16_t sat_q15(int32_t x) {
    if (x > 32767) return 32767;
    if (x < -32768) return -32768;
    return (int16_t)x;
}

/**
 * @brief Round and shift right with proper rounding
 * 
 * @param x Value to shift
 * @param shift_bits Number of bits to shift right
 * @return Rounded result
 */
static inline int32_t rshift_round(int32_t x, uint8_t shift_bits) {
    return (x + (1 << (shift_bits - 1))) >> shift_bits;
}

// ── Q0.32 Phase Accumulation ───────────────────────────────────────────────────

/**
 * @brief Convert frequency (Hz) to Q0.32 phase increment
 * 
 * @param freq_hz Frequency in Hz
 * @param sample_rate Sample rate in Hz
 * @return Q0.32 phase increment per sample
 */
static inline uint32_t hz_to_inc_q32(float freq_hz, float sample_rate) {
    double inc = (double)freq_hz * 4294967296.0 / (double)sample_rate;
    if (inc < 0.0) return 0;
    if (inc > 4294967295.0) return 4294967295;
    return (uint32_t)inc;
}

/**
 * @brief Increment phase accumulator (Q0.32)
 * 
 * @param phase Current phase accumulator
 * @param inc Phase increment per sample
 * @return Updated phase accumulator
 */
static inline uint32_t inc_phase_q32(uint32_t phase, uint32_t inc) {
    return phase + inc;
}

// ── Q24.8 Sample Phase Math ────────────────────────────────────────────────────

/**
 * @brief Convert frequency to Q24.8 sample phase increment
 * 
 * @param freq_hz Frequency in Hz
 * @param sample_rate Sample rate in Hz
 * @param num_samples Number of samples in wavetable
 * @return Q24.8 signed phase increment (enables reverse playback)
 */
static inline int32_t hz_to_inc_q24_8(float freq_hz, float sample_rate, uint32_t num_samples) {
    double inc = (double)freq_hz * (double)num_samples * 256.0 / (double)sample_rate;
    if (inc > 2147483647.0) return 2147483647; 
    if (inc < -2147483648.0) return -2147483648;
    return (int32_t)inc;
}

/**
 * @brief Increment signed sample phase (Q24.8)
 * 
 * @param phase Current sample phase
 * @param inc Phase increment per sample
 * @return Updated sample phase
 */
static inline int32_t inc_sample_phase(int32_t phase, int32_t inc) {
    return phase + inc;
}

// ── Q5.27 Mix Bus Math ─────────────────────────────────────────────────────────

/**
 * @brief Convert Q1.15 sample to Q5.27 mix bus format
 * 
 * @param sample Q1.15 audio sample
 * @return Q5.27 mix bus value (12-bit left shift for 4-voice headroom)
 */
static inline int32_t q15_to_mix(int16_t sample) {
    return ((int32_t)sample) << 12;
}

/**
 * @brief Convert Q5.27 mix bus to Q1.15 audio sample
 * 
 * @param accum Q5.27 mix bus accumulator
 * @return Q1.15 audio sample with proper rounding and saturation
 */
static inline int16_t mix_to_q15(int32_t accum) {
    return sat_q15(rshift_round(accum, 12));
}

// ── PWM Conversion ─────────────────────────────────────────────────────────────

/**
 * @brief Convert Q1.15 sample to 12-bit PWM duty cycle
 * 
 * Maps Q1.15 range (-32768..+32767) to PWM range (0..4095) with proper
 * centering and rounding for high-quality audio output.
 * 
 * @param sample Q1.15 audio sample
 * @return 12-bit PWM duty cycle value
 */
static inline uint16_t q15_to_pwm(int16_t sample) {
    // Convert signed to unsigned (0..65535)
    uint32_t unsigned_sample = ((uint16_t)sample) ^ 0x8000u;
    // Scale to PWM resolution with rounding
    uint32_t product = unsigned_sample * (4096u - 1u);
    return (uint16_t)((product + 0x8000u) >> 16);
}

// ── ADC Conversion ─────────────────────────────────────────────────────────────

/**
 * @brief Convert 12-bit ADC value to Q1.15 range
 * 
 * @param adc_value 12-bit ADC value (0..4095)
 * @return Q1.15 value (-32768..+32767)
 */
static inline int16_t adc_to_q15(uint16_t adc_value) {
    // Convert 12-bit to 16-bit with proper scaling
    int32_t scaled = ((int32_t)adc_value - 2048) << 3;  // Center at 0, scale up
    return sat_q15(scaled);
}

/**
 * @brief Convert 12-bit ADC value to normalized float (0.0..1.0)
 * 
 * @param adc_value 12-bit ADC value (0..4095)
 * @return Normalized float value
 */
static inline float adc_to_float(uint16_t adc_value) {
    return (float)adc_value / 4095.0f;
}

// ── Lookup Table Helpers ───────────────────────────────────────────────────────

/**
 * @brief Linear interpolation between two Q1.15 values
 * 
 * @param a First Q1.15 value
 * @param b Second Q1.15 value
 * @param t Interpolation factor (0..255, Q0.8)
 * @return Interpolated Q1.15 result
 */
static inline int16_t lerp_q15(int16_t a, int16_t b, uint8_t t) {
    int32_t diff = (int32_t)b - (int32_t)a;
    int32_t interpolated = (int32_t)a + ((diff * (int32_t)t) >> 8);
    return sat_q15(interpolated);
}

/**
 * @brief Extract table index from Q0.32 phase
 * 
 * @param phase Q0.32 phase accumulator
 * @param table_bits Number of bits for table size (e.g., 10 for 1024 entries)
 * @return Table index (0 to 2^table_bits - 1)
 */
static inline uint32_t phase_to_index(uint32_t phase, uint8_t table_bits) {
    return phase >> (32 - table_bits);
}

/**
 * @brief Extract interpolation factor from Q0.32 phase
 * 
 * @param phase Q0.32 phase accumulator
 * @param table_bits Number of bits for table size
 * @return Interpolation factor (0..255, Q0.8)
 */
static inline uint8_t phase_to_mu(uint32_t phase, uint8_t table_bits) {
    return (phase >> (32 - table_bits - 8)) & 0xFF;
}


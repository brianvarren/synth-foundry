/**
 * @file synth_voice.h
 * @brief Voice structure for audio synthesis
 * 
 * Defines the Voice structure used for individual synthesizer voices.
 * Uses fixed-point arithmetic for efficient real-time audio processing.
 * 
 * @author Brian Varren
 * @version 1.0
 * @date 2024
 */

#pragma once
#include <stdint.h>

// ── Voice Structure ────────────────────────────────────────────────────────────────

/**
 * @brief Individual synthesizer voice
 * 
 * Represents a single voice in the synthesizer with fixed-point arithmetic
 * for optimal performance in real-time audio processing.
 * 
 * Fixed-point formats:
 * - phase: Q0.32 (32-bit phase accumulator, 0 to 2^32-1 represents 0 to 2π)
 * - inc:   Q0.32 (phase increment per sample, determines frequency)
 * - amp_cur: Q1.15 (current amplitude, -32768 to +32767 represents -1.0 to +1.0)
 * - env:   Q1.15 (envelope value, -32768 to +32767 represents -1.0 to +1.0)
 */
typedef struct {
    uint32_t phase;      // Q0.32 - Current phase accumulator
    uint32_t inc;        // Q0.32 - Phase increment per sample (frequency)
    int16_t  amp_cur;    // Q1.15 - Current amplitude
    int16_t  env;        // Q1.15 - Envelope value (temporary until envelopes land)
    uint8_t  active;     // Voice active flag (0 = inactive, 1 = active)
} Voice;

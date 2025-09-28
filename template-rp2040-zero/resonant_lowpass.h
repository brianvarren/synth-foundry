/**
 * @file resonant_lowpass.h
 * @brief Fixed-point 2-pole resonant low-pass filter with feedback control
 *
 * Provides a lightweight resonant filter optimised for the RP2040 audio path.
 * The filter operates entirely in Q1.15 fixed-point math inside the hot audio
 * loop and exposes float-based configuration helpers for human-friendly tuning.
 *
 * Usage pattern:
 *  - Call resonant_lowpass_init() on startup
 *  - Configure cutoff and feedback via resonant_lowpass_set_* helpers
 *  - Process samples with resonant_lowpass_process() inside the audio loop
 *
 * The feedback parameter `r` ranges from 0 (no resonance) to just below 1.0 for
 * self-resonant behaviour. Higher values approach sine-like output from noise.
 *
 * @author Brian Varren
 * @version 1.0
 * @date 2024
 */

#pragma once

#include <stdint.h>

/**
 * @struct ResonantLowpass2P
 * @brief Internal state for the 2-pole resonant low-pass filter
 */
typedef struct {
    int32_t stage1;       /**< First integrator state (scaled Q1.15 << 8) */
    int32_t stage2;       /**< Second integrator state (scaled Q1.15 << 8) */
    int16_t g_q15;        /**< Cutoff coefficient in Q1.15 */
    int16_t feedback_q15; /**< Feedback (resonance) coefficient in Q1.15 */
} ResonantLowpass2P;

void resonant_lowpass_init(ResonantLowpass2P* filter);
void resonant_lowpass_reset(ResonantLowpass2P* filter);
void resonant_lowpass_set_cutoff(ResonantLowpass2P* filter, float cutoff_hz, float sample_rate_hz);
void resonant_lowpass_set_feedback(ResonantLowpass2P* filter, float feedback);

/**
 * @brief Process one Q1.15 sample through the resonant low-pass filter
 *
 * @param filter Pointer to filter state
 * @param input  Q1.15 audio sample
 * @return Filtered Q1.15 sample
 */
static inline int16_t resonant_lowpass_process(ResonantLowpass2P* filter, int16_t input) {
    const int32_t STATE_SHIFT = 8;                          // Extra fractional bits for headroom
    const int32_t STATE_MAX   =  32767 << STATE_SHIFT;
    const int32_t STATE_MIN   = -32768 << STATE_SHIFT;

    int32_t stage1 = filter->stage1;
    int32_t stage2 = filter->stage2;

    // Upscale input to match internal precision
    int32_t in_scaled = ((int32_t)input) << STATE_SHIFT;

    // Apply feedback: drive = input - r * y
    int32_t fb = (int32_t)(((int64_t)filter->feedback_q15 * (int64_t)stage2) >> 15);
    int32_t drive = in_scaled - fb;
    if (drive > STATE_MAX) drive = STATE_MAX;
    if (drive < STATE_MIN) drive = STATE_MIN;

    // First integrator: stage1 += g * (drive - stage1)
    int32_t delta1 = drive - stage1;
    int32_t inc1 = (int32_t)(((int64_t)filter->g_q15 * (int64_t)delta1) >> 15);
    stage1 += inc1;
    if (stage1 > STATE_MAX) stage1 = STATE_MAX;
    if (stage1 < STATE_MIN) stage1 = STATE_MIN;

    // Second integrator: stage2 += g * (stage1 - stage2)
    int32_t delta2 = stage1 - stage2;
    int32_t inc2 = (int32_t)(((int64_t)filter->g_q15 * (int64_t)delta2) >> 15);
    stage2 += inc2;
    if (stage2 > STATE_MAX) stage2 = STATE_MAX;
    if (stage2 < STATE_MIN) stage2 = STATE_MIN;

    filter->stage1 = stage1;
    filter->stage2 = stage2;

    int32_t out = stage2 >> STATE_SHIFT;
    if (out > 32767) out = 32767;
    if (out < -32768) out = -32768;
    return (int16_t)out;
}


/**
 * @file audio_engine_render.cpp
 * @brief Core audio rendering engine for Synth Foundry
 * 
 * This module provides the main audio rendering function that processes
 * audio blocks in real-time. It interfaces with the DACless system to
 * fill audio buffers with synthesized audio data.
 * 
 * ## Audio Processing Pipeline
 * 
 * **Block-Based Processing**: Audio is processed in fixed-size blocks
 * defined by AUDIO_BLOCK_SIZE for deterministic real-time performance.
 * 
 * **Fixed-Point Processing**: All audio processing uses Q1.15 format
 * for sample data to ensure deterministic performance on RP2040.
 * 
 * **PWM Output**: Samples are converted to 12-bit PWM duty cycles
 * for high-quality audio output without external DAC.
 * 
 * ## Usage
 * 
 * Call ae_render_block() whenever the DMA system signals that a new
 * audio buffer is ready for processing (callback_flag is set).
 * 
 * @author Brian Varren
 * @version 1.0
 * @date 2024
 */

#include <Arduino.h>
#include "audio_engine.h"
#include "DACless.h"
#include "ADCless.h"
#include "adc_filter.h"
#include "fixed_point_utils.h"
#include "resonant_bandpass.h"

// ── Audio Engine State ──────────────────────────────────────────────────────
static uint32_t g_noise_state = 12345;  // PRNG state for noise generation
static ResonantBandpass2P g_noise_filter;
static float g_noise_filter_cutoff_hz = 50.0f;
static float g_noise_filter_feedback = 0.98f;
static bool g_noise_filter_inited = false;
static bool g_noise_filter_params_dirty = true;

// ── Audio Rendering Implementation ──────────────────────────────────────────

/**
 * @brief Generate white noise sample using linear congruential generator
 * 
 * Fast, deterministic pseudo-random number generator optimized for
 * audio rate processing. Uses fixed-point arithmetic throughout.
 * 
 * @return int16_t White noise sample in Q1.15 format
 */
static inline int16_t generate_noise_sample() {
    // Linear congruential generator with good spectral properties
    g_noise_state = g_noise_state * 1103515245UL + 12345UL;
    
    // Convert to Q1.15 format (-32768 to +32767)
    return (int16_t)((g_noise_state >> 16) ^ 0x8000);
}

static void ae_update_noise_filter() {
    if (!g_noise_filter_inited) {
        resonant_bandpass_init(&g_noise_filter);
        g_noise_filter_inited = true;
        g_noise_filter_params_dirty = true;
    }

    if (g_noise_filter_params_dirty) {
        resonant_bandpass_set_cutoff(&g_noise_filter, g_noise_filter_cutoff_hz, audio_rate);
        resonant_bandpass_set_feedback(&g_noise_filter, g_noise_filter_feedback);
        g_noise_filter_params_dirty = false;
    }
}

void audio_tick(void) {

    // Process audio when either channel is ready to prevent buffer underruns
    // This fixes the pop issue caused by waiting for both channels simultaneously
    if (callback_flag_L > 0) {
        adc_filter_update_from_dma();
        ae_render_block();
        callback_flag_L = 0;
    }
}

void ae_set_noise_filter(float cutoff_hz, float feedback) {
    if (cutoff_hz < 0.0f) {
        cutoff_hz = 0.0f;
    }
    if (feedback < 0.0f) {
        feedback = 0.0f;
    }
    if (feedback > 0.9995f) {
        feedback = 0.9995f;
    }

    g_noise_filter_cutoff_hz = cutoff_hz;
    g_noise_filter_feedback = feedback;
    g_noise_filter_params_dirty = true;
}

void ae_reset_noise_filter() {
    if (!g_noise_filter_inited) {
        resonant_bandpass_init(&g_noise_filter);
        g_noise_filter_inited = true;
        g_noise_filter_params_dirty = true;
        return;
    }

    resonant_bandpass_reset(&g_noise_filter);
}

/**
 * @brief Render a complete audio block
 * 
 * Processes AUDIO_BLOCK_SIZE samples and fills the active output buffer
 * with synthesized audio data. This function is called from the main
 * audio loop when the DMA system signals buffer completion.
 * 
 * Currently generates white noise for testing. Will be expanded to
 * include full synthesis engine functionality.
 */
void ae_render_block() {
    volatile uint16_t* target = out_buf_ptr_L;
    if (!target) {
        return;  // Safety check - no valid buffer pointer
    }

    ae_update_noise_filter();

    // Map filtered ADC 0 (0..4095) to resonance feedback (Q1.15)
    const int32_t kFeedbackMaxQ15 = 32700; // keep headroom to prevent runaway
    uint16_t feedback_adc = adc_filter_get(0);
    int32_t feedback_q15 = ((int32_t)feedback_adc * kFeedbackMaxQ15 + 2047) / 4095;
    resonant_bandpass_set_feedback_q15(&g_noise_filter, (int16_t)feedback_q15);
    g_noise_filter_feedback = (float)feedback_q15 / 32767.0f;

    // Generate audio samples for the current buffer
    for (int i = 0; i < AUDIO_BLOCK_SIZE; i++) {
        // Generate white noise sample in Q1.15 format
        int16_t audio_sample = generate_noise_sample();

        // Shape noise into a resonant tone (cascade stages)
        audio_sample = resonant_bandpass_process(&g_noise_filter, audio_sample);

        // Convert Q1.15 audio sample to 12-bit PWM duty cycle
        target[i] = q15_to_pwm(audio_sample);
    }
}

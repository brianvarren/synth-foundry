/**
 * @file audio_engine_render.h
 * @brief Audio rendering engine interface
 * 
 * This header provides the interface for the core audio rendering system.
 * The audio engine processes audio in fixed-size blocks and outputs
 * PWM-compatible samples for the DACless system.
 * 
 * ## Audio Processing Flow
 * 
 * 1. **DMA Interrupt**: DMA system signals buffer completion via callback_flag
 * 2. **Block Render**: ae_render_block() processes AUDIO_BLOCK_SIZE samples  
 * 3. **PWM Conversion**: Audio samples converted to PWM duty cycles
 * 4. **Buffer Fill**: Active output buffer filled with processed audio
 * 
 * ## Integration Points
 * 
 * **DACless System**: Provides output buffers and PWM conversion utilities
 * **Fixed-Point Math**: Uses Q1.15 format for all audio sample processing
 * **Real-Time Constraints**: Must complete processing within buffer time
 * 
 * @author Brian Varren
 * @version 1.0
 * @date 2024
 */

#pragma once

#include <stdint.h>

// ── Audio Engine Interface ──────────────────────────────────────────────────

/**
 * @brief Render a complete audio block
 * 
 * Processes audio for the current buffer indicated by out_buf_ptr_L.
 * This function must complete processing within the time allocated
 * for one audio buffer to maintain real-time performance.
 * 
 * The function fills AUDIO_BLOCK_SIZE samples in the active output
 * buffer with PWM duty cycle values ready for DMA transfer.
 * 
 * ## Performance Requirements
 * 
 * - Must complete within ~333μs at 48kHz sample rate (16-sample blocks)
 * - Uses only fixed-point arithmetic for deterministic performance
 * - No dynamic memory allocation or blocking operations
 * 
 * ## Usage Example
 * 
 * ```cpp
 * if (callback_flag) {
 *     ae_render_block();
 *     callback_flag = 0;
 * }
 * ```
 */
void ae_render_block();

/**
 * @brief Service routine for the audio engine
 *
 * Called from the Arduino main loop to handle DMA callbacks and render
 * the next audio block when required.
 */
void audio_tick();

/**
 * @brief Configure the resonant low-pass filter used by the noise source
 *
 * @param cutoff_hz Target cutoff/resonant frequency in Hz
 * @param feedback  Feedback amount (0.0 .. <1.0) controlling resonance
 */
void ae_set_noise_filter(float cutoff_hz, float feedback);

/**
 * @brief Clear the resonant filter state to avoid parameter change transients
 */
void ae_reset_noise_filter();

/**
 * @brief Advance to the next glyph in the playlist and refresh filter params
 */
void ae_next_glyph();

/**
 * @brief Get name/label of the current glyph/chord for UI display
 */
const char* ae_current_glyph_name();

/**
 * @brief Get current octave shift (-2 to +2)
 */
int ae_get_octave_shift();

/**
 * @brief Set octave shift (-2 to +2)
 */
void ae_set_octave_shift(int octaves);

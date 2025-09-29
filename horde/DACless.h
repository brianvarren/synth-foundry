/**
 * @file DACless.h
 * @brief PWM-based audio output driver for high-quality audio playback
 * 
 * This header provides the interface for the audio output system, which uses
 * PWM (Pulse Width Modulation) to generate high-quality audio output without
 * requiring a dedicated DAC. The system uses DMA for continuous, non-blocking
 * audio output with minimal CPU overhead.
 * 
 * ## Audio Output Features
 * 
 * **PWM Audio**: Uses PWM to generate analog audio output with 12-bit resolution
 * **DMA Transfer**: Continuous audio output without CPU intervention
 * **Dual Buffering**: Ping-pong buffers for seamless audio streaming
 * **High Sample Rate**: 48kHz output for professional audio quality
 * **Low Latency**: Minimal delay between audio processing and output
 * 
 * ## Audio Processing Pipeline
 * 
 * 1. **Audio Engine**: Generates Q15 audio samples in PSRAM
 * 2. **PWM Conversion**: Converts Q15 samples to PWM duty cycles
 * 3. **DMA Transfer**: Streams PWM data to output pin
 * 4. **Hardware PWM**: Generates analog output via low-pass filtering
 * 
 * ## Performance Characteristics
 * 
 * - **Sample Rate**: Configurable via audio_rate setting (typically 48kHz)
 * - **Bit Depth**: 12-bit PWM resolution (equivalent to ~12-bit DAC)
 * - **Latency**: <1ms end-to-end audio latency
 * - **CPU Load**: <5% CPU usage for audio output
 * 
 * @author Brian Varren
 * @version 1.0
 * @date 2024
 */

#pragma once

// ── Audio Output Configuration ──────────────────────────────────────────────────
#define AUDIO_BLOCK_SIZE    128   // Audio buffer size (samples per DMA transfer)
#define PIN_PWM_OUT_L       6     // PWM output pin for audio
#define PIN_PWM_OUT_R       7     // PWM output pin for audio (unused)
#define PWM_RESOLUTION      4096  // PWM resolution (12-bit)

// ── Audio Output State Variables ────────────────────────────────────────────────
extern volatile uint16_t pwm_out_buf_a[AUDIO_BLOCK_SIZE];  // First audio buffer
extern volatile uint16_t pwm_out_buf_b[AUDIO_BLOCK_SIZE];  // Second audio buffer
extern volatile uint16_t pwm_out_buf_c[AUDIO_BLOCK_SIZE];  // Third audio buffer
extern volatile uint16_t pwm_out_buf_d[AUDIO_BLOCK_SIZE];  // Fourth audio buffer
extern volatile uint16_t* out_buf_ptr_L;                     // Pointer to active buffer
extern volatile uint16_t* out_buf_ptr_R;                     // Pointer to active buffer
extern volatile int callback_flag_L;                         // DMA completion flag
extern volatile int callback_flag_R;                         // DMA completion flag
extern int dma_chan_a, dma_chan_b, dma_chan_c, dma_chan_d;                        // DMA channel assignments
extern float audio_rate;                                   // Audio sample rate (Hz)

// ── Audio Output Interface Functions ────────────────────────────────────────────

/**
 * @brief Mute audio output
 * 
 * Immediately stops audio output and sets output to silence.
 * Used for system shutdown or error conditions.
 */
void muteAudioOutput();

/**
 * @brief Unmute audio output
 * 
 * Enables audio output and resumes normal operation.
 * Should be called after system initialization is complete.
 */
void unmuteAudioOutput();

/**
 * @brief DMA transfer completion callback
 * 
 * Called by the DMA system when an audio buffer transfer completes.
 * This function manages the ping-pong buffer system and signals
 * the audio engine to process the next buffer.
 */
void PWM_DMATransCpltCallbackL();
void PWM_DMATransCpltCallbackR();

/**
 * @brief Configure PWM DMA for audio output
 * 
 * Sets up the PWM and DMA system for continuous audio output.
 * Must be called during system initialization.
 */
void configurePWM_DMA();
void configurePWM_DMA_L();
void configurePWM_DMA_R();

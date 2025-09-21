/**
 * @file audio_engine.h
 * @brief Audio engine interface for XYLEM synthesizer
 * 
 * This header provides the interface for the audio engine, which generates
 * audio samples and fills the PWM output buffers for continuous audio playback.
 * The audio engine operates in real-time and must complete processing within
 * the audio buffer time constraints.
 * 
 * ## Audio Engine Architecture
 * 
 * **Block Processing**: Processes audio in blocks of AUDIO_BLOCK_SIZE samples
 * **Real-time Performance**: Optimized for low-latency audio generation
 * **Dual Channel**: Supports stereo audio output
 * **PWM Integration**: Directly fills PWM output buffers
 * **Context Integration**: Uses context parameters for musical control
 * 
 * ## Audio Processing Pipeline
 * 
 * 1. **Audio Generation**: Synthesize audio samples based on context parameters
 * 2. **Effect Processing**: Apply context-based effects (consonance, precision, pace, density)
 * 3. **PWM Conversion**: Convert audio samples to PWM duty cycles
 * 4. **Buffer Filling**: Fill DMA buffers for continuous output
 * 
 * ## Context Parameter Integration
 * 
 * The audio engine uses the following context parameters:
 * - **Consonance**: Controls harmonic content and frequency relationships
 * - **Precision**: Affects sample accuracy and bit depth
 * - **Pace**: Influences tempo-related effects and modulation
 * - **Density**: Controls note/event density and amplitude
 * - **Root Note**: Sets the musical root for harmonic relationships
 * 
 * @author Brian Varren
 * @version 1.0
 * @date 2024
 */

#pragma once
#include <cstdint>

// ── Audio Engine Configuration ────────────────────────────────────────────────────
#define AUDIO_ENGINE_BLOCK_SIZE     128         // Audio block size (samples)
#define AUDIO_ENGINE_CENTER_VALUE   2047        // Center value for PWM (50% duty cycle)

// ── Audio Engine Interface Functions ──────────────────────────────────────────────

/**
 * @brief Initialize the audio engine
 * 
 * Sets up the audio engine for operation. Must be called before any
 * audio processing functions.
 * 
 * @return true if initialization successful, false otherwise
 */
bool audioEngineInit();

/**
 * @brief Render a block of audio samples
 * 
 * Generates audio samples and fills the specified PWM output buffer.
 * This function is called by the DMA callback system when a new
 * audio buffer is needed.
 * 
 * @param out_buf Pointer to PWM output buffer
 * @param block_size Number of samples to render
 */
void ae_render_block(volatile uint16_t* out_buf, uint16_t block_size);

/**
 * @brief Get current audio engine status
 * 
 * @return true if audio engine is running, false if stopped
 */
bool audioEngineIsRunning();

/**
 * @brief Start audio engine processing
 * 
 * Enables audio generation and processing. Audio will be generated
 * based on current context parameters.
 */
void audioEngineStart();

/**
 * @brief Stop audio engine processing
 * 
 * Disables audio generation. Output will be silence (center PWM value).
 */
void audioEngineStop();

/**
 * @brief Process audio callback from DACless system
 * 
 * This function should be called from the main loop to check for
 * audio buffer completion and render new audio when needed.
 */
void audioEngineProcessCallback();
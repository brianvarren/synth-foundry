/**
 * @file audio_engine_render.cpp
 * @brief Audio rendering engine for XYLEM synthesizer
 * 
 * Simplified architecture providing basic audio generation with context parameter
 * integration. Designed for real-time audio processing with minimal CPU overhead.
 * 
 * Key Concepts:
 * - Block-based processing: Renders complete audio blocks for DMA compatibility
 * - Context parameter integration: Uses consonance, precision, pace, density, root_note
 * - PWM output: Converts audio samples to PWM duty cycles for analog output
 * - Center value output: Currently outputs silence (center PWM value) for testing
 * 
 * @author Brian Varren
 * @version 1.0
 * @date 2024
 */

#include <stdint.h>
#include <math.h>
#include <string.h>
#include "audio_engine.h"
#include "DACless.h"
#include "context_params.h"
#include "tables.h"
#include "synth_voice.h"
#include <Arduino.h>

// Get audio rate from DACless system
extern float audio_rate;

// DACless system integration
extern volatile uint16_t* out_buf_ptr;
extern volatile int callback_flag;

// ── Audio Engine State ────────────────────────────────────────────────────────────
static bool audio_engine_running = false;
static bool audio_engine_initialized = false;

// Context parameter integration
extern ContextParams contextParams;

// ── Voice Management ──────────────────────────────────────────────────────────────
// Global voice for testing (will be expanded to voice array later)
Voice v0 = {0, 0, 0, 0, 0};

// ── Helper Functions ──────────────────────────────────────────────────────────────

/**
 * @brief Convert Q15 signed sample to unsigned PWM value
 * 
 * Converts 16-bit signed audio sample (-32768 to +32767) to PWM duty cycle
 * (0 to PWM_RESOLUTION-1) for hardware output.
 * 
 * @param sample Q15 signed sample (-32768 to +32767)
 * @return PWM duty cycle value (0 to PWM_RESOLUTION-1)
 */
static inline uint16_t q15_to_pwm_u(int16_t sample) {
    uint32_t u = ((uint16_t)sample) ^ 0x8000u;  // Convert signed to unsigned (flip MSB)
    return (uint16_t)((u * (PWM_RESOLUTION - 1u)) >> 16);  // Scale to PWM range
}

/**
 * @brief Generate silence sample
 * 
 * Returns a sample that represents silence (center PWM value).
 * Used when no audio is being generated.
 * 
 * @return Silence sample (0 for Q15, center value for PWM)
 */
static inline int16_t generate_silence() {
    return 0;  // Q15 silence (center value)
}


/**
 * @brief Apply context parameter effects
 * 
 * Applies various effects based on context parameters to modify the audio sample.
 * This is where the musical intelligence of the system is implemented.
 * Uses fixed-point arithmetic for efficiency.
 * 
 * @param sample Input sample to process
 * @return Processed sample with context effects applied
 */
static inline int16_t apply_context_effects(int16_t sample) {
    // Use precision parameter to control sample accuracy
    // Higher precision = more accurate reproduction
    // Convert to Q15: precision/255 * 32767
    int32_t precision_factor = ((int32_t)contextParams.precision * 32767) / 255;
    
    // Use pace parameter to control tempo-related effects
    // This could be used for rhythmic modulation or time-based effects
    int32_t pace_factor = ((int32_t)contextParams.pace * 32767) / 255;
    
    // Apply precision-based quantization (simulate bit depth reduction)
    if (precision_factor < 32767) {
        int32_t quantized = ((int32_t)sample * precision_factor) >> 15;
        sample = (int16_t)quantized;
    }
    
    // Apply pace-based modulation (simple tremolo effect)
    // Use fixed-point phase accumulator for tremolo
    static uint32_t tremolo_phase = 0;
    uint32_t tremolo_inc = (pace_factor >> 8) + 1;  // Scale tremolo rate
    tremolo_phase += tremolo_inc;
    
    // Generate tremolo using sine LUT
    int16_t tremolo = interp_sine_q15(tremolo_phase);
    tremolo = (int16_t)((tremolo * pace_factor) >> 15);  // Scale tremolo depth
    tremolo = 32767 + (tremolo >> 3);  // Center around 1.0, reduce depth
    
    // Apply tremolo: sample * tremolo / 32767
    int32_t result = ((int32_t)sample * tremolo) >> 15;
    sample = (int16_t)result;
    
    return sample;
}

/**
 * @brief Initialize voice with frequency and amplitude
 * 
 * Sets up a voice with the specified frequency and amplitude using fixed-point arithmetic.
 * 
 * @param voice Pointer to voice to initialize
 * @param freq_hz Frequency in Hz
 * @param amplitude Q15 amplitude (0 to 32767)
 */
static void init_voice(Voice* voice, float freq_hz, int16_t amplitude) {
    if (!voice) return;
    
    // Calculate phase increment: freq_hz * 2^32 / sample_rate
    // Using 32-bit phase accumulator for high precision
    uint32_t inc = (uint32_t)((freq_hz * 4294967296.0f) / audio_rate);
    
    voice->phase = 0;
    voice->inc = inc;
    voice->amp_cur = amplitude;
    voice->env = 32767;  // Full envelope for now
    voice->active = 1;
}

// ── Main Render Function ──────────────────────────────────────────────────────────

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
void ae_render_block(volatile uint16_t* out_buf, uint16_t block_size) {
    // Safety check
    if (!audio_engine_initialized || !out_buf) {
        return;
    }
    
    // Early exit if audio engine is not running
    if (!audio_engine_running) {
        const uint16_t silence_pwm = AUDIO_ENGINE_CENTER_VALUE;
        for (uint16_t i = 0; i < block_size; i++) {
            out_buf[i] = silence_pwm;
        }
        return;
    }
    
    // ── Generate Audio Samples ──────────────────────────────────────────────────
    // Main audio generation loop - processes one sample at a time
    
    for (uint16_t n = 0; n < block_size; n++) {
        // Generate base audio sample using fixed-point voice
        int16_t sample = 0;
        
        if (v0.active) {
            // Generate sine wave using LUT
            sample = interp_sine_q15(v0.phase);
            
            // Apply amplitude scaling
            sample = (int16_t)(((int32_t)sample * v0.amp_cur) >> 15);
            
            // Update phase accumulator
            v0.phase += v0.inc;
        }
        
        // Apply context parameter effects
        sample = apply_context_effects(sample);
        
        // Convert to PWM value
        uint16_t pwm = q15_to_pwm_u(sample);
        
        // Store in output buffer
        out_buf[n] = pwm;
    }
}

// ── Audio Engine Control Functions ────────────────────────────────────────────────

/**
 * @brief Initialize the audio engine
 * 
 * Sets up the audio engine for operation. Must be called before any
 * audio processing functions.
 * 
 * @return true if initialization successful, false otherwise
 */
bool audioEngineInit() {
    if (audio_engine_initialized) {
        return true; // Already initialized
    }
    
    // Initialize audio engine state
    audio_engine_running = false;
    audio_engine_initialized = true;
    
    // Print initialization message
    Serial.println("Audio engine initialized successfully!");
    Serial.printf("Audio rate: %.1f Hz\n", audio_rate);
    
    return true;
}

/**
 * @brief Get current audio engine status
 * 
 * @return true if audio engine is running, false if stopped
 */
bool audioEngineIsRunning() {
    return audio_engine_running;
}

/**
 * @brief Start audio engine processing
 * 
 * Enables audio generation and processing. Audio will be generated
 * based on current context parameters.
 */
void audioEngineStart() {
    if (!audio_engine_initialized) {
        Serial.println("ERROR: Audio engine not initialized!");
        return;
    }
    
    // Initialize voice with test tone (440 Hz A4, moderate amplitude)
    init_voice(&v0, 440.0f, 16383);  // Half amplitude for safety
    
    audio_engine_running = true;
    Serial.println("Audio engine started with test tone (440 Hz)");
}

/**
 * @brief Stop audio engine processing
 * 
 * Disables audio generation. Output will be silence (center PWM value).
 */
void audioEngineStop() {
    // Deactivate voice
    v0.active = 0;
    
    audio_engine_running = false;
    Serial.println("Audio engine stopped");
}

/**
 * @brief Process audio callback from DACless system
 * 
 * This function should be called from the main loop to check for
 * audio buffer completion and render new audio when needed.
 */
void audioEngineProcessCallback() {
    // Check if DACless system has completed a buffer transfer
    if (callback_flag) {
        callback_flag = 0;  // Clear the flag
        
        // Render audio to the current buffer
        if (out_buf_ptr) {
            ae_render_block(out_buf_ptr, AUDIO_BLOCK_SIZE);
        }
    }
}
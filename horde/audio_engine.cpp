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

#include "HexGlyphHarmony.h" // now using runtime-param APIs

#include <math.h>

// ── Audio Engine State ──────────────────────────────────────────────────────
static constexpr int kVoiceCount = 6;

static ResonantBandpass2P g_noise_filters[kVoiceCount];
static uint16_t g_voice_gain_q15[kVoiceCount];
static int g_active_voice_count = 0;
static int16_t g_voice_gain_norm_q15 = 32767;
static uint32_t g_noise_state = 12345;  // PRNG state for noise generation
static int g_feedback_update_voice = 0;
static float g_noise_filter_cutoff_hz = 110.0f;
static float g_noise_filter_feedback = 0.98f;
static bool g_noise_filter_inited = false;
static volatile bool g_noise_filter_params_dirty = true;
static float g_prev_voice_cutoff_hz[kVoiceCount] = {0};

// ── Glyph Playlist ───────────────────────────────────────────────────────────
typedef struct {
    unsigned mask;         // 12-bit glyph mask (intervals relative to root)
    const char* name;      // chord label for UI
    int root_semitone;     // root note semitone offset (0=C, 1=C#, 2=D, etc.)
} glyph_entry_t;

static const glyph_entry_t g_glyphs[] = {
    { 0x089u, "Dm",       2 },      // D minor: 0,3,7 with root D
    { 0x091u, "Bb",       10 },     // Bb major: 0,4,7 with root Bb
    { 0x091u, "G",        7 }       // G major: 0,4,7 with root G (Dorian color via B natural)
};
static const int kGlyphCount = (int)(sizeof(g_glyphs)/sizeof(g_glyphs[0]));
static volatile int g_current_glyph = 0;
static volatile int g_octave_shift = 0;  // -2 to +2 octaves
static int g_prev_applied_octave_shift = 0;

const char* ae_current_glyph_name(){
    if (g_current_glyph < 0 || g_current_glyph >= kGlyphCount) return "";
    return g_glyphs[g_current_glyph].name;
}

void ae_next_glyph(){
    g_current_glyph = (g_current_glyph + 1) % kGlyphCount;
    g_noise_filter_params_dirty = true;
}

int ae_get_octave_shift(){
    return g_octave_shift;
}

void ae_set_octave_shift(int octaves){
    if (octaves < -2) octaves = -2;
    if (octaves > 2) octaves = 2;
    g_octave_shift = octaves;
    g_noise_filter_params_dirty = true;
}

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
    g_noise_state = g_noise_state * 1103515245UL + 12345UL;
    return (int16_t)((g_noise_state >> 16) ^ 0x8000);
}

static void ae_update_noise_filter() {
    if (!g_noise_filter_inited) {
        for (int voice = 0; voice < kVoiceCount; ++voice) {
            resonant_bandpass_init(&g_noise_filters[voice]);
        }
        g_noise_filter_inited = true;
        g_noise_filter_params_dirty = true;
    }

    if (g_noise_filter_params_dirty) {
        float voice_freqs[kVoiceCount];
        const glyph_entry_t ge = g_glyphs[g_current_glyph];
        
        // Apply root transposition to the base frequency
        float root_freq = g_noise_filter_cutoff_hz;
        if (ge.root_semitone != 0) {
            root_freq *= powf(2.0f, (float)ge.root_semitone / 12.0f);
        }
        
        // Apply octave shift
        if (g_octave_shift != 0) {
            root_freq *= powf(2.0f, (float)g_octave_shift);
        }
        
        unsigned char voice_count = HexGlyphHarmony_compute_from(
            root_freq,
            ge.mask,
            (unsigned char)kVoiceCount,
            0, /* no additional rotation - root is already applied */
            1, /* include root */
            1, /* spread octaves */
            voice_freqs);
        if (voice_count == 0) {
            voice_freqs[0] = g_noise_filter_cutoff_hz;
            voice_count = 1;
        }
        if (voice_count > kVoiceCount) {
            voice_count = kVoiceCount;
        }
        g_active_voice_count = voice_count;

        // ── Smooth Voice Leading: reorder to minimize movement ───────────────
        // Strategy: for each previous voice target, choose nearest new pitch
        // allowing octave shifts in [-2, +2], and avoid reusing the same base
        // candidate more than necessary.
        // IMPORTANT: if octave shift changed, skip reordering so transposition
        // is audibly applied and not cancelled by octave wrapping.
        bool have_prev = (g_prev_voice_cutoff_hz[0] > 0.0f || g_prev_voice_cutoff_hz[1] > 0.0f ||
            g_prev_voice_cutoff_hz[2] > 0.0f || g_prev_voice_cutoff_hz[3] > 0.0f ||
            g_prev_voice_cutoff_hz[4] > 0.0f || g_prev_voice_cutoff_hz[5] > 0.0f);
        if (have_prev && g_octave_shift == g_prev_applied_octave_shift) {

            float reordered[kVoiceCount];
            for (int i = 0; i < kVoiceCount; ++i) reordered[i] = 0.0f;
            bool base_taken[6] = {false,false,false,false,false,false};

            auto cents_distance = [](float a, float b) -> float {
                if (!(a > 0.0f) || !(b > 0.0f)) return 1.0e9f;
                float ratio = a / b;
                if (ratio < 1.0f) ratio = 1.0f / ratio;
                return 1200.0f * log2f(ratio);
            };

            for (int v = 0; v < g_active_voice_count; ++v) {
                float prev = g_prev_voice_cutoff_hz[v];
                if (!(prev > 0.0f)) {
                    continue; // will fill later
                }
                float best_freq = 0.0f;
                float best_cost = 1.0e9f;
                int best_base = -1;
                for (int b = 0; b < g_active_voice_count; ++b) {
                    if (base_taken[b]) continue;
                    float base = voice_freqs[b];
                    for (int oct = -2; oct <= 2; ++oct) {
                        float cand = base * powf(2.0f, (float)oct);
                        if (cand < 20.0f || cand > 8192.0f) continue;
                        float cost = cents_distance(cand, prev);
                        if (cost < best_cost) {
                            best_cost = cost;
                            best_freq = cand;
                            best_base = b;
                        }
                    }
                }
                if (best_base >= 0) {
                    base_taken[best_base] = true;
                    reordered[v] = best_freq;
                }
            }

            // Assign remaining voices in ascending order to unused bases
            for (int v = 0; v < g_active_voice_count; ++v) {
                if (reordered[v] > 0.0f) continue;
                for (int b = 0; b < g_active_voice_count; ++b) {
                    if (base_taken[b]) continue;
                    reordered[v] = voice_freqs[b];
                    base_taken[b] = true;
                    break;
                }
            }

            // Copy back
            for (int v = 0; v < g_active_voice_count; ++v) {
                voice_freqs[v] = reordered[v] > 0.0f ? reordered[v] : voice_freqs[v];
            }
        }
        // Record applied octave shift for next comparison
        g_prev_applied_octave_shift = g_octave_shift;

        int32_t feedback_q15 = (int32_t)lroundf(g_noise_filter_feedback * 32767.0f);
        if (feedback_q15 < 0) feedback_q15 = 0;
        if (feedback_q15 > 32767) feedback_q15 = 32767;

        float base_cutoff = (g_noise_filter_cutoff_hz > 1.0f) ? g_noise_filter_cutoff_hz : 1.0f;
        unsigned glyph_mask = ge.mask; // for logging only
        uint32_t weight_sum = 0;

        for (int voice = 0; voice < g_active_voice_count; ++voice) {
            float voice_cutoff = voice_freqs[voice];
            if (voice_cutoff < 20.0f) {
                voice_cutoff = 20.0f;
            } else if (voice_cutoff > 8192.0f) {
                voice_cutoff = 8192.0f;
            }
            resonant_bandpass_set_cutoff(&g_noise_filters[voice], voice_cutoff, audio_rate);
            resonant_bandpass_set_feedback_q15(&g_noise_filters[voice], (int16_t)feedback_q15);

            float ratio = voice_cutoff / base_cutoff;
            if (ratio < 0.001f) {
                ratio = 0.001f;
            }
            float gain = powf(ratio, -0.5f);
            if (gain > 1.0f) {
                gain = 1.0f;
            }
            uint16_t q15 = (uint16_t)lroundf(gain * 32767.0f);
            g_voice_gain_q15[voice] = q15;
            weight_sum += q15;
            g_prev_voice_cutoff_hz[voice] = voice_cutoff;
        }

        if (Serial) {
            Serial.print(F("[Glyph] mask=0x"));
            Serial.println(glyph_mask, HEX);
            Serial.print(F("[Glyph] root semitone="));
            Serial.println(ge.root_semitone);
            Serial.print(F("[Glyph] base Hz="));
            Serial.println(g_noise_filter_cutoff_hz, 3);
            Serial.print(F("[Glyph] root Hz="));
            Serial.println(root_freq, 3);
            Serial.print(F("[Glyph] octave shift="));
            Serial.println(g_octave_shift);
            Serial.print(F("[Glyph] name="));
            Serial.println(ae_current_glyph_name());
            for (int voice = 0; voice < g_active_voice_count; ++voice) {
                Serial.print(F("  voice["));
                Serial.print(voice);
                Serial.print(F("] Hz="));
                Serial.println(voice_freqs[voice], 3);
            }
        }

        for (int voice = g_active_voice_count; voice < kVoiceCount; ++voice) {
            g_voice_gain_q15[voice] = 0;
        }

        if (weight_sum == 0) {
            weight_sum = 1;
            if (g_active_voice_count > 0) {
                g_voice_gain_q15[0] = 32767;
            }
        }

        float weight_sum_norm = (float)weight_sum / 32768.0f;
        if (weight_sum_norm < 0.03125f) { // avoid extreme boosts
            weight_sum_norm = 0.03125f;
        }
        float inv = 1.0f / weight_sum_norm;
        if (inv > 1.9990f) {
            inv = 1.9990f;
        }
        g_voice_gain_norm_q15 = (int16_t)lroundf(inv * 32767.0f);
        g_feedback_update_voice = 0;
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
        for (int voice = 0; voice < kVoiceCount; ++voice) {
            resonant_bandpass_init(&g_noise_filters[voice]);
        }
        g_noise_filter_inited = true;
        g_noise_filter_params_dirty = true;
        return;
    }

    for (int voice = 0; voice < kVoiceCount; ++voice) {
        resonant_bandpass_reset(&g_noise_filters[voice]);
        g_voice_gain_q15[voice] = 0;
    }
    g_active_voice_count = 0;
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
    float norm = (float)feedback_adc / 4095.0f;
    float shaped = norm * (2.0f - norm);   // bias knob toward higher Q region
    if (shaped < 0.0f) shaped = 0.0f;
    if (shaped > 1.0f) shaped = 1.0f;
    int32_t feedback_q15 = (int32_t)(shaped * kFeedbackMaxQ15 + 0.5f);
    for (int i = 0; i < 2; ++i) {
        int idx = (g_feedback_update_voice + i) % kVoiceCount;
        resonant_bandpass_set_feedback_q15(&g_noise_filters[idx], (int16_t)feedback_q15);
    }
    g_feedback_update_voice = (g_feedback_update_voice + 2) % kVoiceCount;
    g_noise_filter_feedback = (float)feedback_q15 / 32767.0f;

    // Generate audio samples for the current buffer
    for (int i = 0; i < AUDIO_BLOCK_SIZE; i++) {
        // Generate white noise sample in Q1.15 format
        int16_t noise_sample = generate_noise_sample();

        int32_t accum = 0;
        for (int voice = 0; voice < g_active_voice_count; ++voice) {
            int16_t voice_sample = resonant_bandpass_process(&g_noise_filters[voice], noise_sample);
            int32_t weighted = (int32_t)(((int64_t)voice_sample * (int64_t)g_voice_gain_q15[voice] + 16384) >> 15);
            accum += weighted;
        }

        if (g_active_voice_count == 0) {
            target[i] = q15_to_pwm(0);
            continue;
        }

        int32_t normalized = (int32_t)(((int64_t)accum * (int64_t)g_voice_gain_norm_q15 + 16384) >> 15);
        if (normalized > 32767) normalized = 32767;
        else if (normalized < -32768) normalized = -32768;

        target[i] = q15_to_pwm((int16_t)normalized);
    }
}

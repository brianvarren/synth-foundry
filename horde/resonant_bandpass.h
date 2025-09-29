/**
 * @file resonant_bandpass.h
 * @brief Fixed-point TPT state-variable resonant band-pass filter
 *
 * Implements a two-pole band-pass using the topology-preserving transform (TPT)
 * structure. Coefficients are computed in floating point when parameters change
 * but the hot audio path uses fixed-point math only.
 */

#pragma once

#include <stdint.h>

typedef struct {
    int64_t ic1_eq;           // Integrator state 1 (Q2.30)
    int64_t ic2_eq;           // Integrator state 2 (Q2.30)
    int32_t g_q26;            // g = tan(pi*fc/fs) (Q5.26)
    int32_t h_q30;            // h = 1 / (1 + g*(g + r)) (Q2.30)
    int32_t r_q30;            // r = 1/Q (Q2.30)

    float cutoff_hz;
    float sample_rate_hz;
    float q;

    uint16_t bp_gain_q15;     // Output gain compensation (Q1.15)
} ResonantBandpass2P;

void resonant_bandpass_init(ResonantBandpass2P* filter);
void resonant_bandpass_reset(ResonantBandpass2P* filter);
void resonant_bandpass_set_cutoff(ResonantBandpass2P* filter, float cutoff_hz, float sample_rate_hz);
void resonant_bandpass_set_feedback(ResonantBandpass2P* filter, float feedback);
void resonant_bandpass_set_feedback_q15(ResonantBandpass2P* filter, int16_t feedback_q15);

static inline int16_t resonant_bandpass_process(ResonantBandpass2P* filter, int16_t input) {
    const int64_t kStateLimit = (int64_t)1 << 33; // Q2.30 guard

    int64_t x_q30 = ((int64_t)input) << 15; // Q1.15 -> Q2.30

    int64_t ic1 = filter->ic1_eq;
    int64_t ic2 = filter->ic2_eq;

    int64_t r_ic1 = (((int64_t)filter->r_q30 * ic1) >> 30);
    int64_t hp_num = x_q30 - ic2 - r_ic1;
    if (hp_num > kStateLimit) hp_num = kStateLimit;
    else if (hp_num < -kStateLimit) hp_num = -kStateLimit;

    int64_t hp_q30 = (((int64_t)filter->h_q30 * hp_num) >> 30);
    if (hp_q30 > kStateLimit) hp_q30 = kStateLimit;
    else if (hp_q30 < -kStateLimit) hp_q30 = -kStateLimit;

    int64_t g_hp = (((int64_t)filter->g_q26 * hp_q30) >> 26);
    if (g_hp > kStateLimit) g_hp = kStateLimit;
    else if (g_hp < -kStateLimit) g_hp = -kStateLimit;

    int64_t bp_q30 = g_hp + ic1;
    if (bp_q30 > kStateLimit) bp_q30 = kStateLimit;
    else if (bp_q30 < -kStateLimit) bp_q30 = -kStateLimit;

    int64_t g_bp = (((int64_t)filter->g_q26 * bp_q30) >> 26);
    if (g_bp > kStateLimit) g_bp = kStateLimit;
    else if (g_bp < -kStateLimit) g_bp = -kStateLimit;

    int64_t lp_q30 = g_bp + ic2;
    if (lp_q30 > kStateLimit) lp_q30 = kStateLimit;
    else if (lp_q30 < -kStateLimit) lp_q30 = -kStateLimit;

    int64_t ic1_new = bp_q30 + g_hp;
    if (ic1_new > kStateLimit) ic1_new = kStateLimit;
    else if (ic1_new < -kStateLimit) ic1_new = -kStateLimit;

    int64_t ic2_new = lp_q30 + g_bp;
    if (ic2_new > kStateLimit) ic2_new = kStateLimit;
    else if (ic2_new < -kStateLimit) ic2_new = -kStateLimit;

    filter->ic1_eq = ic1_new;
    filter->ic2_eq = ic2_new;

    int32_t y_q15 = (int32_t)((bp_q30 + (1LL << 14)) >> 15);
    int64_t scaled64 = (int64_t)y_q15 * (int64_t)filter->bp_gain_q15 + (1LL << 14);
    int32_t scaled = (int32_t)(scaled64 >> 15);
    if (scaled > 32767) scaled = 32767;
    else if (scaled < -32768) scaled = -32768;

    return (int16_t)scaled;
}

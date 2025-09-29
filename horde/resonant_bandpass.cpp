#include "resonant_bandpass.h"
#include "resonant_bandpass_gain_table.h"

#include <math.h>
#include <limits.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace {

constexpr float kDefaultSampleRateHz = 48000.0f;
constexpr float kDefaultCutoffHz     = 440.0f;
constexpr float kMinCutoffHz         = 1.0f;
constexpr float kMaxCutoffRatio      = 0.49f;
constexpr float kMinQ                = 0.10f;
constexpr float kMaxQ                = 120.0f;
const float kLogMinQ = logf(kMinQ);
const float kLogMaxQ = logf(kMaxQ);

static inline int32_t float_to_q30(float value) {
    if (value > 1.999999f) {
        value = 1.999999f;
    } else if (value < -1.999999f) {
        value = -1.999999f;
    }
    return (int32_t)lroundf(value * 1073741824.0f); // 1 << 30
}

static inline int32_t float_to_q26(float value) {
    if (value > 31.99999f) {
        value = 31.99999f;
    } else if (value < -31.99999f) {
        value = -31.99999f;
    }
    return (int32_t)lroundf(value * 67108864.0f); // 1 << 26
}

static inline float q26_to_float(int32_t value) {
    return (float)value / 67108864.0f;
}

static inline float q30_to_float(int32_t value) {
    return (float)value / 1073741824.0f;
}

static uint32_t g_feedback_r_lut_q30[256];
static float g_feedback_q_lut[256];
static bool g_feedback_lut_initialized = false;

static void init_feedback_lut() {
    if (g_feedback_lut_initialized) {
        return;
    }
    for (int i = 0; i < 256; ++i) {
        float feedback = (float)i / 255.0f;
        float q = expf(kLogMinQ + feedback * (kLogMaxQ - kLogMinQ));
        float r = 1.0f / q;
        g_feedback_q_lut[i] = q;
        g_feedback_r_lut_q30[i] = float_to_q30(r);
    }
    g_feedback_lut_initialized = true;
}

static inline int freq_to_index(float fc) {
    if (fc <= kBpGainFreqs[0]) return 0;
    if (fc >= kBpGainFreqs[kBpGainFreqBins - 1]) return kBpGainFreqBins - 1;
    for (int i = 0; i < kBpGainFreqBins - 1; ++i) {
        float midpoint = 0.5f * (kBpGainFreqs[i] + kBpGainFreqs[i + 1]);
        if (fc < midpoint) {
            return i;
        }
    }
    return kBpGainFreqBins - 1;
}

static inline int q_to_index(float q) {
    if (q <= kBpGainQs[0]) return 0;
    if (q >= kBpGainQs[kBpGainQBins - 1]) return kBpGainQBins - 1;
    for (int i = 0; i < kBpGainQBins - 1; ++i) {
        float midpoint = 0.5f * (kBpGainQs[i] + kBpGainQs[i + 1]);
        if (q < midpoint) {
            return i;
        }
    }
    return kBpGainQBins - 1;
}

static inline uint16_t lookup_bp_gain(float cutoff_hz, float q) {
    int f_idx = freq_to_index(cutoff_hz);
    int q_idx = q_to_index(q);
    return kBpGainTable[q_idx][f_idx];
}

static void rebuild_coefficients(ResonantBandpass2P* filter) {
    if (!filter) {
        return;
    }

    float sr = (filter->sample_rate_hz > 1.0f) ? filter->sample_rate_hz : kDefaultSampleRateHz;
    float fc = filter->cutoff_hz;
    if (fc < kMinCutoffHz) {
        fc = kMinCutoffHz;
    }
    float max_cutoff = sr * kMaxCutoffRatio * 0.5f;
    if (fc > max_cutoff) {
        fc = max_cutoff;
    }

    float q = filter->q;
    if (q < kMinQ) {
        q = kMinQ;
    } else if (q > kMaxQ) {
        q = kMaxQ;
    }

    float g = tanf((float)M_PI * fc / sr);
    if (!isfinite(g)) {
        g = 0.0f;
    }
    const float r = 1.0f / q;
    const float denom = 1.0f + g * (g + r);
    const float h = 1.0f / denom;

    filter->h_q30 = float_to_q30(h);
    filter->r_q30 = float_to_q30(r);
    filter->g_q26 = float_to_q26(g);
}

} // namespace

void resonant_bandpass_init(ResonantBandpass2P* filter) {
    if (!filter) {
        return;
    }
    init_feedback_lut();
    filter->ic1_eq = 0;
    filter->ic2_eq = 0;
    filter->q = 5.0f;
    filter->cutoff_hz = kDefaultCutoffHz;
    filter->sample_rate_hz = kDefaultSampleRateHz;
    rebuild_coefficients(filter);
    filter->bp_gain_q15 = lookup_bp_gain(filter->cutoff_hz, filter->q);
}

void resonant_bandpass_reset(ResonantBandpass2P* filter) {
    if (!filter) {
        return;
    }
    filter->ic1_eq = 0;
    filter->ic2_eq = 0;
    filter->bp_gain_q15 = lookup_bp_gain(filter->cutoff_hz, filter->q);
}

void resonant_bandpass_set_cutoff(ResonantBandpass2P* filter, float cutoff_hz, float sample_rate_hz) {
    if (!filter || sample_rate_hz <= 0.0f) {
        return;
    }

    if (cutoff_hz <= 0.0f) {
        filter->cutoff_hz = 0.0f;
        filter->sample_rate_hz = sample_rate_hz;
        filter->g_q26 = 0;
        filter->h_q30 = 0;
        filter->r_q30 = 0;
        filter->bp_gain_q15 = lookup_bp_gain(kBpGainFreqs[0], filter->q);
        return;
    }

    filter->cutoff_hz = cutoff_hz;
    filter->sample_rate_hz = sample_rate_hz;

    rebuild_coefficients(filter);
    filter->bp_gain_q15 = lookup_bp_gain(filter->cutoff_hz, filter->q);
}

void resonant_bandpass_set_feedback(ResonantBandpass2P* filter, float feedback) {
    if (!filter) {
        return;
    }

    if (feedback < 0.0f) {
        feedback = 0.0f;
    } else if (feedback > 0.9995f) {
        feedback = 0.9995f;
    }
    int32_t feedback_q15 = (int32_t)lroundf(feedback * 32767.0f);
    if (feedback_q15 < 0) feedback_q15 = 0;
    if (feedback_q15 > 32767) feedback_q15 = 32767;
    resonant_bandpass_set_feedback_q15(filter, (int16_t)feedback_q15);
}

void resonant_bandpass_set_feedback_q15(ResonantBandpass2P* filter, int16_t feedback_q15) {
    if (!filter) {
        return;
    }
    if (!g_feedback_lut_initialized) {
        init_feedback_lut();
    }

    if (feedback_q15 < 0) feedback_q15 = 0;
    if (feedback_q15 > 32767) feedback_q15 = 32767;

    int index = feedback_q15 >> 7;  // 0..255
    if (index > 255) index = 255;

    filter->q = g_feedback_q_lut[index];
    filter->r_q30 = g_feedback_r_lut_q30[index];

    float g = q26_to_float(filter->g_q26);
    float r = q30_to_float(filter->r_q30);
    float denom = 1.0f + g * (g + r);
    if (denom <= 0.0f) {
        denom = 1.0f;
    }
    filter->h_q30 = float_to_q30(1.0f / denom);

    filter->bp_gain_q15 = lookup_bp_gain(filter->cutoff_hz, filter->q);
}

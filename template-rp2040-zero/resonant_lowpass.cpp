#include "resonant_lowpass.h"
#include <math.h>

static inline int16_t float_to_q15(float value) {
    if (value >= 0.999969f) return 32766;  // prevent overflow when multiplying
    if (value <= -1.0f)     return -32768;
    return (int16_t)lroundf(value * 32768.0f);
}

void resonant_lowpass_init(ResonantLowpass2P* filter) {
    if (!filter) {
        return;
    }
    filter->stage1 = 0;
    filter->stage2 = 0;
    filter->g_q15 = 0;
    filter->feedback_q15 = 0;
}

void resonant_lowpass_reset(ResonantLowpass2P* filter) {
    if (!filter) {
        return;
    }
    filter->stage1 = 0;
    filter->stage2 = 0;
}

void resonant_lowpass_set_cutoff(ResonantLowpass2P* filter, float cutoff_hz, float sample_rate_hz) {
    if (!filter || sample_rate_hz <= 0.0f) {
        return;
    }

    if (cutoff_hz < 0.0f) {
        cutoff_hz = 0.0f;
    }

    const float nyquist = sample_rate_hz * 0.5f;
    if (cutoff_hz > nyquist * 0.99f) {
        cutoff_hz = nyquist * 0.99f;
    }

    if (cutoff_hz == 0.0f) {
        filter->g_q15 = 0;
        return;
    }

    const float two_pi = 6.28318530718f;
    float exponent = -two_pi * cutoff_hz / sample_rate_hz;
    float g = 1.0f - expf(exponent);
    if (g < 0.0000305f) {
        g = 0.0000305f; // about 1 LSB in Q15
    }
    if (g > 0.9995f) {
        g = 0.9995f;
    }

    filter->g_q15 = float_to_q15(g);
}

void resonant_lowpass_set_feedback(ResonantLowpass2P* filter, float feedback) {
    if (!filter) {
        return;
    }

    if (feedback < 0.0f) {
        feedback = 0.0f;
    }
    if (feedback > 0.9995f) {
        feedback = 0.9995f;
    }

    filter->feedback_q15 = float_to_q15(feedback);
}


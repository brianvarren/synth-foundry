#include <stdint.h>
#include <math.h>
#include <string.h>
#include "ADCless.h"
#include "adc_filter.h"
#include "audio_engine.h"
#include "pico_interp.h"
#include "sf_globals_bridge.h"
#include "config_pins.h"
#include <hardware/pwm.h>
#include <hardware/gpio.h>
#include <Arduino.h>

// Forward declaration
void ae_render_block(const int16_t* samples,
                     uint32_t total_samples,
                     ae_state_t engine_state,
                     volatile int64_t* io_phase_q32_32);  // SIGNED

// ── Transport state ──────────────────────────────────────────────────
static volatile ae_state_t s_state = AE_STATE_IDLE;
static volatile ae_mode_t  s_mode  = AE_MODE_FORWARD;

// ── Phase state (SIGNED for through-zero FM) ────────────────────────
volatile int64_t g_phase_q32_32 = 0;  // SIGNED phase accumulator
uint64_t g_inc_base_q32_32 = (1ULL << 32);  // Base increment (always positive)
int64_t g_inc_q32_32 = (1LL << 32);  // Current signed increment
int32_t g_pm_scale_q16_16 = 0;  // Unused
volatile uint16_t g_playhead_norm_u16 = 0;  // Unused but kept

// ── Reset trigger and LED state ─────────────────────────────────────
volatile bool g_reset_trigger_pending = false;
static bool s_reset_trigger_last_state = false;
static bool s_loop_led_state = false;
static uint32_t s_loop_led_off_time = 0;
static const uint32_t LOOP_LED_BLINK_MS = 10;

// ── Sample buffer state ──────────────────────────────────────────────
static const int16_t* g_samples_q15 = nullptr;
static uint32_t g_total_samples = 0;
static const uint32_t MIN_LOOP_LEN_CONST = 64;
static uint32_t g_span_start = 0;
static uint32_t g_span_len = 0;

void audio_engine_set_mode(ae_mode_t m) {
    s_mode = m;
}

void audio_engine_arm(bool armed) {
    s_state = armed ? AE_STATE_READY : AE_STATE_IDLE;
}

void audio_engine_play(bool play) {
    if (s_state == AE_STATE_IDLE) return;
    s_state = play ? AE_STATE_PLAYING : AE_STATE_PAUSED;
}

ae_state_t audio_engine_get_state(void) { return s_state; }
ae_mode_t  audio_engine_get_mode(void) { return s_mode; }

static inline void loop_mapper_recalc_spans() {
    uint32_t total = g_total_samples;
    uint32_t minlen = (MIN_LOOP_LEN_CONST < total) ? MIN_LOOP_LEN_CONST : (total ? total : 1);
    g_span_start = (total > minlen) ? (total - minlen) : 0;
    g_span_len   = (total > minlen) ? (total - minlen) : 0;
}

void playback_bind_loaded_buffer(uint32_t src_sample_rate_hz,
                                 uint32_t out_sample_rate_hz,
                                 uint32_t sample_count)
{
    g_samples_q15   = reinterpret_cast<const int16_t*>(sf::audioData);
    g_total_samples = sample_count;
    g_inc_base_q32_32 = (uint64_t)(((uint64_t)src_sample_rate_hz << 32) / (uint64_t)out_sample_rate_hz);
    loop_mapper_recalc_spans();
}

void audio_init(void) {
    const uint16_t silence_pwm = PWM_RESOLUTION / 2;
    for (int i = 0; i < AUDIO_BLOCK_SIZE; i++) {
        pwm_out_buf_a[i] = silence_pwm;
        pwm_out_buf_b[i] = silence_pwm;
        pwm_out_buf_c[i] = silence_pwm;
        pwm_out_buf_d[i] = silence_pwm;
    }
    
    configurePWM_DMA_L();
    configurePWM_DMA_R();
    unmuteAudioOutput();
    setupInterpolators();
}

void audio_tick(void) {
    if (callback_flag_L > 0 || callback_flag_R > 0) {
        adc_filter_update_from_dma();
        ae_render_block(g_samples_q15, g_total_samples, s_state, &g_phase_q32_32);
        callback_flag_L = 0;
        callback_flag_R = 0;
    }
}

void process() {
    // Delegated to audio_tick
}

// ── Reset Trigger Functions ─────────────────────────────────────────
void audio_engine_reset_trigger_init(void) {
    gpio_init(RESET_TRIGGER_PIN);
    gpio_set_dir(RESET_TRIGGER_PIN, GPIO_IN);
    gpio_pull_down(RESET_TRIGGER_PIN);
    s_reset_trigger_last_state = gpio_get(RESET_TRIGGER_PIN);
    g_reset_trigger_pending = false;
}

void audio_engine_reset_trigger_poll(void) {
    bool current_state = gpio_get(RESET_TRIGGER_PIN);
    if (!s_reset_trigger_last_state && current_state) {
        g_reset_trigger_pending = true;
    }
    s_reset_trigger_last_state = current_state;
}

// ── Loop LED Functions ───────────────────────────────────────────────
void audio_engine_loop_led_init(void) {
    gpio_init(LOOP_LED_PIN);
    gpio_set_dir(LOOP_LED_PIN, GPIO_OUT);
    gpio_put(LOOP_LED_PIN, 0);
    s_loop_led_state = false;
    s_loop_led_off_time = 0;
}

void audio_engine_loop_led_update(void) {
    if (s_loop_led_state && millis() >= s_loop_led_off_time) {
        gpio_put(LOOP_LED_PIN, 0);
        s_loop_led_state = false;
    }
}

void audio_engine_loop_led_blink(void) {
    gpio_put(LOOP_LED_PIN, 1);
    s_loop_led_state = true;
    s_loop_led_off_time = millis() + LOOP_LED_BLINK_MS;
}
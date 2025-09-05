#include <stdint.h>
#include <math.h>
#include <string.h>
#include "ADCless.h"
#include "adc_filter.h"
#include "audio_engine.h"
#include "sf_globals_bridge.h"
#include <hardware/pwm.h>
#include <Arduino.h>  // Add for Serial

// Debug state
static volatile ae_dbg_snapshot_t s_last_snap = {0};
static volatile ae_dbg_level_t    s_dbg_level = AE_DBG_ERR;
static uint32_t s_dbg_last_print_ms = 0;
static uint32_t s_dbg_change_counter = 0; // for TRACE throttling

void audio_engine_debug_set_level(ae_dbg_level_t lvl){ s_dbg_level = lvl; }
ae_dbg_level_t audio_engine_debug_get_level(void){ return s_dbg_level; }

// Publish the loop + transport state for the display
static inline void publish_display_state(uint16_t start_q12, uint16_t len_q12, uint32_t playhead, uint32_t total) {
    disp_write_begin();
    g_disp.loop_start_q12 = start_q12;
    g_disp.loop_len_q12   = len_q12;
    g_disp.playhead       = playhead;
    g_disp.total          = total;
    disp_write_end();
}

ae_dbg_snapshot_t audio_engine_get_last_snapshot(void){
    ae_dbg_snapshot_t tmp;
    memcpy(&tmp, (const void*)&s_last_snap, sizeof(tmp));
    return tmp;
}

// ── Debug helper functions ──────────────────────────────────────────────────
static void dbg_print_snapshot(const ae_dbg_snapshot_t& s) {
  Serial.print(F("[AE] st="));  Serial.print((int)s.state);
  Serial.print(F(" mode="));    Serial.print((int)s.mode);
  Serial.print(F(" dir="));     Serial.print((int)s.dir);
  Serial.print(F(" idx="));     Serial.print(s.idx);
  Serial.print(F(" loop=["));   Serial.print(s.loop_start);
  Serial.print(F(","));         Serial.print(s.loop_end);
  Serial.print(F("] inc="));    Serial.print((int32_t)s.inc_signed);
  Serial.print(F(" xf="));      Serial.print(s.xfade_len);
  Serial.print(F(" ph="));      Serial.print(s.phase_q16_16, HEX);
  Serial.print(F(" play%="));   Serial.print((uint32_t)s.playhead_u16 * 100u / 65535u);
  Serial.print(F(" diag=0x"));  Serial.println(s.diag_flags, HEX);
}

void audio_engine_debug_poll(void)
{
  const ae_dbg_level_t lvl = s_dbg_level;
  if (lvl == AE_DBG_OFF) return;

  const uint32_t now = millis();
  const uint32_t every_ms =
      (lvl == AE_DBG_ERR)   ? 250 :
      (lvl == AE_DBG_INFO)  ? 500 :
      (lvl == AE_DBG_TRACE) ? 50  : 500;

  if ((now - s_dbg_last_print_ms) < every_ms) return;
  s_dbg_last_print_ms = now;

  const ae_dbg_snapshot_t snap = audio_engine_get_last_snapshot();

  // ERROR level: only print if there's a problem preventing playback
  if (lvl == AE_DBG_ERR) {
    if (snap.diag_flags & (AE_DIAG_NO_BUFFER | AE_DIAG_STATE_NOT_PLAY | AE_DIAG_LOOP_INVALID | AE_DIAG_ZERO_INC)) {
      dbg_print_snapshot(snap);
    }
    return;
  }

  // INFO level: print once per interval, but de-noise if nothing changed
  static ae_dbg_snapshot_t prev = {0};
  bool changed =
    snap.state      != prev.state ||
    snap.mode       != prev.mode  ||
    snap.dir        != prev.dir   ||
    snap.loop_start != prev.loop_start ||
    snap.loop_end   != prev.loop_end ||
    snap.inc_signed != prev.inc_signed ||
    snap.diag_flags != prev.diag_flags;

  if (lvl == AE_DBG_INFO) {
    if (changed) {
      dbg_print_snapshot(snap);
      prev = snap;
    }
    return;
  }

  // TRACE level: always print (but still rate-limited)
  dbg_print_snapshot(snap);
  prev = snap;
}

// ── Transport/direction state ──────────────────────────────────────────────
static volatile ae_state_t s_state = AE_STATE_IDLE;
static volatile ae_mode_t  s_mode  = AE_MODE_FORWARD;
static int8_t              s_dir   = +1;     // +1 fwd, -1 rev (used for REVERSE/ALTERNATE)

// // Live playhead for UI (0..65535). 16-bit write/read is atomic on RP2-class MCUs.
// volatile uint16_t g_playhead_norm_u16 = 0;

void audio_engine_set_mode(ae_mode_t m) {
    s_mode = m;
    // sync s_dir with mode
    if (m == AE_MODE_FORWARD)  s_dir = +1;
    if (m == AE_MODE_REVERSE)  s_dir = -1;
    // AE_MODE_ALTERNATE keeps current s_dir until it hits a boundary
}

void audio_engine_arm(bool armed) {
    if (!g_phase_q32_32) { /* optional reset */ }
    s_state = armed ? AE_STATE_READY : AE_STATE_IDLE;
}

void audio_engine_play(bool play) {
    if (s_state == AE_STATE_IDLE) return;    // need a buffer first
    s_state = play ? AE_STATE_PLAYING : AE_STATE_PAUSED;
}

ae_state_t audio_engine_get_state(void){ return s_state; }
ae_mode_t  audio_engine_get_mode(void){  return s_mode;  }


// ── Phasor state ───────────────────────────────────────────────────────────
// Phase accumulator in Q32.32 (prevents phase overflow with large samples)
// Advances by inc_q each sample, wraps inside loop window.
volatile uint64_t g_phase_q32_32 = 0;


// ── Pitch base ─────────────────────────────────────────────────────────────
// Base increment in Q32.32 (unity speed = src_rate / out_rate).
// Set when you bind/load a file, or updated from UART in the future.
uint64_t g_inc_base_q32_32 = (1u << 32);   // default = 1.0 ratio


// ── Phase modulation scaling ──────────────────────────────────────────────
int64_t g_pm_scale_q32_32 = 0;             // default = no PM


static const int16_t* g_samples_q15 = nullptr;

static uint32_t g_total_samples = 0;      // set when file is bound
static uint32_t g_min_loop_len  = 64;     // safety: prevent zero/too-short loops
static uint32_t g_span_start    = 0;      // = total - min_loop_len (precomputed)
static uint32_t g_span_len      = 0;      // = total - min_loop_len (same span)


// ── Tune knob ──────────────────────────────────────────────────────────────
// 257-entry Q16.16 table for ratio = 2^(0.5 * x), x in [-1, +1]
static uint32_t kExpo1Oct_Q16[257];

static void init_expo_table_1oct() {
    for (uint32_t i = 0; i <= 256; ++i) {
        float x     = ((int32_t)i - 128) / 128.0f;   // -1..+1
        float ratio = exp2f(0.5f * x);
        kExpo1Oct_Q16[i] = (uint32_t)lrintf(ratio * 65536.0f);
    }
}

// Control-rate lookup with linear interp (integer)
static inline uint32_t inc_from_adc_expo_LUT(uint16_t adc12, uint32_t base_inc_q16_16) {
    // Map 0..4095 -> 0..256 with 8.4 fixed-point (top 8 bits index)
    uint32_t t    = ((uint32_t)adc12 * 256u) >> 12;         // 0..256
    uint32_t idx  = (t > 256u) ? 256u : t;
    // (Optional) do 8.8 lerp for extra smoothness:
    uint32_t i0   = (adc12 * 256u) >> 12;                   // 0..256
    if (i0 >= 256u) return (uint32_t)(((uint64_t)base_inc_q16_16 * kExpo1Oct_Q16[256]) >> 16);
    uint32_t w8   = (((uint32_t)adc12 * 256u) >> 4) & 0xFF; // fractional weight 0..255
    uint32_t a    = kExpo1Oct_Q16[i0];
    uint32_t b    = kExpo1Oct_Q16[i0 + 1];
    uint32_t ratio_q16 = a + (((uint64_t)(b - a) * w8) >> 8);
    return (uint32_t)(((uint64_t)base_inc_q16_16 * ratio_q16) >> 16);
}


// ── Loop zone ──────────────────────────────────────────────────────────────
// Call whenever a new file is loaded or min length changes
static inline void loop_mapper_recalc_spans() {
    uint32_t total = g_total_samples;
    uint32_t minlen = (g_min_loop_len < total) ? g_min_loop_len : (total ? total : 1);
    g_span_start = (total > minlen) ? (total - minlen) : 0;
    g_span_len   = (total > minlen) ? (total - minlen) : 0;
}

// Map 12-bit ADCs to sample indices; audio-rate safe.
// adc_start,len: 0..4095 (from DMA); outputs: start,end in [0..total)
static inline void derive_loop_indices(uint16_t adc_start,
                                       uint16_t adc_len,
                                       uint32_t total,
                                       uint32_t* __restrict out_start,
                                       uint32_t* __restrict out_end)
{
    // start ∈ [0 .. total - min_len]
    // len   ∈ [min_len .. total]
    // NOTE: spans precomputed, no divides
    uint32_t start = (uint32_t(adc_start) * g_span_start) >> 12;   // 12-bit shift
    uint32_t len   = g_min_loop_len + ((uint32_t(adc_len) * g_span_len) >> 12);

    // Safety clamps (rarely hit if spans are correct)
    if (start > total - g_min_loop_len) start = total - g_min_loop_len;
    if (len   < g_min_loop_len)         len   = g_min_loop_len;
    if (start + len > total)            len   = total - start;

    *out_start = start;
    *out_end   = start + len;  // [start, end)
}


static inline int16_t adc12_to_q15_centered(uint16_t raw12) {
    // 0..4095 -> -32768..+32752 (approx). One shift is enough and cheap.
    int32_t centered = (int32_t)raw12 - 2048;    // -2048..+2047
    return (int16_t)(centered << 4);             // scale to ~Q15
}

static inline uint16_t q15_to_pwm_u(int16_t s) {
    // Map [-32768..32767] to [0..PWM_RESOLUTION-1]
    uint32_t u = ((uint16_t)s) ^ 0x8000u;        // offset-binary 0..65535
    return (uint16_t)((u * (PWM_RESOLUTION - 1u)) >> 16);
}

// Call this after the loader fills sf::audioData + metadata
void playback_bind_loaded_buffer(uint32_t src_sample_rate_hz,
                                 uint32_t out_sample_rate_hz,
                                 uint32_t sample_count)
{
    g_samples_q15   = reinterpret_cast<const int16_t*>(sf::audioData);
    g_total_samples = sample_count;

    // Unity base: src_hz / out_hz in Q16.16
    g_inc_base_q32_32 = (uint64_t)(((uint64_t)src_sample_rate_hz << 32) / (uint64_t)out_sample_rate_hz);

    // Ensure loop spans are valid for the new file
    loop_mapper_recalc_spans();
    
    // Debug log
    Serial.print(F("[AE] Buffer bound: "));
    Serial.print(sample_count);
    Serial.print(F(" samples @ "));
    Serial.print(src_sample_rate_hz);
    Serial.println(F(" Hz"));
}


void audio_init(void) {
    init_expo_table_1oct();
    configurePWM_DMA();
    unmuteAudioOutput();
    
    // Set initial debug level
    audio_engine_debug_set_level(AE_DBG_INFO);

    // dma_start_channel_mask(1u << dma_chan);
    Serial.printf("[AE] DMA started? %d\n", dma_channel_is_busy(dma_chan_a));

    Serial.println(F("[AE] Audio engine initialized"));
}

void audio_tick(void) {
    if (callback_flag > 0) {
        process();
        callback_flag = 0;
    }
}

void process() {

    adc_filter_update_from_dma();

    // ── sticky loop region (active used for playback; pending updated per block)
    static uint32_t s_active_start = 0;
    static uint32_t s_active_end   = 0;

    uint32_t diag = AE_DIAG_NONE;

    // Early out
    if (s_state != AE_STATE_PLAYING || !g_samples_q15 || g_total_samples < 2) {
        for (uint32_t i = 0; i < AUDIO_BLOCK_SIZE; ++i) out_buf_ptr[i] = 0;
        if (!g_samples_q15 || g_total_samples < 2) diag |= AE_DIAG_NO_BUFFER;
        if (s_state != AE_STATE_PLAYING)            diag |= AE_DIAG_STATE_NOT_PLAY;

        ae_dbg_snapshot_t snap = {};
        snap.phase_q16_16  = (uint32_t)(g_phase_q32_32 >> 16);
        snap.total_samples = g_total_samples;
        snap.loop_start    = s_active_start;
        snap.loop_end      = s_active_end ? s_active_end : g_total_samples;
        snap.inc_q16_16    = (1u << 16);
        snap.inc_signed    = (1u << 16);
        snap.mode          = (uint8_t)AE_MODE_FORWARD;
        snap.state         = (uint8_t)s_state;
        snap.dir           = 1;
        snap.diag_flags    = diag;
        memcpy((void*)&s_last_snap, &snap, sizeof(snap));
        return;
    }

    // ── Read controls (filtered ADCs) ─────────────────────────────────────────────
    const uint16_t adc_start_raw = adc_filter_get(ADC_LOOP_START_CH);
    const uint16_t adc_len_raw   = adc_filter_get(ADC_LOOP_LEN_CH);

    // ── Map to samples (64-bit intermediates) ────────────────────────────────
    const uint32_t MIN_LOOP_LEN  = 64;
    const uint32_t total_samples = g_total_samples;

    const uint32_t max_start_pos = (total_samples > MIN_LOOP_LEN) ? (total_samples - MIN_LOOP_LEN) : 0;
    const uint32_t pending_start = (uint32_t)(
        ((uint64_t)adc_start_raw * (uint64_t)max_start_pos) / 4095u
    );

    const uint32_t span_total = (total_samples > MIN_LOOP_LEN) ? (total_samples - MIN_LOOP_LEN) : 0;
    const uint32_t pending_len = MIN_LOOP_LEN + (uint32_t)(
        ((uint64_t)adc_len_raw * (uint64_t)span_total) / 4095u
    );

    uint32_t pending_end = pending_start + pending_len;
    if (pending_end > total_samples) pending_end = total_samples;   // contiguous inside file

    // ── Initialize active region once (or if invalid) ────────────────────────
    if (s_active_end <= s_active_start || s_active_end > total_samples) {
        s_active_start = pending_start;
        s_active_end   = pending_end;
        // Optionally snap phase inside region on first activation
        uint64_t ph = g_phase_q32_32;
        uint32_t idx0 = (uint32_t)(ph >> 32);
        if (idx0 < s_active_start || idx0 >= s_active_end) {
            g_phase_q32_32 = ((uint64_t)s_active_start) << 32;
        }
    }

    // ── Hot loop: Q32.32 phase; wrap only when crossing end ─────────────────
    const int16_t* __restrict samples = g_samples_q15;
    uint64_t phase_q = g_phase_q32_32;
    const uint64_t INC = (1ULL << 32); // 1.0x

    for (uint32_t i = 0; i < AUDIO_BLOCK_SIZE; ++i) {
        const uint32_t prev_idx = (uint32_t)(phase_q >> 32);
        phase_q += INC;
        uint32_t idx = (uint32_t)(phase_q >> 32);

        const bool crossed_end = (prev_idx < s_active_end) && (idx >= s_active_end);
        const bool hit_eof     = (idx >= total_samples);

        if (crossed_end || hit_eof) {
            // Adopt the latest pending region *at wrap time*
            s_active_start = pending_start;
            s_active_end   = pending_end;

            idx     = s_active_start;
            phase_q = ((uint64_t)s_active_start) << 32;
        }

        out_buf_ptr[i] = q15_to_pwm_u(samples[idx]);
    }

    // ── Persist phase ────────────────────────────────────────────────────────
    g_phase_q32_32 = phase_q;

    // ── UI/debug: publish ACTIVE region so display matches what you hear ────
    const uint32_t current_idx = (uint32_t)(phase_q >> 32);
    const uint16_t playhead_u16 = (uint16_t)(
        ((uint64_t)current_idx * 65535u) / (uint64_t)total_samples
    );
    const uint16_t start_q12 = (uint16_t)(
        ((uint64_t)s_active_start * 4095u) / (uint64_t)total_samples
    );
    const uint16_t len_q12 = (uint16_t)(
        ((uint64_t)(s_active_end - s_active_start) * 4095u) / (uint64_t)total_samples
    );

    ae_dbg_snapshot_t snap = {};
    snap.phase_q16_16  = (uint32_t)(phase_q >> 16);
    snap.idx           = current_idx;
    snap.loop_start    = s_active_start;
    snap.loop_end      = s_active_end;
    snap.total_samples = total_samples;
    snap.inc_q16_16    = (1u << 16);
    snap.inc_signed    = (1u << 16);
    snap.xfade_len     = 0;
    snap.playhead_u16  = playhead_u16;
    snap.mode          = (uint8_t)AE_MODE_FORWARD;
    snap.state         = (uint8_t)s_state;
    snap.dir           = 1;
    snap.diag_flags    = diag;
    memcpy((void*)&s_last_snap, &snap, sizeof(snap));

    publish_display_state(start_q12, len_q12, current_idx, total_samples);
}


// void process() {
//     uint32_t diag = AE_DIAG_NONE;

//     // Early outs: no buffer or not playing
//     if (s_state != AE_STATE_PLAYING || !g_samples_q15 || g_total_samples < 2) {
//         // Clear output buffer
//         for (uint32_t i = 0; i < AUDIO_BLOCK_SIZE; ++i) {
//             out_buf_ptr[i] = 0;
//         }
        
//         // Set diagnostic flags
//         if (!g_samples_q15 || g_total_samples < 2) diag |= AE_DIAG_NO_BUFFER;
//         if (s_state != AE_STATE_PLAYING) diag |= AE_DIAG_STATE_NOT_PLAY;
        
//         // Update debug snapshot for early out case
//         ae_dbg_snapshot_t snap;
//         snap.phase_q16_16  = g_phase_q16_16;
//         snap.idx           = 0;
//         snap.loop_start    = 0;
//         snap.loop_end      = g_total_samples;
//         snap.total_samples = g_total_samples;
//         snap.inc_q16_16    = (1 << 16);  // 1.0 increment
//         snap.inc_signed    = (1 << 16);
//         snap.xfade_len     = 0;
//         snap.playhead_u16  = 0;
//         snap.mode          = (uint8_t)AE_MODE_FORWARD;  // Always forward now
//         snap.state         = (uint8_t)s_state;
//         snap.dir           = 1;  // Always forward
//         snap.diag_flags    = diag;
//         memcpy((void*)&s_last_snap, &snap, sizeof(snap));
        
//         return;
//     }

//     // Cache locals for maximum speed
//     const int16_t* __restrict samples = g_samples_q15;
//     uint32_t phase_q = g_phase_q16_16;
//     const uint32_t total_samples = g_total_samples;

//     // ── Hot loop: bare minimum for maximum speed ───────────────────────────────
//     for (uint32_t i = 0; i < AUDIO_BLOCK_SIZE; ++i) {
//         // Advance phase by 1.0 (fixed 1:1 playback rate)
//         phase_q += (1 << 16);  // Q16.16: increment by 1.0
        
//         // Get integer sample index
//         uint32_t idx = phase_q >> 16;
        
//         // Simple wrap-around at end of buffer
//         if (idx >= total_samples) {
//             idx = 0;
//             phase_q = 0;  // Reset phase for clean loop
//         }
        
//         // Fetch sample and convert to PWM output
//         out_buf_ptr[i] = q15_to_pwm_u(samples[idx]);
//     }

//     // Store updated phase
//     g_phase_q16_16 = phase_q;

//     // Calculate normalized playhead position for UI (0-65535)
//     uint32_t current_idx = phase_q >> 16;
//     uint16_t playhead_u16 = (uint16_t)((current_idx * 65535u) / total_samples);

//     // Update debug snapshot with current state
//     ae_dbg_snapshot_t snap;
//     snap.phase_q16_16  = phase_q;
//     snap.idx           = current_idx;
//     snap.loop_start    = 0;                    // Always start at beginning
//     snap.loop_end      = total_samples;        // Always end at file end
//     snap.total_samples = total_samples;
//     snap.inc_q16_16    = (1 << 16);           // Fixed 1.0 increment
//     snap.inc_signed    = (1 << 16);           // Always positive (forward)
//     snap.xfade_len     = 0;                   // No crossfading
//     snap.playhead_u16  = playhead_u16;
//     snap.mode          = (uint8_t)AE_MODE_FORWARD;  // Always forward
//     snap.state         = (uint8_t)s_state;
//     snap.dir           = 1;                   // Always forward direction
//     snap.diag_flags    = diag;

//     // Use memcpy to update volatile struct atomically
//     memcpy((void*)&s_last_snap, &snap, sizeof(snap));

//     publish_display_state(/*start_q12=*/0, /*len_q12=*/4095, /*playhead=*/current_idx, /*total=*/total_samples);

// }













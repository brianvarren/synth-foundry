#include "sf_globals_bridge.h"

volatile uint32_t g_core0_setup_done = 0;
// sf_display_state_t g_disp = {0};


//#include "sf_globals_bridge.h"

// ───────────────────────── Storage (volatile globals) ───────────────────────
volatile uint16_t g_vis_start_q12    = 0;
volatile uint16_t g_vis_len_q12      = 0;
volatile uint32_t g_vis_total        = 1;   // never 0 to avoid div-by-zero in UI
volatile uint32_t g_vis_playhead_idx = 0;
volatile uint8_t  g_vis_xfade_active = 0;
volatile uint32_t g_vis_playhead2_idx= 0;

volatile uint32_t g_vis_seq          = 0;   // seqlock counter (even=stable)

// ───────────────────────────── Helper (seqlock) ─────────────────────────────
static inline void vis_begin_write(void) {
  // increment to odd => readers see "writing"
  g_vis_seq++;
}

static inline void vis_end_write(void) {
  // increment to even => readers see "stable"
  g_vis_seq++;
}

// ───────────────────────────── Publish APIs ─────────────────────────────────
void publish_display_state(uint16_t start_q12, uint16_t len_q12,
                           uint32_t playhead_idx, uint32_t total) {
  vis_begin_write();
  g_vis_start_q12     = start_q12;
  g_vis_len_q12       = len_q12;
  g_vis_playhead_idx  = playhead_idx;
  g_vis_total         = (total == 0u) ? 1u : total;
  g_vis_xfade_active  = 0;
  g_vis_playhead2_idx = 0;
  vis_end_write();
}

void publish_display_state2(uint16_t start_q12, uint16_t len_q12,
                            uint32_t playhead_idx, uint32_t total,
                            uint8_t xfade_active, uint32_t playhead2_idx) {
  vis_begin_write();
  g_vis_start_q12     = start_q12;
  g_vis_len_q12       = len_q12;
  g_vis_playhead_idx  = playhead_idx;                 // primary (head)
  g_vis_total         = (total == 0u) ? 1u : total;
  g_vis_xfade_active  = (uint8_t)(xfade_active ? 1u : 0u);
  g_vis_playhead2_idx = playhead2_idx;                // secondary (tail)
  vis_end_write();
}

# XYLEM

A compact RP2040 synth voice that spawns a swarm of tones—calm drones to alien metallic chatter—clock-synced and driven by **eight** simple controls, now mixed to a **single mono PWM** output.

# Controls (unchanged in spirit, adjusted for mono)

**Context over UART (global)**

1. **Density** – how many voices you hear and how thick the mix is.
2. **Pace** – how fast new voices are spawned and how fast envelopes evolve.
3. **Precision** – tight to the grid vs humanized timing.
4. **Consonance** – harmonic flavor; later maps to FM ratios.

**Panel (hands-on)**
5\) **Index/Drive** – today: soft-clip/character; later: FM depth.
6\) **Ratio Morph** – today: detune/interval spread; later: FM ratio sweep & in-between clang.
7\) **Repeat Tendency** – likelihood to reuse phrase seeds vs wander.
8\) **Texture (was Stereo Spread)** – replaces stereo width: controls **diffuser amount / brightness tilt / micro-noise “air.”**

# How it behaves (mono)

* LocalMetronome syncs everything; **Precision** decides how closely spawns land on subdivisions.
* Each voice has an envelope so the swarm breathes.
* **Repeat Tendency** creates motifs you recognize without needing stereo tricks.
* The “space” feeling comes from **subtle mono diffusion** (a tiny allpass/delay), **not** from panning.

# Inside the box (today’s scaffold, mono)

* **Engine:** many lightweight **sine oscillators** summed to one mono bus.
* **Timing:** block-rate control; LocalMetronome for beat math; spawns scheduled from **Pace × Precision**.
* **Post:** gentle **soft-clip** (mapped to Density/Drive) → **tiny mono diffuser** (1–2 short allpasses or a 10–25 ms single delay with low feedback) → dither (optional) → **PWM mono**.

## PWM mono specifics (RP2040)

* **One PWM slice, one channel** (or mirrored A/B tied together if layout prefers).
* **Sample cadence:** DMA writes 1× `uint16_t` per sample to `pwm_hw->slice[s].cc`.
* **Clocking:** choose `clkdiv + top` so PWM carrier ≫ audio rate (e.g., \~187.5 kHz carrier at 48 kHz audio).
* **DMA:** single channel, double buffer (A/B) sized `AUDIO_BLOCK_SIZE`.
* **ISR:** block boundary only (flip buffer, swap param snapshot), **no per-sample work**.

# Tomorrow’s upgrade (FM pairs, still mono)

* Each voice becomes **carrier + modulator** (two sines).
* **Consonance / Ratio Morph** selects **integer ratios** (1:1, 2:1, 3:2, 5:4, …) with smooth “free” regions between them.
* **Index/Drive** becomes FM **depth** with its **own envelope** (often snappier than the amp env).
* Render swaps to **phase modulation**: `y = sin(phase_car + mod * index)` and remains mono.

# Design plan

## Phase 1 — Playable sine swarm (mono)

1. **Static state (no heap):**

   * `VOICE_MAX` (start 24…48): `phase`, `phase_inc`, `amp_cur/target`, `env_pos/stride`, `seed`, `lifetime`.
   * Shared **SINE\_LUT\[2048]**, a few **ENV** tables, small **Texture** tilt table, mono **diffuser** buffer(s).
2. **Control-rate per block:**

   * Read/smooth UART(4) + ADC(4).
   * From **Pace/Precision** compute spawns this block (grid-snap vs jitter).
   * From **Consonance/Ratio Morph** choose pitch neighborhoods (interval clusters now; ratios later).
   * From **Density** set effective voice budget and gain comp (so “more” doesn’t clip).
   * From **Repeat Tendency** bias RNG toward reusing last phrase seeds.
   * From **Texture** set diffuser mix/feedback and brightness tilt.
3. **Render loop (per sample):** sum sines → apply envelope → **soft-clip** → **mono diffuser** → scale to `uint16_t` → write to the single PWM buffer.
4. **Test staircase:** 12 → 24 → 32 voices; enable diffuser; verify controls feel musical.

## Phase 2 — FM drop-in (no UI change)

1. Add per-voice `phase_mod`, `phase_inc_mod`, `idx_cur/target`, `env_pos_mod/stride_mod`, `ratio_num/den`.
2. Map **Consonance/Ratio Morph** to **ratio sets** with smooth interpolation through in-between metallic regions.
3. Promote **Index/Drive** to FM **depth**; give it an **independent envelope**.
4. Replace oscillator read with PM: compute `phi = phase_car + ((sin(phase_mod) * idx_cur) << K)`; LUT fetch; mix.
5. Profile; cap polyphony around **24–32 FM voices** as needed.

## Phase 3 — Polish

* Add rare “flock” events at higher Pace to move a subset of voices together.
* Optional: swap mono diffuser between **allpass** and **short delay** modes under **Texture**.
* Scene system: a few storable presets for ratio sets, env shapes, Texture mode.

# Engineering constraints (kept)

* **Static/global only** (no `new`, no `malloc`; pmalloc only for PSRAM audio buffers if used).
* **Fixed-point**: q0.32 phase; q1.15 gains/envelopes; block-rate slews to prevent zippering.
* **Two-core option:** Core0 = audio render; Core1 = control/scheduling; swap a small **param snapshot** each block for lock-free updates.
* **Glitch safety:** spawn at envelope start (or zero-cross), ramp down on kill, saturate rather than clip.

# Why mono still sings

* The swarm’s **timing interplay, envelopes, and FM motion** do the spatial/psychoacoustic heavy lifting even with one speaker.
* **Texture** (diffusion + brightness tilt) restores depth and “air” without stereo.
* You get **more CPU headroom** for voices and FM depth, plus **simpler hardware** (one PWM pin, one DMA channel).

If you want, Bob can turn this into a repo-ready README section with a small ASCII signal-flow diagram and a control table tailored to the mono build.

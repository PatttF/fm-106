/* SPDX-License-Identifier: GPL-3.0-only */
/* FM-106: PolyBLEP Digitally Controlled Oscillator (DCO)
 * Modeled on the Roland Juno-106 DCO:
 * - Shared phase accumulator for Saw, Pulse, and Sub-oscillator
 * - PolyBLEP anti-aliased Saw and Pulse
 * - Juno-106 inverted duty-cycle comparator
 * - Sub-oscillator CD4013 flip-flop with exponential AudioTaper
 * - Calibrated mix levels (Saw: 0.6, Pulse: 0.5, Sub: 0.75, Noise: 1.2) */
#pragma once
#include <stdint.h>
#include "fm106_tables.h"

/* Fixed-point Q15 multiplication */
static inline int32_t fm106_mulq15(int32_t a, int32_t b)
{
    return (a * b) >> 15;
}

/* PolyBLEP residual around 32-bit phase wrap using precomputed float reciprocal
 * inv_inc = 1.0f / (float)inc (pass blep_inc = 0 when inc < 2u).
 * Eliminates up to 4 hardware 32-bit integer divisions per sample per voice. */
static inline __attribute__((always_inline)) int32_t fm106_poly_blep_f(uint32_t ph, uint32_t inc, float inv_inc)
{
    if (ph < inc) {
        float t = (float)ph * inv_inc;
        return (int32_t)(((t + t) - t * t - 1.0f) * 32768.0f);
    }
    if (ph > (0xFFFFFFFFu - inc)) {
        float om = 1.0f - (float)(0xFFFFFFFFu - ph) * inv_inc;
        return (int32_t)((om * om) * 32768.0f);
    }
    return 0;
}

/* Legacy wrapper when inv_inc is not precomputed */
static inline int32_t fm106_poly_blep(uint32_t ph, uint32_t inc)
{
    if (inc < 2u) return 0;
    return fm106_poly_blep_f(ph, inc, 1.0f / (float)inc);
}

/* Bandlimited sawtooth: normalized Q15 (-32767..32767) */
static inline __attribute__((always_inline)) int32_t fm106_osc_saw(uint32_t ph, uint32_t inc, int32_t blep_reset)
{
    (void)inc;
    return ((int32_t)(ph >> 16) - 32768) - blep_reset;
}

/* Fast 32-bit PRNG for Juno-106 analog noise generator */
static inline __attribute__((always_inline)) uint32_t fm106_noise32(uint32_t *seed)
{
    uint32_t s = *seed;
    s ^= s << 13;
    s ^= s >> 17;
    s ^= s << 5;
    *seed = s;
    return s;
}

typedef struct {
    uint32_t ph;                /* 32-bit phase accumulator */
    uint8_t sub_state;          /* CD4013 flip-flop state (0 or 1) */
    uint32_t noise_seed;        /* PRNG state */
    int32_t noise_lpf;          /* 1-pole lowpass at 5.3 kHz */
    uint8_t saw_on;             /* 0 or 1 */
    uint8_t pulse_on;           /* 0 or 1 */
    uint8_t sub_on;             /* 0 or 1 */
    uint8_t sub_level;          /* 0..127 slider value */
    uint8_t noise_level;        /* 0..127 slider value */
} fm106_dco_t;

/* Retrigger reset: clears per-note oscillator state only.
 * Waveform config (saw_on/pulse_on/sub_on/sub_level/noise_level) and
 * noise_seed are deliberately preserved -- note_on calls this on every
 * retrigger, and wiping the config would destroy the patch settings. */
static inline void fm106_dco_reset(fm106_dco_t *d)
{
    d->ph = 0;
    d->sub_state = 0;
    d->noise_lpf = 0;   /* noise_seed kept: LFSR stays continuous across notes */
}

static inline void fm106_dco_init(fm106_dco_t *d, uint32_t voice_idx)
{
    d->ph = 0;
    d->sub_state = 0;
    d->noise_seed = 123456789u + voice_idx * 987654321u;
    d->noise_lpf = 0;
    d->saw_on = 1;
    d->pulse_on = 0;
    d->sub_on = 0;
    d->sub_level = 0;
    d->noise_level = 0;
}

/* Render one audio sample from the DCO with precomputed 1/eff_inc (returns Q15) */
static inline __attribute__((always_inline)) int32_t fm106_dco_process_fast(
    fm106_dco_t *d,
    uint32_t eff_inc,
    uint32_t blep_inc,
    float inv_inc,
    uint8_t pw_slider,
    uint8_t pw_mode,
    int32_t lfo_tri,
    int32_t env_q15)
{
    (void)env_q15;
    uint32_t prev_ph = d->ph;
    uint32_t next_ph = prev_ph + eff_inc;
    d->ph = next_ph;

    /* Detect phase wrap: toggle sub-oscillator flip-flop */
    if (next_ph < prev_ph) {
        d->sub_state = !d->sub_state;
    }

    /* Shared PolyBLEP residual at phase reset */
    int32_t blep_reset = fm106_poly_blep_f(next_ph, blep_inc, inv_inc);

    int32_t total = 0;

    /* 1. Sawtooth: amp 0.6 (19660 in Q15) */
    if (d->saw_on) {
        int32_t saw = fm106_osc_saw(next_ph, eff_inc, blep_reset);
        total += (saw * 19660) >> 15;
    }

    /* 2. Pulse with Juno-106 inverted duty cycle: amp 0.5 (16384 in Q15 = >> 1) */
    if (d->pulse_on) {
        /* Compute pulse width duty (0.50..0.95) from fader / LFO */
        int32_t mod_pw;
        if (pw_mode == 0) {   /* LFO PWM */
            mod_pw = ((int32_t)pw_slider * (32768 + lfo_tri)) >> 16;
        } else {              /* Manual PWM */
            mod_pw = pw_slider;
        }
        if (mod_pw < 0) mod_pw = 0;
        if (mod_pw > 105) mod_pw = 105;

        /* Range: 50% to 95% -> 0x80000000 to 0xF3333333 (Juno-106 hardware panel limit: byte 105 = 95%).
         * pw_off <= 105 * 18407003 = 1,932,735,315 (< 2^31), so both eff_pw and pulse_dc fit in 32-bit! */
        uint32_t pw_off = (uint32_t)mod_pw * 18407003u;
        uint32_t eff_pw = 0x7FFFFFFFu - pw_off;

        /* Comparator + dual PolyBLEP */
        int32_t pulse = (next_ph < eff_pw) ? -32767 : 32767;
        pulse -= blep_reset;
        uint32_t pw_edge = next_ph - eff_pw;
        pulse += fm106_poly_blep_f(pw_edge, blep_inc, inv_inc);

        /* AC coupling: remove theoretical DC offset of rectangular pulse
         * (matches hardware C11 10µF coupling capacitor at steady state) */
        int32_t pulse_dc = (int32_t)(pw_off >> 16);
        pulse -= pulse_dc;

        total += pulse >> 1;
    }

    /* 3. Sub-oscillator: 1 octave down square, amp 0.75 * FM106_SUB_LEVEL_J106 */
    if (d->sub_on && d->sub_level > 0) {
        int32_t sub_sign = d->sub_state ? -32767 : 32767;
        int32_t abs_blep = blep_reset < 0 ? -blep_reset : blep_reset;
        if (abs_blep > 32768) abs_blep = 32768;
        int32_t sub = (sub_sign * (32768 - abs_blep)) >> 15;
        int32_t taper = FM106_SUB_LEVEL_J106[d->sub_level > 127u ? 127u : d->sub_level];
        int32_t sub_scaled = (sub * taper) >> 15;
        total += (sub_scaled * 3) >> 2;
    }

    /* 4. Analog Noise Generator: 5.3 kHz 1-pole RC filtered (Roland 2SC945 noise path), calibrated to reference RMS */
    if (d->noise_level > 0) {
        uint32_t nr = fm106_noise32(&d->noise_seed);
        int32_t raw_n = (int32_t)(nr >> 16) - 32768;   /* Q15 bipolar noise */
        /* 1-pole RC lowpass at 5305 Hz: g1 ~= 0.284 in Q15 = 9300 */
        d->noise_lpf += (9300 * (raw_n - d->noise_lpf)) >> 15;
        int32_t n_level = FM106_NOISE_LEVEL_J106[d->noise_level > 127u ? 127u : d->noise_level];
        int32_t noise_scaled = (d->noise_lpf * n_level) >> 15;
        /* Gain 2.776x (91000 / 32768 == 11375 / 4096, exact in 32-bit) */
        total += (noise_scaled * 11375) >> 12;
    }

    return total;
}

static inline int32_t fm106_dco_process(
    fm106_dco_t *d,
    uint32_t eff_inc,
    uint8_t pw_slider,
    uint8_t pw_mode,
    int32_t lfo_tri,
    int32_t env_q15)
{
    uint32_t blep_inc = (eff_inc < 2u) ? 0u : eff_inc;
    float inv_inc = blep_inc ? (1.0f / (float)blep_inc) : 0.0f;
    return fm106_dco_process_fast(d, eff_inc, blep_inc, inv_inc, pw_slider, pw_mode, lfo_tri, env_q15);
}

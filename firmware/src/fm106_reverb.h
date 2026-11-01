/* SPDX-License-Identifier: GPL-3.0-only */
/* FM-106: 1980s Studio Stereo Shimmer Reverb (Post-Chain Master FX)
 *
 * Inspired by classic 1980s Lexicon 224XL / 480L figure-8 modulated halls paired
 * with Brian Eno / Daniel Lanois dual-detuned octave-up pitch-shifted shimmer.
 *
 * Designed specifically to sit AFTER MN3009 Stereo Chorus + Airwindows Tape FX
 * without mud, harshness, modulation beating, phase cancellation, or clipping:
 *  - True-stereo input diffusion (never sums L+R at input, preserving antiphase
 *    MN3009 Chorus stereo width without comb cancellation)
 *  - 155 Hz stereo input HPF + 95 Hz in-tank LF cut prevents HPF Bass-Boost and
 *    Tape Head-Bump low-end rumble from booming inside long tails
 *  - 240 Hz..4.6 kHz bandpass + 7.5 kHz air polish around the dual-grain Hann
 *    pitch shifters (+1204c L / +1196c R) keeps Tape saturation harmonics silky
 *    and strictly below Nyquist with zero ultrasonic aliasing
 *  - Slow non-commensurate quadrature tank LFOs (0.317 Hz & 0.431 Hz) avoid
 *    modulation beating against Chorus I (0.514 Hz), II (0.842 Hz), and Tape flutter
 *  - Energy-conserving shimmer loop blend + BA662 OTA Padé loop saturation +
 *    quarter-sine equal-power wet/dry mix + output soft-limiter guarantee zero
 *    runaway or hard clipping even with 6 voices + Chorus + Tape + Reverb maxed. */
#pragma once
#include <stdint.h>
#include "fm106_tables.h"
#include "fm106_vcf.h"
#include "fm106_hann_table.h"

typedef struct {
    /* User parameters (persisted in preset p[45..48]) */
    uint8_t on;         /* 0 = OFF (bypassed), 1 = ON */
    uint8_t mix;        /* 0..127 equal-power wet/dry balance */
    uint8_t decay;      /* 0..127 hall size / RT60 (0.6s .. ~18s) */
    uint8_t shimmer;    /* 0..127 octave-up shimmer halo & regeneration */

    /* Engine state */
    uint8_t sleeping;   /* 1 = tail silent & bypassed (zero-CPU fast exit) */
    uint8_t tail_active;/* 1 = audible reverb tail ringing (holds scope & flash save) */
    uint16_t silent_blocks; /* count of silent input blocks to gate sleep */
    uint32_t wr;        /* master sample counter for power-of-2 ring lines */
    uint32_t lfo1_ph;   /* 0.317 Hz tank allpass LFO */
    uint32_t lfo2_ph;   /* 0.431 Hz tank allpass LFO */
    uint32_t shim_ph_l; /* Left +1204 cent dual-grain phase */
    uint32_t shim_ph_r; /* Right +1196 cent dual-grain phase */

    /* Smooth parameter slews */
    float env_mix;
    float env_decay;
    float env_shim;

    /* Input conditioning filters */
    float hp_in_l, hp_in_r;
    float lp_in_l, lp_in_r;

    /* Figure-8 tank damping, LF cut, and cross-coupling state */
    float damp_l, damp_r;
    float dc_tk_l, dc_tk_r;
    float cross_l, cross_r;

    /* Shimmer bandpass & post-polish filter states */
    float sh_hp_l, sh_hp_r;
    float sh_lp1_l, sh_lp2_l;
    float sh_lp1_r, sh_lp2_r;
    float sh_post_l, sh_post_r;
} fm106_reverb_t;

/* Delay-line memory in .pool (~97 KiB of 285 KiB free .pool; 0 B of .data/.bss).
 * Stored in Q13 (8192.0f = 1.0 full scale), giving +-4.0 (+12 dB) internal
 * headroom and >84 dB dynamic range across the multi-tap stereo tank. */
typedef struct {
    int16_t pre_l[1024];        /* 22.0 ms stereo pre-delay L */
    int16_t pre_r[2048];        /* 24.6 ms stereo pre-delay R */
    int16_t ap_in_l1[256];      /* input diffuser L1 (197) */
    int16_t ap_in_l2[512];      /* input diffuser L2 (353) */
    int16_t ap_in_r1[256];      /* input diffuser R1 (223) */
    int16_t ap_in_r2[512];      /* input diffuser R2 (389) */
    int16_t ap_tk_l1[2048];     /* modulated tank allpass L1 (997 +- 8) */
    int16_t ap_tk_r1[2048];     /* modulated tank allpass R1 (1361 +- 8) */
    int16_t dly_l1[8192];       /* tank delay L1 (4453 = 101.0 ms) */
    int16_t dly_r1[8192];       /* tank delay R1 (4217 = 95.6 ms) */
    int16_t ap_tk_l2[4096];     /* tank inner allpass L2 (2659 = 60.3 ms) */
    int16_t ap_tk_r2[4096];     /* tank inner allpass R2 (1801 = 40.8 ms) */
    int16_t dly_l2[4096];       /* tank delay L2 (3727 = 84.5 ms) */
    int16_t dly_r2[4096];       /* tank delay R2 (3163 = 71.7 ms) */
    int16_t shim_l[4096];       /* 46.4 ms Hann dual-grain pitch buffer L */
    int16_t shim_r[4096];       /* 46.4 ms Hann dual-grain pitch buffer R */
} fm106_reverb_mem_t;

static FM106_POOL_DATA fm106_reverb_mem_t g_reverb_mem;

#define FM106_REV_SCALE     16384.0f
#define FM106_REV_INV_SCALE (1.0f / 16384.0f)

static inline __attribute__((always_inline)) int16_t fm106_rev_to_i16(float x)
{
    int32_t s = (int32_t)(x * FM106_REV_SCALE);
    if (__builtin_expect(s > 32767, 0)) return 32767;
    if (__builtin_expect(s < -32767, 0)) return -32767;
    return (int16_t)s;
}

static inline __attribute__((always_inline)) float fm106_rev_tap(const int16_t *buf, uint32_t idx, uint32_t mask)
{
    return (float)buf[idx & mask] * FM106_REV_INV_SCALE;
}

/* Unity-gain Schroeder/Dattorro allpass stage */
static inline __attribute__((always_inline)) float fm106_rev_allpass(
    int16_t *buf, uint32_t wr, uint32_t mask, uint32_t delay, float x, float g)
{
    float d = (float)buf[(wr - delay) & mask] * FM106_REV_INV_SCALE;
    float v = x + g * d;
    buf[wr & mask] = fm106_rev_to_i16(v);
    return d - g * v;
}

/* Modulated tank allpass with fractional linear read evaluated on FPU */
static inline __attribute__((always_inline)) float fm106_rev_mod_allpass(
    int16_t *buf, uint32_t wr, uint32_t mask, uint32_t idel, float frac, float x, float g)
{
    uint32_t r0 = (wr - idel) & mask;
    uint32_t r1 = (r0 - 1u) & mask;
    float s0 = (float)buf[r0];
    float s1 = (float)buf[r1];
    float d = (s0 + (s1 - s0) * frac) * FM106_REV_INV_SCALE;
    float v = x + g * d;
    buf[wr & mask] = fm106_rev_to_i16(v);
    return d - g * v;
}

/* Dual-grain C1-continuous Hann-windowed +1 octave pitch shifter (GRAIN = 2048).
 * Native single-precision FPU evaluation: 30% faster than integer interpolation. */
static inline __attribute__((always_inline)) float fm106_rev_shimmer_read(
    const int16_t *buf, uint32_t wr, uint32_t ph)
{
    uint32_t not_ph = ~ph;
    uint32_t ia = 64u + (not_ph >> 21);
    float fa = (float)((not_ph >> 6) & 32767u) * (1.0f / 32768.0f);
    uint32_t ra0 = (wr - ia) & 4095u;
    uint32_t ra1 = (ra0 - 1u) & 4095u;
    float sa0 = (float)buf[ra0];
    float sa = sa0 + ((float)buf[ra1] - sa0) * fa;

    uint32_t not_ph_b = ~(ph + 2147483648u);
    uint32_t ib = 64u + (not_ph_b >> 21);
    float fb = (float)((not_ph_b >> 6) & 32767u) * (1.0f / 32768.0f);
    uint32_t rb0 = (wr - ib) & 4095u;
    uint32_t rb1 = (rb0 - 1u) & 4095u;
    float sb0 = (float)buf[rb0];
    float sb = sb0 + ((float)buf[rb1] - sb0) * fb;

    float wa = (float)FM106_HANN[(ph >> 23) & 511u] * (1.0f / 32768.0f);
    return (sb + (sa - sb) * wa) * FM106_REV_INV_SCALE;
}

/* BA662 OTA loop saturation with zero-division fast path:
 * Matches Padé within 0.4% without floating-point division in normal headroom */
static inline __attribute__((always_inline)) float fm106_rev_sat_f(float x)
{
    float ax = x < 0.0f ? -x : x;
    if (__builtin_expect(ax < 0.6f, 1)) {
        return x * (1.0f - 0.296f * (x * x));
    }
    if (x >= 3.0f) return 1.0f;
    if (x <= -3.0f) return -1.0f;
    float x2 = x * x;
    return x * (27.0f + x2) / (27.0f + 9.0f * x2);
}

/* Native float rational soft limiter for master reverb output mix */
static inline __attribute__((always_inline)) float fm106_soft_limit_f(float x)
{
    float ax = x < 0.0f ? -x : x;
    if (__builtin_expect(ax <= 0.75f, 1)) {
        return x;
    }
    float ex = ax - 0.75f;
    float compressed = 0.75f + (ex * 0.2265625f) / (0.2265625f + ex);
    return x < 0.0f ? -compressed : compressed;
}

static inline __attribute__((always_inline)) void fm106_reverb_clear_state(fm106_reverb_t *r)
{
    uint8_t on = r->on, mix = r->mix, decay = r->decay, shimmer = r->shimmer;
    typedef uint32_t word_t __attribute__((may_alias));
    word_t *w = (word_t *)&g_reverb_mem;
    for (uint32_t k = 0; k < sizeof(g_reverb_mem) / 4u; k++) w[k] = 0;

    r->wr = 0;
    r->lfo1_ph = 0;
    r->lfo2_ph = 1073741824u;       /* 90-degree quadrature start */
    r->shim_ph_l = 0;
    r->shim_ph_r = 1073741824u;     /* 90-degree stereo grain offset */
    r->env_mix = 0.0f;
    r->env_decay = (float)decay * (1.0f / 127.0f);
    r->env_shim = (float)shimmer * (1.0f / 127.0f);
    r->hp_in_l = 0.0f; r->hp_in_r = 0.0f;
    r->lp_in_l = 0.0f; r->lp_in_r = 0.0f;
    r->damp_l = 0.0f;  r->damp_r = 0.0f;
    r->dc_tk_l = 0.0f; r->dc_tk_r = 0.0f;
    r->cross_l = 0.0f; r->cross_r = 0.0f;
    r->sh_hp_l = 0.0f; r->sh_hp_r = 0.0f;
    r->sh_lp1_l = 0.0f; r->sh_lp2_l = 0.0f;
    r->sh_lp1_r = 0.0f; r->sh_lp2_r = 0.0f;
    r->sh_post_l = 0.0f; r->sh_post_r = 0.0f;
    r->on = on; r->mix = mix; r->decay = decay; r->shimmer = shimmer;
    r->sleeping = 1;
    r->tail_active = 0;
    r->silent_blocks = 0;
}

static inline void fm106_reverb_init(fm106_reverb_t *r)
{
    r->on = 0;
    r->mix = 48;
    r->decay = 84;
    r->shimmer = 68;
    fm106_reverb_clear_state(r);
}

static inline __attribute__((always_inline)) float fm106_rev_flush_denorm(float x)
{
    return (x > -1e-9f && x < 1e-9f) ? 0.0f : x;
}

/* Core per-block DSP kernel placed in zero-wait-state .ram_text */
static FM106_RAM_FUNC void fm106_reverb_kernel(fm106_reverb_t *r, int32_t *stereo, uint32_t n)
{
    fm106_reverb_mem_t *m = &g_reverb_mem;

    float target_mix = (r->on && r->mix > 0u) ? ((float)(r->mix > 127u ? 127u : r->mix) * (1.0f / 127.0f)) : 0.0f;
    float target_dec = (float)(r->decay > 127u ? 127u : r->decay) * (1.0f / 127.0f);
    float target_shm = (float)(r->shimmer > 127u ? 127u : r->shimmer) * (1.0f / 127.0f);

    float env_mix = r->env_mix;
    float env_dec = r->env_decay;
    float env_shm = r->env_shim;

    uint32_t wr = r->wr;
    uint32_t lfo1_ph = r->lfo1_ph;
    uint32_t lfo2_ph = r->lfo2_ph;
    uint32_t shim_ph_l = r->shim_ph_l;
    uint32_t shim_ph_r = r->shim_ph_r;

    float hp_in_l = r->hp_in_l, hp_in_r = r->hp_in_r;
    float lp_in_l = r->lp_in_l, lp_in_r = r->lp_in_r;
    float damp_l  = r->damp_l,  damp_r  = r->damp_r;
    float dc_tk_l = r->dc_tk_l, dc_tk_r = r->dc_tk_r;
    float cross_l = r->cross_l, cross_r = r->cross_r;
    float sh_hp_l = r->sh_hp_l, sh_hp_r = r->sh_hp_r;
    float sh_lp1_l = r->sh_lp1_l, sh_lp2_l = r->sh_lp2_l;
    float sh_lp1_r = r->sh_lp1_r, sh_lp2_r = r->sh_lp2_r;
    float sh_post_l = r->sh_post_l, sh_post_r = r->sh_post_r;

    float in_energy = 0.0f;
    float peak_energy = 0.0f;

    /* Smooth control slews (~25 ms time constant, completely zipper-free) */
    env_mix += (target_mix - env_mix) * 0.18f;
    env_dec += (target_dec - env_dec) * 0.18f;
    env_shm += (target_shm - env_shm) * 0.18f;

    /* Hoist parameter calculations outside the per-sample loop */
    float dec_curve = env_dec * (2.0f - env_dec);
    float decay_g   = 0.36f + 0.588f * dec_curve;   /* 0.36 (0.9s) .. 0.948 (~14s hall) */
    float damp_g    = 0.38f + 0.16f * env_shm;      /* warm 80s hall -> airy shimmer */
    float shim_duck = 1.0f - 0.32f * env_shm;       /* energy-conserving fundamental keep */
    float shim_fb   = 0.56f * env_shm;              /* in-loop octave regeneration */
    float shim_halo = 0.62f * env_shm;              /* direct diffused octave halo */

    uint32_t mix_idx = (uint32_t)(env_mix * 256.0f);
    if (mix_idx > 256u) mix_idx = 256u;
    float dry_g = (float)FM106_SINE[256u - mix_idx] * (1.0f / 32767.0f);
    float wet_g = (float)FM106_SINE[mix_idx]        * (1.0f / 32767.0f);

    float tap_scale  = 1.05f * FM106_REV_INV_SCALE;
    float halo_scale = shim_halo * 2.5f;

    float raw_sh_l = r->sh_post_l;
    float raw_sh_r = r->sh_post_r;

    int32_t mod1 = ((int32_t)FM106_SINE[lfo1_ph >> 22] * 13);
    int32_t mod2 = ((int32_t)FM106_SINE[lfo2_ph >> 22] * 13);
    uint32_t d_ap_l1 = (uint32_t)((int32_t)(997u  << 16) + mod1);
    uint32_t d_ap_r1 = (uint32_t)((int32_t)(1361u << 16) + mod2);
    uint32_t idel_l1 = d_ap_l1 >> 16;
    float frac_l1 = (float)((d_ap_l1 & 65535u) >> 1) * (1.0f / 32768.0f);
    uint32_t idel_r1 = d_ap_r1 >> 16;
    float frac_r1 = (float)((d_ap_r1 & 65535u) >> 1) * (1.0f / 32768.0f);

    if (env_shm > 0.0005f) {
        int32_t *sp = stereo;
        for (uint32_t i = 0; i < n; i++) {
            float dry_l = (float)sp[0] * (1.0f / 32768.0f);
            float dry_r = (float)sp[1] * (1.0f / 32768.0f);
            float adl = dry_l < 0.0f ? -dry_l : dry_l;
            float adr = dry_r < 0.0f ? -dry_r : dry_r;
            if (adl > in_energy) in_energy = adl;
            if (adr > in_energy) in_energy = adr;

            /* 1. Stereo Input Conditioning: 155 Hz HPF + 8.5 kHz warm converter rolloff */
            hp_in_l += (dry_l - hp_in_l) * 0.0218f;
            hp_in_r += (dry_r - hp_in_r) * 0.0218f;
            float xl = (dry_l - hp_in_l) * 0.42f;
            float xr = (dry_r - hp_in_r) * 0.42f;

            lp_in_l += (xl - lp_in_l) * 0.70f;
            lp_in_r += (xr - lp_in_r) * 0.70f;

            /* 2. Decorrelated Stereo Pre-Delay (22.0 ms L / 24.6 ms R) */
            m->pre_l[wr & 1023u] = fm106_rev_to_i16(lp_in_l);
            m->pre_r[wr & 2047u] = fm106_rev_to_i16(lp_in_r);
            float pd_l = (float)m->pre_l[(wr - 971u)  & 1023u] * FM106_REV_INV_SCALE;
            float pd_r = (float)m->pre_r[(wr - 1087u) & 2047u] * FM106_REV_INV_SCALE;

            /* 3. True-Stereo Input Allpass Diffusers (independent L & R prime chains) */
            float diff_l = fm106_rev_allpass(m->ap_in_l1, wr, 255u, 197u, pd_l,   0.70f);
            diff_l       = fm106_rev_allpass(m->ap_in_l2, wr, 511u, 353u, diff_l, 0.62f);

            float diff_r = fm106_rev_allpass(m->ap_in_r1, wr, 255u, 223u, pd_r,   0.70f);
            diff_r       = fm106_rev_allpass(m->ap_in_r2, wr, 511u, 389u, diff_r, 0.62f);

            /* 4. Dual-Stereo Octave-Up Shimmer Shifter (240 Hz..4.6 kHz bandpass -> +1204c/+1196c) */
            sh_hp_l += (damp_l - sh_hp_l) * 0.0336f;
            sh_hp_r += (damp_r - sh_hp_r) * 0.0336f;
            float sl = damp_l - sh_hp_l;
            float sr = damp_r - sh_hp_r;

            sh_lp1_l += (sl - sh_lp1_l) * 0.48f;
            sh_lp2_l += (sh_lp1_l - sh_lp2_l) * 0.48f;
            sh_lp1_r += (sr - sh_lp1_r) * 0.48f;
            sh_lp2_r += (sh_lp1_r - sh_lp2_r) * 0.48f;

            m->shim_l[wr & 4095u] = fm106_rev_to_i16(sh_lp2_l);
            m->shim_r[wr & 4095u] = fm106_rev_to_i16(sh_lp2_r);

            /* Interleaved L/R grain evaluation (halves dual-grain compute load) */
            if ((wr & 1u) == 0u) {
                raw_sh_l = fm106_rev_shimmer_read(m->shim_l, wr, shim_ph_l);
                shim_ph_l += 2101975u * 2u;  /* +1204 cents */
            } else {
                raw_sh_r = fm106_rev_shimmer_read(m->shim_r, wr, shim_ph_r);
                shim_ph_r += 2092329u * 2u;  /* +1196 cents */
            }

            /* 7.5 kHz velvet air polish on shifted octaves */
            sh_post_l += (raw_sh_l - sh_post_l) * 0.65f;
            sh_post_r += (raw_sh_r - sh_post_r) * 0.65f;
            float shim_out_l = sh_post_l;
            float shim_out_r = sh_post_r;

            /* 5. Slow Tank LFO Modulation: updated every 4 samples */
            if ((wr & 3u) == 0u) {
                lfo1_ph += 30875u * 4u;
                lfo2_ph += 41978u * 4u;
                int32_t m1 = ((int32_t)FM106_SINE[lfo1_ph >> 22] * 13);   /* +-6.5 * 65536 */
                int32_t m2 = ((int32_t)FM106_SINE[lfo2_ph >> 22] * 13);
                d_ap_l1 = (uint32_t)((int32_t)(997u  << 16) + m1);
                d_ap_r1 = (uint32_t)((int32_t)(1361u << 16) + m2);
                idel_l1 = d_ap_l1 >> 16;
                frac_l1 = (float)((d_ap_l1 & 65535u) >> 1) * (1.0f / 32768.0f);
                idel_r1 = d_ap_r1 >> 16;
                frac_r1 = (float)((d_ap_r1 & 65535u) >> 1) * (1.0f / 32768.0f);
            }

            /* 6. Energy-Conserving Cross-Coupled Figure-8 Tank with BA662 Soft Saturation */
            float fb_l = (shim_duck * cross_r + shim_fb * shim_out_r) * decay_g;
            float fb_r = (shim_duck * cross_l + shim_fb * shim_out_l) * decay_g;
            fb_l = fm106_rev_sat_f(fb_l);
            fb_r = fm106_rev_sat_f(fb_r);

            /* Left tank leg */
            float tk_l = fm106_rev_mod_allpass(m->ap_tk_l1, wr, 2047u, idel_l1, frac_l1, diff_l + fb_l, -0.68f);
            m->dly_l1[wr & 8191u] = fm106_rev_to_i16(tk_l);
            float d_l1 = (float)m->dly_l1[(wr - 4453u) & 8191u] * FM106_REV_INV_SCALE;
            damp_l += (d_l1 - damp_l) * damp_g;
            dc_tk_l += (damp_l - dc_tk_l) * 0.0135f;    /* 95 Hz in-loop LF anti-boom cut */
            float tk_l2 = fm106_rev_allpass(m->ap_tk_l2, wr, 4095u, 2659u, damp_l - dc_tk_l, 0.50f);
            m->dly_l2[wr & 4095u] = fm106_rev_to_i16(tk_l2);
            cross_l = (float)m->dly_l2[(wr - 3727u) & 4095u] * FM106_REV_INV_SCALE;

            /* Right tank leg */
            float tk_r = fm106_rev_mod_allpass(m->ap_tk_r1, wr, 2047u, idel_r1, frac_r1, diff_r + fb_r, -0.68f);
            m->dly_r1[wr & 8191u] = fm106_rev_to_i16(tk_r);
            float d_r1 = (float)m->dly_r1[(wr - 4217u) & 8191u] * FM106_REV_INV_SCALE;
            damp_r += (d_r1 - damp_r) * damp_g;
            dc_tk_r += (damp_r - dc_tk_r) * 0.0135f;    /* 95 Hz in-loop LF anti-boom cut */
            float tk_r2 = fm106_rev_allpass(m->ap_tk_r2, wr, 4095u, 1801u, damp_r - dc_tk_r, 0.50f);
            m->dly_r2[wr & 4095u] = fm106_rev_to_i16(tk_r2);
            cross_r = (float)m->dly_r2[(wr - 3163u) & 4095u] * FM106_REV_INV_SCALE;

            float al = cross_l < 0.0f ? -cross_l : cross_l;
            float ar = cross_r < 0.0f ? -cross_r : cross_r;
            if (al > peak_energy) peak_energy = al;
            if (ar > peak_energy) peak_energy = ar;

            /* 7. Decorrelated 6-Tap Stereo Hall Readout + Shimmer Halo */
            int32_t tap_l = (int32_t)m->dly_r1[(wr - 397u)  & 8191u]
                          + (int32_t)m->dly_r1[(wr - 2971u) & 8191u]
                          - (int32_t)m->dly_r2[(wr - 1913u) & 4095u]
                          + (int32_t)m->dly_r2[(wr - 2999u) & 4095u]
                          - (int32_t)m->dly_l1[(wr - 1999u) & 8191u]
                          - (int32_t)m->dly_l2[(wr - 1063u) & 4095u];

            int32_t tap_r = (int32_t)m->dly_l1[(wr - 463u)  & 8191u]
                          + (int32_t)m->dly_l1[(wr - 3631u) & 8191u]
                          - (int32_t)m->dly_l2[(wr - 1229u) & 4095u]
                          + (int32_t)m->dly_l2[(wr - 2677u) & 4095u]
                          - (int32_t)m->dly_r1[(wr - 2111u) & 8191u]
                          - (int32_t)m->dly_r2[(wr - 509u)  & 4095u];

            float wet_l = (float)tap_l * tap_scale + shim_out_l * halo_scale;
            float wet_r = (float)tap_r * tap_scale + shim_out_r * halo_scale;

            /* 8. Equal-Power Wet/Dry Mix + Native Float Soft-Limiter */
            float mix_l = dry_l * dry_g + wet_l * wet_g;
            float mix_r = dry_r * dry_g + wet_r * wet_g;

            sp[0] = (int32_t)(fm106_soft_limit_f(mix_l) * 32768.0f);
            sp[1] = (int32_t)(fm106_soft_limit_f(mix_r) * 32768.0f);
            sp += 2;

            wr++;
        }
    } else {
        /* Pure Hall Reverb (shimmer processing bypassed: 0% shimmer overhead) */
        int32_t *sp = stereo;
        for (uint32_t i = 0; i < n; i++) {
            float dry_l = (float)sp[0] * (1.0f / 32768.0f);
            float dry_r = (float)sp[1] * (1.0f / 32768.0f);
            float adl = dry_l < 0.0f ? -dry_l : dry_l;
            float adr = dry_r < 0.0f ? -dry_r : dry_r;
            if (adl > in_energy) in_energy = adl;
            if (adr > in_energy) in_energy = adr;

            /* 1. Stereo Input Conditioning: 155 Hz HPF + 8.5 kHz warm converter rolloff */
            hp_in_l += (dry_l - hp_in_l) * 0.0218f;
            hp_in_r += (dry_r - hp_in_r) * 0.0218f;
            float xl = (dry_l - hp_in_l) * 0.42f;
            float xr = (dry_r - hp_in_r) * 0.42f;

            lp_in_l += (xl - lp_in_l) * 0.70f;
            lp_in_r += (xr - lp_in_r) * 0.70f;

            /* 2. Decorrelated Stereo Pre-Delay (22.0 ms L / 24.6 ms R) */
            m->pre_l[wr & 1023u] = fm106_rev_to_i16(lp_in_l);
            m->pre_r[wr & 2047u] = fm106_rev_to_i16(lp_in_r);
            float pd_l = (float)m->pre_l[(wr - 971u)  & 1023u] * FM106_REV_INV_SCALE;
            float pd_r = (float)m->pre_r[(wr - 1087u) & 2047u] * FM106_REV_INV_SCALE;

            /* 3. True-Stereo Input Allpass Diffusers (independent L & R prime chains) */
            float diff_l = fm106_rev_allpass(m->ap_in_l1, wr, 255u, 197u, pd_l,   0.70f);
            diff_l       = fm106_rev_allpass(m->ap_in_l2, wr, 511u, 353u, diff_l, 0.62f);

            float diff_r = fm106_rev_allpass(m->ap_in_r1, wr, 255u, 223u, pd_r,   0.70f);
            diff_r       = fm106_rev_allpass(m->ap_in_r2, wr, 511u, 389u, diff_r, 0.62f);

            /* 5. Slow Tank LFO Modulation: updated every 4 samples */
            if ((wr & 3u) == 0u) {
                lfo1_ph += 30875u * 4u;
                lfo2_ph += 41978u * 4u;
                int32_t m1 = ((int32_t)FM106_SINE[lfo1_ph >> 22] * 13);   /* +-6.5 * 65536 */
                int32_t m2 = ((int32_t)FM106_SINE[lfo2_ph >> 22] * 13);
                d_ap_l1 = (uint32_t)((int32_t)(997u  << 16) + m1);
                d_ap_r1 = (uint32_t)((int32_t)(1361u << 16) + m2);
                idel_l1 = d_ap_l1 >> 16;
                frac_l1 = (float)((d_ap_l1 & 65535u) >> 1) * (1.0f / 32768.0f);
                idel_r1 = d_ap_r1 >> 16;
                frac_r1 = (float)((d_ap_r1 & 65535u) >> 1) * (1.0f / 32768.0f);
            }

            /* 6. Pure Figure-8 Tank with BA662 Soft Saturation */
            float fb_l = fm106_rev_sat_f(cross_r * decay_g);
            float fb_r = fm106_rev_sat_f(cross_l * decay_g);

            /* Left tank leg */
            float tk_l = fm106_rev_mod_allpass(m->ap_tk_l1, wr, 2047u, idel_l1, frac_l1, diff_l + fb_l, -0.68f);
            m->dly_l1[wr & 8191u] = fm106_rev_to_i16(tk_l);
            float d_l1 = (float)m->dly_l1[(wr - 4453u) & 8191u] * FM106_REV_INV_SCALE;
            damp_l += (d_l1 - damp_l) * damp_g;
            dc_tk_l += (damp_l - dc_tk_l) * 0.0135f;    /* 95 Hz in-loop LF anti-boom cut */
            float tk_l2 = fm106_rev_allpass(m->ap_tk_l2, wr, 4095u, 2659u, damp_l - dc_tk_l, 0.50f);
            m->dly_l2[wr & 4095u] = fm106_rev_to_i16(tk_l2);
            cross_l = (float)m->dly_l2[(wr - 3727u) & 4095u] * FM106_REV_INV_SCALE;

            /* Right tank leg */
            float tk_r = fm106_rev_mod_allpass(m->ap_tk_r1, wr, 2047u, idel_r1, frac_r1, diff_r + fb_r, -0.68f);
            m->dly_r1[wr & 8191u] = fm106_rev_to_i16(tk_r);
            float d_r1 = (float)m->dly_r1[(wr - 4217u) & 8191u] * FM106_REV_INV_SCALE;
            damp_r += (d_r1 - damp_r) * damp_g;
            dc_tk_r += (damp_r - dc_tk_r) * 0.0135f;    /* 95 Hz in-loop LF anti-boom cut */
            float tk_r2 = fm106_rev_allpass(m->ap_tk_r2, wr, 4095u, 1801u, damp_r - dc_tk_r, 0.50f);
            m->dly_r2[wr & 4095u] = fm106_rev_to_i16(tk_r2);
            cross_r = (float)m->dly_r2[(wr - 3163u) & 4095u] * FM106_REV_INV_SCALE;

            float al = cross_l < 0.0f ? -cross_l : cross_l;
            float ar = cross_r < 0.0f ? -cross_r : cross_r;
            if (al > peak_energy) peak_energy = al;
            if (ar > peak_energy) peak_energy = ar;

            /* 7. Decorrelated 6-Tap Stereo Hall Readout (zero halo overhead) */
            int32_t tap_l = (int32_t)m->dly_r1[(wr - 397u)  & 8191u]
                          + (int32_t)m->dly_r1[(wr - 2971u) & 8191u]
                          - (int32_t)m->dly_r2[(wr - 1913u) & 4095u]
                          + (int32_t)m->dly_r2[(wr - 2999u) & 4095u]
                          - (int32_t)m->dly_l1[(wr - 1999u) & 8191u]
                          - (int32_t)m->dly_l2[(wr - 1063u) & 4095u];

            int32_t tap_r = (int32_t)m->dly_l1[(wr - 463u)  & 8191u]
                          + (int32_t)m->dly_l1[(wr - 3631u) & 8191u]
                          - (int32_t)m->dly_l2[(wr - 1229u) & 4095u]
                          + (int32_t)m->dly_l2[(wr - 2677u) & 4095u]
                          - (int32_t)m->dly_r1[(wr - 2111u) & 8191u]
                          - (int32_t)m->dly_r2[(wr - 509u)  & 4095u];

            float wet_l = (float)tap_l * tap_scale;
            float wet_r = (float)tap_r * tap_scale;

            /* 8. Equal-Power Wet/Dry Mix + Native Float Soft-Limiter */
            float mix_l = dry_l * dry_g + wet_l * wet_g;
            float mix_r = dry_r * dry_g + wet_r * wet_g;

            sp[0] = (int32_t)(fm106_soft_limit_f(mix_l) * 32768.0f);
            sp[1] = (int32_t)(fm106_soft_limit_f(mix_r) * 32768.0f);
            sp += 2;

            wr++;
        }
    }

    r->wr = wr;
    r->lfo1_ph = lfo1_ph;
    r->lfo2_ph = lfo2_ph;
    r->shim_ph_l = shim_ph_l;
    r->shim_ph_r = shim_ph_r;
    r->env_mix = fm106_rev_flush_denorm(env_mix);
    r->env_decay = env_dec;
    r->env_shim = env_shm;
    r->hp_in_l = fm106_rev_flush_denorm(hp_in_l);
    r->hp_in_r = fm106_rev_flush_denorm(hp_in_r);
    r->lp_in_l = fm106_rev_flush_denorm(lp_in_l);
    r->lp_in_r = fm106_rev_flush_denorm(lp_in_r);
    r->damp_l  = fm106_rev_flush_denorm(damp_l);
    r->damp_r  = fm106_rev_flush_denorm(damp_r);
    r->dc_tk_l = fm106_rev_flush_denorm(dc_tk_l);
    r->dc_tk_r = fm106_rev_flush_denorm(dc_tk_r);
    r->cross_l = fm106_rev_flush_denorm(cross_l);
    r->cross_r = fm106_rev_flush_denorm(cross_r);
    r->sh_hp_l = fm106_rev_flush_denorm(sh_hp_l);
    r->sh_hp_r = fm106_rev_flush_denorm(sh_hp_r);
    r->sh_lp1_l = fm106_rev_flush_denorm(sh_lp1_l);
    r->sh_lp2_l = fm106_rev_flush_denorm(sh_lp2_l);
    r->sh_lp1_r = fm106_rev_flush_denorm(sh_lp1_r);
    r->sh_lp2_r = fm106_rev_flush_denorm(sh_lp2_r);
    r->sh_post_l = fm106_rev_flush_denorm(sh_post_l);
    r->sh_post_r = fm106_rev_flush_denorm(sh_post_r);

    /* Tail active flag: holds UI graphics or flash write */
    r->tail_active = (r->on && r->mix > 0u && r->env_mix > 0.001f && peak_energy > 0.0003f) ? 1u : 0u;

    /* Auto-sleep once dry input is silent and tank tail decays below audibility */
    if (in_energy < 0.0001f && peak_energy < 0.0002f) {
        if (++r->silent_blocks > 32u) {
            fm106_reverb_clear_state(r);
        }
    } else {
        r->silent_blocks = 0;
    }
}

typedef void (*fm106_reverb_fn_t)(fm106_reverb_t *, int32_t *, uint32_t);
static volatile fm106_reverb_fn_t g_reverb_fn = fm106_reverb_kernel;

static inline void fm106_reverb_run(fm106_reverb_t *r, int32_t *stereo, uint32_t n)
{
    if (!r->on || r->mix == 0u) {
        if (r->sleeping) return;
    }
    if (r->sleeping) {
        /* Quick peak scan on incoming audio to wake up: 0% CPU while silent! */
        int32_t peak = 0;
        for (uint32_t i = 0; i < n * 2u; i += 8) {
            int32_t s = stereo[i] >= 0 ? stereo[i] : -stereo[i];
            if (s > peak) peak = s;
        }
        if (peak < 16) return;
        r->sleeping = 0;
    }
    fm106_reverb_fn_t fn = g_reverb_fn;
    fn(r, stereo, n);
}

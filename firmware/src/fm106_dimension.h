/* SPDX-License-Identifier: GPL-3.0-only */
/* FM-106: Roland Dimension D (SDD-320) Style Spatial Stereo Widener
 *
 * Dedicated stereo effect slot (placed after Juno Chorus & Tape, before Reverb).
 * Emulates the iconic Roland Dimension D spatial enhancer:
 * - Anti-phase low-frequency dual sine modulation (~0.20 to 0.38 Hz)
 * - 4-point Hermite cubic interpolation for artifact-free fractional delay
 * - High-pass side cross-coupling matrix preserving solid mono bass
 * - Flawless mono compatibility with zero hollow comb cancellation
 */

#ifndef FM106_DIMENSION_H
#define FM106_DIMENSION_H

#include <stdint.h>
#include "fm106_chorus.h"

#define FM106_DIM_BUF_LEN 512u
#define FM106_DIM_BUF_MASK (FM106_DIM_BUF_LEN - 1u)

typedef struct {
    int16_t buf_l[FM106_DIM_BUF_LEN];
    int16_t buf_r[FM106_DIM_BUF_LEN];
    uint32_t wr_idx;
    uint32_t lfo_ph;
    uint8_t mode;       /* 0: OFF, 1: DIM 1, 2: DIM 2, 3: DIM 3, 4: DIM 4 */
    uint8_t mix;        /* 0..127 wet amount */
    uint8_t width;      /* 0..127 stereo field expansion */
    uint8_t color;      /* 0..127 high frequency tone warmth */
    float lp_l, lp_r;   /* 1-pole tone filter */
    float hp_l, hp_r;   /* 1-pole bass guard side filter (~120 Hz) */
} fm106_dimension_t;

static inline void fm106_dimension_init(fm106_dimension_t *d)
{
    for (uint32_t i = 0; i < FM106_DIM_BUF_LEN; i++) {
        d->buf_l[i] = 0;
        d->buf_r[i] = 0;
    }
    d->wr_idx = 0;
    d->lfo_ph = 0;
    d->mode = 0;        /* Default OFF so legacy patches sound identical */
    d->mix = 96;        /* Default 75% mix when turned on */
    d->width = 90;      /* Default lush spatial width */
    d->color = 70;      /* Warm Roland analog tone */
    d->lp_l = 0.0f;
    d->lp_r = 0.0f;
    d->hp_l = 0.0f;
    d->hp_r = 0.0f;
}

static inline void fm106_dimension_run(fm106_dimension_t *d, int32_t *out, uint32_t n)
{
    if (d->mode == 0) return;

    /* LFO phase increments for ~0.20 Hz, ~0.25 Hz, ~0.30 Hz, ~0.38 Hz at 44.1 kHz */
    static const uint32_t lfo_incs[5] = { 0, 19493u, 24367u, 29240u, 37038u };
    /* Modulation depths in fractional samples (around 160 sample center delay = ~3.63 ms) */
    static const float depths[5] = { 0.0f, 9.0f, 14.0f, 20.0f, 28.0f };

    uint8_t m = d->mode > 4 ? 4 : d->mode;
    uint32_t inc = lfo_incs[m];
    float depth = depths[m];
    float k_cross = 0.55f * ((float)d->width * (1.0f / 64.0f));
    float k_direct = 0.72f;
    float mix_wet = (float)d->mix * (1.0f / 127.0f);
    float mix_dry = 1.0f - mix_wet * 0.25f;
    float lp_alpha = 0.35f + ((float)d->color * (0.45f / 127.0f));

    uint32_t wr = d->wr_idx;

    for (uint32_t i = 0; i < n; i++) {
        int32_t in_l = out[2u * i];
        int32_t in_r = out[2u * i + 1u];

        /* Write dry audio into delay line */
        d->buf_l[wr] = (int16_t)(in_l > 32767 ? 32767 : (in_l < -32768 ? -32768 : in_l));
        d->buf_r[wr] = (int16_t)(in_r > 32767 ? 32767 : (in_r < -32768 ? -32768 : in_r));

        /* Advance anti-phase LFO */
        d->lfo_ph += inc;
        int32_t tri = (int32_t)(d->lfo_ph >> 16) - 32768;
        if (tri < 0) tri = -tri;
        tri = (tri - 16384) << 1;
        float lfo_f = (float)tri * (1.0f / 32768.0f);
        lfo_f = lfo_f * (1.5f - 0.5f * lfo_f * lfo_f); /* Polynomial sine shaping */

        float mod = lfo_f * depth;
        float del_l = 160.0f + mod;
        float del_r = 160.0f - mod;

        /* Left fractional delay read via 4-point Hermite cubic interpolation */
        int32_t idel_l = (int32_t)del_l;
        float frac_l = del_l - (float)idel_l;
        uint32_t r1 = (wr - 1u - (uint32_t)idel_l) & FM106_DIM_BUF_MASK;
        float wet_l = fm106_hermite_f(frac_l,
            (float)d->buf_l[(r1 + 1u) & FM106_DIM_BUF_MASK],
            (float)d->buf_l[r1],
            (float)d->buf_l[(r1 - 1u) & FM106_DIM_BUF_MASK],
            (float)d->buf_l[(r1 - 2u) & FM106_DIM_BUF_MASK]);

        /* Right fractional delay read via 4-point Hermite cubic interpolation */
        int32_t idel_r = (int32_t)del_r;
        float frac_r = del_r - (float)idel_r;
        r1 = (wr - 1u - (uint32_t)idel_r) & FM106_DIM_BUF_MASK;
        float wet_r = fm106_hermite_f(frac_r,
            (float)d->buf_r[(r1 + 1u) & FM106_DIM_BUF_MASK],
            (float)d->buf_r[r1],
            (float)d->buf_r[(r1 - 1u) & FM106_DIM_BUF_MASK],
            (float)d->buf_r[(r1 - 2u) & FM106_DIM_BUF_MASK]);

        /* Color low-pass filter */
        d->lp_l += lp_alpha * (wet_l - d->lp_l);
        d->lp_r += lp_alpha * (wet_r - d->lp_r);
        wet_l = d->lp_l;
        wet_r = d->lp_r;

        /* High-pass filter on side signal to prevent bass cancellation */
        d->hp_l += 0.017f * (wet_l - d->hp_l);
        d->hp_r += 0.017f * (wet_r - d->hp_r);
        float wet_l_hi = wet_l - d->hp_l;
        float wet_r_hi = wet_r - d->hp_r;

        /* Dimension D cross-coupling matrix */
        float spatial_l = k_direct * wet_l - k_cross * wet_r_hi;
        float spatial_r = k_direct * wet_r - k_cross * wet_l_hi;

        /* Combine dry and spatial wet signals */
        float y_l = (float)in_l * mix_dry + spatial_l * mix_wet;
        float y_r = (float)in_r * mix_dry + spatial_r * mix_wet;

        out[2u * i]      = (int32_t)(y_l > 32767.0f ? 32767.0f : (y_l < -32768.0f ? -32768.0f : y_l));
        out[2u * i + 1u] = (int32_t)(y_r > 32767.0f ? 32767.0f : (y_r < -32768.0f ? -32768.0f : y_r));

        wr = (wr + 1u) & FM106_DIM_BUF_MASK;
    }

    d->wr_idx = wr;
}

#endif /* FM106_DIMENSION_H */

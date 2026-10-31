/* SPDX-License-Identifier: GPL-3.0-only */
/* FM-106: Roland MN3009 BBD Stereo Chorus Emulation
 * Replicates the hardware:
 * - 256-stage dual BBD fractional delay lines
 * - LFO frequencies: Chorus I = 0.393 Hz, Chorus II = 0.797 Hz, Chorus I+II = 8.00 Hz
 * - Antiphase modulation (+LFO for Left, -LFO for Right)
 * - 2nd-order Butterworth reconstruction biquad filter (9,661 Hz)
 * - Calibrated IC6 summer gains (kDry = 0.863, kWet = 1.257) */
#pragma once
#include <stdint.h>
#include "fm106_tables.h"
#include "fm106_osc.h"
#include "fm106_vcf.h"

#define FM106_CHORUS_LEN 1024u
#define FM106_CHORUS_MASK (FM106_CHORUS_LEN - 1u)

enum {
    FM106_CHORUS_OFF = 0,
    FM106_CHORUS_I = 1,
    FM106_CHORUS_II = 2,
    FM106_CHORUS_I_II = 3
};

/* 2nd-order Butterworth lowpass biquad (9,661 Hz at 44.1 kHz) in 32-bit float:
 * Eliminates Q15 fixed-point truncation noise and recursive limit cycles. */
typedef struct {
    float ic1;
    float ic2;
} fm106_bbd_filter_t;

static inline void fm106_bbd_filter_init(fm106_bbd_filter_t *f)
{
    f->ic1 = 0.0f;
    f->ic2 = 0.0f;
}

/* Round-to-nearest float->int */
static inline __attribute__((always_inline)) int32_t fm106_f2i_round(float x)
{
    return (int32_t)(x + (x >= 0.0f ? 0.5f : -0.5f));
}

/* g ~= 0.826078f, a1 ~= 0.351990f in single-precision floating point */
static inline float fm106_bbd_filter_process(fm106_bbd_filter_t *f, float in)
{
    float v3 = in - f->ic2;
    float v1 = 0.35198975f * f->ic1 + 0.29074097f * v3;
    float v2 = f->ic2 + 0.8260784f * v1;
    f->ic1 = 2.0f * v1 - f->ic1;
    f->ic2 = 2.0f * v2 - f->ic2;
    return v2;
}

/* 4-point Hermite interpolation on the single-precision FPU:
 * Exact C1-continuous fractional delay read returning float */
static inline __attribute__((always_inline)) float fm106_hermite_f(int32_t frac_q15, float y0, float y1, float y2, float y3)
{
    float t  = (float)frac_q15 * (1.0f / 32768.0f);
    float c1 = 0.5f * (y2 - y0);
    float c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
    float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
    return (((c3 * t + c2) * t + c1) * t) + y1;
}

typedef struct {
    int16_t buf_l[FM106_CHORUS_LEN];
    int16_t buf_r[FM106_CHORUS_LEN];
    uint32_t wr_idx;
    uint32_t lfo_ph;
    uint8_t mode;               /* FM106_CHORUS_* */
    fm106_bbd_filter_t filt_l;
    fm106_bbd_filter_t filt_r;
} fm106_chorus_t;

static inline void fm106_chorus_init(fm106_chorus_t *c)
{
    c->wr_idx = 0;
    c->lfo_ph = 0;
    c->mode = FM106_CHORUS_I;
    fm106_bbd_filter_init(&c->filt_l);
    fm106_bbd_filter_init(&c->filt_r);
    for (uint32_t i = 0; i < FM106_CHORUS_LEN; i++) {
        c->buf_l[i] = 0;
        c->buf_r[i] = 0;
    }
}

static inline void fm106_chorus_process(fm106_chorus_t *c, int32_t mono_in, int32_t *out_l, int32_t *out_r)
{
    if (c->mode == FM106_CHORUS_OFF) {
        *out_l = fm106_soft_limit(mono_in);
        *out_r = fm106_soft_limit(mono_in);
        return;
    }

    /* Soft limit input to preserve analog headroom and dry:wet ratio */
    int32_t in_sat = fm106_soft_limit(mono_in);

    /* Store incoming audio in delay rings */
    uint32_t wr = c->wr_idx;
    c->buf_l[wr] = (int16_t)in_sat;
    c->buf_r[wr] = (int16_t)in_sat;
    c->wr_idx = (wr + 1u) & FM106_CHORUS_MASK;

    /* LFO parameters according to mode */
    uint32_t lfo_inc;
    int32_t center_samples = 154;   /* ~3.5 ms at 44.1 kHz */
    int32_t depth_samples;          /* modulation swing */

    switch (c->mode) {
    case FM106_CHORUS_I:
        lfo_inc = 50098u;           /* 0.514 Hz (Juno-106 hardware rate) */
        depth_samples = 94;         /* +-2.13 ms (4.26 ms pp) */
        break;
    case FM106_CHORUS_II:
        lfo_inc = 82068u;           /* 0.842 Hz */
        depth_samples = 75;         /* +-1.71 ms (3.42 ms pp) */
        break;
    case FM106_CHORUS_I_II:
    default:
        lfo_inc = 765109u;          /* 7.85 Hz fast vibrato */
        depth_samples = 10;         /* +-0.236 ms */
        break;
    }

    c->lfo_ph += lfo_inc;

    /* Triangle LFO: -32768..32767 */
    int32_t lfo_tri = (int32_t)(c->lfo_ph >> 16) - 32768;
    if (lfo_tri < 0) lfo_tri = -lfo_tri;
    lfo_tri = (lfo_tri - 16384) << 1;

    /* Antiphase modulation (16.16 fixed point) */
    int32_t delay_l_q16 = (center_samples << 16) + lfo_tri * depth_samples;
    int32_t delay_r_q16 = (center_samples << 16) - lfo_tri * depth_samples;

    /* 4-point Hermite interpolation for C1-continuous fractional delay taps (eliminates zipper noise) */
    int32_t int_del_l = delay_l_q16 >> 16;
    int32_t frac_l = (delay_l_q16 >> 1) & 0x7FFFu;
    uint32_t rd_l1 = (wr - 1u - (uint32_t)int_del_l) & FM106_CHORUS_MASK;
    uint32_t rd_l0 = (rd_l1 + 1u) & FM106_CHORUS_MASK;
    uint32_t rd_l2 = (rd_l1 - 1u) & FM106_CHORUS_MASK;
    uint32_t rd_l3 = (rd_l1 - 2u) & FM106_CHORUS_MASK;
    float wet_l = fm106_hermite_f(frac_l, (float)c->buf_l[rd_l0], (float)c->buf_l[rd_l1], (float)c->buf_l[rd_l2], (float)c->buf_l[rd_l3]);

    /* Read Right tap */
    int32_t int_del_r = delay_r_q16 >> 16;
    int32_t frac_r = (delay_r_q16 >> 1) & 0x7FFFu;
    uint32_t rd_r1 = (wr - 1u - (uint32_t)int_del_r) & FM106_CHORUS_MASK;
    uint32_t rd_r0 = (rd_r1 + 1u) & FM106_CHORUS_MASK;
    uint32_t rd_r2 = (rd_r1 - 1u) & FM106_CHORUS_MASK;
    uint32_t rd_r3 = (rd_r1 - 2u) & FM106_CHORUS_MASK;
    float wet_r = fm106_hermite_f(frac_r, (float)c->buf_r[rd_r0], (float)c->buf_r[rd_r1], (float)c->buf_r[rd_r2], (float)c->buf_r[rd_r3]);

    /* Apply Butterworth reconstruction filter in float */
    wet_l = fm106_bbd_filter_process(&c->filt_l, wet_l);
    wet_r = fm106_bbd_filter_process(&c->filt_r, wet_r);

    /* IC6 summer output: authentic Roland dry:wet ratio (0.863 : 1.257 = 21209 : 30892 in Q15) */
    float dry_f = (float)in_sat * (21209.0f / 32768.0f);
    int32_t sl = fm106_f2i_round(dry_f + wet_l * (30892.0f / 32768.0f));
    int32_t sr = fm106_f2i_round(dry_f + wet_r * (30892.0f / 32768.0f));

    *out_l = fm106_soft_limit(sl);
    *out_r = fm106_soft_limit(sr);
}

/* SPDX-License-Identifier: GPL-3.0-only */
/* FM-106: Roland MN3009 BBD Stereo Chorus Emulation
 * Replicates the hardware (Florian Anwander reference):
 * - 256-stage dual BBD fractional delay lines
 * - LFO frequencies & modulation amounts:
 *     Chorus I:    0.514 Hz, depth 94 samples (~2.13 ms, 100% amount, antiphase)
 *     Chorus II:   0.842 Hz, depth 75 samples (~1.71 ms, 100% amount, antiphase)
 *     Chorus I+II: 1.000 Hz, depth  8 samples (~0.18 ms,   8% amount, in-phase vibrato)
 * - Antiphase modulation for I & II; in-phase modulation for I+II ("fast mode")
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

/* 2nd-order Butterworth lowpass biquad (9,661 Hz at 44.1 kHz) */
typedef struct {
    int32_t ic1;
    int32_t ic2;
} fm106_bbd_filter_t;

static inline void fm106_bbd_filter_init(fm106_bbd_filter_t *f)
{
    f->ic1 = 0;
    f->ic2 = 0;
}

/* g ~= 0.826 (27066 in Q15), a1 ~= 0.352 (11534 in Q15) */
static inline int32_t fm106_bbd_filter_process(fm106_bbd_filter_t *f, int32_t in)
{
    int32_t v3 = in - f->ic2;
    int32_t v1 = ((11534 * f->ic1) >> 15) + ((9527 * v3) >> 15);
    int32_t v2 = f->ic2 + ((27066 * v1) >> 15);
    f->ic1 = (v1 << 1) - f->ic1;
    f->ic2 = (v2 << 1) - f->ic2;
    return v2;
}

/* 4-point Hermite interpolation in Q15: C1-continuous fractional delay read */
static inline int32_t fm106_hermite(int32_t frac_q15, int32_t y0, int32_t y1, int32_t y2, int32_t y3)
{
    int32_t c0 = y1;
    int32_t c1 = (y2 - y0) >> 1;
    int32_t c2 = y0 - y1 + (y2 - y1) - ((y1 - (y2 << 1) + y3) >> 1);
    int32_t c3 = ((y3 - y0) >> 1) + y1 - y2 + ((y1 - y2) >> 1);
    int32_t val = (int32_t)(((int64_t)c3 * frac_q15) >> 15);
    val = (int32_t)(((int64_t)(val + c2) * frac_q15) >> 15);
    val = (int32_t)(((int64_t)(val + c1) * frac_q15) >> 15);
    return val + c0;
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
        lfo_inc = 50098u;           /* 0.514 Hz (Juno-106 hardware rate, 100% amount) */
        depth_samples = 94;         /* +-2.13 ms (4.26 ms pp) */
        break;
    case FM106_CHORUS_II:
        lfo_inc = 82068u;           /* 0.842 Hz (100% amount) */
        depth_samples = 75;         /* +-1.71 ms (3.42 ms pp) */
        break;
    case FM106_CHORUS_I_II:
    default:
        lfo_inc = 97391u;           /* 1.000 Hz (Florian Anwander Juno-60 reference: 1 Hz) */
        depth_samples = 8;          /* 8% amount (+-0.18 ms subtle ensemble vibrato) */
        break;
    }

    c->lfo_ph += lfo_inc;

    /* Triangle LFO: -32768..32767 */
    int32_t lfo_tri = (int32_t)(c->lfo_ph >> 16) - 32768;
    if (lfo_tri < 0) lfo_tri = -lfo_tri;
    lfo_tri = (lfo_tri - 16384) << 1;

    /* Antiphase modulation for Chorus I & II; in-phase for I+II ("fast mode") */
    int32_t delay_l_q16 = (center_samples << 16) + lfo_tri * depth_samples;
    int32_t delay_r_q16 = (c->mode == FM106_CHORUS_I_II)
                        ? delay_l_q16
                        : (center_samples << 16) - lfo_tri * depth_samples;

    /* 4-point Hermite interpolation for C1-continuous fractional delay taps (eliminates zipper noise) */
    int32_t int_del_l = delay_l_q16 >> 16;
    int32_t frac_l = (delay_l_q16 >> 1) & 0x7FFFu;
    uint32_t rd_l1 = (wr - 1u - (uint32_t)int_del_l) & FM106_CHORUS_MASK;
    uint32_t rd_l0 = (rd_l1 + 1u) & FM106_CHORUS_MASK;
    uint32_t rd_l2 = (rd_l1 - 1u) & FM106_CHORUS_MASK;
    uint32_t rd_l3 = (rd_l1 - 2u) & FM106_CHORUS_MASK;
    int32_t wet_l = fm106_hermite(frac_l, c->buf_l[rd_l0], c->buf_l[rd_l1], c->buf_l[rd_l2], c->buf_l[rd_l3]);

    /* Read Right tap */
    int32_t int_del_r = delay_r_q16 >> 16;
    int32_t frac_r = (delay_r_q16 >> 1) & 0x7FFFu;
    uint32_t rd_r1 = (wr - 1u - (uint32_t)int_del_r) & FM106_CHORUS_MASK;
    uint32_t rd_r0 = (rd_r1 + 1u) & FM106_CHORUS_MASK;
    uint32_t rd_r2 = (rd_r1 - 1u) & FM106_CHORUS_MASK;
    uint32_t rd_r3 = (rd_r1 - 2u) & FM106_CHORUS_MASK;
    int32_t wet_r = fm106_hermite(frac_r, c->buf_r[rd_r0], c->buf_r[rd_r1], c->buf_r[rd_r2], c->buf_r[rd_r3]);

    /* Apply Butterworth reconstruction filter */
    wet_l = fm106_bbd_filter_process(&c->filt_l, wet_l);
    wet_r = fm106_bbd_filter_process(&c->filt_r, wet_r);

    /* IC6 summer output: authentic Roland dry:wet ratio (0.863 : 1.257 = 21209 : 30892 in Q15) */
    int32_t sl = (int32_t)(((int64_t)in_sat * 21209LL + (int64_t)wet_l * 30892LL) >> 15);
    int32_t sr = (int32_t)(((int64_t)in_sat * 21209LL + (int64_t)wet_r * 30892LL) >> 15);

    *out_l = fm106_soft_limit(sl);
    *out_r = fm106_soft_limit(sr);
}

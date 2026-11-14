/* SPDX-License-Identifier: GPL-3.0-only */
/* FM-106: Roland MN3009 BBD Stereo Chorus & Analog Jack Board Emulation
 * Replicates the hardware:
 * - Dual 256-stage Panasonic MN3009 BBD fractional delay lines driven by MN3101 clocking
 * - Dual-filter topology:
 *     Pre-BBD anti-aliasing 2nd-order Butterworth biquad (9,661 Hz)
 *     Post-BBD reconstruction 2nd-order Butterworth biquads (9,661 Hz) Left & Right
 *     Together forming authentic 24 dB/oct cumulative roll-off for warm, silky analog highs
 * - Jack Board analog output stage (17.5 kHz 1-pole TPT line driver & RF suppression model):
 *     Models op-amp summing feedback caps, output transistor slew, RF bypass caps and cable loading,
 *     providing the signature natural analog high-end smoothness across both Dry and Wet modes.
 * - LFO frequencies & hardware-calibrated modulation sweeps (at 44.1 kHz):
 *     Chorus I:    0.514 Hz, depth 81 samples (~1.845 ms, 1.66 ms - 5.35 ms, antiphase)
 *     Chorus II:   0.863 Hz, depth 75 samples (~1.705 ms, 1.79 ms - 5.19 ms, antiphase)
 *     Chorus I+II: 9.750 Hz, depth 9 samples (~0.204 ms, 3.29 ms - 3.70 ms, in-phase Leslie vibrato)
 * - Antiphase modulation for Chorus I & II; in-phase sine-smoothed modulation for I+II ("Leslie rotary mode")
 * - 4-point Hermite cubic interpolation for C1-continuous fractional delay taps (zero zipper noise)
 * - Calibrated IC6 summer gains (kDry = 0.863, kWet = 1.257 = 21209 : 30892 in Q15) */
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

/* 1-pole TPT analog line-driver model (17.5 kHz at 44.1 kHz):
 * Replicates the Jack Board RF bypass caps, op-amp slew and cable loading */
typedef struct {
    float s;
} fm106_analog_stage_t;

static inline void fm106_analog_stage_init(fm106_analog_stage_t *f)
{
    f->s = 0.0f;
}

static inline __attribute__((always_inline)) float fm106_analog_stage_process(fm106_analog_stage_t *f, float in)
{
    /* G = g / (1 + g) ~= 0.749453f for fc = 17.5 kHz at 44.1 kHz */
    float v = 0.749453f * (in - f->s);
    float y = v + f->s;
    f->s = y + v;
    if (__builtin_expect(f->s > -1.0e-15f && f->s < 1.0e-15f, 0)) f->s = 0.0f;
    return y;
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
    if (__builtin_expect(f->ic1 > -1.0e-15f && f->ic1 < 1.0e-15f, 0)) f->ic1 = 0.0f;
    if (__builtin_expect(f->ic2 > -1.0e-15f && f->ic2 < 1.0e-15f, 0)) f->ic2 = 0.0f;
    return v2;
}

/* 4-point Hermite interpolation on the single-precision FPU:
 * Exact C1-continuous fractional delay read returning float */
static inline __attribute__((always_inline)) float fm106_hermite_f(float t, float y0, float y1, float y2, float y3)
{
    float c1 = 0.5f * (y2 - y0);
    float c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
    float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
    return (((c3 * t + c2) * t + c1) * t) + y1;
}

typedef struct {
    int16_t buf[FM106_CHORUS_LEN];
    uint32_t wr_idx;
    uint32_t lfo_ph;
    uint8_t mode;                /* FM106_CHORUS_* */
    fm106_bbd_filter_t filt_in;  /* Pre-BBD anti-aliasing filter */
    fm106_bbd_filter_t filt_l;   /* Post-BBD reconstruction filter Left */
    fm106_bbd_filter_t filt_r;   /* Post-BBD reconstruction filter Right */
    fm106_analog_stage_t line_l; /* Jack Board analog line driver Left */
    fm106_analog_stage_t line_r; /* Jack Board analog line driver Right */
} fm106_chorus_t;

static inline void fm106_chorus_init(fm106_chorus_t *c)
{
    c->wr_idx = 0;
    c->lfo_ph = 0;
    c->mode = FM106_CHORUS_I;
    fm106_bbd_filter_init(&c->filt_in);
    fm106_bbd_filter_init(&c->filt_l);
    fm106_bbd_filter_init(&c->filt_r);
    fm106_analog_stage_init(&c->line_l);
    fm106_analog_stage_init(&c->line_r);
    for (uint32_t i = 0; i < FM106_CHORUS_LEN; i++) {
        c->buf[i] = 0;
    }
}

static inline void fm106_chorus_process(fm106_chorus_t *c, int32_t mono_in, int32_t *out_l, int32_t *out_r)
{
    if (c->mode == FM106_CHORUS_OFF) {
        float mono_f = (float)fm106_soft_limit(mono_in);
        float out_l_f = fm106_analog_stage_process(&c->line_l, mono_f);
        float out_r_f = fm106_analog_stage_process(&c->line_r, mono_f);
        *out_l = fm106_f2i_round(out_l_f);
        *out_r = fm106_f2i_round(out_r_f);
        return;
    }

    /* Soft limit input to preserve analog headroom and dry:wet ratio */
    int32_t in_sat = fm106_soft_limit(mono_in);

    /* Pre-BBD anti-aliasing filter (9,661 Hz 2nd-order Butterworth biquad) */
    float in_filtered = fm106_bbd_filter_process(&c->filt_in, (float)in_sat);
    int32_t in_bbd = fm106_f2i_round(in_filtered);

    /* Store incoming anti-aliased audio in delay ring (shared buffer for L/R antiphase taps) */
    uint32_t wr = c->wr_idx;
    c->buf[wr] = (int16_t)in_bbd;
    c->wr_idx = (wr + 1u) & FM106_CHORUS_MASK;

    /* LFO parameters according to mode */
    uint32_t lfo_inc;
    int32_t center_samples = 154;   /* ~3.492 ms at 44.1 kHz */
    int32_t depth_samples;          /* modulation swing */

    switch (c->mode) {
    case FM106_CHORUS_I:
        lfo_inc = 50098u;           /* 0.514 Hz (Juno-106 hardware rate, ~0.5 Hz) */
        depth_samples = 81;         /* +-1.845 ms (3.69 ms pp, min 1.66 ms, max 5.35 ms) */
        break;
    case FM106_CHORUS_II:
        lfo_inc = 84114u;           /* 0.863 Hz (Juno-106 hardware rate, ~0.83-0.86 Hz) */
        depth_samples = 75;         /* +-1.705 ms (3.41 ms pp, min 1.79 ms, max 5.19 ms) */
        break;
    case FM106_CHORUS_I_II:
    default:
        lfo_inc = 950112u;          /* 9.75 Hz fast Leslie rotary vibrato (Juno hardware) */
        depth_samples = 9;          /* +-0.204 ms (min 3.29 ms, max 3.70 ms) */
        break;
    }

    c->lfo_ph += lfo_inc;

    /* Triangle LFO: -1.0f .. +1.0f in floating-point */
    int32_t lfo_tri = (int32_t)(c->lfo_ph >> 16) - 32768;
    if (lfo_tri < 0) lfo_tri = -lfo_tri;
    lfo_tri = (lfo_tri - 16384) << 1;
    float lfo_f = (float)lfo_tri * (1.0f / 32768.0f);

    /* For Chorus I+II (fast rotary mode at 9.75 Hz), the analog RC timing networks
     * smooth the sharp triangle turnaround corners into a smooth sine-like waveform:
     * 1.5 * x - 0.5 * x^3 provides a continuous, zero-derivative peak turnaround. */
    if (c->mode == FM106_CHORUS_I_II) {
        lfo_f = lfo_f * (1.5f - 0.5f * lfo_f * lfo_f);
    }

    float mod_samples = lfo_f * (float)depth_samples;
    float delay_l = (float)center_samples + mod_samples;
    float delay_r = (c->mode == FM106_CHORUS_I_II)
                  ? delay_l
                  : ((float)center_samples - mod_samples);

    /* 4-point Hermite interpolation for C1-continuous fractional delay taps (eliminates zipper noise) */
    int32_t int_del_l = (int32_t)delay_l;
    float frac_l_f = delay_l - (float)int_del_l;
    uint32_t rd_l1 = (wr - 1u - (uint32_t)int_del_l) & FM106_CHORUS_MASK;
    uint32_t rd_l0 = (rd_l1 + 1u) & FM106_CHORUS_MASK;
    uint32_t rd_l2 = (rd_l1 - 1u) & FM106_CHORUS_MASK;
    uint32_t rd_l3 = (rd_l1 - 2u) & FM106_CHORUS_MASK;
    float wet_l = fm106_hermite_f(frac_l_f, (float)c->buf[rd_l0], (float)c->buf[rd_l1], (float)c->buf[rd_l2], (float)c->buf[rd_l3]);

    /* Read Right tap */
    int32_t int_del_r = (int32_t)delay_r;
    float frac_r_f = delay_r - (float)int_del_r;
    uint32_t rd_r1 = (wr - 1u - (uint32_t)int_del_r) & FM106_CHORUS_MASK;
    uint32_t rd_r0 = (rd_r1 + 1u) & FM106_CHORUS_MASK;
    uint32_t rd_r2 = (rd_r1 - 1u) & FM106_CHORUS_MASK;
    uint32_t rd_r3 = (rd_r1 - 2u) & FM106_CHORUS_MASK;
    float wet_r = fm106_hermite_f(frac_r_f, (float)c->buf[rd_r0], (float)c->buf[rd_r1], (float)c->buf[rd_r2], (float)c->buf[rd_r3]);

    /* Apply Butterworth reconstruction filter in float */
    wet_l = fm106_bbd_filter_process(&c->filt_l, wet_l);
    wet_r = fm106_bbd_filter_process(&c->filt_r, wet_r);

    /* IC6 summer output: authentic Roland dry:wet ratio (0.863 : 1.257 = 21209 : 30892 in Q15) */
    float dry_f = (float)in_sat * (21209.0f / 32768.0f);
    float mix_l = dry_f + wet_l * (30892.0f / 32768.0f);
    float mix_r = dry_f + wet_r * (30892.0f / 32768.0f);

    /* Jack Board analog output stage (17.5 kHz 1-pole line driver & RF suppression model) */
    float out_smooth_l = fm106_analog_stage_process(&c->line_l, mix_l);
    float out_smooth_r = fm106_analog_stage_process(&c->line_r, mix_r);

    *out_l = fm106_soft_limit_f32(out_smooth_l);
    *out_r = fm106_soft_limit_f32(out_smooth_r);
}


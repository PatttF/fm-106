/* SPDX-License-Identifier: GPL-3.0-only */
/* FM-106: Roland Juno-106 IR3109 / BA662 OTA 4-Pole TPT Ladder Filter & HPF
 * Replicates the hardware:
 * - D7811G VCF cutoff DAC calculation ($04D5..$064E)
 * - kV4Hz[4096] measured Juno-106 voice card V4 frequency profile
 * - ResK_J106 quartic polynomial resonance curve with self-oscillation
 * - Circuit-accurate BA662 OTA differential pair summing and input compensation
 * - 4-pole TPT nonlinear ladder stages with OTA saturation
 * - 4-position HPF (Bass Boost low shelf, Flat, 236 Hz, 754 Hz) */
#pragma once
#include <stdint.h>
#include "fm106_tables.h"

/* Fast clamp */
static inline int32_t fm106_clamp(int32_t v, int32_t lo, int32_t hi)
{
    return v < lo ? lo : v > hi ? hi : v;
}

/* Rational analog soft limiter:
 * - Transparent linear pass-through up to 24576 (-2.5 dBFS)
 * - Smooth rational compression curve asymptotically approaching 32000 (< 32767)
 * - Zero hard digital clipping, zero harsh odd harmonics */
static inline int32_t fm106_soft_limit(int32_t x)
{
    int32_t ax = x < 0 ? -x : x;
    if (ax <= 24576) {
        return x;
    }
    int32_t excess = ax - 24576;
    int32_t ex_scaled = excess >> 5;
    int32_t den = 232 + ex_scaled;
    int32_t num = ex_scaled * 7424;
    int32_t compressed = 24576 + (num / den);
    return x < 0 ? -compressed : compressed;
}

/* BA662 OTA Padé differential saturation: input Q15 (0..3.0, where 3.0 = 98304) -> Q15 */
static inline int32_t fm106_ota_sat(int32_t x)
{
    int32_t a = x < 0 ? -x : x;
    int32_t i = a / 384;   /* 256 segments across 0..3.0 */
    int32_t y;
    if (i >= 256) {
        y = 32767;
    } else {
        int32_t rem = a - i * 384;
        y = (int32_t)FM106_OTA_SAT[i] + (((int32_t)(FM106_OTA_SAT[i + 1] - FM106_OTA_SAT[i]) * rem) / 384);
    }
    return x < 0 ? -y : y;
}

/* 16-bit add matching uPD7811 DADDNC EA,BC */
static inline uint16_t fm106_vcf_add(uint16_t ea, uint16_t bc, uint8_t *overflow)
{
    uint32_t res = (uint32_t)ea + bc;
    if (res > 0xFFFFu) *overflow = 0;
    return (uint16_t)res;
}

/* 16-bit subtract matching uPD7811 DSUBNB EA,BC */
static inline uint16_t fm106_vcf_sub(uint16_t ea, uint16_t bc, uint8_t *overflow)
{
    if (bc > ea) *overflow = 1;
    return (uint16_t)(ea - bc);
}

/* 14-bit DAC clamp matching uPD7811 range 0x0000..0x3FFF */
static inline uint16_t fm106_vcf_clamp(uint16_t ea, uint8_t overflow)
{
    if (ea > 0x3FFFu) {
        return overflow ? 0x0000u : 0x3FFFu;
    }
    return ea;
}

/* 16-bit multiply: (coeff * val) >> 8 */
static inline uint16_t fm106_mul8x16_hi(uint8_t coeff, uint16_t val)
{
    return (uint16_t)(((uint32_t)coeff * val) >> 8);
}

/* Clean-room reimplementation of the D7811G VCF cutoff DAC calculation ($04D5-$064E) */
static inline uint16_t fm106_calc_vcf_freq(
    uint16_t vcfCutoff,    /* 0x0000..0x3F80 */
    uint16_t vcfLfoSignal, /* pre-computed LFO modulation amount */
    uint16_t vcfBendAmt,   /* pre-computed bend modulation amount */
    uint8_t  vcfEnvMod,    /* env mod depth 0..254 */
    uint8_t  vcfKeyTrack,  /* key track depth 0..254 */
    uint8_t  lfoPolarity,  /* 0 = add, 1 = subtract */
    uint8_t  bendPolarity, /* 0 = add, 1 = subtract */
    uint8_t  envPolarity,  /* 1 = positive env, 0 = inverted */
    uint16_t envelope,     /* 14-bit ADSR level 0x0000..0x3FFF */
    uint16_t pitch         /* 8.8 fixed-point pitch (note * 256) */
) {
    uint16_t ea = vcfCutoff;
    uint8_t overflow = 0;

    /* Step 1: Base = Cutoff +- LFO +- Bend */
    if (!lfoPolarity) ea = fm106_vcf_add(ea, vcfLfoSignal, &overflow);
    else              ea = fm106_vcf_sub(ea, vcfLfoSignal, &overflow);

    if (!bendPolarity) ea = fm106_vcf_add(ea, vcfBendAmt, &overflow);
    else               ea = fm106_vcf_sub(ea, vcfBendAmt, &overflow);

    /* Step 2: Envelope Modulation */
    uint16_t scaledEnv = fm106_mul8x16_hi(vcfEnvMod, envelope);
    if (envPolarity) ea = fm106_vcf_add(ea, scaledEnv, &overflow);
    else             ea = fm106_vcf_sub(ea, scaledEnv, &overflow);

    /* Step 3: Key Tracking (scaled by 3/8) */
    uint16_t pScaled = (pitch >> 2) + (pitch >> 3);
    static const uint16_t MIDDLE_C_SCALED = 0x1680u;   /* MIDI 60 * 256 * 0.375 */

    if (pScaled > MIDDLE_C_SCALED) {
        uint16_t keyDelta = fm106_mul8x16_hi(vcfKeyTrack, pScaled - MIDDLE_C_SCALED);
        ea = fm106_vcf_add(ea, keyDelta, &overflow);
    } else {
        uint16_t keyDelta = fm106_mul8x16_hi(vcfKeyTrack, MIDDLE_C_SCALED - pScaled);
        ea = fm106_vcf_sub(ea, keyDelta, &overflow);
    }

    return fm106_vcf_clamp(ea, overflow);
}

typedef struct {
    int32_t z[4];               /* 4-pole TPT integrator states (Q24) */
    uint32_t noise_seed;        /* PRNG seed for adaptive thermal noise */
    int32_t env_follow;         /* Leaky input envelope follower (22 ms decay) */
} fm106_vcf_t;

static inline void fm106_vcf_init(fm106_vcf_t *f)
{
    f->z[0] = 0;
    f->z[1] = 0;
    f->z[2] = 0;
    f->z[3] = 0;
    f->noise_seed = 123456789u + (uint32_t)((uintptr_t)f * 31u);
    f->env_follow = 0;
}

/* 4-pole TPT OTA ladder filter with BA662 feedback differential and saturation
 * input: Q15, dac: 0..16383, res: 0..127 -> returns Q15 */
static inline int32_t fm106_vcf_process(fm106_vcf_t *f, int32_t input, uint16_t dac, uint8_t res)
{
    /* Leaky input peak envelope follower with 22 ms decay (matching KR106VCF.h mEnvDecay = 0.998969 in Q15: 32734) */
    int32_t in_abs = input < 0 ? -input : input;
    int32_t decay = (f->env_follow * 32734) >> 15;
    f->env_follow = in_abs > decay ? in_abs : decay;

    int32_t z0 = f->z[0] >> 9;
    int32_t z1 = f->z[1] >> 9;
    int32_t z2 = f->z[2] >> 9;
    int32_t z3 = f->z[3] >> 9;
    int32_t z_sum = (z0 < 0 ? -z0 : z0) + (z1 < 0 ? -z1 : z1) +
                    (z2 < 0 ? -z2 : z2) + (z3 < 0 ? -z3 : z3);
    int32_t energy = f->env_follow > z_sum ? f->env_follow : z_sum;

    /* Adaptive thermal noise: seeds self-oscillation on quiet/pure-resonance patches (e.g. B66 Toy Rhodes).
     * Only injects when sound has been silent for hundreds of ms (energy < 50), preventing zero-crossing static */
    f->noise_seed = f->noise_seed * 196314165u + 907633515u;
    if (energy < 50) {
        int32_t white = (int32_t)(f->noise_seed >> 16) - 32768;
        int32_t noise_lvl = 327 / (1 + (energy >> 2));
        input += (white * noise_lvl) >> 15;
    }

    /* Lookup g1 from measured Juno-106 voice card V4 profile */
    uint32_t code = (uint32_t)dac >> 2;   /* 12-bit DAC code (0..4095) */
    if (code > 4095u) code = 4095u;
    int32_t g1 = FM106_VCF_G1[code];      /* Q15: 0..32767 */

    /* Lookup resonance gain k from ResK_J106 polynomial */
    if (res > 127u) res = 127u;
    int32_t k = FM106_RES_K[res];         /* Q12: 0..18000 */

    /* Anti-aliasing at 1x OS: fade k to zero near Nyquist (code > 3500) matching KR106VCF_OPTIMIZED.h */
    if (code > 3500u) {
        int32_t fade = 4095 - (int32_t)code;
        if (fade < 0) fade = 0;
        k = (k * fade) / 595;
    }

    /* Peak-pull compensation: aligns resonant peak with hardware analog filter */
    g1 = (g1 * (int32_t)FM106_PEAK_PULL[res]) >> 12;
    if (g1 > 32767) g1 = 32767;

    /* Derived powers of g1 in Q15 */
    int32_t g1_2 = (g1 * g1) >> 15;
    int32_t g1_3 = (g1_2 * g1) >> 15;
    int32_t G    = (g1_2 * g1_2) >> 15;   /* G = g1^4 */

    /* TPT feedback denominator reciprocal: 1 / (1 + k*G) in Q12 */
    int32_t kG = (k * G) >> 15;           /* Q12 */
    int32_t den = 4096 + kG;
    int32_t invDen = (4096 * 4096) / (den > 0 ? den : 1);

    /* Linear cascade predictor with 64-bit sum of products to prevent 32-bit overflow */
    int64_t sum_prod = (int64_t)z0 * g1_3 + (int64_t)z1 * g1_2 + (int64_t)z2 * g1;
    int32_t sum_z = (int32_t)(sum_prod >> 15) + z3;
    int32_t S = (int32_t)(((int64_t)(32768 - g1) * sum_z) >> 15);

    /* BA662 OTA differential pair:
     * +IN = input * kInputRatio (0.065 in Q15: 2130)
     * -IN = feedback signal S */
    int32_t otaDiff = (int32_t)(((int64_t)input * 2130) >> 15) - S;
    int32_t fb_scale = FM106_FB_SCALE[res];         /* Q12: 1.26..4.20 */
    int32_t inv_fb_scale = FM106_INV_FB_SCALE[res]; /* Q12: 0.79..0.238 */
    int32_t diff_scaled = (int32_t)(((int64_t)otaDiff * fb_scale) >> 12);
    int32_t sat = fm106_ota_sat(diff_scaled);
    int32_t otaOut = (int32_t)(((int64_t)sat * inv_fb_scale) >> 12);

    /* Filter input = direct path (kDirectGain 0.142 in Q15: 4653) + resonance OTA */
    int32_t direct = (int32_t)(((int64_t)input * 4653) >> 15);
    int32_t fb = (int32_t)(((int64_t)k * otaOut) >> 12);
    int32_t u = (int32_t)(((int64_t)(direct + fb) * invDen) >> 12);
    int32_t u_q24 = u << 9;

    /* 4 cascaded 1-pole TPT stages in Q24 with saturation guarding */
    for (int j = 0; j < 4; j++) {
        int32_t delta = (int32_t)(((int64_t)g1 * (u_q24 - f->z[j])) >> 15);
        int32_t y = f->z[j] + delta;
        int32_t next_z = y + delta;
        if (next_z > 134217727) next_z = 134217727;
        else if (next_z < -134217728) next_z = -134217728;
        f->z[j] = next_z;
        u_q24 = y;
    }

    int32_t u_out = u_q24 >> 9;
    /* Output makeup gain: 8.59x passband compensation (8.59 * 1024 = 8796).
     * Output is NOT hard clamped to 32767: headroom is preserved for downstream
     * voice summation and analog rational soft-limiting (fm106_soft_limit). */
    int32_t out = (int32_t)(((int64_t)u_out * 8796LL) >> 10);
    return out;
}

/* 1-pole TPT DC blocker (1.59 Hz at 44.1 kHz, models Juno coupling capacitors):
 * alpha = 2 * g / (1 + g) ~= 0.000226534 -> in Q24: 3800 */
static inline int32_t fm106_dc_block(int32_t in, int64_t *state)
{
    int64_t in_q16 = (int64_t)in << 16;
    int64_t diff = in_q16 - *state;
    *state += (diff * 3800LL) >> 24;
    return in - (int32_t)(*state >> 16);
}

/* Pre-HPF 0.35 Hz AC coupling capacitor DC blocker (10µF / 44.9K on hardware):
 * alpha = 2 * g / (1 + g) ~= 4.9867e-5 -> in Q24: 837 */
static inline int32_t fm106_dc_block_hpf(int32_t in, int64_t *state)
{
    int64_t in_q16 = (int64_t)in << 16;
    int64_t diff = in_q16 - *state;
    *state += (diff * 837LL) >> 24;
    return in - (int32_t)(*state >> 16);
}

/* 4-Position Juno-106 High-Pass Filter (HPF)
 * Mode 0: Bass Boost (+10.5 dB low shelf, modeled on hardware U2/U1 circuit)
 * Mode 1: Bypass / Flat (with 0.35 Hz AC coupling)
 * Mode 2: 236 Hz 1-pole TPT HPF
 * Mode 3: 754 Hz 1-pole TPT HPF */
typedef struct {
    int64_t bb_s1;              /* Bass boost stage 1 state in Q16 */
    int64_t bb_s2;              /* Bass boost stage 2 state in Q16 */
    int64_t hp_state;           /* Cut stage state in Q16 */
    int64_t dc_state;           /* 0.35 Hz AC coupling state in Q16 */
    uint8_t mode;               /* 0..3 */
} fm106_hpf_t;

static inline void fm106_hpf_init(fm106_hpf_t *h)
{
    h->bb_s1 = 0;
    h->bb_s2 = 0;
    h->hp_state = 0;
    h->dc_state = 0;
    h->mode = 1;                /* Flat default */
}

/* 1-pole TPT HPF cut stage */
static inline int32_t fm106_hpf_cut(int32_t in, int64_t *state, int64_t g_div_q24)
{
    int64_t diff = ((int64_t)in << 16) - *state;
    int64_t v_q16 = (diff * g_div_q24) >> 24;
    int64_t lp_q16 = *state + v_q16;
    *state = lp_q16 + v_q16;
    return in - (int32_t)(lp_q16 >> 16);
}

/* Juno-106 Mode 0 Bass Boost Filter:
 * Models the U2 non-inverting op-amp + U1 summer biquad (+10.5 dB DC gain, +1.41 dB HF gain)
 * Stage 1: pole 59.4 Hz, zero 72.0 Hz
 * Stage 2: pole 72.3 Hz, zero 795.8 Hz */
static inline int32_t fm106_bass_boost(fm106_hpf_t *h, int32_t in)
{
    /* Stage 1: 1-pole TPT (g1_div in Q24: 70704, kz1 in Q15: 27019) */
    int64_t diff1 = ((int64_t)in << 16) - h->bb_s1;
    int64_t v1_q16 = (diff1 * 70704LL) >> 24;
    int64_t lp1_q16 = h->bb_s1 + v1_q16;
    h->bb_s1 = lp1_q16 + v1_q16;
    int32_t lp1 = (int32_t)(lp1_q16 >> 16);
    int32_t y1 = lp1 + (int32_t)(((int64_t)(in - lp1) * 27019LL) >> 15);

    /* Stage 2: 1-pole TPT (g2_div in Q24: 86019, kz2 in Q15: 2979) */
    int64_t diff2 = ((int64_t)y1 << 16) - h->bb_s2;
    int64_t v2_q16 = (diff2 * 86019LL) >> 24;
    int64_t lp2_q16 = h->bb_s2 + v2_q16;
    h->bb_s2 = lp2_q16 + v2_q16;
    int32_t lp2 = (int32_t)(lp2_q16 >> 16);
    int32_t y2 = lp2 + (int32_t)(((int64_t)(y1 - lp2) * 2979LL) >> 15);

    /* Boost path summing (alpha * G2_dc = 2.3496 -> Q15: 77000) */
    int32_t boosted = in + (int32_t)(((int64_t)y2 * 77000LL) >> 15);
    return boosted;
}

static inline int32_t fm106_hpf_process(fm106_hpf_t *h, int32_t in)
{
    switch (h->mode) {
    case 0: {   /* Bass boost: +10.5 dB shelf + 0.35 Hz AC coupling */
        int32_t boosted = fm106_bass_boost(h, in);
        return fm106_dc_block_hpf(boosted, &h->dc_state);
    }
    case 1:     /* Flat (bypass with 0.35 Hz AC coupling) */
        return fm106_dc_block_hpf(in, &h->dc_state);
    case 2: {   /* 236 Hz 1-pole HPF (g_div in Q24: 277423) */
        int32_t dc_blocked = fm106_dc_block_hpf(in, &h->dc_state);
        return fm106_hpf_cut(dc_blocked, &h->hp_state, 277423LL);
    }
    case 3: {   /* 754 Hz 1-pole HPF (g_div in Q24: 856006) */
        int32_t dc_blocked = fm106_dc_block_hpf(in, &h->dc_state);
        return fm106_hpf_cut(dc_blocked, &h->hp_state, 856006LL);
    }
    default:
        return in;
    }
}

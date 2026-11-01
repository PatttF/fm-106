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
 * - Evaluated on the hardware single-precision FPU when above threshold */
static inline __attribute__((always_inline)) int32_t fm106_soft_limit(int32_t x)
{
    int32_t ax = x < 0 ? -x : x;
    if (ax <= 24576) {
        return x;
    }
    float ex = (float)((ax - 24576) >> 5);
    int32_t compressed = 24576 + (int32_t)((ex * 7424.0f) / (232.0f + ex));
    return x < 0 ? -compressed : compressed;
}

/* Rational analog soft limiter in single-precision float domain (maps to int32 range -32000..+32000):
 * Transparent linear pass-through up to |x| <= 24576.0f (-2.5 dBFS) */
static inline __attribute__((always_inline)) int32_t fm106_soft_limit_f32(float x)
{
    float ax = x < 0.0f ? -x : x;
    if (__builtin_expect(ax <= 24576.0f, 1)) {
        return (int32_t)(x + (x >= 0.0f ? 0.5f : -0.5f));
    }
    float ex = (ax - 24576.0f) * (1.0f / 32.0f);
    float compressed = 24576.0f + (ex * 7424.0f) / (232.0f + ex);
    int32_t c = (int32_t)(compressed + 0.5f);
    return x < 0.0f ? -c : c;
}

/* BA662 OTA Padé differential saturation in single-precision float:
 * Exact continuous KR106VCF_OPTIMIZED.h curve x * (27 + x^2) / (27 + 9*x^2) */
static inline __attribute__((always_inline)) float fm106_ota_sat_f(float x)
{
    if (x >= 3.0f) return 1.0f;
    if (x <= -3.0f) return -1.0f;
    float x2 = x * x;
    return x * (27.0f + x2) / (27.0f + 9.0f * x2);
}

/* Legacy Q15 wrapper (0..3.0 where 3.0 = 98304 -> Q15) */
static inline int32_t fm106_ota_sat(int32_t x)
{
    return (int32_t)(fm106_ota_sat_f((float)x * (1.0f / 32768.0f)) * 32767.0f);
}

/* 16-bit add matching uPD7811 DADDNC EA,BC */
static inline __attribute__((always_inline)) uint16_t fm106_vcf_add(uint16_t ea, uint16_t bc, uint8_t *overflow)
{
    uint32_t res = (uint32_t)ea + bc;
    if (res > 0xFFFFu) *overflow = 0;
    return (uint16_t)res;
}

/* 16-bit subtract matching uPD7811 DSUBNB EA,BC */
static inline __attribute__((always_inline)) uint16_t fm106_vcf_sub(uint16_t ea, uint16_t bc, uint8_t *overflow)
{
    if (bc > ea) *overflow = 1;
    return (uint16_t)(ea - bc);
}

/* 14-bit DAC clamp matching uPD7811 range 0x0000..0x3FFF */
static inline __attribute__((always_inline)) uint16_t fm106_vcf_clamp(uint16_t ea, uint8_t overflow)
{
    if (ea > 0x3FFFu) {
        return overflow ? 0x0000u : 0x3FFFu;
    }
    return ea;
}

/* 16-bit multiply: (coeff * val) >> 8 */
static inline __attribute__((always_inline)) uint16_t fm106_mul8x16_hi(uint8_t coeff, uint16_t val)
{
    return (uint16_t)(((uint32_t)coeff * val) >> 8);
}

/* Clean-room reimplementation of the D7811G VCF cutoff DAC calculation ($04D5-$064E) */
static inline __attribute__((always_inline)) uint16_t fm106_calc_vcf_freq(
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
    float z[4];                 /* 4-pole TPT integrator states (normalized ±1.0f = ±32768) */
    float env_follow;           /* Leaky input envelope follower (22 ms decay) */
    float g1, g1_2, g1_3;       /* Cached TPT stage gains for (cached_code, cached_res) */
    float k, inv_den;           /* Cached resonance gain and 1 / (1 + k * G) */
    float fb_scale, inv_fb_scale; /* Cached BA662 OTA differential scaling */
    uint32_t noise_seed;        /* PRNG seed for adaptive thermal noise */
    uint16_t cached_code;       /* Last 12-bit DAC code */
    uint8_t cached_res;         /* Last resonance fader value */
    uint8_t cache_valid;        /* 1 once cached coefficients are populated */
} fm106_vcf_t;

static inline void fm106_vcf_init(fm106_vcf_t *f)
{
    f->z[0] = 0.0f;
    f->z[1] = 0.0f;
    f->z[2] = 0.0f;
    f->z[3] = 0.0f;
    f->env_follow = 0.0f;
    f->g1 = 0.0f;
    f->g1_2 = 0.0f;
    f->g1_3 = 0.0f;
    f->k = 0.0f;
    f->inv_den = 1.0f;
    f->fb_scale = 1.0f;
    f->inv_fb_scale = 1.0f;
    f->noise_seed = 123456789u + (uint32_t)((uintptr_t)f * 31u);
    f->cached_code = 0xFFFFu;
    f->cached_res = 0xFFu;
    f->cache_valid = 0u;
}

/* 4-pole TPT OTA ladder filter with BA662 feedback differential and saturation
 * Executed on the hardware single-precision FPU (matching KR106VCF_OPTIMIZED.h
 * continuous circuit equations with measured kV4Hz / FM106_VCF_G1 profile).
 * input: Q15, dac: 0..16383, res: 0..127 -> returns Q15 */
static inline __attribute__((always_inline)) int32_t fm106_vcf_process(fm106_vcf_t *f, int32_t input, uint16_t dac, uint8_t res)
{
    float x = (float)input * (1.0f / 32768.0f);

    /* Leaky input peak envelope follower with 22 ms decay (KR106VCF.h mEnvDecay = 0.998969f) */
    float in_abs = x < 0.0f ? -x : x;
    float decay = f->env_follow * 0.998969f;
    f->env_follow = in_abs > decay ? in_abs : decay;

    float z0 = f->z[0], z1 = f->z[1], z2 = f->z[2], z3 = f->z[3];
    float z_sum = (z0 < 0.0f ? -z0 : z0) + (z1 < 0.0f ? -z1 : z1) +
                  (z2 < 0.0f ? -z2 : z2) + (z3 < 0.0f ? -z3 : z3);
    float energy = f->env_follow > z_sum ? f->env_follow : z_sum;

    /* Adaptive thermal noise: seeds self-oscillation on quiet/pure-resonance patches (e.g. B66 Toy Rhodes).
     * Only injects when resonance is near self-oscillation (res >= 120) and energy is decaying, preventing noise floor */
    f->noise_seed = f->noise_seed * 196314165u + 907633515u;
    if (res >= 120u && energy < (5.0f / 32768.0f)) {
        float white = ((float)(int32_t)(f->noise_seed >> 16) - 32768.0f) * (1.0f / 32768.0f);
        x += white * (1.0f / 32768.0f);
    }

    /* Recompute g1, k, G and 1/(1+k*G) only when 12-bit DAC code or resonance changes */
    uint32_t code = (uint32_t)dac >> 2;   /* 12-bit DAC code (0..4095) */
    if (code > 4095u) code = 4095u;
    if (res > 127u) res = 127u;
    if (!f->cache_valid || code != f->cached_code || res != f->cached_res) {
        f->cached_code = (uint16_t)code;
        f->cached_res  = res;
        f->cache_valid = 1u;

        /* Lookup g1 from measured Juno-106 voice card V4 profile */
        float g1 = (float)FM106_VCF_G1[code] * (1.0f / 32767.0f);

        /* Lookup resonance gain k from ResK_J106 polynomial */
        float k = (float)FM106_RES_K[res] * (1.0f / 4096.0f);

        /* Anti-aliasing at 1x OS: fade k to zero near Nyquist (code > 3500) */
        if (code > 3500u) {
            k *= (float)(4095u - code) * (1.0f / 595.0f);
        }

        /* Peak-pull compensation: aligns resonant peak with hardware analog filter */
        g1 *= (float)FM106_PEAK_PULL[res] * (1.0f / 4096.0f);
        if (g1 > 1.0f) g1 = 1.0f;

        float g1_2 = g1 * g1;
        float g1_3 = g1_2 * g1;
        float G    = g1_2 * g1_2;   /* G = g1^4 */

        f->g1           = g1;
        f->g1_2         = g1_2;
        f->g1_3         = g1_3;
        f->k            = k;
        f->inv_den      = 1.0f / (1.0f + k * G);
        f->fb_scale     = (float)FM106_FB_SCALE[res] * (1.0f / 4096.0f);
        f->inv_fb_scale = (float)FM106_INV_FB_SCALE[res] * (1.0f / 4096.0f);
    }

    /* Linear cascade predictor in single-precision float (register-held, zero 64-bit spills) */
    float g1 = f->g1;
    float sum_z = z0 * f->g1_3 + z1 * f->g1_2 + z2 * g1 + z3;
    float S = (1.0f - g1) * sum_z;

    /* BA662 OTA differential pair:
     * +IN = x * kInputRatio (2130 / 32768 = 0.06500244f)
     * -IN = feedback signal S */
    float ota_diff = x * (2130.0f / 32768.0f) - S;
    float sat = fm106_ota_sat_f(ota_diff * f->fb_scale);
    float ota_out = sat * f->inv_fb_scale;

    /* Filter input = direct path (kDirectGain 4653 / 32768 = 0.14199829f) + resonance OTA */
    float u = (x * (4653.0f / 32768.0f) + f->k * ota_out) * f->inv_den;

    /* 4 cascaded 1-pole TPT stages (register-held z0..z3 with ±8.0f saturation guarding) */
    float d0 = g1 * (u - z0);  float y0 = z0 + d0; z0 = y0 + d0;
    float d1 = g1 * (y0 - z1); float y1 = z1 + d1; z1 = y1 + d1;
    float d2 = g1 * (y1 - z2); float y2 = z2 + d2; z2 = y2 + d2;
    float d3 = g1 * (y2 - z3); float y3 = z3 + d3; z3 = y3 + d3;
    if (z0 > 8.0f) z0 = 8.0f; else if (z0 < -8.0f) z0 = -8.0f;
    if (z1 > 8.0f) z1 = 8.0f; else if (z1 < -8.0f) z1 = -8.0f;
    if (z2 > 8.0f) z2 = 8.0f; else if (z2 < -8.0f) z2 = -8.0f;
    if (z3 > 8.0f) z3 = 8.0f; else if (z3 < -8.0f) z3 = -8.0f;
    f->z[0] = z0;
    f->z[1] = z1;
    f->z[2] = z2;
    f->z[3] = z3;

    /* Output makeup gain: 8.59x passband compensation (8796 / 1024 = 8.58984375f).
     * Headroom is preserved for downstream voice summation and fm106_soft_limit. */
    return (int32_t)(y3 * (8.58984375f * 32768.0f));
}

/* 1-pole TPT DC blocker (1.59 Hz at 44.1 kHz, models Juno coupling capacitors):
 * alpha = 2 * g / (1 + g) ~= 0.000226534f */
static inline __attribute__((always_inline)) int32_t fm106_dc_block(int32_t in, float *state)
{
    float x = (float)in;
    float s = *state + (x - *state) * (3800.0f / 16777216.0f);
    if (s > -1e-9f && s < 1e-9f) s = 0.0f;
    *state = s;
    return in - (int32_t)s;
}

/* Pre-HPF 0.35 Hz AC coupling capacitor DC blocker (10µF / 44.9K on hardware):
 * alpha = 2 * g / (1 + g) ~= 4.9889e-5f */
static inline __attribute__((always_inline)) int32_t fm106_dc_block_hpf(int32_t in, float *state)
{
    float x = (float)in;
    float s = *state + (x - *state) * (837.0f / 16777216.0f);
    if (s > -1e-9f && s < 1e-9f) s = 0.0f;
    *state = s;
    return in - (int32_t)s;
}

/* 4-Position Juno-106 High-Pass Filter (HPF)
 * Mode 0: Bass Boost (+10.5 dB low shelf, modeled on hardware U2/U1 circuit)
 * Mode 1: Bypass / Flat (with 0.35 Hz AC coupling)
 * Mode 2: 236 Hz 1-pole TPT HPF
 * Mode 3: 754 Hz 1-pole TPT HPF */
typedef struct {
    float bb_s1;                /* Bass boost stage 1 state */
    float bb_s2;                /* Bass boost stage 2 state */
    float hp_state;             /* Cut stage state */
    float dc_state;             /* 0.35 Hz AC coupling state */
    uint8_t mode;               /* 0..3 */
} fm106_hpf_t;

static inline void fm106_hpf_init(fm106_hpf_t *h)
{
    h->bb_s1 = 0.0f;
    h->bb_s2 = 0.0f;
    h->hp_state = 0.0f;
    h->dc_state = 0.0f;
    h->mode = 1;                /* Flat default */
}

/* 1-pole TPT HPF cut stage */
static inline __attribute__((always_inline)) int32_t fm106_hpf_cut(int32_t in, float *state, float g_div)
{
    float x = (float)in;
    float s = *state;
    float v = (x - s) * g_div;
    float lp = s + v;
    s = lp + v;
    if (s > -1e-9f && s < 1e-9f) s = 0.0f;
    *state = s;
    return (int32_t)(x - lp);
}

/* Juno-106 Mode 0 Bass Boost Filter:
 * Models the U2 non-inverting op-amp + U1 summer biquad (+10.5 dB DC gain, +1.41 dB HF gain)
 * Stage 1: pole 59.4 Hz, zero 72.0 Hz
 * Stage 2: pole 72.3 Hz, zero 795.8 Hz */
static inline __attribute__((always_inline)) int32_t fm106_bass_boost(fm106_hpf_t *h, int32_t in)
{
    float x = (float)in;

    /* Stage 1: 1-pole TPT (g1_div: 70704 / 2^24, kz1: 27019 / 2^15) */
    float s1 = h->bb_s1;
    float v1 = (x - s1) * (70704.0f / 16777216.0f);
    float lp1 = s1 + v1;
    s1 = lp1 + v1;
    if (s1 > -1e-9f && s1 < 1e-9f) s1 = 0.0f;
    h->bb_s1 = s1;
    float y1 = lp1 + (x - lp1) * (27019.0f / 32768.0f);

    /* Stage 2: 1-pole TPT (g2_div: 86019 / 2^24, kz2: 2979 / 2^15) */
    float s2 = h->bb_s2;
    float v2 = (y1 - s2) * (86019.0f / 16777216.0f);
    float lp2 = s2 + v2;
    s2 = lp2 + v2;
    if (s2 > -1e-9f && s2 < 1e-9f) s2 = 0.0f;
    h->bb_s2 = s2;
    float y2 = lp2 + (y1 - lp2) * (2979.0f / 32768.0f);

    /* Boost path summing (alpha * G2_dc = 77000 / 32768) */
    return (int32_t)(x + y2 * (77000.0f / 32768.0f));
}

static inline int32_t fm106_hpf_process(fm106_hpf_t *h, int32_t in)
{
    /* Pre-HPF AC coupling capacitor (10µF / 44.9K, C38 on hardware Juno-106):
     * sits in series before the 4-position HPF switch in all modes */
    int32_t dc_blocked = fm106_dc_block_hpf(in, &h->dc_state);

    switch (h->mode) {
    case 0:    /* Bass boost: +10.5 dB low shelf on AC signal */
        return fm106_bass_boost(h, dc_blocked);
    case 1:    /* Flat (bypass) */
        return dc_blocked;
    case 2:    /* 236 Hz 1-pole HPF (g_div: 277423 / 2^24) */
        return fm106_hpf_cut(dc_blocked, &h->hp_state, 277423.0f / 16777216.0f);
    case 3:    /* 754 Hz 1-pole HPF (g_div: 856006 / 2^24) */
        return fm106_hpf_cut(dc_blocked, &h->hp_state, 856006.0f / 16777216.0f);
    default:
        return in;
    }
}


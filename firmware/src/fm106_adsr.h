/* SPDX-License-Identifier: GPL-3.0-only */
/* FM-106: Authentic Roland Juno-106 D7811G Envelope Generator
 * Replicates the NEC uPD7811G voice CPU digital envelope generator:
 * - Firmware tick accumulator at 236.211 Hz (4.2335 ms period).
 * - 14-bit integer envelope ($0000..$3FFF = 0..16383).
 * - Linear attack ramp with exact AttackIncFromSlider lookup table.
 * - Exponential decay/release toward target using CalcDecay ($083D ROM 8x8 multiply).
 * - Linear interpolation between tick boundaries at 44.1 kHz. */
#pragma once
#include <stdint.h>
#include "fm106_tables.h"

#define FM106_ENV_MAX 0x3FFFu    /* 14-bit envelope peak: 16383 */

enum {
    FM106_ENV_IDLE = 0,
    FM106_ENV_ATTACK = 1,
    FM106_ENV_DECAY = 2,
    FM106_ENV_SUSTAIN = 3,
    FM106_ENV_RELEASE = 4
};

typedef struct {
    uint8_t stage;              /* FM106_ENV_* */
    uint16_t env14;             /* current 14-bit envelope (0..0x3FFF) */
    uint16_t target14;          /* sustain level in 0..0x3FFF */
    uint16_t atk_inc;           /* increment per tick */
    uint16_t dec_mul;           /* 16-bit multiplier per tick */
    uint16_t rel_mul;           /* 16-bit multiplier per tick */
    uint16_t env_prev;          /* previous tick envelope for interpolation */
    uint16_t env_next;          /* next tick envelope for interpolation */
    uint32_t tick_accum;        /* fractional tick step accumulator */
} fm106_adsr_t;

static inline void fm106_adsr_init(fm106_adsr_t *e)
{
    e->stage = FM106_ENV_IDLE;
    e->env14 = 0;
    e->target14 = 0;
    e->atk_inc = 0x3FFF;
    e->dec_mul = 0x1000;
    e->rel_mul = 0x1000;
    e->env_prev = 0;
    e->env_next = 0;
    e->tick_accum = 0;
}

static inline void fm106_adsr_params(fm106_adsr_t *e, uint8_t a, uint8_t d, uint8_t s, uint8_t r)
{
    if (a > 127u) a = 127u;
    if (d > 127u) d = 127u;
    if (s > 127u) s = 127u;
    if (r > 127u) r = 127u;

    e->atk_inc = FM106_ENV_ATK[a];
    e->dec_mul = FM106_ENV_DEC_REL[d];
    e->rel_mul = FM106_ENV_DEC_REL[r];
    e->target14 = (uint16_t)(((uint32_t)s * FM106_ENV_MAX) / 127u);
}

static inline void fm106_adsr_tick(fm106_adsr_t *e);

static inline void fm106_adsr_gate_on(fm106_adsr_t *e)
{
    e->stage = FM106_ENV_ATTACK;
    e->tick_accum = 0;
    if (e->atk_inc >= FM106_ENV_MAX) {
        /* Instant attack (Attack = 0): ramps from previous level to peak over 4.2ms DAC RC tick */
        e->env_prev = e->env14;
        e->env14 = FM106_ENV_MAX;
        e->env_next = FM106_ENV_MAX;
        e->stage = FM106_ENV_DECAY;
    } else {
        e->env_prev = e->env14;
        fm106_adsr_tick(e);
        e->env_next = e->env14;
    }
}

static inline void fm106_adsr_gate_off(fm106_adsr_t *e)
{
    if (e->stage != FM106_ENV_IDLE) {
        e->stage = FM106_ENV_RELEASE;
        /* Maintain envelope continuity: set both interpolation bounds to current level */
        e->env_prev = e->env14;
        e->env_next = e->env14;
    }
}

/* Authentic D7811G calcDecay: 3 partial products of 8x8 MUL */
static inline uint16_t fm106_adsr_calc_decay(uint16_t value, uint16_t coeff)
{
    uint32_t vh = (uint32_t)(value >> 8);
    uint32_t vl = (uint32_t)(value & 0xFFu);
    uint32_t ch = (uint32_t)(coeff >> 8);
    uint32_t cl = (uint32_t)(coeff & 0xFFu);
    return (uint16_t)((vh * ch)
         + ((vh * cl) >> 8)
         + ((vl * ch) >> 8));
}

/* Advance one firmware tick (~236.2 Hz, 4.2335 ms) */
static inline void fm106_adsr_tick(fm106_adsr_t *e)
{
    switch (e->stage) {
    case FM106_ENV_ATTACK: {
        uint32_t next = (uint32_t)e->env14 + e->atk_inc;
        if (next >= FM106_ENV_MAX) {
            e->env14 = FM106_ENV_MAX;
            e->stage = FM106_ENV_DECAY;
        } else {
            e->env14 = (uint16_t)next;
        }
        break;
    }
    case FM106_ENV_DECAY: {
        if (e->env14 <= e->target14) {
            e->env14 = e->target14;
            e->stage = FM106_ENV_SUSTAIN;
        } else {
            uint16_t diff = e->env14 - e->target14;
            diff = fm106_adsr_calc_decay(diff, e->dec_mul);
            e->env14 = e->target14 + diff;
            if (e->env14 <= e->target14) {
                e->env14 = e->target14;
                e->stage = FM106_ENV_SUSTAIN;
            }
        }
        break;
    }
    case FM106_ENV_SUSTAIN:
        e->env14 = e->target14;
        break;
    case FM106_ENV_RELEASE: {
        if (e->env14 == 0) {
            e->stage = FM106_ENV_IDLE;
        } else {
            e->env14 = fm106_adsr_calc_decay(e->env14, e->rel_mul);
            if (e->env14 <= 1u) {
                e->env14 = 0;
                e->stage = FM106_ENV_IDLE;
            }
        }
        break;
    }
    default:
        break;
    }
}

/* Audio-rate sample processing at 44.1 kHz.
 * Runs the 236.211 Hz tick and linearly interpolates between tick boundaries.
 * Returns Q15 (0..32766). */
static inline int32_t fm106_adsr_process(fm106_adsr_t *e)
{
    /* Tick rate step: 236.211 / 44100 * 65536 ~= 351 */
    e->tick_accum += 351u;
    if (e->tick_accum >= 65536u) {
        e->tick_accum -= 65536u;
        e->env_prev = e->env_next;
        fm106_adsr_tick(e);
        e->env_next = e->env14;
    }

    /* Linear interpolation between previous and next tick values */
    int32_t diff = (int32_t)e->env_next - (int32_t)e->env_prev;
    int32_t interp = (int32_t)e->env_prev + ((diff * (int32_t)e->tick_accum) >> 16);
    if (interp < 0) interp = 0;
    if (interp > (int32_t)FM106_ENV_MAX) interp = FM106_ENV_MAX;

    return interp << 1;   /* Scale 14-bit to Q15 (0..32766) */
}

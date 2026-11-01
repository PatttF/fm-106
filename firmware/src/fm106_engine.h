/* SPDX-License-Identifier: GPL-3.0-only */
/* FM-106: 6-Voice Polyphonic Synthesizer Engine with Dual-Core DSP
 * Coordinates Voice Allocation, DCO/VCF/VCA/ENV/LFO DSP chains,
 * Dual-Core CPU0/CPU1 job splitting, HPF and Stereo BBD Chorus. */
#pragma once
#include <stdint.h>
#include "fm106_tables.h"
#include "fm106_osc.h"
#include "fm106_vcf.h"
#include "fm106_adsr.h"
#include "fm106_chorus.h"
#include "fm106_fx.h"
#include "fm106_reverb.h"
#include "fm106_seq.h"
#include "fm106_presets.h"

#define FM106_NUM_VOICES 6u

enum {
    FM106_MODE_MONO = 0,
    FM106_MODE_POLY1 = 1,
    FM106_MODE_POLY2 = 2,
};

typedef struct {
    uint8_t note;               /* MIDI note (0..127), 255 = unassigned */
    uint8_t gate;               /* 1 = held, 0 = released */
    uint8_t vel;                /* velocity (1..127) */
    uint8_t gliding;            /* 1 = exponential legato portamento curr_inc -> base_inc */
    uint8_t seq_vel;            /* 0 = non-seq (pure Juno invariant), 1..127 = seq step vel */
    uint8_t seq_accent;         /* 1 = seq ACCENT step (Roland VCF+VCA emphasis) */
    uint32_t age;               /* note-on age for voice stealing */
    uint32_t base_inc;          /* target pitch phase step */
    uint32_t curr_inc;          /* live slewed pitch phase step during legato slide */
    fm106_dco_t dco;
    fm106_vcf_t vcf;
    fm106_adsr_t adsr;
    float dc_post_osc;
    float dc_post_vcf;
    int32_t vca_slew;           /* Q15 VCA RC slew filter state (tau = 687 us) */
    int32_t vcf_slew;           /* 14-bit VCF cutoff CV RC slew state (tau = 522 us) */
} fm106_voice_t;

typedef struct {
    /* 6 Polyphonic Voices */
    fm106_voice_t voice[FM106_NUM_VOICES];
    uint32_t age_counter;
    uint8_t round_robin_next;

    /* Voice Mode & Allocation (Mono/Poly I/Poly II from Juno-106) */
    uint8_t voice_mode;         /* 0 = MONO (unison), 1 = POLY I, 2 = POLY II */
    uint8_t mono_stack[16];     /* Mono note stack */
    uint8_t mono_stack_len;
    int16_t mono_note;          /* Sounding unison note, -1 = none */
    uint8_t voice_tbl[FM106_NUM_VOICES]; /* Poly I priority table (bubble sort) */
    uint8_t last_note[FM106_NUM_VOICES];
    uint8_t ever_played[FM106_NUM_VOICES];

    /* Current Patch Parameters */
    uint8_t patch_idx;          /* 0..127 */
    char patch_name[16];

    /* DCO */
    uint8_t dco_lfo_depth;      /* 0..127 */
    uint8_t dco_pwm_depth;      /* 0..127 */
    uint8_t dco_pwm_mode;       /* 0 = lfo, 1 = manual */
    uint8_t dco_pulse_on;       /* 0 or 1 */
    uint8_t dco_saw_on;         /* 0 or 1 */
    uint8_t dco_sub_on;         /* 0 or 1 */
    uint8_t dco_sub_level;      /* 0..127 */
    uint8_t dco_noise_level;    /* 0..127 */
    uint8_t dco_range;          /* 0 = 16', 1 = 8', 2 = 4' */

    /* MIDI */
    uint8_t midi_channel;       /* 0 = All (omni), 1..16 */
    uint8_t midi_out_ch;        /* 1..16: channel for CC/PC/notes we send */
    uint8_t midi_local;         /* 1 = panel keys play the engine, 0 = keys only send MIDI */
    uint8_t pc_in;              /* 1 = accept program change, 0 = ignore */
    uint8_t sync_mode;          /* 0 = off, 1 = master, 2 = slave */

    /* HPF */
    fm106_hpf_t hpf;

    /* VCF */
    uint8_t vcf_cutoff;         /* 0..127 */
    uint8_t vcf_resonance;      /* 0..127 */
    uint8_t vcf_env_mod;        /* 0..127 */
    uint8_t vcf_env_polarity;   /* 0 = pos, 1 = inv */
    uint8_t vcf_lfo_mod;        /* 0..127 */
    uint8_t vcf_kbd_track;      /* 0..127 */

    /* VCA */
    uint8_t vca_mode;           /* 0 = ENV, 1 = GATE */
    uint8_t vca_level;          /* 0..127 */

    /* ENV */
    uint8_t env_a, env_d, env_s, env_r;

    /* LFO */
    uint8_t lfo_rate;           /* 0..127 */
    uint8_t lfo_delay;          /* 0..127 */
    uint32_t lfo_ph;
    int32_t lfo_val;            /* current LFO tri (-32768..32767) */
    uint32_t lfo_delay_timer;   /* samples since key trigger */

    /* Stereo Chorus */
    fm106_chorus_t chorus;

    /* Global insert FX chain (Melodee-derived: phaser/width/formant/tape/spit/echo/room/reverb;
     * page order = chain order, chorus precedes) */
    fm106_fx_t fx[FM106_FX_SLOTS];

    /* Post-chain 1980s Stereo Shimmer Reverb (runs after Chorus & Tape) */
    fm106_reverb_t reverb;

    /* Performance / Controls */
    int8_t octave_shift;        /* -2 .. +2 */
    int8_t pitch_bend;          /* semitones (-12..+12) */
    int16_t pitch_bend_val;     /* -8192 .. +8191 from MIDI pitch wheel */
    uint8_t bender_dco;         /* 0..127 */
    uint8_t bender_vcf;         /* 0..127 */
    uint8_t hold_pedal;         /* 0 or 1 */

    /* Arpeggiator */
    uint8_t arp_on;
    uint8_t arp_mode;           /* 0 = UP, 1 = U&D, 2 = DOWN */
    uint8_t arp_range;          /* 1..3 (octave count) */
    uint8_t arp_rate;           /* 0..127 */
    uint32_t arp_phase;         /* Q20 step phase accumulator */
    uint16_t arp_step_idx;      /* index into the expanded sequence */
    int8_t arp_dir;             /* +1 / -1 -- Up/Down turnaround */
    int16_t arp_last_note;      /* sounding arp note, -1 = none */
    uint32_t arp_pool[4];       /* 128-bit sorted note pool (what gets arpped) */
    uint32_t keys_down[4];      /* 128-bit physical key state (arp seed source) */

    /* Step sequencer (single-track, B1; see fm106_seq.h) */
    fm106_seq_t seq;
} fm106_engine_t;

static fm106_engine_t g_synth;

/* Scratch buffers for dual-core voice mixing */
static int32_t g_mix0[256];
static int32_t g_mix1[256];
static int16_t g_lfo_buf[256];
static volatile uint32_t g_cpu0_load_pct;
static volatile uint32_t g_cpu1_load_pct;
static volatile uint32_t g_cpu0_ticks;
static volatile uint32_t g_cpu1_ticks;
static volatile uint32_t g_cpu0_peak;   /* peak-hold render ticks (cleared by CC 119 / value 1 query) */

/* Calculate phase step inc for MIDI note and octave range */
static inline uint32_t fm106_calc_inc(uint32_t note, int8_t oct_shift, uint8_t range)
{
    int32_t n = (int32_t)note + (oct_shift * 12);
    if (range == 0) n -= 12;        /* 16' (-1 oct) */
    else if (range == 2) n += 12;   /* 4'  (+1 oct) */

    if (n < 0) n = 0;
    if (n > 127) n = 127;

    uint32_t semi = (uint32_t)n % 12u;
    uint32_t oct = (uint32_t)n / 12u;
    uint32_t inc = FM106_INC_C9[semi];
    if (oct < 10u) {
        inc >>= (10u - oct);
    } else if (oct > 10u) {
        inc <<= (oct - 10u);
    }
    return inc;
}

/* Unison detune spread in parts-per-million (~±3 cents spread across 6 voices, matching analog unison) */
static const int32_t FM106_UNISON_DETUNE_PPM[FM106_NUM_VOICES] = {
    -1731, -1040, -347, 347, 1040, 1731
};

static inline uint32_t fm106_calc_unison_inc(uint32_t note, int8_t oct_shift, uint8_t range, uint32_t voice_idx)
{
    uint32_t base = fm106_calc_inc(note, oct_shift, range);
    if (voice_idx < FM106_NUM_VOICES) {
        int64_t diff = ((int64_t)base * FM106_UNISON_DETUNE_PPM[voice_idx]) / 1000000LL;
        return (uint32_t)((int64_t)base + diff);
    }
    return base;
}

/* Change Voice Mode (0 = MONO, 1 = POLY I, 2 = POLY II) cleanly without stuck notes */
static void fm106_set_voice_mode(fm106_engine_t *e, uint8_t mode)
{
    if (mode > 2u) mode = FM106_MODE_POLY1;
    if (e->voice_mode == mode) return;

    uint8_t prev_mode = e->voice_mode;
    e->voice_mode = mode;

    /* If switching between Mono and Poly, cleanly release sounding voices */
    if ((prev_mode == FM106_MODE_MONO) != (mode == FM106_MODE_MONO)) {
        for (uint32_t i = 0; i < FM106_NUM_VOICES; i++) {
            if (e->voice[i].gate || e->voice[i].adsr.stage != FM106_ENV_IDLE) {
                e->voice[i].gate = 0;
                fm106_adsr_gate_off(&e->voice[i].adsr);
            }
        }
        e->mono_stack_len = 0;
        e->mono_note = -1;
    }

    /* Reset priority table for Poly I */
    for (uint32_t i = 0; i < FM106_NUM_VOICES; i++) {
        e->voice_tbl[i] = (uint8_t)i;
    }
}

/* Apply preset reverb fields (defaults to OFF with lush 80s shimmer faders staged) */
static inline void fm106_reverb_apply_preset(fm106_reverb_t *r, const fm106_preset_t *p)
{
    if (p->p[45] == 0u && p->p[46] == 0u && p->p[47] == 0u && p->p[48] == 0u) {
        r->on      = 0u;
        r->mix     = 48u;
        r->decay   = 84u;
        r->shimmer = 68u;
    } else {
        r->on      = p->p[45] ? 1u : 0u;
        r->mix     = p->p[46] > 127u ? 127u : p->p[46];
        r->decay   = p->p[47] > 127u ? 127u : p->p[47];
        r->shimmer = p->p[48] > 127u ? 127u : p->p[48];
    }
}

/* Load preset patch into engine */
static void fm106_load_preset(fm106_engine_t *e, uint32_t idx)
{
    fm1_irq_off();
#if DINGHY_DUAL_CORE
    if (audio_worker_online) audio_worker_join();
#endif
    if (idx >= FM106_NUM_PRESETS) idx = 0;
    e->patch_idx = (uint8_t)idx;
    const fm106_preset_t *p = &FM106_PRESETS[idx];

    uint32_t pi = 0;
    while (p->name[pi] && pi < sizeof(e->patch_name) - 1u) {
        e->patch_name[pi] = p->name[pi];
        pi++;
    }
    e->patch_name[pi] = '\0';

    e->bender_dco     = p->p[0];
    e->bender_vcf     = p->p[1];
    e->arp_rate       = p->p[2];
    e->lfo_rate       = p->p[3];
    e->lfo_delay      = p->p[4];
    e->dco_lfo_depth  = p->p[5];
    e->dco_pwm_depth  = p->p[6];
    e->dco_sub_level  = p->p[7];
    e->dco_noise_level= p->p[8];
    e->hpf.mode       = p->p[9] > 3 ? 1 : p->p[9];
    e->vcf_cutoff     = p->p[10];
    e->vcf_resonance  = p->p[11];
    e->vcf_env_mod    = p->p[12];
    e->vcf_lfo_mod    = p->p[13];
    e->vcf_kbd_track  = p->p[14];
    e->vca_level      = p->p[15];
    e->env_a          = p->p[16];
    e->env_d          = p->p[17];
    e->env_s          = p->p[18];
    e->env_r          = p->p[19];

    e->dco_pulse_on   = p->p[23];
    e->dco_saw_on     = p->p[24];
    e->dco_sub_on     = p->p[25];

    /* Chorus mode: OFF, I, II, or I+II */
    if (p->p[27] && p->p[28]) e->chorus.mode = FM106_CHORUS_I_II;
    else if (p->p[28]) e->chorus.mode = FM106_CHORUS_II;
    else if (p->p[27]) e->chorus.mode = FM106_CHORUS_I;
    else e->chorus.mode = FM106_CHORUS_OFF;

    e->dco_range        = p->p[29] > 2 ? 1 : p->p[29];
    e->arp_mode         = p->p[30] > 2 ? 0 : p->p[30];
    e->arp_range        = p->p[31] ? p->p[31] : 1;
    e->dco_pwm_mode     = p->p[33];
    e->vcf_env_polarity = p->p[34];
    e->vca_mode         = p->p[35];

    e->fx[0].type      = p->p[36] >= FM106_FX_COUNT ? 0 : p->p[36];
    e->fx[0].mix       = p->p[37] > 127 ? 127 : p->p[37];
    e->fx[0].rate      = p->p[38] > 127 ? 127 : p->p[38];
    e->fx[0].character = p->p[39] > 127 ? 127 : p->p[39];
    fm106_reverb_apply_preset(&e->reverb, p);

    /* SEQ track params */
    e->seq.bpm    = p->p[40] < 40 ? 120 : (p->p[40] > 240 ? 240 : p->p[40]);
    e->seq.sdiv   = p->p[41] > 9 ? 3 : p->p[41];
    e->seq.slen   = (uint16_t)(p->p[42] | ((uint16_t)p->p[65] << 8));
    if (e->seq.slen < 1u) e->seq.slen = 16u;
    if (e->seq.slen > FM106_NSTEP) e->seq.slen = FM106_NSTEP;
    /* old presets predate SEQ (p[43]=0): default to a full-step gate,
     * not a 39% stub that chirps on slow-attack patches */
    e->seq.sgate  = p->p[43] < 1 ? 128 : (p->p[43] > 127 ? 127 : p->p[43]);
    e->seq.swing  = p->p[44] > 100 ? 0 : p->p[44];

    /* Voice allocation mode (0 = default/Poly I, 1 = Mono unison, 2 = Poly I, 3 = Poly II) */
    if (p->p[49] == 1u) {
        fm106_set_voice_mode(e, FM106_MODE_MONO);
    } else if (p->p[49] == 3u) {
        fm106_set_voice_mode(e, FM106_MODE_POLY2);
    } else {
        fm106_set_voice_mode(e, FM106_MODE_POLY1);
    }

    /* Update voice parameters */
    for (uint32_t i = 0; i < FM106_NUM_VOICES; i++) {
        fm106_adsr_params(&e->voice[i].adsr, e->env_a, e->env_d, e->env_s, e->env_r);
        e->voice[i].dco.saw_on = e->dco_saw_on;
        e->voice[i].dco.pulse_on = e->dco_pulse_on;
        e->voice[i].dco.sub_on = e->dco_sub_on;
        e->voice[i].dco.sub_level = e->dco_sub_level;
        e->voice[i].dco.noise_level = e->dco_noise_level;
        if (e->voice[i].note != 255u) {
            e->voice[i].base_inc = (e->voice_mode == FM106_MODE_MONO)
                ? fm106_calc_unison_inc(e->voice[i].note, 0, e->dco_range, i)
                : fm106_calc_inc(e->voice[i].note, 0, e->dco_range);
            if (!e->voice[i].gliding) e->voice[i].curr_inc = e->voice[i].base_inc;
        }
    }
    fm1_irq_on();
}

/* Engine initialization */
static void fm106_engine_init(fm106_engine_t *e)
{
    e->age_counter = 0;
    e->round_robin_next = 0;
    e->voice_mode = FM106_MODE_POLY1;
    e->mono_stack_len = 0;
    e->mono_note = -1;
    for (uint32_t i = 0; i < FM106_NUM_VOICES; i++) {
        e->voice_tbl[i] = (uint8_t)i;
        e->last_note[i] = 255u;
        e->ever_played[i] = 0;
    }
    e->octave_shift = 0;
    e->pitch_bend = 0;
    e->pitch_bend_val = 0;
    e->hold_pedal = 0;
    e->arp_on = 0;
    e->arp_mode = 0;
    e->arp_range = 1;
    e->arp_phase = 0;
    e->arp_step_idx = 0;
    e->arp_dir = 1;
    e->arp_last_note = -1;
    for (uint32_t i = 0; i < 4u; i++) {
        e->arp_pool[i] = 0;
        e->keys_down[i] = 0;
    }
    fm106_seq_init(&e->seq);
    e->lfo_ph = 0;
    e->lfo_val = 0;
    e->lfo_delay_timer = 0;

    fm106_hpf_init(&e->hpf);
    fm106_chorus_init(&e->chorus);
    for (uint32_t i = 0; i < FM106_FX_SLOTS; i++) fm106_fx_init(&e->fx[i], i);
    fm106_reverb_init(&e->reverb);

    for (uint32_t i = 0; i < FM106_NUM_VOICES; i++) {
        e->voice[i].note = 255u;
        e->voice[i].gate = 0;
        e->voice[i].vel = 0;
        e->voice[i].gliding = 0;
        e->voice[i].seq_vel = 0;
        e->voice[i].seq_accent = 0;
        e->voice[i].age = 0;
        e->voice[i].base_inc = 0;
        e->voice[i].curr_inc = 0;
        fm106_dco_init(&e->voice[i].dco, i);
        fm106_vcf_init(&e->voice[i].vcf);
        fm106_adsr_init(&e->voice[i].adsr);
        e->voice[i].dc_post_osc = 0.0f;
        e->voice[i].dc_post_vcf = 0.0f;
        e->voice[i].vca_slew = 0;
        e->voice[i].vcf_slew = 0;
    }

    fm106_load_preset(e, 0);
}

/* Voice allocation for Note On: MONO (Unison), POLY I (Bubble sort), POLY II (Linear priority) */
static void fm106_note_on(fm106_engine_t *e, uint32_t note, uint32_t vel)
{
    if (vel == 0) {
        return;
    }

    e->lfo_delay_timer = 0;

    /* -------------------------------------------------------------
     * Mode 0: MONO (Unison - all 6 voices stacked on one note)
     * ------------------------------------------------------------- */
    if (e->voice_mode == FM106_MODE_MONO) {
        for (uint32_t i = 0; i < e->mono_stack_len; i++) {
            if (e->mono_stack[i] == (uint8_t)note) {
                for (uint32_t j = i; j + 1 < e->mono_stack_len; j++) {
                    e->mono_stack[j] = e->mono_stack[j + 1];
                }
                e->mono_stack_len--;
                break;
            }
        }
        if (e->mono_stack_len < 16u) {
            e->mono_stack[e->mono_stack_len++] = (uint8_t)note;
        }

        int was_sounding = (e->mono_note >= 0);
        e->mono_note = (int16_t)note;

        for (uint32_t i = 0; i < FM106_NUM_VOICES; i++) {
            fm106_voice_t *v = &e->voice[i];
            v->note = (uint8_t)note;
            v->gate = 1;
            v->vel = (uint8_t)vel;
            v->gliding = 0;
            v->seq_vel = 0;
            v->seq_accent = 0;
            v->age = ++e->age_counter;
            v->base_inc = fm106_calc_unison_inc(note, 0, e->dco_range, i);
            v->curr_inc = v->base_inc;
            if (!was_sounding) {
                fm106_dco_reset(&v->dco);
                v->vca_slew = 0;
            }
            if (!e->dco_saw_on && !e->dco_pulse_on && !e->dco_sub_on && e->dco_noise_level == 0 && e->vcf_resonance > 80) {
                if (v->vcf.z[0] == 0.0f && v->vcf.z[1] == 0.0f) {
                    v->vcf.z[0] = 50.0f * (1.0f / 32768.0f);
                }
            }
            fm106_adsr_gate_on(&v->adsr);
        }
        return;
    }

    /* -------------------------------------------------------------
     * Mode 1: POLY I (Hardware bubble-sort allocator with restart candidate)
     * Matches Roland Juno-106 IC20 ROM at $0AAF/$0AF8.
     * ------------------------------------------------------------- */
    if (e->voice_mode == FM106_MODE_POLY1) {
        int restart_pos = -1;
        int first_free_pos = -1;

        /* Walk back-to-front */
        for (int pos = (int)FM106_NUM_VOICES - 1; pos >= 0; pos--) {
            uint8_t vi = e->voice_tbl[pos];
            if (e->voice[vi].gate) {
                first_free_pos = pos + 1;
                break;
            }
            if (e->ever_played[vi] && e->last_note[vi] == (uint8_t)note) {
                restart_pos = pos;
                break; /* FIRST match from the back wins */
            }
        }
        if (first_free_pos < 0) first_free_pos = 0;

        uint32_t pick;
        if (restart_pos >= 0) {
            /* Bubble matched voice to slot 0 */
            for (int pos = restart_pos; pos > 0; pos--) {
                uint8_t tmp = e->voice_tbl[pos];
                e->voice_tbl[pos] = e->voice_tbl[pos - 1];
                e->voice_tbl[pos - 1] = tmp;
            }
            pick = e->voice_tbl[0];
        } else if (first_free_pos < (int)FM106_NUM_VOICES) {
            pick = e->voice_tbl[first_free_pos];
        } else {
            /* Capacity full: steal oldest voice */
            uint32_t oldest_age = 0xFFFFFFFFu;
            pick = e->voice_tbl[FM106_NUM_VOICES - 1];
            for (uint32_t i = 0; i < FM106_NUM_VOICES; i++) {
                if (e->voice[i].age < oldest_age) {
                    oldest_age = e->voice[i].age;
                    pick = i;
                }
            }
        }

        fm106_voice_t *v = &e->voice[pick];
        int is_retrigger = (v->gate || v->adsr.stage != FM106_ENV_IDLE);
        v->note = (uint8_t)note;
        v->gate = 1;
        v->vel = (uint8_t)vel;
        v->gliding = 0;
        v->seq_vel = 0;
        v->seq_accent = 0;
        v->age = ++e->age_counter;
        v->base_inc = fm106_calc_inc(note, 0, e->dco_range);
        v->curr_inc = v->base_inc;
        e->last_note[pick] = (uint8_t)note;
        e->ever_played[pick] = 1;

        if (!is_retrigger) {
            fm106_dco_reset(&v->dco);
            v->vca_slew = 0;
        }
        if (!e->dco_saw_on && !e->dco_pulse_on && !e->dco_sub_on && e->dco_noise_level == 0 && e->vcf_resonance > 80) {
            if (v->vcf.z[0] == 0.0f && v->vcf.z[1] == 0.0f) v->vcf.z[0] = 50.0f * (1.0f / 32768.0f);
        }
        fm106_adsr_gate_on(&v->adsr);
        return;
    }

    /* -------------------------------------------------------------
     * Mode 2: POLY II (Linear search, fixed priority. Voice 0 first)
     * Matches Roland Juno-106 IC20 ROM at $0A76/$0B30.
     * ------------------------------------------------------------- */
    {
        /* 1. If note already sounding on a voice, retrigger it */
        for (uint32_t i = 0; i < FM106_NUM_VOICES; i++) {
            if (e->voice[i].note == (uint8_t)note && (e->voice[i].gate || e->voice[i].adsr.stage != FM106_ENV_IDLE)) {
                e->voice[i].gate = 1;
                e->voice[i].vel = (uint8_t)vel;
                e->voice[i].gliding = 0;
                e->voice[i].seq_vel = 0;
                e->voice[i].seq_accent = 0;
                e->voice[i].age = ++e->age_counter;
                e->voice[i].base_inc = fm106_calc_inc(note, 0, e->dco_range);
                e->voice[i].curr_inc = e->voice[i].base_inc;
                fm106_adsr_gate_on(&e->voice[i].adsr);
                return;
            }
        }

        /* 2. Linear search from voice 0: find first completely idle voice */
        uint32_t pick = FM106_NUM_VOICES;
        for (uint32_t i = 0; i < FM106_NUM_VOICES; i++) {
            if (e->voice[i].adsr.stage == FM106_ENV_IDLE && !e->voice[i].gate && e->voice[i].vca_slew <= 10) {
                pick = i;
                break;
            }
        }

        int is_retrigger = 0;
        /* 3. If none idle, linear search for first released voice */
        if (pick >= FM106_NUM_VOICES) {
            for (uint32_t i = 0; i < FM106_NUM_VOICES; i++) {
                if (!e->voice[i].gate) {
                    pick = i;
                    is_retrigger = 1;
                    break;
                }
            }
        }

        /* 4. If all gated, steal voice with oldest age */
        if (pick >= FM106_NUM_VOICES) {
            uint32_t oldest_age = 0xFFFFFFFFu;
            for (uint32_t i = 0; i < FM106_NUM_VOICES; i++) {
                if (e->voice[i].age < oldest_age) {
                    oldest_age = e->voice[i].age;
                    pick = i;
                }
            }
            is_retrigger = 1;
        }

        fm106_voice_t *v = &e->voice[pick];
        v->note = (uint8_t)note;
        v->gate = 1;
        v->vel = (uint8_t)vel;
        v->gliding = 0;
        v->seq_vel = 0;
        v->seq_accent = 0;
        v->age = ++e->age_counter;
        v->base_inc = fm106_calc_inc(note, 0, e->dco_range);
        v->curr_inc = v->base_inc;

        if (!is_retrigger) {
            fm106_dco_reset(&v->dco);
            v->vca_slew = 0;
        }
        if (!e->dco_saw_on && !e->dco_pulse_on && !e->dco_sub_on && e->dco_noise_level == 0 && e->vcf_resonance > 80) {
            if (v->vcf.z[0] == 0.0f && v->vcf.z[1] == 0.0f) v->vcf.z[0] = 50.0f * (1.0f / 32768.0f);
        }
        fm106_adsr_gate_on(&v->adsr);
    }
}

/* Sequencer Note-On: wraps fm106_note_on and tags step velocity / accent */
static void fm106_seq_note_on(fm106_engine_t *e, uint32_t note, uint32_t vel, uint8_t accent)
{
    fm106_note_on(e, note, vel);
    for (uint32_t i = 0; i < FM106_NUM_VOICES; i++) {
        if (e->voice[i].note == (uint8_t)note && e->voice[i].gate) {
            e->voice[i].seq_vel = (uint8_t)vel;
            e->voice[i].seq_accent = accent ? 1u : 0u;
            break;
        }
    }
}

/* Legato Sequencer Slide: glides an already-sounding voice to new_note without retrigger */
static void fm106_note_slide_voice(fm106_engine_t *e, uint32_t vi, uint32_t new_note, uint32_t vel, uint8_t accent)
{
    fm106_voice_t *v = &e->voice[vi];
    if (!v->curr_inc) v->curr_inc = v->base_inc;
    v->note = (uint8_t)new_note;
    v->gate = 1;
    v->vel = (uint8_t)vel;
    v->seq_vel = (uint8_t)vel;
    v->seq_accent = accent ? 1u : 0u;
    v->age = ++e->age_counter;
    v->base_inc = fm106_calc_inc(new_note, 0, e->dco_range);
    v->gliding = (v->curr_inc != v->base_inc) ? 1u : 0u;
    if (v->adsr.stage == FM106_ENV_IDLE || v->adsr.stage == FM106_ENV_RELEASE ||
        accent || (e->vca_mode == 0u && v->adsr.env14 < 1024u)) {
        fm106_adsr_gate_on(&v->adsr);
    }
}

/* Voice note off: advances ADSR release phase while silencing Gate-mode VCA */
static void fm106_note_off(fm106_engine_t *e, uint32_t note)
{
    /* Mode 0: MONO */
    if (e->voice_mode == FM106_MODE_MONO) {
        for (uint32_t i = 0; i < e->mono_stack_len; i++) {
            if (e->mono_stack[i] == (uint8_t)note) {
                for (uint32_t j = i; j + 1 < e->mono_stack_len; j++) {
                    e->mono_stack[j] = e->mono_stack[j + 1];
                }
                e->mono_stack_len--;
                break;
            }
        }
        if (e->mono_note == (int16_t)note) {
            if (e->mono_stack_len > 0) {
                uint8_t prev_note = e->mono_stack[e->mono_stack_len - 1];
                e->mono_note = (int16_t)prev_note;
                for (uint32_t i = 0; i < FM106_NUM_VOICES; i++) {
                    e->voice[i].note = prev_note;
                    e->voice[i].base_inc = fm106_calc_unison_inc(prev_note, 0, e->dco_range, i);
                    if (!e->voice[i].gliding) e->voice[i].curr_inc = e->voice[i].base_inc;
                }
            } else {
                e->mono_note = -1;
                for (uint32_t i = 0; i < FM106_NUM_VOICES; i++) {
                    e->voice[i].gate = 0;
                    if (!e->hold_pedal) {
                        fm106_adsr_gate_off(&e->voice[i].adsr);
                    }
                }
            }
        }
        return;
    }

    /* Mode 1: POLY I (Bubble released voice to back of voice_tbl) */
    if (e->voice_mode == FM106_MODE_POLY1) {
        int found_pos = -1;
        for (uint32_t pos = 0; pos < FM106_NUM_VOICES; pos++) {
            uint8_t vi = e->voice_tbl[pos];
            if (e->voice[vi].note == (uint8_t)note && e->voice[vi].gate) {
                found_pos = (int)pos;
                break;
            }
        }
        if (found_pos >= 0) {
            uint8_t vi = e->voice_tbl[found_pos];
            e->voice[vi].gate = 0;
            if (!e->hold_pedal) {
                fm106_adsr_gate_off(&e->voice[vi].adsr);
            }
            /* Bubble to back */
            for (uint32_t pos = (uint32_t)found_pos; pos + 1 < FM106_NUM_VOICES; pos++) {
                uint8_t tmp = e->voice_tbl[pos];
                e->voice_tbl[pos] = e->voice_tbl[pos + 1];
                e->voice_tbl[pos + 1] = tmp;
            }
        }
        return;
    }

    /* Mode 2: POLY II (Release first matching voice only, ROM $0B30) */
    for (uint32_t i = 0; i < FM106_NUM_VOICES; i++) {
        if (e->voice[i].note == (uint8_t)note && e->voice[i].gate) {
            e->voice[i].gate = 0;
            if (!e->hold_pedal) {
                fm106_adsr_gate_off(&e->voice[i].adsr);
            }
            break;
        }
    }
}

/* ------------------------------------------------------------------ *
 * Arpeggiator (ported from KR106Arpeggiator.h)
 *
 * Rate comes from the Juno-106 VR1 Schmitt-trigger relaxation
 * oscillator: f = 1 / (2 * Rt * C4 * 0.6633), Rt = R21 + VR1, giving
 * ~1.55 Hz (slowest) to ~48.6 Hz (fastest) -- i.e. 93..2916 steps/min.
 * FM106_ARP_INC_Q20 holds those rates pre-converted to a per-sample
 * Q20 phase increment at 44100 Hz, so no runtime division is needed.
 * ------------------------------------------------------------------ */

static const uint32_t FM106_ARP_INC_Q20[128] = {
    37u, 37u, 37u, 38u, 38u, 38u, 39u, 39u,
    39u, 40u, 40u, 40u, 41u, 41u, 41u, 42u,
    42u, 42u, 43u, 43u, 44u, 44u, 44u, 45u,
    45u, 46u, 46u, 46u, 47u, 47u, 48u, 48u,
    49u, 49u, 50u, 50u, 51u, 51u, 52u, 53u,
    53u, 54u, 54u, 55u, 56u, 56u, 57u, 58u,
    58u, 59u, 60u, 60u, 61u, 62u, 63u, 64u,
    64u, 65u, 66u, 67u, 68u, 69u, 70u, 71u,
    72u, 73u, 74u, 75u, 77u, 78u, 79u, 80u,
    82u, 83u, 85u, 86u, 88u, 89u, 91u, 93u,
    95u, 96u, 98u, 100u, 103u, 105u, 107u, 110u,
    112u, 115u, 118u, 121u, 124u, 127u, 130u, 134u,
    138u, 142u, 146u, 150u, 155u, 160u, 166u, 172u,
    178u, 185u, 192u, 200u, 209u, 218u, 229u, 240u,
    252u, 266u, 282u, 299u, 319u, 341u, 367u, 397u,
    433u, 475u, 527u, 591u, 674u, 782u, 933u, 1156u
};

/* Physical keyboard top note (Juno-106: 61 keys, MIDI 36..96).
 * Notes above this wrap down by octaves -- the arp cannot produce a
 * note higher than the keyboard that drives it. */
#define FM106_ARP_MAX_NOTE 96u

static inline void fm106_bit_set(uint32_t *bs, uint32_t i)
{
    bs[i >> 5] |= 1u << (i & 31u);
}

static inline void fm106_bit_clr(uint32_t *bs, uint32_t i)
{
    bs[i >> 5] &= ~(1u << (i & 31u));
}

static inline int fm106_bit_get(const uint32_t *bs, uint32_t i)
{
    return (int)((bs[i >> 5] >> (i & 31u)) & 1u);
}

static inline uint32_t fm106_bit_count(const uint32_t *bs)
{
    uint32_t c = 0;
    for (uint32_t i = 0; i < 4u; i++) {
        uint32_t x = bs[i];
        x = x - ((x >> 1) & 0x55555555u);
        x = (x & 0x33333333u) + ((x >> 2) & 0x33333333u);
        c += (((x + (x >> 4)) & 0x0F0F0F0Fu) * 0x01010101u) >> 24;
    }
    return c;
}

/* Expanded sequence length: every held note repeated once per octave */
static uint32_t fm106_arp_seq_len(const fm106_engine_t *e)
{
    return fm106_bit_count(e->arp_pool) * (uint32_t)e->arp_range;
}

/* Note at ascending index `idx` in the expanded sequence */
static int32_t fm106_arp_seq_note(const fm106_engine_t *e, uint32_t idx)
{
    uint32_t i = 0;
    for (uint32_t oct = 0; oct < (uint32_t)e->arp_range; oct++) {
        for (uint32_t n = 0; n < 128u; n++) {
            if (!fm106_bit_get(e->arp_pool, n)) {
                continue;
            }
            uint32_t note = n + oct * 12u;
            while (note > FM106_ARP_MAX_NOTE) {
                note -= 12u;
            }
            if (i == idx) {
                return (int32_t)note;
            }
            i++;
        }
    }
    return -1;
}

/* Advance the sequence and return the next note to play */
static int32_t fm106_arp_next(fm106_engine_t *e)
{
    uint32_t len = fm106_arp_seq_len(e);
    if (len == 0) {
        return -1;
    }
    if (e->arp_step_idx >= len) {
        e->arp_step_idx = 0;
    }

    int32_t note;
    switch (e->arp_mode) {
    case 1: /* Up/Down -- peak and trough notes play once */
        note = fm106_arp_seq_note(e, e->arp_step_idx);
        if (len > 1) {
            e->arp_step_idx = (uint16_t)((int32_t)e->arp_step_idx + e->arp_dir);
            if (e->arp_step_idx >= len) {
                e->arp_step_idx = (uint16_t)(len - 2u);
                e->arp_dir = -1;
            } else if (e->arp_step_idx == 0 && e->arp_dir < 0) {
                e->arp_step_idx = 1;
                e->arp_dir = 1;
            }
        }
        break;

    case 2: /* Down */
        note = fm106_arp_seq_note(e, len - 1u - (uint32_t)e->arp_step_idx);
        e->arp_step_idx = (uint16_t)((e->arp_step_idx + 1u) % len);
        break;

    default: /* Up */
        note = fm106_arp_seq_note(e, e->arp_step_idx);
        e->arp_step_idx = (uint16_t)((e->arp_step_idx + 1u) % len);
        break;
    }
    return note;
}

/* Advance the arpeggiator across one audio block of n frames.
 * Steps are quantised to the block boundary (5.8 ms at 256 frames) but
 * the phase remainder is kept, so timing does not drift. */
static void fm106_arp_step(fm106_engine_t *e, uint32_t n)
{
    if (!e->arp_on) {
        return;
    }

    if (fm106_bit_count(e->arp_pool) == 0) {
        /* Pool emptied: release whatever was still sounding */
        if (e->arp_last_note >= 0) {
            fm106_note_off(e, (uint32_t)e->arp_last_note);
            e->arp_last_note = -1;
        }
        return;
    }

    uint32_t inc = FM106_ARP_INC_Q20[e->arp_rate > 127u ? 127u : e->arp_rate];
    e->arp_phase += inc * n;

    for (uint32_t guard = 0; e->arp_phase >= (1u << 20) && guard < 8u; guard++) {
        e->arp_phase -= (1u << 20);

        /* Each step retriggers the gate -- envelope re-attacks */
        if (e->arp_last_note >= 0) {
            fm106_note_off(e, (uint32_t)e->arp_last_note);
        }
        int32_t note = fm106_arp_next(e);
        if (note >= 0) {
            fm106_note_on(e, (uint32_t)note, 127);
            e->arp_last_note = (int16_t)note;
        }
    }
    if (e->arp_phase >= (1u << 20)) {
        e->arp_phase = 0;   /* cannot happen at 256 frames; bounds debt */
    }
}

/* ---- Elektron-style parameter locks ----
 * Param index space = preset p[0..39] (sound params only; 20-22/26/32 are
 * unused gaps). get feeds the record diff; set is called by the audio ISR
 * at step boundaries — lock-free field writes only, mirroring the side
 * effects of fm106_load_preset. */
static uint8_t fm106_param_get(const fm106_engine_t *e, uint8_t idx)
{
    switch (idx) {
    case 0:  return e->bender_dco;
    case 1:  return e->bender_vcf;
    case 2:  return e->arp_rate;
    case 3:  return e->lfo_rate;
    case 4:  return e->lfo_delay;
    case 5:  return e->dco_lfo_depth;
    case 6:  return e->dco_pwm_depth;
    case 7:  return e->dco_sub_level;
    case 8:  return e->dco_noise_level;
    case 9:  return e->hpf.mode > 3 ? 1 : e->hpf.mode;
    case 10: return e->vcf_cutoff;
    case 11: return e->vcf_resonance;
    case 12: return e->vcf_env_mod;
    case 13: return e->vcf_lfo_mod;
    case 14: return e->vcf_kbd_track;
    case 15: return e->vca_level;
    case 16: return e->env_a;
    case 17: return e->env_d;
    case 18: return e->env_s;
    case 19: return e->env_r;
    case 23: return e->dco_pulse_on & 1u;
    case 24: return e->dco_saw_on & 1u;
    case 25: return e->dco_sub_on & 1u;
    case 27: return (e->chorus.mode == FM106_CHORUS_I ||
                     e->chorus.mode == FM106_CHORUS_I_II) ? 1u : 0u;
    case 28: return (e->chorus.mode == FM106_CHORUS_II ||
                     e->chorus.mode == FM106_CHORUS_I_II) ? 1u : 0u;
    case 29: return e->dco_range > 2 ? 1 : e->dco_range;
    case 30: return e->arp_mode > 2 ? 0 : e->arp_mode;
    case 31: return e->arp_range ? e->arp_range : 1u;
    case 33: return e->dco_pwm_mode & 1u;
    case 34: return e->vcf_env_polarity & 1u;
    case 35: return e->vca_mode & 1u;
    case 36: return e->fx[0].type < FM106_FX_COUNT ? e->fx[0].type : 0u;
    case 37: return (uint8_t)e->fx[0].mix;
    case 38: return (uint8_t)e->fx[0].rate;
    case 39: return (uint8_t)e->fx[0].character;
    case 45: return e->reverb.on & 1u;
    case 46: return e->reverb.mix;
    case 47: return e->reverb.decay;
    case 48: return e->reverb.shimmer;
    case 49: return e->voice_mode;
    default: return 0;
    }
}

static void fm106_param_set(fm106_engine_t *e, uint8_t idx, uint8_t v)
{
    uint32_t i;
    switch (idx) {
    case 0:  e->bender_dco = v; break;
    case 1:  e->bender_vcf = v; break;
    case 2:  e->arp_rate = v; break;
    case 3:  e->lfo_rate = v; break;
    case 4:  e->lfo_delay = v; break;
    case 5:  e->dco_lfo_depth = v; break;
    case 6:  e->dco_pwm_depth = v; break;
    case 7:  e->dco_sub_level = v;
             for (i = 0; i < FM106_NUM_VOICES; i++) e->voice[i].dco.sub_level = v;
             break;
    case 8:  e->dco_noise_level = v;
             for (i = 0; i < FM106_NUM_VOICES; i++) e->voice[i].dco.noise_level = v;
             break;
    case 9:  e->hpf.mode = v > 3 ? 1 : v; break;
    case 10: e->vcf_cutoff = v; break;
    case 11: e->vcf_resonance = v; break;
    case 12: e->vcf_env_mod = v; break;
    case 13: e->vcf_lfo_mod = v; break;
    case 14: e->vcf_kbd_track = v; break;
    case 15: e->vca_level = v; break;
    case 16: e->env_a = v; goto env_sync;
    case 17: e->env_d = v; goto env_sync;
    case 18: e->env_s = v; goto env_sync;
    case 19: e->env_r = v; goto env_sync;
    env_sync:
        for (i = 0; i < FM106_NUM_VOICES; i++)
            fm106_adsr_params(&e->voice[i].adsr, e->env_a, e->env_d, e->env_s, e->env_r);
        break;
    case 23: e->dco_pulse_on = v & 1u;
             for (i = 0; i < FM106_NUM_VOICES; i++) e->voice[i].dco.pulse_on = v & 1u;
             break;
    case 24: e->dco_saw_on = v & 1u;
             for (i = 0; i < FM106_NUM_VOICES; i++) e->voice[i].dco.saw_on = v & 1u;
             break;
    case 25: e->dco_sub_on = v & 1u;
             for (i = 0; i < FM106_NUM_VOICES; i++) e->voice[i].dco.sub_on = v & 1u;
             break;
    case 27: { /* chorus I bit; re-derive mode from both bits */
        uint8_t ii = (e->chorus.mode == FM106_CHORUS_II ||
                      e->chorus.mode == FM106_CHORUS_I_II);
        e->chorus.mode = v ? (ii ? FM106_CHORUS_I_II : FM106_CHORUS_I)
                           : (ii ? FM106_CHORUS_II : FM106_CHORUS_OFF);
        break;
    }
    case 28: {
        uint8_t i1 = (e->chorus.mode == FM106_CHORUS_I ||
                      e->chorus.mode == FM106_CHORUS_I_II);
        e->chorus.mode = v ? (i1 ? FM106_CHORUS_I_II : FM106_CHORUS_II)
                           : (i1 ? FM106_CHORUS_I : FM106_CHORUS_OFF);
        break;
    }
    case 29: e->dco_range = v > 2 ? 1 : v;
             for (i = 0; i < FM106_NUM_VOICES; i++)
                 if (e->voice[i].note != 255u) {
                     e->voice[i].base_inc = (e->voice_mode == FM106_MODE_MONO)
                         ? fm106_calc_unison_inc(e->voice[i].note, 0, e->dco_range, i)
                         : fm106_calc_inc(e->voice[i].note, 0, e->dco_range);
                     if (!e->voice[i].gliding) e->voice[i].curr_inc = e->voice[i].base_inc;
                 }
             break;
    case 30: e->arp_mode = v > 2 ? 0 : v; break;
    case 31: e->arp_range = v ? v : 1; break;
    case 33: e->dco_pwm_mode = v & 1u; break;
    case 34: e->vcf_env_polarity = v & 1u; break;
    case 35: e->vca_mode = v & 1u; break;
    case 36: e->fx[0].type = v >= FM106_FX_COUNT ? 0 : v; break; /* render re-inits via want */
    case 37: e->fx[0].mix = v; break;
    case 38: e->fx[0].rate = v; break;
    case 39: e->fx[0].character = v; break;
    case 45: e->reverb.on = v ? 1u : 0u; break;
    case 46: e->reverb.mix = v > 127u ? 127u : v; break;
    case 47: e->reverb.decay = v > 127u ? 127u : v; break;
    case 48: e->reverb.shimmer = v > 127u ? 127u : v; break;
    case 49: fm106_set_voice_mode(e, v > 2u ? FM106_MODE_POLY1 : v); break;
    default: break; /* unused indices */
    }
}

/* Refresh the lock cache from live params (play start / preset load), so
 * the record diff starts clean and a preset load is never mistaken for a
 * knob edit. */
static void fm106_param_snapshot(fm106_engine_t *e)
{
    for (uint8_t i = 0; i < FM106_NPARAM; i++)
        e->seq.plock_cache[i] = fm106_param_get(e, i);
}

/* Advance the step sequencer across one audio block of n frames. Lock-free,
 * CPU0-only (mutates voice state via fm106_note_on/off) — mirrors arp_step.
 * Handles gate countdown, step stepping (internal-sample or MIDI-slave units),
 * and fires note events through the engine note seam. */
static void fm106_seq_advance(fm106_engine_t *e, uint32_t n)
{
    fm106_seq_t *s = &e->seq;

    /* units to advance this block: internal clock = samples; MIDI-slave
     * interpolates beat-units between F8 pulses (elapsed-ms timebase) */
    uint32_t nadv = n;
    if (s->midi_sync_active) {
        if (s->mc_seen != s->mc_seen_last) {   /* fresh pulse: restart interp */
            s->mc_seen_last = s->mc_seen;
            s->mc_elapsed_ms = 0; s->mc_elapsed_frac = 0;
        }
        s->mc_elapsed_frac += n * 1000u;
        s->mc_elapsed_ms = s->mc_elapsed_frac / 44100u;
        s->mc_elapsed_frac %= 44100u;
        nadv = fm106_midi_clock_advance(s, s->mc_elapsed_ms);
    }

    /* gate countdown (with ratchet retriggers inside the step) — counted in
     * the same units as nadv (samples internal, MIDI beat-units slaved) */
    if (s->seq_n && !s->seq_hold) {
        if (s->seq_off <= nadv) {
            for (uint8_t i = 0; i < s->seq_n; i++) {
                fm106_note_off(e, s->seq_notes[i]);
                fm106_seq_msg_push(s, 0, s->seq_notes[i], 0); /* MIDI-out: note off */
            }
            s->seq_n = 0;
            if (s->seq_rch) {          /* ratchet: refire the same notes */
                uint32_t gate = s->seq_rch_period * (s->step[s->seq_idx].acc
                    ? s->step[s->seq_idx].acc
                    : (uint32_t)s->sgate * 2u) / 255u;
                if (!gate) gate = 1;
                fm106_step_t *st = &s->step[s->seq_idx];
                uint8_t m = st->n > 4u ? 4u : st->n;
                uint8_t is_acc = (uint8_t)(st->flags & FM106_SF_ACCENT);
                for (uint8_t i = 0; i < m; i++) {
                    fm106_seq_note_on(e, st->note[i], s->seq_rch_vel, is_acc);
                    fm106_seq_msg_push(s, 1, st->note[i], s->seq_rch_vel); /* MIDI-out */
                    if (s->seq_n < 4u) s->seq_notes[s->seq_n++] = st->note[i];
                }
                s->seq_off = gate;
                s->seq_rch--;
            }
        } else {
            s->seq_off -= nadv;
        }
    }
    if (!s->playing) return;

    uint32_t period = fm106_seq_div_samples(s, s->sdiv);
    uint32_t len = s->slen ? (uint32_t)s->slen : 1u;
    s->seq_pos += nadv;
    for (;;) {
        uint32_t cur = fm106_seq_step_samples(s, period, s->seq_idx);
        if (s->seq_pos < cur && s->seq_pos != 0x7FFFFFFFu + nadv) break;
        uint8_t starting = s->seq_pos >= 0x7FFFFFFFu;
        s->seq_pos = starting ? 0u : s->seq_pos - cur;
        s->seq_idx = (uint16_t)((s->seq_idx + 1u) % len);

        /* p-locks: apply this step's locks (also on rest/empty steps), and
         * refresh the cache so the record diff doesn't see the apply as a
         * fresh edit */
        for (uint16_t li = 0; li < s->nplock; li++) {
            if (s->plock[li].step == s->seq_idx) {
                fm106_param_set(e, s->plock[li].param, s->plock[li].value);
                s->plock_cache[s->plock[li].param] = s->plock[li].value;
            }
        }

        fm106_step_t *st = &s->step[s->seq_idx];
        uint8_t note[4], vel[4]; uint32_t gate = 0;
        int m = fm106_seq_plan(s, s->seq_idx, note, vel, &gate);
        uint8_t slide_in = s->seq_hold && s->seq_n;
        uint8_t next_tie = s->step[(s->seq_idx + 1u) % len].time == FM106_ST_TIE;

        if (m == -2) { /* TIE: extend gate, keep sounding */
            s->seq_off = gate;
            s->seq_hold = (st->flags & FM106_SF_SLIDE) || next_tie;
            continue;
        }
        if (m <= 0) { /* rest / empty / failed roll: release */
            for (uint8_t i = 0; i < s->seq_n; i++) {
                fm106_note_off(e, s->seq_notes[i]);
                fm106_seq_msg_push(s, 0, s->seq_notes[i], 0); /* MIDI-out: note off */
            }
            s->seq_n = 0; s->seq_hold = 0; s->seq_rch = 0; s->seq_rch_n = 0;
            continue;
        }
        uint8_t is_acc = (uint8_t)(st->flags & FM106_SF_ACCENT);
        if (!slide_in) {
            for (uint8_t i = 0; i < s->seq_n; i++) {
                fm106_note_off(e, s->seq_notes[i]);
                fm106_seq_msg_push(s, 0, s->seq_notes[i], 0); /* MIDI-out: note off */
            }
            s->seq_n = 0;
            for (int i = 0; i < m; i++) {
                fm106_seq_note_on(e, note[i], vel[i], is_acc);
                fm106_seq_msg_push(s, 1, note[i], vel[i]); /* MIDI-out: note on */
                if (s->seq_n < 4u) s->seq_notes[s->seq_n++] = note[i];
            }
        } else {
            /* Legato slide: pair active voices 1-to-1 (holding common chord tones
             * steady and gliding moving voices), preventing any stuck-voice leak. */
            uint8_t old_n = s->seq_n;
            uint8_t old_notes[4];
            int8_t  old_vi[4];
            uint8_t used_v = 0;
            for (uint8_t i = 0; i < old_n; i++) {
                old_notes[i] = s->seq_notes[i];
                old_vi[i] = -1;
                for (uint32_t vi = 0; vi < FM106_NUM_VOICES; vi++) {
                    if (!(used_v & (1u << vi)) && e->voice[vi].note == old_notes[i] &&
                        (e->voice[vi].gate || e->voice[vi].adsr.stage != FM106_ENV_IDLE)) {
                        old_vi[i] = (int8_t)vi;
                        used_v |= (uint8_t)(1u << vi);
                        break;
                    }
                }
            }
            s->seq_n = 0;
            uint8_t old_taken[4] = {0, 0, 0, 0};
            uint8_t new_done[4]  = {0, 0, 0, 0};

            for (int i = 0; i < m; i++) {
                for (uint8_t j = 0; j < old_n; j++) {
                    if (!old_taken[j] && old_notes[j] == note[i] && old_vi[j] >= 0) {
                        old_taken[j] = 1;
                        new_done[i] = 1;
                        fm106_note_slide_voice(e, (uint32_t)old_vi[j], note[i], vel[i], is_acc);
                        if (s->seq_n < 4u) s->seq_notes[s->seq_n++] = note[i];
                        break;
                    }
                }
            }
            for (int i = 0; i < m; i++) {
                if (new_done[i]) continue;
                int8_t pick_j = -1;
                for (uint8_t j = 0; j < old_n; j++) {
                    if (!old_taken[j] && old_vi[j] >= 0) { pick_j = (int8_t)j; break; }
                }
                if (pick_j >= 0) {
                    old_taken[pick_j] = 1;
                    new_done[i] = 1;
                    fm106_note_slide_voice(e, (uint32_t)old_vi[pick_j], note[i], vel[i], is_acc);
                    fm106_seq_msg_push(s, 1, note[i], vel[i]);
                    fm106_seq_msg_push(s, 0, old_notes[pick_j], 0);
                    if (s->seq_n < 4u) s->seq_notes[s->seq_n++] = note[i];
                }
            }
            for (int i = 0; i < m; i++) {
                if (!new_done[i]) {
                    fm106_seq_note_on(e, note[i], vel[i], is_acc);
                    fm106_seq_msg_push(s, 1, note[i], vel[i]);
                    if (s->seq_n < 4u) s->seq_notes[s->seq_n++] = note[i];
                }
            }
            for (uint8_t j = 0; j < old_n; j++) {
                if (!old_taken[j]) {
                    fm106_note_off(e, old_notes[j]);
                    fm106_seq_msg_push(s, 0, old_notes[j], 0);
                }
            }
        }
        s->seq_off = gate;
        s->seq_hold = (st->flags & FM106_SF_SLIDE) || next_tie;
        /* ratchet: arm retriggers within this step (not on slide/tie) */
        if (st->ratchet > 1u && !s->seq_hold) {
            s->seq_rch = (uint8_t)(st->ratchet - 1u);
            s->seq_rch_n = st->ratchet;
            s->seq_rch_vel = (uint8_t)(st->flags & FM106_SF_ACCENT) ? 127u : (st->vel ? st->vel : 96u);
            s->seq_rch_period = fm106_seq_step_samples(s, period, s->seq_idx) / st->ratchet;
        } else {
            s->seq_rch = 0; s->seq_rch_n = 0;
        }
    }
}

/* Panel/MIDI key press. Feeds the arp pool when the arp is enabled. */
static void fm106_key_on(fm106_engine_t *e, uint32_t note, uint32_t vel)
{
    if (note > 127u) {
        return;
    }
    fm1_irq_off();
#if DINGHY_DUAL_CORE
    if (audio_worker_online) audio_worker_join();
#endif
    if (fm106_bit_count(e->keys_down) == 0) {
        e->lfo_delay_timer = 0;
    }
    fm106_bit_set(e->keys_down, note);

    if (e->arp_on) {
        fm106_bit_set(e->arp_pool, note);
        if (fm106_bit_count(e->arp_pool) == 1u) {
            /* First note in: restart the sequence */
            e->arp_step_idx = 0;
            e->arp_dir = 1;
        }
        fm1_irq_on();
        return;   /* the arp owns the voices, not this key */
    }
    fm106_note_on(e, note, vel);
    fm1_irq_on();
}

/* Panel/MIDI key release. Hold pedal keeps arp notes in the pool. */
static void fm106_key_off(fm106_engine_t *e, uint32_t note)
{
    if (note > 127u) {
        return;
    }
    fm1_irq_off();
#if DINGHY_DUAL_CORE
    if (audio_worker_online) audio_worker_join();
#endif
    fm106_bit_clr(e->keys_down, note);

    if (e->arp_on) {
        if (!e->hold_pedal) {
            fm106_bit_clr(e->arp_pool, note);
        }
        fm1_irq_on();
        return;
    }
    fm106_note_off(e, note);
    fm1_irq_on();
}

/* Arp on/off transition. Enabling seeds from keys already held and
 * releases their chord voices; disabling restores them when held. */
static void fm106_arp_enable(fm106_engine_t *e, int on)
{
    on = on ? 1 : 0;
    if (on == (e->arp_on ? 1 : 0)) {
        return;
    }
    fm1_irq_off();
#if DINGHY_DUAL_CORE
    if (audio_worker_online) audio_worker_join();
#endif

    if (on) {
        for (uint32_t n = 0; n < 128u; n++) {
            if (fm106_bit_get(e->keys_down, n)) {
                fm106_bit_set(e->arp_pool, n);
                fm106_note_off(e, n);   /* arp takes over from the chord */
            }
        }
        e->arp_step_idx = 0;
        e->arp_dir = 1;
        e->arp_last_note = -1;
        e->arp_phase = 0;
        e->arp_on = 1;
    } else {
        if (e->arp_last_note >= 0) {
            fm106_note_off(e, (uint32_t)e->arp_last_note);
            e->arp_last_note = -1;
        }
        e->arp_on = 0;
        e->arp_pool[0] = e->arp_pool[1] = e->arp_pool[2] = e->arp_pool[3] = 0;
        e->arp_step_idx = 0;
        e->arp_dir = 1;
        e->arp_phase = 0;
        if (e->hold_pedal) {
            for (uint32_t n = 0; n < 128u; n++) {
                if (fm106_bit_get(e->keys_down, n)) {
                    fm106_note_on(e, n, 127);
                }
            }
        }
    }
    fm1_irq_on();
}

/* Render single voice for n frames into sum[] (accumulate == 0 overwrites sum[],
 * accumulate != 0 adds into sum[]). Placed in .ram_text on target so CPU0 and CPU1
 * execute concurrently from zero-wait-state internal RAMTEXT without XIP flash stalls. */
static FM106_RAM_FUNC void fm106_render_voice_ex(fm106_engine_t *e, uint32_t vi, int32_t *sum, uint32_t n, int accumulate)
{
    fm106_voice_t *v = &e->voice[vi];
    if (v->adsr.stage == FM106_ENV_IDLE && !v->gate && v->vca_slew <= 10) {
        v->vca_slew = 0;
        if (!accumulate) {
            for (uint32_t i = 0; i < n; i++) sum[i] = 0;
        }
        return;   /* Voice silent */
    }

    /* VCF base parameters for this voice (0x3F80 / 127 == 128, 254 / 127 == 2) */
    uint16_t vcfCutoff  = (uint16_t)((uint32_t)e->vcf_cutoff << 7);
    uint8_t lfoToVcf    = (uint8_t)(e->vcf_lfo_mod << 1);
    uint8_t vcfEnvMod   = (uint8_t)(e->vcf_env_mod << 1);
    uint8_t vcfKeyTrack = (uint8_t)(e->vcf_kbd_track << 1);
    uint8_t envPol      = (e->vcf_env_polarity == 0) ? 1u : 0u;
    uint16_t pitch88    = (uint16_t)((uint32_t)v->note << 8);
    uint8_t vcf_res     = e->vcf_resonance;
    uint8_t pw_depth    = e->dco_pwm_depth;
    uint8_t pw_mode     = e->dco_pwm_mode;
    uint8_t dco_lfo     = e->dco_lfo_depth;
    uint8_t vca_mode    = e->vca_mode;
    uint8_t hold_pedal  = e->hold_pedal;

    /* Sequencer per-step dynamics & accent (100% invariant 1.0x for keys/arp/MIDI) */
    float vca_norm = 1.0f / 32768.0f;
    if (v->seq_vel != 0u) {
        if (v->seq_accent) {
            uint32_t boosted = (uint32_t)vcfCutoff + 1400u;
            vcfCutoff = (uint16_t)(boosted > 0x3FFFu ? 0x3FFFu : boosted);
            vca_norm = 1.22f * (1.0f / 32768.0f);
        } else if (v->seq_vel < 96u) {
            float g = 0.25f + 0.75f * ((float)v->seq_vel * (1.0f / 96.0f));
            vca_norm = g * (1.0f / 32768.0f);
        } else if (v->seq_vel > 100u) {
            float t = (float)(v->seq_vel - 100u) * (1.0f / 27.0f);
            uint32_t boosted = (uint32_t)vcfCutoff + (uint32_t)(t * 900.0f);
            vcfCutoff = (uint16_t)(boosted > 0x3FFFu ? 0x3FFFu : boosted);
            vca_norm = (1.0f + 0.16f * t) * (1.0f / 32768.0f);
        }
    }

    /* Hoist block-constant DCO pitch bend and VCF pitch bend outside the 256-sample loop */
    int32_t dco_bend_mod = 0;
    if (e->pitch_bend_val != 0 && e->bender_dco > 0) {
        dco_bend_mod = ((int32_t)e->pitch_bend_val * (int32_t)e->bender_dco) >> 13;
    }

    uint16_t vcfBendAmt = 0;
    uint8_t bendPol = 0;
    if (e->pitch_bend_val != 0 && e->bender_vcf > 0) {
        int32_t raw_b = e->pitch_bend_val;
        bendPol = (raw_b < 0) ? 1u : 0u;
        uint32_t abs_b = raw_b < 0 ? (uint32_t)(-raw_b) : (uint32_t)raw_b;
        vcfBendAmt = (uint16_t)((abs_b * (uint32_t)e->bender_vcf) >> 9);
    }

    /* When DCO LFO depth is 0 and not gliding, eff_inc and 1/eff_inc are invariant across the block */
    uint32_t base_inc = v->base_inc;
    uint8_t gliding = v->gliding;
    float glide_inc = gliding ? (float)(v->curr_inc ? v->curr_inc : base_inc) : 0.0f;
    float target_inc = (float)base_inc;
    uint32_t static_inc = base_inc;
    if (dco_bend_mod != 0) {
        static_inc = (uint32_t)((int32_t)static_inc + ((dco_bend_mod * (int32_t)(static_inc >> 8)) >> 7));
    }
    uint32_t static_blep_inc = (static_inc < 2u) ? 0u : static_inc;
    float static_inv_inc = static_blep_inc ? (1.0f / (float)static_blep_inc) : 0.0f;
    uint16_t dac_slewed = (uint16_t)v->vcf_slew;   /* held VCF CV, refreshed every 4th sample */

    for (uint32_t i = 0; i < n; i++) {
        int32_t lfo_s = (int32_t)g_lfo_buf[i];

        uint32_t eff_inc;
        uint32_t blep_inc;
        float inv_inc;
        if (gliding || dco_lfo > 0) {
            if (gliding) {
                glide_inc += (target_inc - glide_inc) * 0.00055f;
                eff_inc = (uint32_t)glide_inc;
            } else {
                eff_inc = base_inc;
            }
            if (dco_lfo > 0) {
                /* Continuous per-sample DCO LFO modulation (~±1 semitone at full depth) */
                int32_t pitch_mod = (lfo_s * (int32_t)dco_lfo) >> 15;
                eff_inc = (uint32_t)((int32_t)eff_inc + ((pitch_mod * (int32_t)(eff_inc >> 8)) >> 3));
            }
            if (dco_bend_mod != 0) {
                eff_inc = (uint32_t)((int32_t)eff_inc + ((dco_bend_mod * (int32_t)(eff_inc >> 8)) >> 7));
            }
            blep_inc = (eff_inc < 2u) ? 0u : eff_inc;
            inv_inc = blep_inc ? (1.0f / (float)blep_inc) : 0.0f;
        } else {
            eff_inc  = static_inc;
            blep_inc = static_blep_inc;
            inv_inc  = static_inv_inc;
        }

        /* Continuous per-sample VCF LFO modulation */
        uint16_t vcfLfoSignal = 0;
        uint8_t lfoPol = 0;
        if (lfoToVcf > 0) {
            uint16_t lfoVal = (uint16_t)(((lfo_s < 0 ? -lfo_s : lfo_s) * 0x1FFFu) >> 15);
            vcfLfoSignal = (uint16_t)(((uint32_t)lfoToVcf * lfoVal) >> 9);
            lfoPol = (lfo_s < 0) ? 1u : 0u;
        }

        /* ADSR envelope */
        int32_t env_q15 = fm106_adsr_process(&v->adsr);

        /* VCA level using hardware 2SA1015/BA662 exponential curve with RC slew filter
         * Models C58 (0.1µF) || R106 (10K) || R105 (22K) -> tau = 687 us (1.51 ms rise/fall) */
        uint32_t vca_idx = (uint32_t)env_q15 >> 7;   /* 0..255 */
        if (vca_idx > 255u) vca_idx = 255u;
        int32_t vca_hw = FM106_VCA_HW[vca_idx];
        int32_t vca_target = (vca_mode == 1) ? ((v->gate || (hold_pedal && v->adsr.stage != FM106_ENV_IDLE)) ? 32767 : 0) : vca_hw;
        v->vca_slew += ((vca_target - v->vca_slew) * 1064) >> 15;
        int32_t vca_gain = v->vca_slew;

        /* DCO audio sample with FPU reciprocal PolyBLEP */
        int32_t dco_s = fm106_dco_process_fast(&v->dco, eff_inc, blep_inc, inv_inc, pw_depth, pw_mode, lfo_s, env_q15);

        /* Post-DCO DC blocker (1.59 Hz, models 10µF coupling cap between DCO and VCF) */
        dco_s = fm106_dc_block(dco_s, &v->dc_post_osc);

        /* VCF Cutoff calculated via genuine D7811G DAC calculation, updated at 11.025 kHz
         * (every 4th sample; the original voice CPU updated its cutoff DAC at ~236 Hz).
         * Holding the CV for 4 samples lets fm106_vcf_process reuse its cached
         * coefficients (g1, k, 1/(1+kG)) on 3 of every 4 samples while the cutoff is
         * moving, instead of recomputing (incl. a float divide) on every sample of every
         * voice during envelope/LFO sweeps. n is a multiple of 4, so i == 0 always updates. */
        if ((i & 3u) == 0u) {
            uint16_t env14_interp = (uint16_t)(env_q15 >> 1);
            uint16_t dac = fm106_calc_vcf_freq(
                vcfCutoff, vcfLfoSignal, vcfBendAmt, vcfEnvMod, vcfKeyTrack,
                lfoPol, bendPol, envPol, env14_interp, pitch88);

            /* VCF Cutoff CV analog RC slew filter: tau = 522 us. Per-sample Q15 coefficient
             * 1393 compounded over the 4-sample step: 1 - (1 - 1393/32768)^4 = 0.15951 -> 5227 */
            if (v->vcf_slew == 0) {
                v->vcf_slew = (int32_t)dac;
            } else {
                v->vcf_slew += (int32_t)(((int32_t)dac - v->vcf_slew) * 5227) >> 15;
            }
            dac_slewed = (uint16_t)v->vcf_slew;
        }

        /* 4-pole TPT ladder filter process (hardware single-precision FPU) */
        int32_t vcf_s = fm106_vcf_process(&v->vcf, dco_s, dac_slewed, vcf_res);

        /* Post-VCF DC blocker (1.59 Hz, models 1µF/100K coupling cap between VCF and VCA) */
        vcf_s = fm106_dc_block(vcf_s, &v->dc_post_vcf);

        /* VCA scaling via FPU (prevents 32-bit overflow on resonant VCF peaks without int64 math) */
        int32_t out_s = (int32_t)((float)vcf_s * ((float)vca_gain * vca_norm));
        if (accumulate) sum[i] += out_s;
        else            sum[i]  = out_s;
    }

    if (gliding) {
        float diff = target_inc - glide_inc;
        if (diff < 0.0f) diff = -diff;
        if (diff <= target_inc * 0.0005f + 1.0f) {
            v->curr_inc = base_inc;
            v->gliding = 0;
        } else {
            v->curr_inc = (uint32_t)glide_inc;
        }
    }
}

typedef void (*fm106_render_fn_t)(fm106_engine_t *, uint32_t, int32_t *, uint32_t, int);
static FM106_RAM_FUNC void fm106_render_voice_ex(fm106_engine_t *e, uint32_t vi, int32_t *sum, uint32_t n, int accumulate);
static volatile fm106_render_fn_t g_render_voice_fn = fm106_render_voice_ex;

static inline void fm106_render_voice(fm106_engine_t *e, uint32_t vi, int32_t *sum, uint32_t n)
{
    fm106_render_fn_t fn = g_render_voice_fn;
    fn(e, vi, sum, n, 1);
}

/* Worker kernel executed on CPU1 from .ram_text: renders dynamically assigned active voices (bitmask) */
static FM106_RAM_FUNC void fm106_worker_kernel(void *context)
{
    uint32_t mask = (uint32_t)(uintptr_t)context;
    int acc = 0;
    for (uint32_t v = 0; v < FM106_NUM_VOICES; v++) {
        if (mask & (1u << v)) {
            fm106_render_voice_ex(&g_synth, v, g_mix1, 256u, acc);
            acc = 1;
        }
    }
    if (!acc) {
        for (uint32_t i = 0; i < 256u; i++) g_mix1[i] = 0;
    }
}

/* Top-level Audio ISR entry point: renders n stereo frames into out[] */
static void fm106_audio_render(int32_t *out, uint32_t n)
{
    uint32_t t0 = fm1_ticks();
    fm106_engine_t *e = &g_synth;

    /* Advance arpeggiator before rendering so its note events land on
     * this block. Must run on CPU0 only (mutates voice state). */
    fm106_arp_step(e, n);

    /* Advance the step sequencer on the same CPU0 audio path (note events via
     * the lock-free fm106_note_on/off seam). */
    fm106_seq_advance(e, n);

    /* Juno-106 LFO range ≈ 0.12 Hz (rate=1) .. 15.6 Hz (rate=127). Phase is 32-bit,
     * read as ph>>16, so one cycle = 2^32 units: step = rate * 12000 gives
     * freq = rate*12000*44100/2^32. rate=0 holds phase. */
    uint32_t lfo_step = (uint32_t)e->lfo_rate * 12000u;
    uint32_t lfo_ph = e->lfo_ph;
    uint32_t delay_samples = (uint32_t)e->lfo_delay * 520u;   /* 0..~1.5s @ 44.1 kHz */
    uint32_t ramp_samples = 11025u;                           /* ~250ms ramp */
    int any_keys = (fm106_bit_count(e->keys_down) > 0) || e->arp_on;
    if (!any_keys) {
        for (uint32_t vi = 0; vi < FM106_NUM_VOICES; vi++) {   /* seq-fired notes gate the LFO too */
            if (e->voice[vi].gate) { any_keys = 1; break; }
        }
    }

    for (uint32_t i = 0; i < n; i++) {
        lfo_ph += lfo_step;
        int32_t lfo_tri = (int32_t)(lfo_ph >> 16) - 32768;
        if (lfo_tri < 0) lfo_tri = -lfo_tri;
        int32_t raw_lfo = (int16_t)((lfo_tri - 16384) << 1);

        if (e->lfo_delay > 0) {
            int32_t lfo_amp;
            if (any_keys) {
                uint32_t t = e->lfo_delay_timer;
                if (t < delay_samples) {
                    lfo_amp = 0;
                } else if (t < delay_samples + ramp_samples) {
                    /* Reciprocal of 11025 in Q16: 32768 * 65536 / 11025 = 194783 */
                    lfo_amp = (int32_t)(((t - delay_samples) * 194783u) >> 16);
                } else {
                    lfo_amp = 32767;
                }
                if (e->lfo_delay_timer < 0xFFFFFFFFu) e->lfo_delay_timer++;
            } else {
                lfo_amp = 0;
            }
            g_lfo_buf[i] = (int16_t)((raw_lfo * lfo_amp) >> 15);
        } else {
            g_lfo_buf[i] = (int16_t)raw_lfo;
        }
    }
    e->lfo_ph = lfo_ph;
    e->lfo_val = g_lfo_buf[n - 1];

    /* Collect active voices so CPU0 and CPU1 always split active DSP evenly
     * across 1..6 notes (with CPU1 taking ceil(k/2) since CPU0 also runs post-FX & IRQs). */
    uint8_t active_vi[FM106_NUM_VOICES];
    uint32_t num_active = 0;
    for (uint32_t vi = 0; vi < FM106_NUM_VOICES; vi++) {
        fm106_voice_t *v = &e->voice[vi];
        if (v->adsr.stage == FM106_ENV_IDLE && !v->gate && v->vca_slew <= 10) {
            v->vca_slew = 0;
        } else {
            active_vi[num_active++] = (uint8_t)vi;
        }
    }

    int worker_submitted = 0;
    uint32_t n0 = num_active;

#if DINGHY_DUAL_CORE
    int ble_busy = 0;
#if FELUCCA_BLE
    uint32_t bst = ble_ui_state();
    if (fm1_ms < 3000u || bst == BLE_ST_STARTING || bst == BLE_ST_SCANNING || bst == BLE_ST_CONNECTING) {
        ble_busy = 1;
    }
#endif
    if (num_active > 0 && audio_worker_online && n == 256u && !ble_busy) {
        /* Dual-Core voice split: CPU0 also runs HPF, VCA, Stereo Chorus, Tape FX, and Reverb.
         * Allocate up to 4..5 voices to CPU1 so both cores run in true balanced parallelism
         * at ~30-35% load each during polyphonic chords:
         *   k = 1: CPU1=1, CPU0=0
         *   k = 2: CPU1=1, CPU0=1
         *   k = 3: CPU1=2, CPU0=1
         *   k = 4: CPU1=3, CPU0=1
         *   k = 5: CPU1=4, CPU0=1
         *   k = 6: CPU1=4, CPU0=2 (or CPU1=5, CPU0=1 when Reverb is active) */
        uint32_t n1;
        if (num_active <= 2u) {
            n1 = (num_active + 1u) >> 1;
        } else if (num_active <= 4u) {
            n1 = num_active - 1u;
        } else {
            n1 = (e->reverb.on && e->reverb.mix > 0u && !e->reverb.sleeping) ? (num_active - 1u) : (num_active - 2u);
        }
        uint32_t cpu1_mask = 0;
        for (uint32_t idx = num_active - n1; idx < num_active; idx++) {
            cpu1_mask |= (1u << active_vi[idx]);
        }
        worker_submitted = audio_worker_submit(fm106_worker_kernel, (void *)(uintptr_t)cpu1_mask);
        if (worker_submitted) {
            n0 = num_active - n1;               /* CPU0 renders remainder while CPU1 runs parallel */
        }
    }
#endif

    /* CPU0 renders its share of active voices (0..3) while CPU1 runs in parallel */
    fm106_render_fn_t render_fn = g_render_voice_fn;
    for (uint32_t idx = 0; idx < n0; idx++) {
        render_fn(e, active_vi[idx], g_mix0, n, idx > 0u);
#if FELUCCA_UAC
        uac_isr_service();
#endif
    }

#if DINGHY_DUAL_CORE
    if (worker_submitted) {
        audio_worker_join();
#if FELUCCA_UAC
        uac_isr_service();
#endif
    }
#endif

    /* Master VCA gain from calibrated law table (Q12 -> float scale) */
    float vca_scale = (float)FM106_VCA_LAW[e->vca_level > 127u ? 127u : e->vca_level] * (1.0f / 4096.0f);

#if FELUCCA_UAC
    uac_isr_service();
#endif

    /* Post-processing: fuse core mix buffers -> HPF -> VCA -> Stereo Chorus -> Insert FX */
    int mix_mode = (num_active == 0u) ? 0 : (worker_submitted ? ((n0 > 0u) ? 3 : 2) : 1);
    for (uint32_t i = 0; i < n; i++) {
        int32_t raw_mix;
        if (mix_mode == 3)      raw_mix = g_mix0[i] + g_mix1[i];
        else if (mix_mode == 1) raw_mix = g_mix0[i];
        else if (mix_mode == 2) raw_mix = g_mix1[i];
        else                    raw_mix = 0;

        /* Headroom scaling: calibrated for clean polyphony across 6 voices even with HPF Bass Boost and max VCA */
        int32_t voice_sum = (raw_mix * 3) >> 6;

        /* Apply 4-position HPF */
        int32_t hpf_out = fm106_hpf_process(&e->hpf, voice_sum);

        /* Apply calibrated VCA level */
        int32_t vca_out = (int32_t)((float)hpf_out * vca_scale);

        /* Apply Stereo BBD Chorus */
        int32_t sl, sr;
        fm106_chorus_process(&e->chorus, vca_out, &sl, &sr);

        out[2u * i]      = sl;
        out[2u * i + 1u] = sr;
    }

    /* Insert FX chain, in-place on the interleaved stereo buffer (page order = chain order; no-op when OFF) */
    for (uint32_t i = 0; i < FM106_FX_SLOTS; i++) fm106_fx_run(&e->fx[i], out, n);

#if FELUCCA_UAC
    uac_isr_service();
#endif

    /* Master 1980s Shimmer Reverb (post-Chorus, post-Tape — final stage of audio chain) */
    fm106_reverb_run(&e->reverb, out, n);

#if FELUCCA_UAC
    uac_isr_service();
#endif

    uint32_t elapsed0 = fm1_ticks() - t0;
    uint32_t elapsed1 = 0;
#if DINGHY_DUAL_CORE
    if (worker_submitted) elapsed1 = audio_worker.last_job_ticks;
#endif
    g_cpu0_ticks = elapsed0;
    g_cpu1_ticks = elapsed1;
    if (elapsed0 > g_cpu0_peak) g_cpu0_peak = elapsed0;

    /* 256 frames at 44.1 kHz = 139,320 ticks of 24 MHz TIMER4 */
    uint32_t l0 = (uint32_t)((float)elapsed0 * (100.0f / 139320.0f));
    uint32_t l1 = (uint32_t)((float)elapsed1 * (100.0f / 139320.0f));
    if (l0 > 99u) l0 = 99u;
    if (l1 > 99u) l1 = 99u;
    g_cpu0_load_pct = (g_cpu0_load_pct * 7u + l0) >> 3;
    g_cpu1_load_pct = (g_cpu1_load_pct * 7u + l1) >> 3;
}


/* SPDX-License-Identifier: GPL-3.0-only */
/* Step sequencer for FM-106 — a single-track, acid-style engine ported from
 * Melodee seq.c © 2026 Leo Kuroshita (@kurogedelic) & Hügelton Instruments
 * (GPL-3.0-only), plus the Melodee MIDI-clock slave (midi_clock.c).
 *
 * B1 scope: ONE track, step note/length/gate, transport, and a MIDI-clock
 * slave. Drum lanes, per-track recording/quantise, pattern banks, motion,
 * chain and arp are dropped (later milestones). This header is PURE
 * timing/state: it never fires notes. fm106_seq_advance() lives in
 * fm106_engine.h (after the note seam) and calls fm106_seq_step(), which here
 * only computes which notes to play and returns them via out[] — so the audio-ISR
 * note events go through the engine's lock-free fm106_note_on/off.
 *
 * Timing: internal = advance in samples (44100/s); MIDI-slave = advance in
 * MIDI_BEAT_UNITS (24576 = 24ppqn x 1024) per beat, so tempo changes never
 * retime a running step/gate/tie — they only rescale the running phase
 * (fm106_midi_clock_mode). Both share step timing via div/swing. */
#ifndef FM106_SEQ_H
#define FM106_SEQ_H

#include <stdint.h>

#define FM106_NSTEP   1024u
#define FM106_ST_NOTE 0u
#define FM106_ST_TIE  1u
#define FM106_ST_REST 2u
#define FM106_SF_ACCENT 1u
#define FM106_SF_SLIDE  2u
#define FM106_MIDI_BEAT_UNITS 24576u /* 24ppqn * 1024 */
#define FM106_SWING_MAX 100

/* ---- Elektron-style parameter locks (B2) ----
 * One lock = (step, param, value). Param indices are the preset p[0..39]
 * sound space (see fm106_param_get/set in fm106_engine.h). Sparse table:
 * 256 slots covers any realistic pattern; a full table drops silently. */
#define FM106_PLOCK_SLOTS 1024u
#define FM106_NPARAM      41u
#define FM106_PARAM_VOICE_MODE 40u

typedef struct {
    uint16_t step;             /* locked step index (0..slen-1) */
    uint8_t  param;            /* FM106 param index (preset p[] space) */
    uint8_t  value;            /* locked value */
} fm106_plock_t;

typedef struct {
    uint8_t note[4];       /* MIDI notes held by this step */
    uint8_t n;             /* notes used (0 = empty step) */
    uint8_t time;          /* FM106_ST_NOTE / _TIE / _REST */
    uint8_t flags;         /* FM106_SF_ACCENT | FM106_SF_SLIDE */
    uint8_t vel;           /* base velocity (0 -> default 96) */
    uint8_t acc;           /* gate length 1..255 (0 -> use sgate) */
    uint8_t probability;   /* 0 = 100%, 1..100 = percent, 101 = silent */
    uint8_t ratchet;       /* 0/1 = off, 2..8 = repeats within the step */
} fm106_step_t;

/* seq → main-loop MIDI-out event (audio worker produces, main loop drains).
 * midi_send is main-loop-only, but seq notes fire in the audio worker, so
 * they cross via this lock-free single-producer/single-consumer queue. */
typedef struct { uint8_t on; uint8_t note; uint8_t vel; } fm106_seq_msg_t;
#define FM106_SEQ_Q 32u            /* power of two; drop-on-full, never block */

typedef struct {
    /* --- user params (panel-editable) --- */
    uint8_t  playing;      /* 1 = running */
    uint8_t  bpm;          /* 40..240 internal tempo */
    uint8_t  sdiv;         /* step divisor (fm106_seq_div_samples) */
    uint16_t slen;         /* pattern length 1..1024 steps */
    uint8_t  sgate;        /* default gate /128 when step.acc == 0 */
    uint8_t  swing;        /* 0..100 global swing */
    fm106_step_t step[FM106_NSTEP];

    /* --- parameter locks (pattern data) --- */
    fm106_plock_t plock[FM106_PLOCK_SLOTS];
    uint16_t nplock;             /* locks in use */
    uint8_t  plock_cache[FM106_NPARAM]; /* last-seen snapshot; keeps the
                                         * record diff and the ISR apply in
                                         * sync (so applied locks aren't
                                         * re-recorded as new changes) */

    /* --- runtime (engine-owned) --- */
    uint32_t seq_pos;      /* fractional position in current step (samples or MIDI units) */
    uint32_t seq_off;      /* remaining gate frames before note-off */
    uint16_t seq_idx;      /* current step index */
    uint8_t  seq_hold;     /* 1 = slide keeps notes sounding past gate */
    uint8_t  seq_n;        /* sounding sequencer notes */
    uint8_t  seq_notes[4]; /* sounding note list (clean release) */
    uint8_t  seq_rch;      /* ratchet: remaining hits after the first */
    uint8_t  seq_rch_n;    /* ratchet: total hits (0 = plain step) */
    uint8_t  seq_rch_vel;  /* ratchet: refire velocity */
    uint32_t seq_rch_period; /* ratchet: samples per hit */
    int32_t  rng;          /* xorshift32 for probability */

    /* --- seq → main-loop MIDI-out queue (single-producer audio worker,
     * single-consumer main loop; full drops the event, never blocks) --- */
    fm106_seq_msg_t msgq[FM106_SEQ_Q];
    volatile uint8_t msg_head;   /* written by producer only */
    volatile uint8_t msg_tail;   /* written by consumer only */

    /* --- MIDI clock slave --- */
    uint8_t  midi_sync_active;
    uint32_t midi_beat_samples; /* external samples-per-beat (0 = none) */
    uint32_t mc_pos, mc_rendered, mc_last_time, mc_interval_time, mc_tempo_time;
    uint8_t  mc_have_pulse, mc_tempo_valid, mc_tempo_n;
    uint8_t  mc_seen, mc_seen_last; /* pulse counter: engine resets interp on new pulse */
    uint32_t mc_elapsed_ms, mc_elapsed_frac; /* engine-side ms since last pulse */
} fm106_seq_t;

static const uint8_t fm106_seq_div_den[6] = {1, 2, 4, 8, 3, 6}; /* Melodee DIV_DEN */

/* SPSC queue: producer = audio worker (fm106_seq_advance), consumer = main
 * loop (drain → midi_send). Head advanced only AFTER the entry is written so
 * the consumer never reads a half-filled slot. */
static inline void fm106_seq_msg_push(fm106_seq_t *s, uint8_t on, uint8_t note, uint8_t vel)
{
    uint8_t next = (uint8_t)((s->msg_head + 1u) & (FM106_SEQ_Q - 1u));
    if (next == s->msg_tail) return;   /* full: drop, never block the audio path */
    s->msgq[s->msg_head].on   = on;
    s->msgq[s->msg_head].note = note;
    s->msgq[s->msg_head].vel  = vel;
    s->msg_head = next;
}
static inline int fm106_seq_msg_pop(fm106_seq_t *s, fm106_seq_msg_t *m)
{
    if (s->msg_tail == s->msg_head) return 0;
    *m = s->msgq[s->msg_tail];
    s->msg_tail = (uint8_t)((s->msg_tail + 1u) & (FM106_SEQ_Q - 1u));
    return 1;
}

static inline uint32_t fm106_seq_rand(fm106_seq_t *s)
{
    uint32_t x = (uint32_t)s->rng;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    s->rng = (int32_t)x;
    return x;
}

static inline uint8_t  fm106_step_gate(const fm106_step_t *s) { return s->acc; }
static inline uint16_t fm106_step_chance(const fm106_step_t *s)
{ return !s->probability ? 100u : (s->probability <= 100u ? s->probability : 0u); }

static inline uint32_t fm106_seq_beat_samples(fm106_seq_t *s)
{ return s->midi_sync_active ? FM106_MIDI_BEAT_UNITS
                              : (uint32_t)44100u * 60u / (s->bpm ? s->bpm : 120u); }

static inline uint32_t fm106_seq_div_samples(fm106_seq_t *s, uint8_t div)
{
    uint32_t q = fm106_seq_beat_samples(s);
    if (div < 6u)  return q / fm106_seq_div_den[div];
    if (div < 10u) return q << (div - 5u);
    return q / fm106_seq_div_den[div % 6u];
}

/* swing: even steps longer, odd shorter (100 -> even 1.4x / odd 0.6x). */
static inline uint32_t fm106_seq_step_samples(fm106_seq_t *s, uint32_t base, uint16_t idx)
{
    int32_t sw = (int32_t)s->swing * (int32_t)base / 250;
    return base + (uint32_t)((idx & 1u) ? -sw : sw);
}

/* Compute step `idx`'s notes. Returns note count, fills out_note/out_vel.
 * Handles probability roll / TIE / REST / empty internally by returning -1
 * (caller must release + rest). Gate length returned via *out_gate (frames). */
static int fm106_seq_plan(fm106_seq_t *s, uint16_t idx,
                          uint8_t *out_note, uint8_t *out_vel, uint32_t *out_gate)
{
    fm106_step_t *st = &s->step[idx % (s->slen ? s->slen : 1u)];
    uint32_t period = fm106_seq_div_samples(s, s->sdiv);
    uint32_t gate = fm106_step_gate(st)
        ? period * fm106_step_gate(st) / 255u
        : period * (uint32_t)s->sgate / 128u;
    uint16_t chance = fm106_step_chance(st);

    if (chance < 100u && (fm106_seq_rand(s) % 100u) >= chance)
        return -1; /* failed roll: rest */
    if (st->time == FM106_ST_TIE) {
        *out_gate = fm106_step_gate(st) ? gate : gate + period / 2u;
        return -2; /* TIE: extend gate only (caller keeps sounding) */
    }
    if (st->time == FM106_ST_REST || !st->n)
        return -1; /* rest/empty: release */

    uint8_t vel = (st->flags & FM106_SF_ACCENT) ? 127u : (st->vel ? st->vel : 96u);
    uint8_t m = st->n > 4u ? 4u : st->n;
    for (uint8_t i = 0; i < m; i++) { out_note[i] = st->note[i]; out_vel[i] = vel; }
    *out_gate = gate;
    return (int)m;
}

static inline void fm106_seq_start(fm106_seq_t *s)
{
    s->seq_idx = (uint16_t)(s->slen ? s->slen - 1u : 0u);
    s->seq_pos = 0x7FFFFFFFu; /* sentinel: step 0 fires on first block */
    s->seq_n = 0; s->seq_hold = 0; s->seq_off = 0;
    s->msg_tail = s->msg_head; /* drop stale queued MIDI-out events */
    s->playing = 1;
}

/* ---- parameter locks (Elektron-style) ----
 * Upsert: one lock per (step,param); a full table drops silently.
 * Safe from the main loop without irq_off: only the main loop appends
 * (single writer), and the audio ISR only reads entries below nplock. */
static void fm106_plock_set(fm106_seq_t *s, uint16_t step, uint8_t param, uint8_t value)
{
    if (param >= FM106_NPARAM) return;
    for (uint32_t i = 0; i < s->nplock; i++) {
        if (s->plock[i].step == step && s->plock[i].param == param) {
            s->plock[i].value = value;
            return;
        }
    }
    if (s->nplock < FM106_PLOCK_SLOTS) {
        s->plock[s->nplock].step  = step;
        s->plock[s->nplock].param = param;
        s->plock[s->nplock].value = value;
        s->nplock++;
    }
}

static inline void fm106_plock_clear(fm106_seq_t *s) { s->nplock = 0; }

/* drop every lock belonging to one step (compacts the table) */
static inline void fm106_plock_clear_step(fm106_seq_t *s, uint16_t step)
{
    uint32_t w = 0;
    for (uint32_t i = 0; i < s->nplock; i++) {
        if (s->plock[i].step != step) {
            s->plock[w] = s->plock[i];
            w++;
        }
    }
    s->nplock = (uint16_t)w;
}

/* ---- MIDI clock slave (Melodee midi_clock.c) ---- */
static inline void fm106_midi_clock_mode(fm106_seq_t *s, uint8_t mode)
{
    if (mode == s->midi_sync_active) return;
    uint32_t oldb = fm106_seq_beat_samples(s);
    s->midi_sync_active = mode;
    uint32_t newb = fm106_seq_beat_samples(s);
    if (oldb && newb && oldb != newb) {
        uint32_t scale = ((newb << 8) + (oldb >> 1)) / oldb;
        s->seq_pos = (uint32_t)(((uint64_t)s->seq_pos * scale) >> 8);
    }
}

static void fm106_midi_clock_transport(fm106_seq_t *s, uint8_t status)
{
    if (status == 0xFAu) {           /* Start */
        s->mc_pos = s->mc_rendered = 0;
        s->mc_have_pulse = s->mc_tempo_valid = 0;
        s->mc_interval_time = 0;
        fm106_seq_start(s);
    } else if (status == 0xFBu) {    /* Continue */
        s->mc_pos = s->mc_rendered;
        s->mc_have_pulse = s->mc_tempo_valid = 0;
        s->mc_interval_time = 0;
        s->playing = 1;
    } else if (status == 0xFCu) {    /* Stop */
        s->playing = 0; /* release handled by engine fm106_seq_advance */
    }
}

static void fm106_midi_clock_pulse(fm106_seq_t *s, uint32_t time)
{
    if (!s->mc_tempo_valid) {
        s->mc_tempo_valid = 1; s->mc_tempo_time = time; s->mc_tempo_n = 0;
    } else if (++s->mc_tempo_n == 6u) {
        uint32_t dt = time - s->mc_tempo_time;
        s->mc_tempo_time = time; s->mc_tempo_n = 0;
        if (dt >= 62u && dt <= 375u) {                     /* 6 pulses -> beat */
            s->midi_beat_samples = (uint32_t)((uint64_t)44100u * dt / 1000u); /* dt in ms */
            uint32_t bpm = (15000u + dt / 2u) / dt;
            s->bpm = (uint8_t)(bpm < 40u ? 40u : bpm > 240u ? 240u : bpm);
        }
    }
    if (s->playing) {
        if (s->mc_have_pulse) {
            uint32_t interval = time - s->mc_last_time;
            if (interval >= 8u && interval <= 80u) s->mc_interval_time = interval;
            s->mc_pos += FM106_MIDI_BEAT_UNITS / 24u; /* 1024 / pulse */
        }
        s->mc_have_pulse = 1;
    }
    if (!s->mc_interval_time) s->mc_interval_time = 60000u / (s->bpm * 24u);
    s->mc_last_time = time;
    s->mc_seen++;   /* engine resets its interp on a fresh pulse */
}

/* Interpolate between pulses for sample-accurate advance; elapsed = ms since
 * the last pulse (the engine accumulates it from audio blocks). Returns the
 * number of beat-units to advance. */
static uint32_t fm106_midi_clock_advance(fm106_seq_t *s, uint32_t elapsed)
{
    if (!s->mc_have_pulse) return 0;
    if (elapsed > s->mc_interval_time) elapsed = s->mc_interval_time;
    uint32_t unit = FM106_MIDI_BEAT_UNITS / 24u;
    uint32_t offset = s->mc_interval_time ? elapsed * unit / s->mc_interval_time : 0;
    if (offset >= unit) offset = unit - 1u;
    uint32_t target = s->mc_pos + offset;
    uint32_t n = (int32_t)(target - s->mc_rendered) > 0 ? target - s->mc_rendered : 0u;
    s->mc_rendered += n;
    return n;
}

static inline void fm106_seq_init(fm106_seq_t *s)
{
    for (uint32_t i = 0; i < sizeof(*s) / 4u; i++) ((uint32_t *)s)[i] = 0;
    s->bpm = 120; s->sdiv = 3; s->slen = 16; s->sgate = 128; s->swing = 0;
    s->rng = (int32_t)0x9E3779B9u;
    s->playing = 0;
}

#endif /* FM106_SEQ_H */

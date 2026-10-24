/* SPDX-License-Identifier: GPL-3.0-only */
/* FM-106: Authentic 6-Voice Roland Juno-106 Synthesizer Emulator
 * Ported from ultramaster_kr106 DSP with dual-core acceleration,
 * 1980s Roland styling, 128 factory presets, and MN3009 BBD Stereo Chorus. */

#define APP_NAME "FM-106"
#define FELUCCA_ID "FM-1_907"

#include "platform.c"
#include "fm106_engine.h"
#include "fm106_sysex.h"

fm106_preset_t g_user_presets[FM106_NUM_USER_PRESETS];
uint32_t g_dirty_screen = 1;

/* ---- 1980s Roland Juno-106 Color Palette */
#define C_BG           RGB(24, 26, 30)       /* Dark metal chassis grey */
#define C_STRIPE_CYAN  RGB(0, 165, 225)      /* Signature Juno-106 cyan stripe */
#define C_ROLAND_RED   RGB(235, 45, 30)      /* Roland badge and button red */
#define C_ROLAND_AMBER RGB(245, 175, 20)     /* Amber / orange accent */
#define C_TEXT_WHITE   RGB(235, 235, 235)    /* Screen silk-screen text */
#define C_TEXT_DIM     RGB(140, 145, 155)    /* Parameter labels */
#define C_LINE_DIV     RGB(55, 60, 70)       /* Panel divider lines */
#define C_LED_ON       RGB(255, 30, 30)      /* Bright red 7-seg / LED */
#define C_LED_OFF      RGB(60, 15, 15)       /* Dim LED glow */
#define C_FADER_BG     RGB(12, 14, 16)       /* Fader slot recess */
#define C_FADER_LINE   RGB(70, 75, 85)       /* Center track line */
#define C_FADER_CAP    RGB(245, 110, 25)     /* Classic Roland orange fader knob */
#define C_KEY_LIT      RGB(235, 45, 30)      /* Playing key illumination */
#define C_KEY_WHITE    RGB(220, 220, 220)    /* Unlit white key */
#define C_KEY_BLACK    RGB(30, 30, 30)       /* Unlit black key */

#define KEY0 53u                             /* Key 0 = F3 (MIDI 53) */

/* Edit pages */
enum {
    PAGE_VCF = 0,       /* FREQ RES ENV LFO */
    PAGE_VCF2,          /* KBD INV B.DCO B.VCF */
    PAGE_DCO,           /* RNG PULSE SAW SUBSW */
    PAGE_DCO2,          /* SUB NOISE PWM DCOLFO */
    PAGE_ENVELOPE,      /* ATK DEC SUS REL */
    PAGE_LFO,           /* RATE DELAY PWM MD - */
    PAGE_ARP,           /* ON MODE RATE RANGE */
    PAGE_FX,            /* CHO HPF VCA LEVEL */
    PAGE_TAPE,          /* TAPE: TYPE MIX RATE CHAR */
    PAGE_SEQ,           /* SEQ: STEP PITCH GATE VEL (piano-roll edit) */
    PAGE_COUNT = 10
};

static uint8_t g_current_page = PAGE_VCF;

/* ---- Edit-page slot descriptors (KR-106 plugin param set) ---- */
enum { SLOT_FADER = 0, SLOT_MODE, SLOT_EMPTY };

static const char *const N_BOOL[2]   = {"OFF", "ON"};
static const char *const N_RNG[3]    = {"16'", "8'", "4'"};
static const char *const N_PWMD[2]   = {"LFO", "MAN"};
static const char *const N_POLY[3]   = {"MONO", "POLY I", "POLY II"};
static const char *const N_INV[2]    = {"NORM", "INV"};
static const char *const N_ARMD[3]   = {"UP", "U&D", "DN"};
static const char *const N_ARNG[3]   = {"1OC", "2OC", "3OC"};
static const char *const N_CHOR[4]   = {"OFF", "I", "II", "I+II"};
static const char *const N_HPF[4]    = {"BST", "FLT", "236", "754"};
static const char *const N_VCA[2]    = {"ENV", "GATE"};
static const char *const N_FX[FM106_FX_COUNT] = {"OFF", "ON"};
static const char *const N_SEQ[2]    = {"STOP", "PLAY"};
static const char *const N_SDIV[10]  = {"1", "1/2", "1/4", "1/8", "1/3", "1/6", "2X", "4X", "8X", "16X"};

typedef struct {
    const char *lbl;
    uint8_t kind;               /* SLOT_FADER / SLOT_MODE / SLOT_EMPTY */
    uint8_t nseg;
    const char *const *names;
} fm1_slot_t;

static const fm1_slot_t FM1_SLOTS[PAGE_COUNT][4] = {
    /* PAGE_VCF */
    {{"FREQ", SLOT_FADER, 0, 0}, {"RES", SLOT_FADER, 0, 0},
     {"ENV", SLOT_FADER, 0, 0},  {"LFO", SLOT_FADER, 0, 0}},
    /* PAGE_VCF2 */
    {{"KBD", SLOT_FADER, 0, 0},   {"INV", SLOT_MODE, 2, N_INV},
     {"B.DCO", SLOT_FADER, 0, 0}, {"B.VCF", SLOT_FADER, 0, 0}},
    /* PAGE_DCO */
    {{"RNG", SLOT_MODE, 3, N_RNG},   {"PULSE", SLOT_MODE, 2, N_BOOL},
     {"SAW", SLOT_MODE, 2, N_BOOL},  {"SUBSW", SLOT_MODE, 2, N_BOOL}},
    /* PAGE_DCO2 */
    {{"SUB", SLOT_FADER, 0, 0}, {"NOISE", SLOT_FADER, 0, 0},
     {"PWM", SLOT_FADER, 0, 0}, {"DCOLFO", SLOT_FADER, 0, 0}},
    /* PAGE_ENVELOPE */
    {{"ATK", SLOT_FADER, 0, 0}, {"DEC", SLOT_FADER, 0, 0},
     {"SUS", SLOT_FADER, 0, 0}, {"REL", SLOT_FADER, 0, 0}},
    /* PAGE_LFO */
    {{"RATE", SLOT_FADER, 0, 0}, {"DELAY", SLOT_FADER, 0, 0},
     {"PWM MD", SLOT_MODE, 2, N_PWMD}, {"ASSIGN", SLOT_MODE, 3, N_POLY}},
    /* PAGE_ARP */
    {{"ARP", SLOT_MODE, 2, N_BOOL}, {"MODE", SLOT_MODE, 3, N_ARMD},
     {"ARAT", SLOT_FADER, 0, 0},    {"ARNG", SLOT_MODE, 3, N_ARNG}},
    /* PAGE_FX */
    {{"CHO", SLOT_MODE, 4, N_CHOR}, {"HPF", SLOT_MODE, 4, N_HPF},
     {"VCA", SLOT_MODE, 2, N_VCA},  {"LEVEL", SLOT_FADER, 0, 0}},
    /* PAGE_TAPE: single tape insert */
    {{"TAPE", SLOT_MODE, FM106_FX_COUNT, N_FX},  {"MIX", SLOT_FADER, 0, 0},
     {"RATE", SLOT_FADER, 0, 0},  {"CHAR", SLOT_FADER, 0, 0}},
    /* PAGE_SEQ */
    {{"SEQ", SLOT_MODE, 2, N_SEQ}, {"LEN", SLOT_FADER, 0, 0},
     {"DIV", SLOT_MODE, 10, N_SDIV}, {"GATE", SLOT_FADER, 0, 0}},
};

/* MIDI CC dispatch for one insert slot: 0=type (d2*COUNT/128), 1=mix, 2=rate, 3=character */
static void fm106_cc_fx(fm106_fx_t *f, uint32_t param, uint8_t d2)
{
    if (param == 0)      f->type = (uint8_t)((d2 * (uint32_t)FM106_FX_COUNT) / 128u) >= FM106_FX_COUNT ? (uint8_t)(FM106_FX_COUNT - 1) : (uint8_t)((d2 * (uint32_t)FM106_FX_COUNT) / 128u);
    else if (param == 1) f->mix = d2 > 127 ? 127 : d2;
    else if (param == 2) f->rate = d2 > 127 ? 127 : d2;
    else if (param == 3) f->character = d2 > 127 ? 127 : d2;
}

/* Current value of slot: fader → 0..127, mode → selected index */
static int32_t fm1_slot_val(uint8_t page, int i)
{
    switch (page) {
    case PAGE_VCF:
        switch (i) {
        case 0: return g_synth.vcf_cutoff;
        case 1: return g_synth.vcf_resonance;
        case 2: return g_synth.vcf_env_mod;
        case 3: return g_synth.vcf_lfo_mod;
        }
        break;
    case PAGE_VCF2:
        switch (i) {
        case 0: return g_synth.vcf_kbd_track;
        case 1: return g_synth.vcf_env_polarity & 1u;
        case 2: return g_synth.bender_dco;
        case 3: return g_synth.bender_vcf;
        }
        break;
    case PAGE_DCO:
        switch (i) {
        case 0: return g_synth.dco_range > 2 ? 1 : g_synth.dco_range;
        case 1: return g_synth.dco_pulse_on & 1u;
        case 2: return g_synth.dco_saw_on & 1u;
        case 3: return g_synth.dco_sub_on & 1u;
        }
        break;
    case PAGE_DCO2:
        switch (i) {
        case 0: return g_synth.dco_sub_level;
        case 1: return g_synth.dco_noise_level;
        case 2: return g_synth.dco_pwm_depth;
        case 3: return g_synth.dco_lfo_depth;
        }
        break;
    case PAGE_ENVELOPE:
        switch (i) {
        case 0: return g_synth.env_a;
        case 1: return g_synth.env_d;
        case 2: return g_synth.env_s;
        case 3: return g_synth.env_r;
        }
        break;
    case PAGE_LFO:
        switch (i) {
        case 0: return g_synth.lfo_rate;
        case 1: return g_synth.lfo_delay;
        case 2: return g_synth.dco_pwm_mode & 1u;
        case 3: return g_synth.voice_mode > 2 ? 1 : g_synth.voice_mode;
        }
        break;
    case PAGE_ARP:
        switch (i) {
        case 0: return g_synth.arp_on & 1u;
        case 1: return g_synth.arp_mode > 2 ? 0 : g_synth.arp_mode;
        case 2: return g_synth.arp_rate;
        case 3: return g_synth.arp_range < 1 ? 0 : (g_synth.arp_range > 3 ? 2 : g_synth.arp_range - 1u);
        }
        break;
    case PAGE_FX:
        switch (i) {
        case 0: return g_synth.chorus.mode & 3u;
        case 1: return g_synth.hpf.mode & 3u;
        case 2: return g_synth.vca_mode & 1u;
        case 3: return g_synth.vca_level;
        }
        break;
    case PAGE_TAPE: {
        const fm106_fx_t *fx = &g_synth.fx[0];
        switch (i) {
        case 0: return fx->type < FM106_FX_COUNT ? fx->type : 0;
        case 1: return fx->mix;
        case 2: return fx->rate;
        case 3: return fx->character;
        }
        break;
    }
    case PAGE_SEQ: {
        const fm106_seq_t *s = &g_synth.seq;
        switch (i) {
        case 0: return s->playing & 1u;
        case 1: return (uint16_t)(s->slen ? s->slen - 1u : 0u); /* 0..63 -> LEN */
        case 2: return s->sdiv > 9 ? 3 : s->sdiv;
        case 3: return s->sgate;
        }
        break;
    }
    default:
        break;
    }
    return 0;
}

/* UI Modes */
enum {
    UI_MODE_NORMAL = 0,
    UI_MODE_MENU,
    UI_MODE_MIDI,
    UI_MODE_CC_MAP,
    UI_MODE_ABOUT,
    UI_MODE_SAVE,
    UI_MODE_COUNT
};

static uint8_t g_ui_mode = UI_MODE_NORMAL;
static uint8_t g_menu_sel = 0;
static uint8_t g_midi_sel = 0;
static uint8_t g_cc_map_sel = 0;
static uint8_t g_cc_map_scroll = 0;

static uint8_t g_save_slot = 0;          /* Target user slot (0..127 -> UA11..UB88) */
static char g_save_name[16];             /* 15 chars + null */
static uint8_t g_save_cursor = 0;        /* 0..14 */
static uint8_t g_save_del_confirm = 0;   /* 0 = normal, 1 = "Are you sure?" */
static uint32_t t_save_down = 0;         /* Timestamp when BTN_SAVE was pressed */
static uint8_t save_held_active = 0;     /* 1 if long press triggered UI_MODE_SAVE */
static char g_banner_msg[32] = {0};      /* Screen notification banner */
static uint32_t g_banner_until = 0;      /* Expiry timestamp for banner */

/* ---- SEQ piano-roll UI state (Melodee single-track port) ---- */
static uint16_t g_seq_cursor   = 0;        /* edit cursor: step 0..slen-1 */
static uint8_t  g_seq_view_lo  = 48;        /* bottom pitch of roll window */
static uint16_t g_seq_view_step= 0;         /* first visible step (0..slen-16) */
static uint8_t  g_seq_rec_arm  = 0;         /* live-record armed */
static uint32_t g_clk_next     = 0;         /* master clock out: next F8 due (ms) */
static uint8_t  g_seq_last_pitch = 60;      /* last entered pitch (for follow) */
static uint8_t  g_seq_row      = 60;        /* K2: selected pitch row (scale cursor) */
static uint8_t  g_seq_dirty    = 1;         /* redraw request */
static uint32_t t_last_seq     = 0;         /* SEQ playhead refresh timer */

/* SEQ options menu (long-press EDIT on the roll) */
static uint8_t  g_seq_menu_on   = 0;
static uint8_t  g_seq_menu_sel  = 0;
static uint32_t t_edit_down    = 0;
static uint8_t  edit_held_active= 0;
static uint32_t t_sel_down     = 0;
static uint8_t  sel_held_active= 0;
#define SEQ_MENU_COUNT 6u

/* note options menu (short-press EDIT on the roll) */
static uint8_t  g_note_menu_on  = 0;
static uint8_t  g_note_menu_sel = 0;
#define NOTE_MENU_COUNT 4u

/* live-record hold tracker: note-on stamps (pitch, step, ms), note-off
 * computes the hold length into the step's gate (acc) — playback length
 * then matches what was actually played */
#define SEQ_HOLD_SLOTS 4u
static struct { uint8_t pitch, used; uint16_t step; uint32_t t0; } g_seq_hold[SEQ_HOLD_SLOTS];

#define SEQ_ROLL_X0   34                   /* step-area left edge */
#define SEQ_STEP_W    12                   /* per-step column width */
#define SEQ_ROWS      14                   /* visible pitch rows */
#define SEQ_ROW_H     12                   /* row height */

static const char *const NOTE_NAMES[12] = {
    "C","C#","D","D#","E","F","F#","G","G#","A","A#","B"
};

static void seq_note_name(uint8_t pitch, char *buf)
{
    int32_t oct = (int32_t)(pitch / 12u) - 1;
    if (oct < 0) oct = 0;
    if (oct > 9) oct = 9;
    const char *n = NOTE_NAMES[pitch % 12u];
    uint8_t j = 0;
    while (n[j] && j < 2) { buf[j] = n[j]; j++; }
    buf[j++] = (char)('0' + oct);
    buf[j] = '\0';
}

/* REC armed + playing: edits land on the playing step (live overdub);
 * otherwise on the edit cursor. */
/* Between transport start and the first audio advance, seq_idx still holds
 * slen-1 (the 0x7FFFFFFF sentinel makes step 0 fire first). Every main-loop
 * reader must see step 0 there, or the playhead flashes on the right edge. */
static uint16_t seq_ph_idx(const fm106_seq_t *s)
{
    return (s->seq_pos >= 0x7FFFFFFFu) ? 0u : s->seq_idx;
}

static uint16_t seq_ui_edit_idx(void)
{
    fm106_seq_t *s = &g_synth.seq;
    uint16_t len = s->slen ? s->slen : 1u;
    return (g_seq_rec_arm && s->playing) ? (uint16_t)(seq_ph_idx(s) % len)
                                         : (uint16_t)(g_seq_cursor % len);
}

/* ---- MIDI CC layer ----
 * One CC per sound param (out: on panel/menu/knob change; in: controller
 * edits). Seq params get 102-106, transport 107, edit helpers 108-113. */

/* Default MIDI CC assignments (FM106 param index -> default CC) */
static const uint8_t g_param_cc_default[FM106_NPARAM] = {
    87, 88, 82, 80, 81,  1, 79,  9, 13, 12,  /* 0-9: bender dco/vcf, arp rate, lfo
                                                rate/delay, dco lfo/pwm depth, sub
                                                level, noise, hpf mode */
    74, 71, 76, 77, 78, 11, 73, 75, 70, 72,  /* 10-19: cutoff, reso, env mod, lfo
                                                mod, kbd track, vca level, env A/D/S/R */
    0xFF, 0xFF, 0xFF,                        /* 20-22: preset-only gaps */
    91, 92, 94, 0xFF, 93, 0xFF,               /* 23-28: pulse/saw/sub on, (gap),
                                                chorus I -> CC 93, chorus II gap */
    95, 89, 90, 0xFF,                        /* 29-32: dco range, arp mode, arp range */
    96, 97, 98,                              /* 33-35: pwm mode, env polarity, vca mode */
    83, 84, 85, 86,                          /* 36-39: fx type/mix/rate/character */
    119,                                     /* 40: voice assign mode (Mono/Poly I/Poly II) */
};

/* Default Sequencer CC assignments (0: tempo, 1: div, 2: len, 3: gate, 4: swing, 5: transport) */
static const uint8_t g_seq_cc_default[6] = { 102, 103, 104, 105, 106, 107 };

/* Active mutable CC mapping arrays (0xFF = disabled / OFF) */
static uint8_t g_param_cc[FM106_NPARAM];
static uint8_t g_seq_cc[6];

/* Menu CC mapping entry definition */
typedef struct {
    const char *name;
    uint8_t type; /* 0 = sound param, 1 = seq param, 2 = reset, 3 = back */
    uint8_t idx;  /* index in g_param_cc or g_seq_cc */
    uint8_t def_cc;
} fm106_cc_entry_t;

static const fm106_cc_entry_t g_cc_map_entries[] = {
    {"CUTOFF",    0, 10, 74},
    {"RESO",      0, 11, 71},
    {"ENV MOD",   0, 12, 76},
    {"LFO MOD",   0, 13, 77},
    {"KBD TRK",   0, 14, 78},
    {"HPF MODE",  0,  9, 12},
    {"ATTACK",    0, 16, 73},
    {"DECAY",     0, 17, 75},
    {"SUSTAIN",   0, 18, 70},
    {"RELEASE",   0, 19, 72},
    {"DCO LFO",   0,  5,  1},
    {"PWM DEPTH", 0,  6, 79},
    {"PWM MODE",  0, 33, 96},
    {"SUB LEVEL", 0,  7,  9},
    {"SUB ON",    0, 25, 94},
    {"NOISE",     0,  8, 13},
    {"PULSE",     0, 23, 91},
    {"SAW",       0, 24, 92},
    {"DCO RANGE", 0, 29, 95},
    {"VCA LEVEL", 0, 15, 11},
    {"VCA MODE",  0, 35, 98},
    {"ENV POLAR", 0, 34, 97},
    {"LFO RATE",  0,  3, 80},
    {"LFO DELAY", 0,  4, 81},
    {"CHORUS",    0, 27, 93},
    {"BND DCO",   0,  0, 87},
    {"BND VCF",   0,  1, 88},
    {"ARP RATE",  0,  2, 82},
    {"ARP MODE",  0, 30, 89},
    {"ARP RANGE", 0, 31, 90},
    {"TAPE TYPE", 0, 36, 83},
    {"TAPE MIX",  0, 37, 84},
    {"TAPE RATE", 0, 38, 85},
    {"TAPE CHAR", 0, 39, 86},
    {"VOICE MODE",0, 40, 119},
    {"SEQ BPM",   1,  0, 102},
    {"SEQ DIV",   1,  1, 103},
    {"SEQ LEN",   1,  2, 104},
    {"SEQ GATE",  1,  3, 105},
    {"SEQ SWING", 1,  4, 106},
    {"SEQ PLAY",  1,  5, 107},
    {"[RESET]",   2,  0,   0},
    {"[< BACK]",  3,  0,   0},
};
#define CC_MAP_COUNT (sizeof(g_cc_map_entries) / sizeof(g_cc_map_entries[0]))
#define CC_MAP_VISIBLE 6
#define CC_MAP_IDX_RESET (CC_MAP_COUNT - 2u)
#define CC_MAP_IDX_BACK  (CC_MAP_COUNT - 1u)

/* param raw value -> full-range CC out (modes/flags stretched for faders) */
static uint8_t param_cc_value(uint8_t i, uint8_t v)
{
    switch (i) {
    case 9: case 29: case 30: case 31: case 40: return (uint8_t)(v * 42u);  /* small enums */
    case 27: return (uint8_t)(g_synth.chorus.mode * 42u);           /* chorus mode */
    case 36: return (uint8_t)((v * 128u) / (FM106_FX_COUNT ? FM106_FX_COUNT : 1u));
    case 23: case 24: case 25: case 28:
    case 33: case 34: case 35: return v ? 127u : 0u;                /* flags */
    default: return v;
    }
}

/* CC value (0-127) -> param raw value on receive */
static uint8_t cc_to_param_value(uint8_t idx, uint8_t v)
{
    switch (idx) {
    case 9:  return v / 32u > 3u ? 3u : v / 32u;      /* hpf mode zones */
    case 23: case 24: case 25: case 33: case 34: case 35:
        return (uint8_t)(v >= 64u);
    case 29: return v / 42u > 2u ? 2u : v / 42u;       /* dco range 0-2 */
    case 30: return v / 42u > 2u ? 2u : v / 42u;       /* arp mode 0-2 */
    case 31: return v >= 85u ? 3u : v >= 42u ? 2u : 1u; /* arp range 1-3 */
    case 40: return v / 42u > 2u ? 2u : v / 42u;       /* voice mode 0-2 */
    default: return v;
    }
}

/* master clock out: F8/FA/FB/FC only when SYNC=MASTER */
static void seq_clock_out(uint8_t status)
{
    if (g_synth.sync_mode == 1u) midi_send(status, 0, 0);
}

/* REC+PLAY count-in: four clicks at the pattern tempo (quarter notes), then
 * the transport starts on the would-be downbeat. Clicks are engine notes with
 * the current sound; beat 4 is a fifth up so the last count stands out. */
static uint8_t  g_count_in;       /* beats left to click; 0xFF = final gap; 0 = idle */
static uint32_t g_count_next_ms;
static uint8_t  g_click_note;     /* sounding click note; 0 = none */
static uint32_t g_click_off_ms;
static void count_in_start(void);
static void count_in_cancel(void);

/* transport: 0=stop, 1=start, 2=continue, 3=toggle (BTN_PLAY / CC 107 / CC out) */
static void seq_transport_cmd(uint8_t cmd)
{
    fm106_seq_t *s = &g_synth.seq;
    uint8_t was = s->playing;
    count_in_cancel();             /* any transport command aborts a pending count-in */
    if (cmd == 0u || (cmd == 3u && was)) {
        if (was) {
            uint8_t nflush = s->seq_n;
            uint8_t flushed[4];
            for (uint8_t i = 0; i < nflush; i++) flushed[i] = s->seq_notes[i];
            uint8_t och = (g_synth.midi_out_ch - 1u) & 0x0Fu;
            fm1_irq_off();
            for (uint8_t i = 0; i < s->seq_n; i++) fm106_note_off(&g_synth, s->seq_notes[i]);
            s->seq_n = 0;
            s->playing = 0;
            s->msg_tail = s->msg_head;  /* drop queued seq MIDI events */
            fm1_irq_on();
            /* release any externally-sounding seq notes (main loop → midi_send ok) */
            for (uint8_t i = 0; i < nflush; i++) midi_send(0x80u | och, flushed[i], 0u);
            seq_clock_out(0xFCu);
            if (g_seq_cc[5] != 0xFFu) midi_send(0xB0u | och, g_seq_cc[5], 0u);
        }
    } else if (cmd == 2u) {         /* continue: resume in place */
        if (!was) {
            s->playing = 1;
            seq_clock_out(0xFBu);
            if (g_seq_cc[5] != 0xFFu) midi_send(0xB0u | ((g_synth.midi_out_ch - 1u) & 0x0Fu), g_seq_cc[5], 1u);
        }
    } else if (cmd == 1u || (cmd == 3u && !was)) {
        fm1_irq_off();
        fm106_param_snapshot(&g_synth); /* p-lock diff starts from the live sound */
        fm106_seq_start(s);
        fm1_irq_on();
        seq_clock_out(0xFAu);
        if (g_seq_cc[5] != 0xFFu) midi_send(0xB0u | ((g_synth.midi_out_ch - 1u) & 0x0Fu), g_seq_cc[5], 1u);
    }
}

static void count_in_start(void)
{
    g_count_in = 4;
    g_count_next_ms = fm106_now_ms();  /* first click on the next loop pass */
}

static void count_in_cancel(void)
{
    g_count_in = 0;
    if (g_click_note) {
        fm1_irq_off();
        fm106_note_off(&g_synth, g_click_note);
        fm1_irq_on();
        g_click_note = 0;
    }
}

/* called from the main loop: click beats, then hand off to the transport */
static void count_in_tick(void)
{
    if (!g_count_in && !g_click_note) return;
    uint32_t now = fm106_now_ms();
    if (g_click_note && (int32_t)(now - g_click_off_ms) >= 0) {
        fm1_irq_off();
        fm106_note_off(&g_synth, g_click_note);
        fm1_irq_on();
        g_click_note = 0;
    }
    if (!g_count_in || (int32_t)(now - g_count_next_ms) < 0) return;
    if (g_count_in == 0xFFu) {         /* final gap elapsed: the downbeat */
        g_count_in = 0;
        seq_transport_cmd(1u);
        return;
    }
    uint32_t bpm = g_synth.seq.bpm ? g_synth.seq.bpm : 120u;
    uint32_t gap = 60000u / bpm;       /* one click per quarter note */
    uint8_t note = (g_count_in == 1u) ? 88u : 81u;  /* beat 4 a fifth up */
    fm1_irq_off();
    if (g_click_note) fm106_note_off(&g_synth, g_click_note);
    fm106_note_on(&g_synth, note, 100u);
    fm1_irq_on();
    g_click_note = note;
    g_click_off_ms = now + 70u;
    g_count_next_ms = now + gap;
    g_count_in = (g_count_in == 1u) ? 0xFFu : (uint8_t)(g_count_in - 1u);
}

/* SYNC menu/CC -> engine clock-slave state (and clear stale pulse interp) */
static void seq_sync_apply(void)
{
    fm106_seq_t *s = &g_synth.seq;
    uint8_t slave = (uint8_t)(g_synth.sync_mode == 2u);
    fm106_midi_clock_mode(s, slave);
    if (slave) {
        s->mc_pos = s->mc_rendered = 0;
        s->mc_have_pulse = s->mc_tempo_valid = 0;
        s->mc_interval_time = 0;
    }
}

/* ---- Elektron-style p-lock record + CC out ----
 * Called once per main-loop pass: diff every sound param against the
 * snapshot. REC armed + playing locks each changed param into the playing
 * step; CC out reports any change (panel, menu or incoming CC) to a
 * controller; otherwise the cache just tracks the live sound, so arming
 * mid-play only locks what the user touches from then on. */
static void plock_scan(void)
{
    fm106_seq_t *s = &g_synth.seq;
    uint8_t rec = (uint8_t)(g_seq_rec_arm && s->playing);
    uint8_t ch = (uint8_t)(0xB0u | ((g_synth.midi_out_ch - 1u) & 0x0Fu));
    static uint8_t primed = 0;
    static uint8_t seq_cache[5]; /* bpm sdiv slen sgate swing (CC 102-106 out) */
    for (uint8_t i = 0; i < FM106_NPARAM; i++) {
        uint8_t v = fm106_param_get(&g_synth, i);
        if (!primed) { s->plock_cache[i] = v; continue; } /* boot: prime silently, no CC dump */
        if (v == s->plock_cache[i]) continue;
        s->plock_cache[i] = v;
        if (g_param_cc[i] != 0xFFu)
            midi_send(ch, g_param_cc[i], param_cc_value(i, v));
        if (rec) {
            uint16_t idx = (uint16_t)(seq_ph_idx(s) % (s->slen ? s->slen : 1u)); /* fresh per lock */
            fm1_irq_off();
            fm106_plock_set(s, idx, i, v);
            fm1_irq_on();
            g_dirty_screen = 1;
        }
    }
    /* seq track params -> CC 102-106 out (scaled to match CC in) */
    uint8_t now[5];
    now[0] = (uint8_t)((s->bpm > 40u ? s->bpm - 40u : 0u) / 2u);           /* 102 tempo */
    now[1] = s->sdiv;                                                      /* 103 division */
    now[2] = (uint8_t)(s->slen > 128u ? 127u : (s->slen ? s->slen - 1u : 0u)); /* 104 length */
    now[3] = s->sgate;                                                     /* 105 gate */
    now[4] = s->swing;                                                     /* 106 swing */
    for (uint8_t j = 0; j < 5u; j++) {
        if (now[j] != seq_cache[j]) {
            if (primed && g_seq_cc[j] != 0xFFu) midi_send(ch, g_seq_cc[j], now[j]);
            seq_cache[j] = now[j];
        }
    }
    primed = 1;
}

/* toggle a pitch in the cursor step (add if absent, remove if present) */
static void seq_ui_toggle_pitch(uint8_t pitch)
{
    fm1_irq_off();
    fm106_step_t *st = &g_synth.seq.step[g_seq_cursor % (g_synth.seq.slen ? g_synth.seq.slen : 1u)];
    for (uint8_t i = 0; i < st->n; i++) {
        if (st->note[i] == pitch) {
            for (uint8_t j = i; j + 1 < st->n; j++) st->note[j] = st->note[j + 1];
            st->n--;
            g_seq_dirty = 1;
            fm1_irq_on();
            return;
        }
    }
    if (st->n < 4u) {
        st->note[st->n++] = pitch;
        st->time = FM106_ST_NOTE;
        if (!st->vel) st->vel = 100;
        g_seq_last_pitch = pitch;
        /* follow: keep pitch in view */
        if (pitch < g_seq_view_lo) g_seq_view_lo = pitch;
        if (pitch > g_seq_view_lo + SEQ_ROWS - 1u) g_seq_view_lo = pitch - SEQ_ROWS + 1u;
        if (g_seq_view_lo > 114u) g_seq_view_lo = 114u; /* 127-13 */
        g_seq_dirty = 1;
    }
    fm1_irq_on();
}

/* SEL short: remove the note at the cursor cell (step x row); none there = wipe the step */
static void seq_ui_clear_step(void)
{
    fm1_irq_off();
    fm106_step_t *st = &g_synth.seq.step[g_seq_cursor % (g_synth.seq.slen ? g_synth.seq.slen : 1u)];
    uint8_t found = 0;
    for (uint8_t i = 0; i < st->n; i++) {
        if (st->note[i] == g_seq_row) {
            for (uint8_t j = i; j + 1 < st->n; j++) st->note[j] = st->note[j + 1];
            st->n--;
            found = 1;
            break;
        }
    }
    if (!found) {
        st->n = 0; st->time = FM106_ST_NOTE; st->flags = 0;
        st->acc = 0; st->vel = 0; st->probability = 0; st->ratchet = 0;
        fm106_plock_clear_step(&g_synth.seq, (uint16_t)(g_seq_cursor % (g_synth.seq.slen ? g_synth.seq.slen : 1u)));
    }
    g_seq_dirty = 1;
    fm1_irq_on();
}

/* K2: move the pitch-row cursor up/down the scale (the white square's row) */
static void seq_ui_move_row(int32_t d)
{
    int32_t r = (int32_t)g_seq_row + d;
    if (r < 1) r = 1;
    if (r > 127) r = 127;
    g_seq_row = (uint8_t)r;
    /* follow: keep the cursor row in the window */
    if (g_seq_row < g_seq_view_lo) g_seq_view_lo = g_seq_row;
    if (g_seq_row > g_seq_view_lo + SEQ_ROWS - 1u) g_seq_view_lo = g_seq_row - SEQ_ROWS + 1u;
    if (g_seq_view_lo > 114u) g_seq_view_lo = 114u;
    g_seq_dirty = 1;
}

static void seq_ui_adjust_gate(int32_t d)
{
    fm1_irq_off();
    fm106_step_t *st = &g_synth.seq.step[seq_ui_edit_idx()];
    int32_t g = st->acc ? (int32_t)st->acc : (int32_t)g_synth.seq.sgate;
    g += d * 8;
    if (g < 1) g = 1;
    if (g > 255) g = 255;
    st->acc = (uint8_t)(g > 255 ? 255 : g);
    g_seq_dirty = 1;
    fm1_irq_on();
}

/* K4: fill the cursor cell (step x row) with this velocity; 0 removes the note */
static void seq_ui_adjust_vel(int32_t d)
{
    fm1_irq_off();
    fm106_step_t *st = &g_synth.seq.step[seq_ui_edit_idx()];
    int8_t idx = -1;
    for (uint8_t i = 0; i < st->n; i++) if (st->note[i] == g_seq_row) { idx = (int8_t)i; break; }
    if (idx < 0) {           /* empty cell: fill it */
        if (st->n >= 4u) { fm1_irq_on(); return; }
        idx = (int8_t)st->n;
        st->note[st->n++] = g_seq_row;
        st->time = FM106_ST_NOTE;
        st->vel = (uint8_t)(d > 0 ? 64 : 96);
        g_seq_last_pitch = g_seq_row;
    }
    int32_t v = (int32_t)st->vel + d * 4;
    if (v <= 0) {           /* velocity 0: the note leaves the step */
        for (uint8_t j = (uint8_t)idx; j + 1 < st->n; j++) st->note[j] = st->note[j + 1];
        st->n--;
        if (!st->n) { st->time = FM106_ST_NOTE; st->flags = 0; st->acc = 0; st->vel = 0; }
        g_seq_dirty = 1;
        fm1_irq_on();
        return;
    }
    if (v > 127) v = 127;
    st->vel = (uint8_t)v;
    g_seq_dirty = 1;
    fm1_irq_on();
}

static void seq_ui_toggle_flag(uint8_t flag)
{
    fm1_irq_off();
    fm106_step_t *st = &g_synth.seq.step[seq_ui_edit_idx()];
    st->flags ^= flag;
    g_seq_dirty = 1;
    fm1_irq_on();
}

/* note options (short-press EDIT): ratchet / tie / clear on the edited step */
static void seq_ui_note_adjust(int32_t d)
{
    if (!d) return;
    fm1_irq_off();
    fm106_step_t *st = &g_synth.seq.step[seq_ui_edit_idx()];
    switch (g_note_menu_sel) {
    case 0: /* RATCHET 1..8 */
        if (d > 0) { if (st->ratchet < 8u) st->ratchet++; }
        else       { if (st->ratchet > 0u) st->ratchet--; }
        break;
    case 1: /* TIE */
        st->time = (st->time == FM106_ST_TIE) ? FM106_ST_NOTE : FM106_ST_TIE;
        break;
    case 2: /* ACCENT */
        st->flags ^= FM106_SF_ACCENT;
        break;
    case 3: /* SLIDE */
        st->flags ^= FM106_SF_SLIDE;
        break;
    }
    g_seq_dirty = 1;
    fm1_irq_on();
}

static void seq_ui_move_cursor(int32_t d)
{
    fm106_seq_t *s = &g_synth.seq;
    int32_t len = s->slen ? (int32_t)s->slen : 1;
    int32_t c = (int32_t)g_seq_cursor + d;
    while (c < 0) c += len;
    while (c >= len) c -= len;
    g_seq_cursor = (uint16_t)c;
    /* follow cursor into view */
    int32_t vs = (int32_t)g_seq_cursor - 8;
    if (vs < 0) vs = 0;
    if (vs > len - 16) vs = len - 16;
    if (vs < 0) vs = 0;
    g_seq_view_step = (uint16_t)vs;
    /* follow cursor note into pitch view */
    fm106_step_t *st = &s->step[g_seq_cursor % (s->slen ? s->slen : 1u)];
    if (st->n) {
        uint8_t p = st->note[0];
        if (p < g_seq_view_lo) g_seq_view_lo = p;
        if (p > g_seq_view_lo + SEQ_ROWS - 1u) g_seq_view_lo = p - SEQ_ROWS + 1u;
        if (g_seq_view_lo > 114u) g_seq_view_lo = 114u;
    }
    g_seq_dirty = 1;
    g_dirty_screen = 1;
}

/* a key (panel keybed or MIDI note-on) contributes to the sequence: live
 * record into the playing (or cursor) step when REC is armed, acid-style
 * step entry on the SEQ page when stopped */
static void seq_note_entry(uint8_t pitch, uint8_t vel)
{
    fm106_seq_t *s = &g_synth.seq;
    if (g_seq_rec_arm) {
        uint16_t len = s->slen ? s->slen : 1u;
        uint16_t idx;
        fm1_irq_off();
        if (s->playing) {
            if (s->seq_pos >= 0x7FFFFFFFu) { /* pre-first-advance: step 0 */
                idx = 0;
            } else {
                /* a keypress in the last quarter of the step means the upcoming
                 * one — land it there, not in the dying step */
                idx = s->seq_idx;
                uint32_t base = fm106_seq_div_samples(s, s->sdiv);
                uint32_t step_len = fm106_seq_step_samples(s, base, s->seq_idx);
                if (step_len && s->seq_pos > step_len - (step_len >> 2))
                    idx = (uint16_t)((idx + 1u) % len);
            }
        } else {
            idx = (uint16_t)(g_seq_cursor % len);
        }
        fm106_step_t *st = &s->step[idx % len];
        uint8_t found = 0;
        for (uint8_t i = 0; i < st->n; i++) if (st->note[i] == pitch) { found = 1; break; }
        if (!found && st->n < 4u) {
            st->note[st->n++] = pitch;
            st->time = FM106_ST_NOTE;
            if (!st->vel) st->vel = vel ? vel : 100u;
            g_seq_last_pitch = pitch;
            /* arm the hold tracker: remember which step took this pitch so
             * note-off can write the held length into that step's gate.
             * Retrigger of a still-held pitch updates the slot in place. */
            uint8_t slot = SEQ_HOLD_SLOTS;
            for (uint8_t h = 0; h < SEQ_HOLD_SLOTS; h++) {
                if (g_seq_hold[h].used && g_seq_hold[h].pitch == pitch) { slot = h; break; }
                if (slot == SEQ_HOLD_SLOTS && !g_seq_hold[h].used) slot = h;
            }
            if (slot < SEQ_HOLD_SLOTS) {
                g_seq_hold[slot].used  = 1;
                g_seq_hold[slot].pitch = pitch;
                g_seq_hold[slot].step  = idx;
                g_seq_hold[slot].t0    = fm106_now_ms();
            }
        }
        fm1_irq_on();
        if (!s->playing) seq_ui_move_cursor(1);
        g_seq_dirty = 1;
        g_dirty_screen = 1;
    } else if (g_ui_mode == UI_MODE_NORMAL && g_current_page == PAGE_SEQ && !s->playing) {
        seq_ui_toggle_pitch(pitch);
        seq_ui_move_cursor(1);
    }
}

/* note-off while live-recording: convert the held length into the recorded
 * step's gate (acc 1..255 = 0..100% of the step period). Holds longer than
 * one step extend into following EMPTY steps as TIEs (sustain, no retrigger),
 * so playback length matches what was held. The chain stops at the first
 * step that already has notes — we never overwrite existing material. */
static void seq_note_release(uint8_t pitch)
{
    fm106_seq_t *s = &g_synth.seq;
    if (!g_seq_rec_arm) return;
    uint32_t now = fm106_now_ms();
    for (uint8_t h = 0; h < SEQ_HOLD_SLOTS; h++) {
        if (g_seq_hold[h].used && g_seq_hold[h].pitch == pitch) {
            uint32_t held_ms = now - g_seq_hold[h].t0;
            uint16_t idx = g_seq_hold[h].step;
            g_seq_hold[h].used = 0;
            uint16_t len = s->slen ? s->slen : 1u;
            idx %= len;
            uint32_t base = fm106_seq_div_samples(s, s->sdiv);
            uint32_t period_ms = base ? (base * 1000u) / 44100u : 0u; /* base < ~1M samples; *1000 fits u32 */
            fm1_irq_off();
            if (!period_ms || held_ms < period_ms) {
                /* under one step: plain gate fraction on the first step */
                uint8_t acc = 255u;
                if (period_ms) {
                    acc = (uint8_t)((held_ms * 255u) / period_ms);
                    if (!acc) acc = 1u;               /* never a zero-length note */
                }
                s->step[idx].acc = acc;
            } else {
                /* one step or more: full gate, then TIE the held span. Ties
                 * must exist before the playhead reaches those steps — they
                 * land now and sustain from the next loop pass onward. */
                uint32_t full = held_ms / period_ms;  /* whole steps held */
                uint32_t rem   = held_ms % period_ms; /* tail fraction */
                if (full > len) full = len;           /* never longer than the loop */
                s->step[idx].acc = 255u;
                for (uint32_t k = 1u; k <= full; k++) {
                    fm106_step_t *st = &s->step[(idx + (uint16_t)k) % len];
                    if (st->n) break;                 /* occupied: stop, don't clobber */
                    st->time = FM106_ST_TIE;
                    st->acc  = 255u;
                }
                if (rem && full < len) {              /* partial tail: last tie carries it */
                    fm106_step_t *st = &s->step[(idx + (uint16_t)full) % len];
                    if (!st->n) {
                        uint8_t acc = (uint8_t)((rem * 255u) / period_ms);
                        if (acc) st->acc = acc;
                    }
                }
            }
            fm1_irq_on();
            g_seq_dirty = 1;
            g_dirty_screen = 1;
            return;
        }
    }
}

/* MIDI CC 99/100/101: accent / slide / ratchet on the edit step (playing
 * step while REC armed + playing, else the cursor step) */
static void seq_ui_cc_flag(uint8_t which, uint8_t v)
{
    fm106_step_t *st = &g_synth.seq.step[seq_ui_edit_idx()];
    fm1_irq_off();
    if (which == 0u) {          /* accent */
        if (v >= 64u) st->flags |= FM106_SF_ACCENT;
        else          st->flags &= (uint8_t)~FM106_SF_ACCENT;
    } else if (which == 1u) {   /* slide (portamento into the next step) */
        if (v >= 64u) st->flags |= FM106_SF_SLIDE;
        else          st->flags &= (uint8_t)~FM106_SF_SLIDE;
    } else {                    /* ratchet: 0/1 = off, 2..8 = hits in the step */
        st->ratchet = v > 8u ? 8u : v;
    }
    fm1_irq_on();
    g_seq_dirty = 1;
    g_dirty_screen = 1;
}

static const char CHAR_SET[] = " ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789*-_./#+!";
#define CHAR_SET_LEN ((int32_t)(sizeof(CHAR_SET) - 1))

#define MENU_MAIN_COUNT 3
#define MENU_MIDI_COUNT 6

#ifndef RING_PUBLISH
#define RING_PUBLISH() __asm__ volatile("" ::: "memory")
#endif

#define SCOPE_RING_SIZE 1024u
static int16_t g_scope_ring[SCOPE_RING_SIZE];
static volatile uint32_t g_scope_head = 0;

/* Audio ISR entry */
static void app_audio(int32_t *out, uint32_t n)
{
    fm106_audio_render(out, n);

    uint32_t h = g_scope_head;
    for (uint32_t i = 0; i < n; i++) {
        int32_t m = (out[2u * i] + out[2u * i + 1u]) >> 1;
        if (m > 32767) m = 32767;
        if (m < -32768) m = -32768;
        g_scope_ring[h & (SCOPE_RING_SIZE - 1u)] = (int16_t)m;
        h++;
    }
    RING_PUBLISH();
    g_scope_head = h;
}

/* Draw text with background plate into current canvas */
static void cv_text_plate(int32_t x, int32_t y, uint32_t w, const char *s,
                          uint16_t c, uint16_t bg, int align)
{
    int32_t tw = text_w(&FONT_S, s);
    int32_t tx = x;
    if (align == 1)
        tx = x + ((int32_t)w - tw) / 2;
    else if (align == 2)
        tx = x + (int32_t)w - tw;
    cv_rect(x, y, w, 16, bg);
    cv_text(tx, y, &FONT_S, s, c);
}

/* Battery Voltage Monitor & Header Indicator */
static int32_t g_batt_adc = -1;
static uint32_t g_t_last_batt = 0;

static int32_t fm106_get_battery_level(void)
{
    if (g_batt_adc < 0 || (fm1_ms - g_t_last_batt >= 1000u)) {
        g_t_last_batt = fm1_ms;
        int32_t r = fm1_adc_read(FM1_ADC_BATT);
        if (r >= 0) {
            if (g_batt_adc < 0) {
                g_batt_adc = r;
            } else {
                g_batt_adc += (r - g_batt_adc) / 4;
            }
        }
    }
    return g_batt_adc;
}

static void draw_battery_indicator(int32_t x, int32_t y)
{
    int32_t raw = fm106_get_battery_level();
    if (raw < 0) return;

    int bars = 0;
    uint16_t bar_col = C_STRIPE_CYAN;
    uint16_t shell_col = C_TEXT_DIM;

    if (raw >= 600) {
        bars = 3;
        bar_col = C_STRIPE_CYAN;
    } else if (raw >= 550) {
        bars = 2;
        bar_col = C_STRIPE_CYAN;
    } else if (raw >= 500) {
        bars = 1;
        bar_col = C_ROLAND_AMBER;
    } else {
        bars = 1;
        bar_col = C_ROLAND_RED;
        shell_col = C_ROLAND_RED;
    }

    /* Battery outer body (18x9) and positive pip (2x5) */
    cv_rect(x, y, 18, 9, shell_col);
    cv_rect(x + 18, y + 2, 2, 5, shell_col);
    cv_rect(x + 1, y + 1, 16, 7, C_BLACK);

    /* Segments: 3 blocks of 4px each, separated by 1px */
    if (bars >= 1) cv_rect(x + 2, y + 2, 4, 5, bar_col);
    if (bars >= 2) cv_rect(x + 7, y + 2, 4, 5, bar_col);
    if (bars >= 3) cv_rect(x + 12, y + 2, 4, 5, bar_col);
}

/* 1. Header + Voice LEDs + Page Title (y=0..75, single blit) */
static void draw_top_section(uint8_t patch_idx, const char *patch_name)
{
    /* 4 Banks x 64 Presets:
     * Bank 0: Factory A (A11..A88,    0..63)
     * Bank 1: Factory B (B11..B88,   64..127)
     * Bank 2: User A    (UA11..UA88, 128..191)
     * Bank 3: User B    (UB11..UB88, 192..255)
     */
    uint32_t bank_idx = (uint32_t)patch_idx / 64u;
    uint32_t local = (uint32_t)patch_idx % 64u;
    uint32_t bank_num = (local / 8u) + 1u;
    uint32_t patch_num = (local % 8u) + 1u;

    char code[8];
    const char *badge_str;
    uint16_t badge_color;

    if (bank_idx == 0u) {
        code[0] = 'A';
        code[1] = (char)('0' + bank_num);
        code[2] = (char)('0' + patch_num);
        code[3] = '\0';
        badge_str = "[FACT A]";
        badge_color = C_STRIPE_CYAN;
    } else if (bank_idx == 1u) {
        code[0] = 'B';
        code[1] = (char)('0' + bank_num);
        code[2] = (char)('0' + patch_num);
        code[3] = '\0';
        badge_str = "[FACT B]";
        badge_color = C_STRIPE_CYAN;
    } else if (bank_idx == 2u) {
        code[0] = 'U';
        code[1] = 'A';
        code[2] = (char)('0' + bank_num);
        code[3] = (char)('0' + patch_num);
        code[4] = '\0';
        badge_str = "[USER A]";
        badge_color = C_ROLAND_AMBER;
    } else {
        code[0] = 'U';
        code[1] = 'B';
        code[2] = (char)('0' + bank_num);
        code[3] = (char)('0' + patch_num);
        code[4] = '\0';
        badge_str = "[USER B]";
        badge_color = C_ROLAND_AMBER;
    }

    /* Patch Name Banner: Code + Preset Name */
    char title_buf[32];
    uint32_t ti = 0;
    while (code[ti] && ti < 5u) { title_buf[ti] = code[ti]; ti++; }
    title_buf[ti++] = ' ';
    title_buf[ti++] = ' ';
    uint32_t pi = 0;
    if (patch_name && patch_name[0]) {
        while (patch_name[pi] && ti < sizeof(title_buf) - 1u) {
            title_buf[ti++] = patch_name[pi++];
        }
    } else {
        const char *bl = "<Blank>";
        while (bl[pi] && ti < sizeof(title_buf) - 1u) {
            title_buf[ti++] = bl[pi++];
        }
    }
    title_buf[ti] = '\0';

    /* Voice active bitmask */
    uint32_t voice_active = 0;
    for (uint32_t v = 0; v < FM106_NUM_VOICES; v++) {
        int active = (g_synth.voice[v].adsr.stage != FM106_ENV_IDLE) || g_synth.voice[v].gate;
        if (active) voice_active |= 1u << v;
    }

    const char *title;
    switch (g_current_page) {
    case PAGE_VCF:      title = "VCF";        break;
    case PAGE_VCF2:     title = "VCF 2";      break;
    case PAGE_DCO:      title = "DCO";        break;
    case PAGE_DCO2:     title = "DCO 2";      break;
    case PAGE_ENVELOPE: title = "ENV (ADSR)"; break;
    case PAGE_LFO:      title = "LFO";        break;
    case PAGE_ARP:      title = "ARPEGGIATOR"; break;
    case PAGE_FX:      title = "FX (CHORUS)"; break;
    case PAGE_TAPE:    title = "FX (TAPE)";        break;
    case PAGE_SEQ:     title = "SEQUENCER"; break;
    default:            title = "FX (TAPE)";        break;
    }

    cv_begin(240, 76, C_BG);

    /* --- Header (y=0..43, 1980s Roland badge directly on chassis grey) --- */
    cv_rect(6, 4, 4, 12, C_ROLAND_RED);
    cv_text(16, 2, &FONT_S, "FM-106", C_STRIPE_CYAN);
    cv_text(138, 2, &FONT_S, badge_str, badge_color);
    draw_battery_indicator(210, 5);
    cv_rect(6, 18, 228, 2, C_STRIPE_CYAN);

    /* Patch Display: Roland recessed bezel with red 7-seg code + white preset name */
    const char *disp_name = patch_name;
    uint16_t name_color = C_TEXT_WHITE;
    if (g_banner_until > fm1_ms && g_banner_msg[0] != '\0') {
        disp_name = g_banner_msg;
        name_color = C_ROLAND_AMBER;
    } else if (patch_name[0] == '\0') {
        disp_name = "<EMPTY>";
        name_color = C_TEXT_DIM;
    }

    cv_rect(6, 22, 228, 18, C_BLACK);
    cv_rect(6, 22, 228, 1, C_LINE_DIV);
    cv_rect(6, 39, 228, 1, C_LINE_DIV);
    cv_text(12, 23, &FONT_S, code, C_LED_ON);
    cv_text(52, 23, &FONT_S, disp_name, name_color);

    /* --- Voice LEDs (y=44..57) --- */
    cv_text(8, 44, &FONT_S, "VOICES", C_TEXT_DIM);
    for (uint32_t v = 0; v < FM106_NUM_VOICES; v++) {
        int active = (voice_active >> v) & 1u;
        uint32_t x = 90u + v * 24u;
        cv_rect((int32_t)x, 46, 18, 9, C_FADER_BG);
        cv_rect((int32_t)x + 2, 48, 14, 5, active ? C_LED_ON : C_LED_OFF);
    }

    /* --- Page Title (y=58..75) --- */
    cv_rect(6, 58, 228, 1, C_LINE_DIV);
    int32_t tit_w = text_w(&FONT_S, title);
    cv_text((240 - tit_w) / 2, 60, &FONT_S, title, C_ROLAND_AMBER);
    cv_rect(6, 74, 228, 1, C_LINE_DIV);

    cv_blit(0, 0);
}

/* 2. Faders Section (y=76..149, canvas 240x74) */
static void draw_faders_section(uint8_t page)
{
    cv_begin(240, 74, C_BG);

    /* --- Fader tracks & mode switches --- */
    for (int i = 0; i < 4; i++) {
        const fm1_slot_t *d = &FM1_SLOTS[page][i];
        int32_t fx = 12 + i * 58;
        int32_t val = fm1_slot_val(page, i);

        if (d->kind == SLOT_EMPTY)
            continue;

        if (d->kind == SLOT_MODE) {
            int32_t sw = 7, gap = 3;
            int32_t total = (int32_t)d->nseg * sw + (int32_t)(d->nseg - 1) * gap;
            int32_t sx = fx + 22 - total / 2;
            for (uint32_t s = 0; s < d->nseg; s++) {
                cv_rect(sx, 22, sw, 14, (int32_t)s == val ? C_ROLAND_AMBER : C_FADER_BG);
                if ((int32_t)s != val) cv_rect(sx + 1, 23, sw - 2, 12, C_FADER_LINE);
                sx += sw + gap;
            }
        } else {
            cv_rect(fx + 19, 16, 6, 36, C_FADER_BG);
            cv_line(fx + 21, 17, fx + 21, 50, C_FADER_LINE);

            int32_t travel = 28;
            int32_t cap_y = 16 + (travel - (val * travel) / 127);
            if (cap_y < 16) cap_y = 16;
            if (cap_y > 44) cap_y = 44;

            cv_rect(fx + 13, cap_y, 18, 8, C_FADER_CAP);
            cv_line(fx + 13, cap_y + 4, fx + 30, cap_y + 4, C_TEXT_WHITE);
        }
    }

    /* Fader labels and values */
    for (int i = 0; i < 4; i++) {
        const fm1_slot_t *d = &FM1_SLOTS[page][i];
        int32_t fx = 12 + i * 58;
        int32_t val = fm1_slot_val(page, i);

        if (d->kind == SLOT_EMPTY)
            continue;

        int32_t lw = text_w(&FONT_S, d->lbl);
        cv_text(fx + (44 - lw) / 2, 1, &FONT_S, d->lbl, C_TEXT_DIM);

        char num_str[8];
        uint16_t num_col = C_TEXT_WHITE;
        if (d->kind == SLOT_MODE) {
            const char *nm = d->names[val];
            uint32_t j = 0;
            while (nm[j] && j < sizeof(num_str) - 1) { num_str[j] = nm[j]; j++; }
            num_str[j] = '\0';
            num_col = C_ROLAND_AMBER;
        } else {
            int valn = val;
            if (valn >= 100) {
                num_str[0] = (char)('0' + valn / 100);
                num_str[1] = (char)('0' + (valn / 10) % 10);
                num_str[2] = (char)('0' + valn % 10);
                num_str[3] = '\0';
            } else if (valn >= 10) {
                num_str[0] = (char)('0' + valn / 10);
                num_str[1] = (char)('0' + valn % 10);
                num_str[2] = '\0';
            } else {
                num_str[0] = (char)('0' + valn);
                num_str[1] = '\0';
            }
        }
        int32_t nw = text_w(&FONT_S, num_str);
        cv_text(fx + (44 - nw) / 2, 56, &FONT_S, num_str, num_col);
    }

    /* Divider separating faders from oscilloscope */
    cv_rect(6, 73, 228, 1, C_LINE_DIV);

    cv_blit(0, 76);
}

/* 3. Oscilloscope Waveform Display (y=150..239, canvas 240x90) */
static void draw_waveform_section(void)
{
    cv_begin(240, 90, C_BG);

    /* Oscilloscope screen frame */
    cv_rect(6, 2, 228, 86, C_BLACK);
    cv_rect(6, 2, 228, 1, C_LINE_DIV);
    cv_rect(6, 87, 228, 1, C_LINE_DIV);
    cv_rect(6, 2, 1, 86, C_LINE_DIV);
    cv_rect(233, 2, 1, 86, C_LINE_DIV);

    /* Subtle CRT reticle grid */
    for (int32_t x = 12; x < 230; x += 8) {
        cv_line(x, 45, x + 3, 45, RGB(30, 42, 50)); /* center 0V line */
        cv_line(x, 23, x + 2, 23, RGB(18, 24, 30)); /* upper +50% line */
        cv_line(x, 67, x + 2, 67, RGB(18, 24, 30)); /* lower -50% line */
    }
    for (int32_t y = 8; y < 82; y += 8) {
        cv_line(60, y, 60, y + 2, RGB(18, 24, 30));
        cv_line(120, y, 120, y + 2, RGB(18, 24, 30));
        cv_line(180, y, 180, y + 2, RGB(18, 24, 30));
    }

    /* Scope watermark label */
    cv_text_plate(10, 4, 32, "OSC", RGB(0, 90, 125), C_BLACK, 0);

    /* Read audio ring buffer */
    uint32_t head = g_scope_head;
    uint32_t search_end = head >= 225u ? head - 225u : 0u;
    uint32_t search_start = head >= 700u ? head - 700u : 0u;
    uint32_t trig = search_end;

    /* Search for rising zero-crossing with hysteresis for waveform stabilization */
    for (uint32_t s = search_start; s < search_end; s++) {
        int16_t s0 = g_scope_ring[s & (SCOPE_RING_SIZE - 1u)];
        int16_t s1 = g_scope_ring[(s + 1u) & (SCOPE_RING_SIZE - 1u)];
        if (s0 <= 0 && s1 > 64) {
            trig = s;
            break;
        }
    }

    /* Render waveform trace in Roland cyan */
    int32_t prev_y = 45;
    for (int32_t x = 8; x <= 231; x++) {
        int16_t s = g_scope_ring[(trig + (uint32_t)(x - 8)) & (SCOPE_RING_SIZE - 1u)];
        /* Map +-32768 to +-38 pixels around y=45 */
        int32_t y = 45 - ((int32_t)s * 38) / 32768;
        if (y < 4) y = 4;
        if (y > 85) y = 85;
        if (x == 8) {
            cv_pset(x, y, C_STRIPE_CYAN);
        } else {
            cv_line(x - 1, prev_y, x, y, C_STRIPE_CYAN);
        }
        prev_y = y;
    }

    cv_blit(0, 150);
}

/* ---- SEQ options menu (long-press EDIT) ---- */
static void seq_ui_menu_adjust(int32_t d)
{
    if (!d) return;
    fm1_irq_off();
    fm106_seq_t *s = &g_synth.seq;
    switch (g_seq_menu_sel) {
    case 0: /* STEPS */
        if (d > 0) { if (s->slen < FM106_NSTEP) s->slen = (uint16_t)(s->slen + 1u); }
        else       { if (s->slen > 1u) s->slen = (uint16_t)(s->slen - 1u); }
        if (g_seq_cursor >= s->slen) g_seq_cursor = (uint16_t)(s->slen - 1u);
        break;
    case 1: /* BPM */
        if (d > 0) { if (s->bpm < 240u) s->bpm++; }
        else       { if (s->bpm > 40u)  s->bpm--; }
        break;
    case 2: /* DIV */
        if (d > 0) { if (s->sdiv < 9u) s->sdiv++; else s->sdiv = 0u; }
        else       { if (s->sdiv > 0u) s->sdiv--; else s->sdiv = 9u; }
        break;
    case 3: /* GATE */
        if (d > 0) { if (s->sgate < 127u) s->sgate = (uint8_t)(s->sgate + 4u > 127u ? 127u : s->sgate + 4u); }
        else       { if (s->sgate > 8u)   s->sgate = (uint8_t)(s->sgate - 4u < 8u ? 8u : s->sgate - 4u); }
        break;
    case 4: /* SWING */
        if (d > 0) { if (s->swing < 100u) s->swing = (uint8_t)(s->swing + 4u > 100u ? 100u : s->swing + 4u); }
        else       { if (s->swing >= 4u) s->swing = (uint8_t)(s->swing - 4u); else s->swing = 0u; }
        break;
    case 5: /* REC arm */
        g_seq_rec_arm ^= 1u;
        break;
    }
    g_seq_dirty = 1;
    fm1_irq_on();
}

/* value text for SEQ menu row i */
static void seq_menu_value(uint32_t i, char *val)
{
    fm106_seq_t *s = &g_synth.seq;
    switch (i) {
    case 0: {                                   /* STEPS (1..1024, 3 digits) */
        uint32_t v = s->slen;
        val[0] = (char)('0' + v / 100);
        val[1] = (char)('0' + (v / 10) % 10);
        val[2] = (char)('0' + v % 10);
        val[3] = '\0';
        break;
    }
    case 1: {                                   /* BPM */
        uint32_t v = s->bpm;
        val[0] = (char)('0' + v / 100);
        val[1] = (char)('0' + (v / 10) % 10);
        val[2] = (char)('0' + v % 10);
        val[3] = '\0';
        break;
    }
    case 2: {                                   /* DIV */
        uint32_t j = 0;
        while (N_SDIV[s->sdiv][j] && j < 7u) { val[j] = N_SDIV[s->sdiv][j]; j++; }
        val[j] = '\0';
        break;
    }
    case 3: {                                   /* GATE */
        uint32_t v = s->sgate;
        val[0] = (char)('0' + v / 100);
        val[1] = (char)('0' + (v / 10) % 10);
        val[2] = (char)('0' + v % 10);
        val[3] = '\0';
        break;
    }
    case 4: {                                   /* SWING */
        uint32_t v = s->swing;
        val[0] = (char)('0' + v / 100);
        val[1] = (char)('0' + (v / 10) % 10);
        val[2] = (char)('0' + v % 10);
        val[3] = '\0';
        break;
    }
    case 5: {                                   /* REC */
        const char *v = g_seq_rec_arm ? "ON" : "OFF";
        uint32_t j = 0;
        while (v[j]) { val[j] = v[j]; j++; }
        val[j] = '\0';
        break;
    }
    }
}

/* one glo-menu-style row: "> LABEL" left, value right, amber when selected */
static void menu_row_draw(uint32_t x, uint32_t y, uint32_t sel, const char *label, const char *val)
{
    char buf[24];
    buf[0] = sel ? '>' : ' ';
    buf[1] = ' ';
    uint32_t j = 2;
    while (label[j-2] && j < sizeof(buf) - 1u) { buf[j] = label[j-2]; j++; }
    buf[j] = '\0';
    uint16_t c = sel ? C_ROLAND_AMBER : C_TEXT_DIM;
    cv_text_plate(x, y, 140, buf, c, C_BLACK, 0);
    cv_text_plate(x + 150, y, 80, val, c, C_BLACK, 1);
}

static void draw_seq_menu(void)
{
    static const char *const opts[SEQ_MENU_COUNT] = {"STEPS", "BPM", "DIV", "GATE", "SWING", "REC"};

    cv_begin(240, 96, C_BG);
    cv_text_plate(0, 0, 240, "SEQ OPTIONS", C_ROLAND_AMBER, C_BLACK, 1);
    for (uint32_t i = 0; i < 4u; i++) {
        char val[8];
        seq_menu_value(i, val);
        menu_row_draw(10, 20u + i * 20u, i == g_seq_menu_sel, opts[i], val);
    }
    cv_blit(0, 22);
    cv_begin(240, 68, C_BG);
    for (uint32_t i = 4u; i < SEQ_MENU_COUNT; i++) {
        char val[8];
        seq_menu_value(i, val);
        menu_row_draw(10, (i - 4u) * 20u, i == g_seq_menu_sel, opts[i], val);
    }
    cv_blit(0, 118);
}

/* ---- note options menu (short-press EDIT): glo-menu style ---- */
static void draw_note_menu(void)
{
    static const char *const opts[NOTE_MENU_COUNT] = {"RATCHET", "TIE", "ACCENT", "SLIDE"};
    fm106_step_t *st = &g_synth.seq.step[seq_ui_edit_idx()];

    cv_begin(240, 96, C_BG);
    cv_text_plate(0, 0, 240, "NOTE OPTIONS", C_ROLAND_AMBER, C_BLACK, 1);
    for (uint32_t i = 0; i < 4u; i++) {
        char val[8];
        switch (i) {
        case 0: {
            uint32_t v = st->ratchet ? st->ratchet : 1u;
            val[0] = (char)('0' + v); val[1] = '\0';
            break;
        }
        case 1: {
            uint8_t tie = st->time == FM106_ST_TIE;
            val[0] = tie ? 'O' : '-'; val[1] = tie ? 'N' : 'F'; val[2] = '\0';
            break;
        }
        case 2: {
            uint8_t on = st->flags & FM106_SF_ACCENT;
            val[0] = on ? 'O' : '-'; val[1] = on ? 'N' : 'F'; val[2] = '\0';
            break;
        }
        default: {
            uint8_t on = st->flags & FM106_SF_SLIDE;
            val[0] = on ? 'O' : '-'; val[1] = on ? 'N' : 'F'; val[2] = '\0';
            break;
        }
        }
        menu_row_draw(10, 20u + i * 20u, i == g_note_menu_sel, opts[i], val);
    }
    cv_blit(0, 22);
}

/* fader-style knob guide for the SEQ page bottom (matches draw_faders_section) */
static void draw_seq_fader_guide(void)
{
    fm106_seq_t *s = &g_synth.seq;
    fm106_step_t *st = &s->step[g_seq_cursor % (s->slen ? s->slen : 1u)];
    static const char *const lbl[4] = {"STEP", "NOTE", "GATE", "VEL"};
    int32_t val[4];
    int32_t max[4];

    val[0] = (int32_t)g_seq_cursor;                 max[0] = FM106_NSTEP - 1; /* 1023 */
    val[1] = (int32_t)g_seq_row;                    max[1] = 127;
    val[2] = st->acc ? (int32_t)st->acc : (int32_t)s->sgate;           max[2] = 255;
    val[3] = (int32_t)st->vel;                      max[3] = 127;

    cv_begin(240, 50, C_BG);
    for (int i = 0; i < 4; i++) {
        int32_t fx = 12 + i * 58;
        int32_t v = val[i];
        if (v < 0) v = 0;
        if (v > max[i]) v = max[i];

        /* fader track + center line (same geometry as draw_faders_section) */
        cv_rect(fx + 19, 10, 6, 20, C_FADER_BG);
        cv_line(fx + 21, 11, fx + 21, 29, C_FADER_LINE);

        int32_t travel = 14;
        int32_t cap_y = 10 + (travel - (v * travel) / (max[i] ? max[i] : 1));
        if (cap_y < 10) cap_y = 10;
        if (cap_y > 24) cap_y = 24;
        cv_rect(fx + 13, cap_y, 18, 8, C_FADER_CAP);
        cv_line(fx + 13, cap_y + 4, fx + 30, cap_y + 4, C_TEXT_WHITE);

        int32_t lw = text_w(&FONT_S, lbl[i]);
        cv_text(fx + (44 - lw) / 2, 0, &FONT_S, lbl[i], C_TEXT_DIM);

        char num_str[8];
        if (i == 1) {
            seq_note_name((uint8_t)v, num_str);
        } else {
            if (v >= 100) {
                num_str[0] = (char)('0' + v / 100);
                num_str[1] = (char)('0' + (v / 10) % 10);
                num_str[2] = (char)('0' + v % 10);
                num_str[3] = '\0';
            } else if (v >= 10) {
                num_str[0] = (char)('0' + v / 10);
                num_str[1] = (char)('0' + v % 10);
                num_str[2] = '\0';
            } else {
                num_str[0] = (char)('0' + v);
                num_str[1] = '\0';
            }
        }
        int32_t nw = text_w(&FONT_S, num_str);
        cv_text(fx + (44 - nw) / 2, 38, &FONT_S, num_str, C_TEXT_WHITE);
    }
    if (g_seq_rec_arm) cv_text(200, 38, &FONT_S, "REC", C_ROLAND_RED);
    cv_blit(0, 190);
}

/* ---- SEQ piano-roll page (Melodee single-track port) ---- */
static void draw_seq_roll_band(uint8_t row_lo, uint8_t row_hi, uint32_t y, int32_t play_idx)
{
    uint8_t rows = row_hi - row_lo;
    cv_begin(240, rows * SEQ_ROW_H, C_BG);

    for (uint8_t r = row_lo; r < row_hi; r++) {
        uint8_t pitch = g_seq_view_lo + SEQ_ROWS - 1u - r;
        int32_t yb = (int32_t)((r - row_lo) * SEQ_ROW_H);

        if (r & 1u) cv_rect(0, yb, 240, SEQ_ROW_H, C_BLACK);

        char nb[4];
        seq_note_name(pitch, nb);
        cv_text(2, yb + 1, &FONT_S, nb, C_TEXT_DIM);

        for (uint8_t c = 0; c < 16; c++) {
            uint16_t step_idx = g_seq_view_step + c;
            if (step_idx >= g_synth.seq.slen) break;
            fm106_step_t *st = &g_synth.seq.step[step_idx];
            int32_t x = SEQ_ROLL_X0 + (int32_t)c * SEQ_STEP_W;

            if ((step_idx & 3u) == 0) cv_rect(x, yb, 1, SEQ_ROW_H, C_LINE_DIV);

            uint8_t has = 0;
            for (uint8_t i = 0; i < st->n; i++) if (st->note[i] == pitch) { has = 1; break; }
            if (has) {
                if (st->time == FM106_ST_TIE) {
                    cv_rect(x + 1, yb + SEQ_ROW_H / 2 - 1, SEQ_STEP_W - 2, 3, C_TEXT_DIM);
                } else {
                    uint16_t bc = (st->flags & FM106_SF_ACCENT) ? C_TEXT_WHITE : C_ROLAND_AMBER;
                    cv_rect(x + 1, yb + 1, SEQ_STEP_W - 2, SEQ_ROW_H - 2, bc);
                }
            }

            if (play_idx >= 0 && step_idx == (uint16_t)play_idx) {
                cv_rect(x, yb, SEQ_STEP_W, 1, C_STRIPE_CYAN);
                cv_rect(x, yb + SEQ_ROW_H - 1, SEQ_STEP_W, 1, C_STRIPE_CYAN);
            }

            if (step_idx == g_seq_cursor) {
                cv_rect(x, yb, SEQ_STEP_W, 1, C_ROLAND_RED);
                cv_rect(x, yb + SEQ_ROW_H - 1, SEQ_STEP_W, 1, C_ROLAND_RED);
                cv_rect(x, yb, 1, SEQ_ROW_H, C_ROLAND_RED);
                cv_rect(x + SEQ_STEP_W - 1, yb, 1, SEQ_ROW_H, C_ROLAND_RED);
                if (pitch == g_seq_row) {   /* white square: the K1 x K2 cursor cell */
                    cv_rect(x + 1, yb + 1, SEQ_STEP_W - 2, 1, C_TEXT_WHITE);
                    cv_rect(x + 1, yb + SEQ_ROW_H - 2, SEQ_STEP_W - 2, 1, C_TEXT_WHITE);
                    cv_rect(x + 1, yb + 1, 1, SEQ_ROW_H - 2, C_TEXT_WHITE);
                    cv_rect(x + SEQ_STEP_W - 2, yb + 1, 1, SEQ_ROW_H - 2, C_TEXT_WHITE);
                }
            }
        }
    }

    cv_blit(0, y);
}

static void draw_seq_page(void)
{
    fm106_seq_t *s = &g_synth.seq;

    /* header */
    cv_begin(240, 22, C_BG);
    cv_text(6, 4, &FONT_S, "SEQ", C_ROLAND_AMBER);
    if (s->playing) cv_text(60, 4, &FONT_S, "PLAY", C_STRIPE_CYAN);
    else           cv_text(60, 4, &FONT_S, "STOP", C_TEXT_DIM);
    char buf[16];
    buf[0] = (char)('0' + s->bpm / 100);
    buf[1] = (char)('0' + (s->bpm / 10) % 10);
    buf[2] = (char)('0' + s->bpm % 10);
    buf[3] = '\0';
    cv_text(110, 4, &FONT_S, buf, C_TEXT_WHITE);
    cv_text(140, 4, &FONT_S, "BPM", C_TEXT_DIM);
    buf[0] = (char)('0' + s->swing / 100);
    buf[1] = (char)('0' + (s->swing / 10) % 10);
    buf[2] = (char)('0' + s->swing % 10);
    buf[3] = '\0';
    cv_text(180, 4, &FONT_S, buf, C_TEXT_WHITE);
    cv_text(210, 4, &FONT_S, "SWG", C_TEXT_DIM);
    cv_blit(0, 0);

    /* roll bands: one playhead snapshot so both halves always agree */
    int32_t ph = s->playing ? (int32_t)seq_ph_idx(s) : -1;
    draw_seq_roll_band(0, 8, 22, ph);
    draw_seq_roll_band(8, 14, 118, ph);

    /* bottom: fader-style knob guide */
    draw_seq_fader_guide();
}

/* 4. OPT Menu (y=76..171, single blit) */
static void draw_menu_main(void)
{
    const char *items[MENU_MAIN_COUNT] = {"MIDI SETUP", "CC MAPPING", "ABOUT"};
    cv_begin(240, 96, C_BG);
    cv_text_plate(0, 0, 240, "OPTIONS", C_ROLAND_AMBER, C_BLACK, 1);
    for (uint32_t i = 0; i < MENU_MAIN_COUNT; i++) {
        uint16_t c = (i == g_menu_sel) ? C_ROLAND_AMBER : C_TEXT_DIM;
        char buf[24];
        buf[0] = (i == g_menu_sel) ? '>' : ' ';
        buf[1] = ' ';
        uint32_t j = 2;
        while (items[i][j-2] && j < sizeof(buf)-1) { buf[j] = items[i][j-2]; j++; }
        buf[j] = '\0';
        cv_text_plate(20, 22 + i*22, 200, buf, c, C_BLACK, 0);
    }
    cv_blit(0, 76);
    cv_begin(240, 68, C_BG);
    cv_blit(0, 172);
}

static void draw_menu_about(void)
{
    /* Canvas 1 (y=76..171, height 96): Header + System Info */
    cv_begin(240, 96, C_BG);
    cv_text_plate(0, 0, 240, "ABOUT FM-106", C_ROLAND_AMBER, C_BLACK, 1);
    cv_text_plate(20, 24, 200, "FIRMWARE:  Beta 5", C_TEXT_WHITE, C_BLACK, 0);
    cv_text_plate(20, 46, 200, "SYNTH:     6-Voice Polyphonic", C_TEXT_DIM, C_BLACK, 0);
    cv_text_plate(20, 68, 200, "DSP ARCH:  Dual-Core 240MHz", C_TEXT_DIM, C_BLACK, 0);
    cv_blit(0, 76);

    /* Canvas 2 (y=172..239, height 68): Audio + Back prompt */
    cv_begin(240, 68, C_BG);
    cv_text_plate(20, 4, 200, "AUDIO:     44.1kHz 16-bit USB", C_TEXT_DIM, C_BLACK, 0);
    cv_text_plate(20, 24, 200, "BUILD:     2026-10", C_TEXT_DIM, C_BLACK, 0);
    cv_rect(6, 44, 228, 1, C_LINE_DIV);
    cv_text_plate(0, 48, 240, "[SEL] / [HOME] Back", C_TEXT_DIM, C_BLACK, 1);
    cv_blit(0, 172);
}

static void draw_menu_midi(void)
{
    char in_str[8], out_str[8], sync_str[8], yn1[8], yn2[8];
    uint32_t ch = g_synth.midi_channel;
    if (ch == 0u) { in_str[0]='A'; in_str[1]='L'; in_str[2]='L'; in_str[3]='\0'; } /* 0 = omni */
    else if (ch > 9) { in_str[0] = (char)('0' + ch/10); in_str[1] = (char)('0' + ch%10); in_str[2] = '\0'; }
    else { in_str[0] = (char)('0' + ch); in_str[1] = '\0'; }
    uint32_t oc = g_synth.midi_out_ch;
    if (oc > 9) { out_str[0] = (char)('0' + oc/10); out_str[1] = (char)('0' + oc%10); out_str[2] = '\0'; }
    else { out_str[0] = (char)('0' + oc); out_str[1] = '\0'; }
    const char *sync_names[3] = {"OFF", "MASTER", "SLAVE"};
    uint32_t s = 0;
    while (sync_names[g_synth.sync_mode][s] && s < 7) { sync_str[s] = sync_names[g_synth.sync_mode][s]; s++; }
    sync_str[s] = '\0';
    const char *yes = "ON", *no = "OFF";
    uint32_t j = 0; while (yes[j] && j < 7) { yn1[j] = yes[j]; j++; } yn1[j] = '\0';
    j = 0; while (no[j] && j < 7) { yn2[j] = no[j]; j++; } yn2[j] = '\0';

    /* rows 0-2 in the 96px band, rows 3-5 in the 68px band below */
    const char *labels[6] = {"MIDI IN", "MIDI OUT", "SYNC", "LOCAL", "PC IN", "CC MAP"};
    const char *vals[6]   = {in_str, out_str, sync_str,
                             g_synth.midi_local ? yes : no,
                             g_synth.pc_in ? yes : no,
                             (g_midi_sel == 5) ? "[SEL] OPEN" : "EDIT >"};
    cv_begin(240, 96, C_BG);
    cv_text_plate(0, 0, 240, "MIDI SETUP", C_ROLAND_AMBER, C_BLACK, 1);
    for (uint32_t i = 0; i < 3u; i++) {
        uint16_t c = (g_midi_sel == i) ? C_ROLAND_AMBER : C_TEXT_DIM;
        cv_text_plate(20, 24 + i*24, 90, labels[i], C_TEXT_DIM, C_BLACK, 0);
        cv_text_plate(140, 24 + i*24, 80, vals[i], c, C_BLACK, 1);
    }
    cv_blit(0, 76);
    cv_begin(240, 68, C_BG);
    for (uint32_t i = 3u; i < 6u; i++) {
        uint16_t c = (g_midi_sel == i) ? C_ROLAND_AMBER : C_TEXT_DIM;
        cv_text_plate(20, 4 + (i-3u)*20, 90, labels[i], C_TEXT_DIM, C_BLACK, 0);
        cv_text_plate(140, 4 + (i-3u)*20, 80, vals[i], c, C_BLACK, 1);
    }
    cv_blit(0, 172);
}

/* 5. MIDI CC Mapping Sub-Menu */
static void draw_menu_cc_map(void)
{
    char title[24];
    uint32_t cur = (uint32_t)g_cc_map_sel + 1u;
    uint32_t tot = (uint32_t)CC_MAP_COUNT;
    uint32_t ti = 0;
    const char *pfx = "CC MAP (";
    while (pfx[ti]) { title[ti] = pfx[ti]; ti++; }
    if (cur >= 10) title[ti++] = (char)('0' + cur / 10);
    title[ti++] = (char)('0' + cur % 10);
    title[ti++] = '/';
    if (tot >= 10) title[ti++] = (char)('0' + tot / 10);
    title[ti++] = (char)('0' + tot % 10);
    title[ti++] = ')';
    title[ti] = '\0';

    /* Canvas 1 (y=76..171, height 96): Header + 4 visible rows */
    cv_begin(240, 96, C_BG);
    cv_text_plate(0, 0, 240, title, C_ROLAND_AMBER, C_BLACK, 1);

    for (uint32_t v = 0; v < 4u; v++) {
        uint32_t idx = g_cc_map_scroll + v;
        if (idx >= CC_MAP_COUNT) break;
        const fm106_cc_entry_t *e = &g_cc_map_entries[idx];
        char val_buf[12];
        if (e->type == 0) {
            uint8_t c = g_param_cc[e->idx];
            if (c == 0xFFu) {
                val_buf[0] = 'O'; val_buf[1] = 'F'; val_buf[2] = 'F'; val_buf[3] = '\0';
            } else {
                val_buf[0] = 'C'; val_buf[1] = 'C'; val_buf[2] = ' ';
                uint32_t vi = 3;
                if (c >= 100) { val_buf[vi++] = (char)('0' + c / 100); val_buf[vi++] = (char)('0' + (c / 10) % 10); }
                else if (c >= 10) { val_buf[vi++] = (char)('0' + c / 10); }
                val_buf[vi++] = (char)('0' + c % 10);
                val_buf[vi] = '\0';
            }
        } else if (e->type == 1) {
            uint8_t c = g_seq_cc[e->idx];
            if (c == 0xFFu) {
                val_buf[0] = 'O'; val_buf[1] = 'F'; val_buf[2] = 'F'; val_buf[3] = '\0';
            } else {
                val_buf[0] = 'C'; val_buf[1] = 'C'; val_buf[2] = ' ';
                uint32_t vi = 3;
                if (c >= 100) { val_buf[vi++] = (char)('0' + c / 100); val_buf[vi++] = (char)('0' + (c / 10) % 10); }
                else if (c >= 10) { val_buf[vi++] = (char)('0' + c / 10); }
                val_buf[vi++] = (char)('0' + c % 10);
                val_buf[vi] = '\0';
            }
        } else if (e->type == 2) {
            val_buf[0] = 'D'; val_buf[1] = 'E'; val_buf[2] = 'F'; val_buf[3] = 'A'; val_buf[4] = 'U'; val_buf[5] = 'L'; val_buf[6] = 'T'; val_buf[7] = '\0';
        } else {
            val_buf[0] = 'E'; val_buf[1] = 'X'; val_buf[2] = 'I'; val_buf[3] = 'T'; val_buf[4] = '\0';
        }
        menu_row_draw(10, 22u + v * 18u, idx == g_cc_map_sel, e->name, val_buf);
    }
    cv_blit(0, 76);

    /* Canvas 2 (y=172..239, height 68): 2 visible rows + Guide */
    cv_begin(240, 68, C_BG);
    for (uint32_t v = 4u; v < 6u; v++) {
        uint32_t idx = g_cc_map_scroll + v;
        if (idx >= CC_MAP_COUNT) break;
        const fm106_cc_entry_t *e = &g_cc_map_entries[idx];
        char val_buf[12];
        if (e->type == 0) {
            uint8_t c = g_param_cc[e->idx];
            if (c == 0xFFu) {
                val_buf[0] = 'O'; val_buf[1] = 'F'; val_buf[2] = 'F'; val_buf[3] = '\0';
            } else {
                val_buf[0] = 'C'; val_buf[1] = 'C'; val_buf[2] = ' ';
                uint32_t vi = 3;
                if (c >= 100) { val_buf[vi++] = (char)('0' + c / 100); val_buf[vi++] = (char)('0' + (c / 10) % 10); }
                else if (c >= 10) { val_buf[vi++] = (char)('0' + c / 10); }
                val_buf[vi++] = (char)('0' + c % 10);
                val_buf[vi] = '\0';
            }
        } else if (e->type == 1) {
            uint8_t c = g_seq_cc[e->idx];
            if (c == 0xFFu) {
                val_buf[0] = 'O'; val_buf[1] = 'F'; val_buf[2] = 'F'; val_buf[3] = '\0';
            } else {
                val_buf[0] = 'C'; val_buf[1] = 'C'; val_buf[2] = ' ';
                uint32_t vi = 3;
                if (c >= 100) { val_buf[vi++] = (char)('0' + c / 100); val_buf[vi++] = (char)('0' + (c / 10) % 10); }
                else if (c >= 10) { val_buf[vi++] = (char)('0' + c / 10); }
                val_buf[vi++] = (char)('0' + c % 10);
                val_buf[vi] = '\0';
            }
        } else if (e->type == 2) {
            val_buf[0] = 'D'; val_buf[1] = 'E'; val_buf[2] = 'F'; val_buf[3] = 'A'; val_buf[4] = 'U'; val_buf[5] = 'L'; val_buf[6] = 'T'; val_buf[7] = '\0';
        } else {
            val_buf[0] = 'E'; val_buf[1] = 'X'; val_buf[2] = 'I'; val_buf[3] = 'T'; val_buf[4] = '\0';
        }
        menu_row_draw(10, 4u + (v - 4u) * 18u, idx == g_cc_map_sel, e->name, val_buf);
    }
    cv_rect(6, 44, 228, 1, C_LINE_DIV);
    cv_text_plate(0, 48, 240, "[SEL] Exit   [ALGO] Value", C_TEXT_DIM, C_BLACK, 1);
    cv_blit(0, 172);
}

/* Save / load settings */
typedef struct {
    int8_t octave;
    uint8_t patch;
    uint8_t midi_channel;   /* 0 = All (omni), 1..16 */
    uint8_t midi_out_ch;    /* 1..16: channel for everything we send */
    uint8_t midi_local;     /* 1 = panel keys play engine, 0 = send-only */
    uint8_t pc_in;          /* 1 = accept program change */
    uint8_t sync_mode;      /* 0 = off, 1 = master, 2 = slave */
    uint8_t cc_custom;      /* 0 = use defaults, 1 = user customized */
    uint8_t voice_mode;     /* 0 = Mono, 1 = Poly I, 2 = Poly II */
    uint8_t param_cc[FM106_NPARAM];
    uint8_t seq_cc[6];
} fm106_settings_t;

static void fm106_format_slot_code(uint8_t patch_idx, char *out)
{
    uint32_t bank_idx = (uint32_t)patch_idx / 64u;
    uint32_t local = (uint32_t)patch_idx % 64u;
    uint32_t b_num = (local / 8u) + 1u;
    uint32_t p_num = (local % 8u) + 1u;
    uint32_t i = 0;
    if (bank_idx == 0u) {
        out[i++] = 'A';
    } else if (bank_idx == 1u) {
        out[i++] = 'B';
    } else if (bank_idx == 2u) {
        out[i++] = 'U';
        out[i++] = 'A';
    } else {
        out[i++] = 'U';
        out[i++] = 'B';
    }
    out[i++] = (char)('0' + b_num);
    out[i++] = (char)('0' + p_num);
    out[i] = '\0';
}

static void fm106_set_banner(const char *prefix, const char *suffix)
{
    uint32_t i = 0;
    while (prefix && prefix[i] && i < sizeof(g_banner_msg) - 1u) {
        g_banner_msg[i] = prefix[i];
        i++;
    }
    uint32_t j = 0;
    while (suffix && suffix[j] && i < sizeof(g_banner_msg) - 1u) {
        g_banner_msg[i++] = suffix[j++];
    }
    g_banner_msg[i] = '\0';
    g_banner_until = fm1_ms + 2000u;
}

static int fm106_find_next_preset(uint32_t cur_patch, int32_t dir)
{
    uint32_t bank = cur_patch / 64u;
    uint32_t slot = cur_patch % 64u;

    int32_t ns = (int32_t)slot + dir;
    while (ns < 0) ns += 64;
    while (ns >= 64) ns -= 64;
    return (int)(bank * 64u + (uint32_t)ns);
}

static uint32_t fm106_bank_first_preset(uint32_t bank, uint32_t fallback_slot)
{
    return bank * 64u + (fallback_slot % 64u);
}

static void fm106_enter_save_menu(void)
{
    g_save_del_confirm = 0;
    g_save_cursor = 0;

    if (g_synth.patch_idx >= 128u) {
        g_save_slot = g_synth.patch_idx - 128u;
        uint32_t len = 0;
        while (g_synth.patch_name[len] && len < 15u) {
            g_save_name[len] = g_synth.patch_name[len];
            len++;
        }
        while (len < 15u) g_save_name[len++] = ' ';
        g_save_name[15] = '\0';
    } else {
        int first_blank = 0;
        for (uint32_t i = 0; i < FM106_NUM_USER_PRESETS; i++) {
            if (g_user_presets[i].name[0] == '\0') {
                first_blank = (int)i;
                break;
            }
        }
        g_save_slot = (uint8_t)first_blank;
        g_save_name[0] = '*';
        uint32_t len = 1;
        uint32_t pi = 0;
        while (g_synth.patch_name[pi] && len < 15u) {
            g_save_name[len++] = g_synth.patch_name[pi++];
        }
        while (len < 15u) g_save_name[len++] = ' ';
        g_save_name[15] = '\0';
    }
    int all_spaces = 1;
    for (int i = 0; i < 15; i++) {
        if (g_save_name[i] != ' ') { all_spaces = 0; break; }
    }
    if (all_spaces) {
        memcpy(g_save_name, "Init Patch     ", 15);
        g_save_name[15] = '\0';
    }
}

static void fm106_do_short_save(fm106_settings_t *set)
{
    char code_buf[8];
    if (g_synth.patch_idx < 128u) {
        int first_blank = -1;
        for (uint32_t i = 0; i < FM106_NUM_USER_PRESETS; i++) {
            if (g_user_presets[i].name[0] == '\0') {
                first_blank = (int)i;
                break;
            }
        }
        if (first_blank < 0) {
            fm106_set_banner("USER BANKS FULL!", "");
            return;
        }
        char target_name[16];
        target_name[0] = '*';
        uint32_t ti = 1, pi = 0;
        while (g_synth.patch_name[pi] && ti < 15u) {
            target_name[ti++] = g_synth.patch_name[pi++];
        }
        target_name[ti] = '\0';
        fm106_save_engine_to_preset(&g_user_presets[first_blank], &g_synth, target_name);
        if (fm106_user_presets_save_flash() != 0) {
            fm106_set_banner("SAVE FAILED!", "FLASH ERR");
            return;
        }
        g_synth.patch_idx = (uint8_t)(128u + (uint32_t)first_blank);
        str_cpy(g_synth.patch_name, target_name, sizeof(g_synth.patch_name));
        set->patch = g_synth.patch_idx;
        set->octave = (int8_t)g_synth.octave_shift;
        settings_save(set, sizeof(*set));
        fm106_format_slot_code(g_synth.patch_idx, code_buf);
        fm106_set_banner("SAVED TO ", code_buf);
    } else {
        uint32_t uidx = g_synth.patch_idx - 128u;
        const char *name_to_use = g_synth.patch_name[0] ? g_synth.patch_name : "User Patch";
        fm106_save_engine_to_preset(&g_user_presets[uidx], &g_synth, name_to_use);
        if (fm106_user_presets_save_flash() != 0) {
            fm106_set_banner("SAVE FAILED!", "FLASH ERR");
            return;
        }
        str_cpy(g_synth.patch_name, name_to_use, sizeof(g_synth.patch_name));
        set->patch = g_synth.patch_idx;
        set->octave = (int8_t)g_synth.octave_shift;
        settings_save(set, sizeof(*set));
        fm106_format_slot_code(g_synth.patch_idx, code_buf);
        fm106_set_banner("SAVED TO ", code_buf);
    }
}

static void draw_save_menu(void)
{
    /* Canvas 1 (y=0..75, height 76): Header + Target Slot */
    cv_begin(240, 76, C_BG);

    cv_rect(6, 4, 4, 12, C_ROLAND_RED);
    cv_text(16, 2, &FONT_S, "SAVE PRESET", C_STRIPE_CYAN);
    cv_text(130, 2, &FONT_S, "[SELECT] slot", C_ROLAND_AMBER);
    cv_rect(6, 18, 228, 2, C_STRIPE_CYAN);

    cv_text(8, 22, &FONT_S, "DESTINATION:", C_TEXT_DIM);

    cv_rect(6, 38, 228, 34, C_BLACK);
    cv_rect(6, 38, 228, 1, C_LINE_DIV);
    cv_rect(6, 71, 228, 1, C_LINE_DIV);

    char target_code[8];
    fm106_format_slot_code(128u + g_save_slot, target_code);
    cv_text(12, 46, &FONT_S, target_code, C_LED_ON);

    const char *target_exist = g_user_presets[g_save_slot].name;
    if (target_exist[0] == '\0') {
        cv_text(68, 46, &FONT_S, "<BLANK SLOT>", C_ROLAND_AMBER);
    } else {
        char exist_buf[24];
        exist_buf[0] = 'O'; exist_buf[1] = 'W'; exist_buf[2] = ':'; exist_buf[3] = ' ';
        uint32_t ei = 4, ti = 0;
        while (target_exist[ti] && ei < sizeof(exist_buf) - 1u) {
            exist_buf[ei++] = target_exist[ti++];
        }
        exist_buf[ei] = '\0';
        cv_text(68, 46, &FONT_S, exist_buf, C_TEXT_DIM);
    }

    cv_blit(0, 0);

    /* Canvas 2 (y=76..159, height 84): Name Editor with Cursor */
    cv_begin(240, 84, C_BG);

    cv_text(8, 2, &FONT_S, "PATCH NAME:", C_TEXT_DIM);
    cv_text(90, 2, &FONT_S, "ALGO:< > PRESET:A-Z", C_STRIPE_CYAN);

    cv_rect(6, 20, 228, 38, C_BLACK);
    cv_rect(6, 20, 228, 1, C_LINE_DIV);
    cv_rect(6, 57, 228, 1, C_LINE_DIV);

    for (uint32_t i = 0; i < 15u; i++) {
        char ch = g_save_name[i];
        if (ch == '\0') ch = ' ';
        int32_t cx = 10 + (int32_t)i * 15;

        if (i == (uint32_t)g_save_cursor) {
            cv_rect(cx, 24, 14, 28, C_ROLAND_AMBER);
            char s[2] = { ch, '\0' };
            cv_text(cx + 3, 30, &FONT_S, s, C_BLACK);
        } else {
            char s[2] = { ch, '\0' };
            cv_text(cx + 3, 30, &FONT_S, s, C_TEXT_WHITE);
        }
    }

    char cur_ch = g_save_name[g_save_cursor];
    if (cur_ch == '\0') cur_ch = ' ';
    char hint_buf[24];
    uint32_t hi = 0;
    const char *hprefix = "CHAR [";
    while (hprefix[hi]) { hint_buf[hi] = hprefix[hi]; hi++; }
    uint32_t cpos = (uint32_t)g_save_cursor + 1u;
    if (cpos >= 10u) {
        hint_buf[hi++] = '1';
        hint_buf[hi++] = (char)('0' + (cpos % 10u));
    } else {
        hint_buf[hi++] = (char)('0' + cpos);
    }
    hint_buf[hi++] = '/'; hint_buf[hi++] = '1'; hint_buf[hi++] = '5'; hint_buf[hi++] = ']';
    hint_buf[hi++] = ':'; hint_buf[hi++] = ' '; hint_buf[hi++] = '\'';
    hint_buf[hi++] = cur_ch;
    hint_buf[hi++] = '\'';
    hint_buf[hi] = '\0';
    cv_text(8, 64, &FONT_S, hint_buf, C_TEXT_DIM);

    cv_blit(0, 76);

    /* Canvas 3 (y=160..239, height 80): Action Buttons + Delete */
    cv_begin(240, 80, C_BG);

    cv_rect(6, 2, 108, 24, C_BLACK);
    cv_rect(6, 2, 108, 1, C_LINE_DIV);
    cv_rect(6, 25, 108, 1, C_LINE_DIV);
    cv_text(14, 6, &FONT_S, "[SAVE] Commit", C_ROLAND_AMBER);

    cv_rect(126, 2, 108, 24, C_BLACK);
    cv_rect(126, 2, 108, 1, C_LINE_DIV);
    cv_rect(126, 25, 108, 1, C_LINE_DIV);
    cv_text(134, 6, &FONT_S, "[HOME] Back", C_TEXT_WHITE);

    int has_preset = (g_save_slot < FM106_NUM_USER_PRESETS) && (g_user_presets[g_save_slot].name[0] != '\0');

    if (!has_preset) {
        cv_text(16, 42, &FONT_S, "(Slot is empty - blank)", C_TEXT_DIM);
    } else {
        if (g_save_del_confirm == 0) {
            cv_rect(6, 34, 228, 36, C_BLACK);
            cv_rect(6, 34, 228, 1, C_ROLAND_RED);
            cv_rect(6, 69, 228, 1, C_ROLAND_RED);
            char target_code[8];
            fm106_format_slot_code(128u + (uint32_t)g_save_slot, target_code);
            char del_lbl[32];
            uint32_t di = 0;
            const char *del_pre = "[SEL] Delete Slot (";
            while (del_pre[di]) { del_lbl[di] = del_pre[di]; di++; }
            uint32_t ci = 0;
            while (target_code[ci] && di < sizeof(del_lbl) - 2u) { del_lbl[di++] = target_code[ci++]; }
            del_lbl[di++] = ')';
            del_lbl[di] = '\0';
            cv_text(14, 44, &FONT_S, del_lbl, C_ROLAND_RED);
        } else {
            cv_rect(6, 32, 228, 42, C_ROLAND_RED);
            cv_text(14, 36, &FONT_S, "ARE YOU SURE?", C_TEXT_WHITE);
            cv_text(14, 52, &FONT_S, "[SEL] Yes, Delete   [HOME] Cancel", C_TEXT_WHITE);
        }
    }

    cv_blit(0, 160);
}

static int synth_quiet(void)
{
    if (fm1_in.notes != 0) return 0;
    for (uint32_t v = 0; v < FM106_NUM_VOICES; v++) {
        if (g_synth.voice[v].gate || g_synth.voice[v].adsr.stage != FM106_ENV_IDLE)
            return 0;
    }
    return 1;
}

static fm106_settings_t set = {0, 0, 0, 1, 1, 1, 0, 0, FM106_MODE_POLY1, {0}, {0}};

/* Initialize application and synth state */
static void fm106_app_init(void)
{
    fm106_engine_init(&g_synth);
    fm106_user_presets_init();

    if (settings_load(&set, sizeof(set)) != 0) {
        set.octave = 0;
        set.patch = 0;
        set.midi_channel = 0;
        set.midi_out_ch = 1;
        set.midi_local = 1;
        set.pc_in = 1;
        set.sync_mode = 0;
        set.cc_custom = 0;
        set.voice_mode = FM106_MODE_POLY1;
    }
    if (set.octave < -2 || set.octave > 2) set.octave = 0;
    if (set.midi_channel > 16) set.midi_channel = 0;    /* 0 = All (omni) */
    if (set.midi_out_ch < 1 || set.midi_out_ch > 16) set.midi_out_ch = 1;
    if (set.midi_local > 1) set.midi_local = 1;
    if (set.pc_in > 1) set.pc_in = 1;
    if (set.sync_mode > 2) set.sync_mode = 0;
    if (set.voice_mode > 2) set.voice_mode = FM106_MODE_POLY1;
    g_synth.octave_shift = set.octave;
    g_synth.midi_channel = set.midi_channel;
    g_synth.midi_out_ch = set.midi_out_ch;
    g_synth.midi_local = set.midi_local;
    g_synth.pc_in = set.pc_in;
    g_synth.sync_mode = set.sync_mode;
    if (set.cc_custom == 1u) {
        memcpy(g_param_cc, set.param_cc, sizeof(g_param_cc));
        memcpy(g_seq_cc, set.seq_cc, sizeof(g_seq_cc));
    } else {
        memcpy(g_param_cc, g_param_cc_default, sizeof(g_param_cc));
        memcpy(g_seq_cc, g_seq_cc_default, sizeof(g_seq_cc));
    }
    seq_sync_apply();
    fm106_load_user_preset(&g_synth, set.patch);
    if (set.voice_mode <= 2u) {
        fm106_set_voice_mode(&g_synth, set.voice_mode);
    }

    lcd_fill(0, 0, 240, 240, C_BG);

    /* Dim ambient backlight for all buttons (labels readable in the dark) */
    for (uint32_t col = 0; col < FM1_NCOL; col++)
        for (uint32_t row = 1; row < 5u; row++)
            if (FM1_KEYMAP[row][col] >= 0 && FM1_KEYMAP[row][col] < 14)
                fm1_led_bg[col] |= (uint8_t)(1u << row);
    fm1_led_bg_ns = 10000u;  /* dim pulse: ~1/6 of a fully lit LED */
}

/* One frame / poll iteration of the application */
static void fm106_app_frame(void)
{
    static uint32_t prev_notes = 0;
    static uint32_t dirty_screen = 1;
    static uint32_t prev_voice_active = 0;
    static uint32_t t_change = 0, dirty_save = 0;
    static uint32_t t_last_scope = 0;
    static uint8_t scope_was_active = 1;
    static uint8_t held[27] = {0};
    static uint32_t lit = 0;
    uint32_t k;
        uint32_t keys, down, up, rel, b;
        uint32_t base = (uint32_t)(KEY0 + 12 * g_synth.octave_shift);
        uint8_t st, d1, d2;

        platform_poll();
        plock_scan();

        /* drain seq MIDI-out events queued by the audio worker (SPSC) */
        {
            fm106_seq_msg_t sm;
            uint8_t och = (g_synth.midi_out_ch - 1u) & 0x0Fu;
            while (fm106_seq_msg_pop(&g_synth.seq, &sm)) {
                if (sm.on) midi_send(0x90u | och, sm.note, sm.vel);
                else       midi_send(0x80u | och, sm.note, 0u);
            }
        }

        /* REC+PLAY count-in: clicks, then the transport starts on the downbeat */
        count_in_tick();

        /* master MIDI clock out (SYNC=MASTER): 24 x F8 per beat while playing */
        if (g_synth.sync_mode == 1u && g_synth.seq.playing && !g_synth.seq.midi_sync_active) {
            uint32_t now = fm106_now_ms();
            uint32_t bpm = g_synth.seq.bpm ? g_synth.seq.bpm : 120u;
            uint32_t gap = 60000u / (bpm * 24u);
            if (!g_clk_next) g_clk_next = now + gap;
            while ((int32_t)(now - g_clk_next) >= 0) {
                midi_send(0xF8u, 0, 0);
                g_clk_next += gap;
                if ((int32_t)(now - g_clk_next) >= (int32_t)gap * 4) { g_clk_next = now; break; }
            }
        } else {
            g_clk_next = 0;
        }

        /* 1. Panel Keys (27 note keyboard) */
        keys = fm1_in.notes;
        down = keys & ~prev_notes;
        up = prev_notes & ~keys;
        prev_notes = keys;

        for (k = 0; k < 27u; k++) {
            if (down >> k & 1u) {
                held[k] = (uint8_t)(base + k);
                if (g_synth.midi_local) fm106_key_on(&g_synth, held[k], 100);
                midi_send(0x90u | ((g_synth.midi_out_ch - 1u) & 0x0Fu), held[k], 100);
                if (g_ui_mode == UI_MODE_NORMAL && g_current_page == PAGE_SEQ) {
                    seq_note_entry(held[k], 100); /* rec arm -> live record; else acid entry */
                    dirty_screen = 1;
                }
            }
            if (up >> k & 1u) {
                if (g_synth.midi_local) fm106_key_off(&g_synth, held[k]);
                midi_send(0x80u | ((g_synth.midi_out_ch - 1u) & 0x0Fu), held[k], 0);
                seq_note_release(held[k]); /* live-rec: stamp held length as gate */
            }
        }

        /* 2. Panel Buttons */
        b = fm1_input_edges(&rel);

        /* Octave shift */
        if ((b & (1u << BTN_OCT_DOWN)) && g_synth.octave_shift > -2) {
            g_synth.octave_shift--;
            set.octave = g_synth.octave_shift;
            dirty_screen = 1;
        }
        if ((b & (1u << BTN_OCT_UP)) && g_synth.octave_shift < 2) {
            g_synth.octave_shift++;
            set.octave = g_synth.octave_shift;
            dirty_screen = 1;
        }

        /* OPT/GLO button: toggle menu */
        if (b & (1u << BTN_GLO)) {
            if (g_ui_mode == UI_MODE_NORMAL) {
                g_ui_mode = UI_MODE_MENU;
                g_menu_sel = 0;
            } else if (g_ui_mode == UI_MODE_CC_MAP) {
                g_ui_mode = (g_menu_sel == 1) ? UI_MODE_MENU : UI_MODE_MIDI;
            } else if (g_ui_mode == UI_MODE_ABOUT) {
                g_ui_mode = UI_MODE_MENU;
            } else {
                g_ui_mode = UI_MODE_NORMAL;
            }
            dirty_screen = 1;
        }

        if (g_ui_mode == UI_MODE_NORMAL) {
            /* FX button: toggle chorus <-> tape page (jumps to chorus from anywhere) */
            if (b & (1u << BTN_FX)) {
                g_current_page = (g_current_page == PAGE_FX) ? PAGE_TAPE : PAGE_FX;
                dirty_screen = 1;
            }
        }

        /* Page jump buttons */
        if (b & (1u << BTN_HOME)) {
            if (g_ui_mode == UI_MODE_NORMAL && g_current_page == PAGE_SEQ && (g_seq_menu_on || g_note_menu_on)) {
                g_seq_menu_on = 0u;      /* HOME closes the SEQ menus first */
                g_note_menu_on = 0u;
                dirty_screen = 1;
            } else if (g_ui_mode == UI_MODE_SAVE) {
                if (g_save_del_confirm) {
                    g_save_del_confirm = 0;
                    dirty_screen = 1;
                } else {
                    g_ui_mode = UI_MODE_NORMAL;
                    dirty_screen = 1;
                }
            } else if (g_ui_mode != UI_MODE_NORMAL) {
                if (g_ui_mode == UI_MODE_CC_MAP) g_ui_mode = (g_menu_sel == 1) ? UI_MODE_MENU : UI_MODE_MIDI;
                else if (g_ui_mode == UI_MODE_MIDI) g_ui_mode = UI_MODE_MENU;
                else if (g_ui_mode == UI_MODE_ABOUT) g_ui_mode = UI_MODE_MENU;
                else g_ui_mode = UI_MODE_NORMAL;
                dirty_screen = 1;
            } else {
                g_current_page = PAGE_VCF;
                dirty_screen = 1;
            }
        }
        /* SEQ page: EDIT long-press menu + short clear; REC arm; SEL/ENV flags */
        if (g_ui_mode == UI_MODE_NORMAL && g_current_page == PAGE_SEQ) {
            if (b & (1u << BTN_EDIT)) {
                if (g_seq_menu_on) {
                    edit_held_active = 0u; g_seq_menu_on = 0u;
                }
                else if (g_note_menu_on) { g_note_menu_on = 0u; }
                else { t_edit_down = fm1_ms; edit_held_active = 0u; }
                dirty_screen = 1;
            }
            if (b & (1u << BTN_REC)) { g_seq_rec_arm ^= 1u; if (!g_seq_rec_arm) count_in_cancel(); g_seq_dirty = 1; dirty_screen = 1; }
            if (b & (1u << BTN_SEL)) {
                if (g_seq_menu_on) g_seq_menu_sel = (g_seq_menu_sel + SEQ_MENU_COUNT - 1u) % SEQ_MENU_COUNT;
                else if (g_note_menu_on) g_note_menu_sel = (g_note_menu_sel + NOTE_MENU_COUNT - 1u) % NOTE_MENU_COUNT;
                else { t_sel_down = fm1_ms; sel_held_active = 0u; }   /* short press clears on release */
                dirty_screen = 1;
            }
            if (b & (1u << BTN_ENV)) {
                if (g_seq_menu_on) g_seq_menu_sel = (g_seq_menu_sel + 1u) % SEQ_MENU_COUNT;
                else if (g_note_menu_on) g_note_menu_sel = (g_note_menu_sel + 1u) % NOTE_MENU_COUNT;
                else seq_ui_toggle_flag(FM106_SF_SLIDE);
                dirty_screen = 1;
            }
        }
        if (b & (1u << BTN_EDIT)) {
            if (g_ui_mode == UI_MODE_NORMAL && g_current_page != PAGE_SEQ) {
                g_current_page = (g_current_page + 1u) % PAGE_COUNT;
                dirty_screen = 1;
            }
        }
        if (b & (1u << BTN_ENV)) {
            if (g_ui_mode == UI_MODE_NORMAL && g_current_page != PAGE_SEQ) {
                g_current_page = PAGE_ENVELOPE;
                dirty_screen = 1;
            }
        }
        if (b & (1u << BTN_LFO)) {
            if (g_ui_mode == UI_MODE_NORMAL && g_current_page != PAGE_SEQ) {
                g_current_page = PAGE_LFO;
                dirty_screen = 1;
            }
        }

        /* SEQ EDIT: held 500ms opens options; release short-clears (or clears all) */
        if (g_ui_mode == UI_MODE_NORMAL && g_current_page == PAGE_SEQ) {
            if (!edit_held_active && !g_seq_menu_on && (fm1_in.buttons & (1u << BTN_EDIT))) {
                if (fm1_ms - t_edit_down >= 500u) {
                    edit_held_active = 1u;
                    g_seq_menu_on = 1u;
                    g_seq_menu_sel = 0u;
                    dirty_screen = 1;
                }
            }
            if (rel & (1u << BTN_EDIT)) {
                if (edit_held_active) {
                    /* menu opened by hold: it stays up until HOME (or EDIT tap) */
                    edit_held_active = 0u;
                    dirty_screen = 1;
                } else if (!g_seq_menu_on && !g_note_menu_on) {
                    /* short press: note options for the edited step */
                    g_note_menu_on = 1u;
                    g_note_menu_sel = 0u;
                    dirty_screen = 1;
                }
                edit_held_active = 0u;
            }
            /* SEQ SEL: held 500ms clears ALL steps; release short clears the step */
            if (!sel_held_active && !g_seq_menu_on && !g_note_menu_on
                && (fm1_in.buttons & (1u << BTN_SEL))) {
                if (fm1_ms - t_sel_down >= 500u) {
                    sel_held_active = 1u;
                    fm1_irq_off();
                    for (uint32_t i = 0; i < FM106_NSTEP; i++) {
                        g_synth.seq.step[i].n = 0;
                        g_synth.seq.step[i].time = FM106_ST_NOTE;
                        g_synth.seq.step[i].flags = 0;
                        g_synth.seq.step[i].acc = 0;
                        g_synth.seq.step[i].vel = 0;
                        g_synth.seq.step[i].probability = 0;
                        g_synth.seq.step[i].ratchet = 0;
                    }
                    fm106_plock_clear(&g_synth.seq); /* wipes go with the pattern */
                    fm1_irq_on();
                    g_seq_dirty = 1;
                    fm106_set_banner("SEQ ", "CLEARED");
                    dirty_screen = 1;
                }
            }
            if (rel & (1u << BTN_SEL)) {
                if (!sel_held_active && !g_seq_menu_on && !g_note_menu_on) {
                    seq_ui_clear_step();
                    dirty_screen = 1;
                }
                sel_held_active = 0u;
            }
        }
        if (b & (1u << BTN_SEL)) {
            if (g_ui_mode == UI_MODE_NORMAL && g_current_page != PAGE_SEQ) {
                g_synth.vca_mode ^= 1u;
                g_dirty_screen = 1;
            } else if (g_ui_mode == UI_MODE_SAVE) {
                int can_del = (g_save_slot < FM106_NUM_USER_PRESETS) && (g_user_presets[g_save_slot].name[0] != '\0');
                if (can_del) {
                    if (g_save_del_confirm == 0) {
                        g_save_del_confirm = 1;
                        dirty_screen = 1;
                    } else {
                        /* Confirmed delete of selected user slot */
                        uint32_t uidx = g_save_slot;
                        char code_buf[8];
                        fm106_format_slot_code(128u + (uint32_t)g_save_slot, code_buf);
                        memset(&g_user_presets[uidx], 0, sizeof(fm106_preset_t));
                        if (fm106_user_presets_save_flash() != 0) {
                            fm106_set_banner("DEL FAILED!", "FLASH ERR");
                        } else {
                            if (g_synth.patch_idx == (uint8_t)(128u + (uint32_t)g_save_slot)) {
                                fm106_load_user_preset(&g_synth, g_synth.patch_idx);
                                set.patch = g_synth.patch_idx;
                                settings_save(&set, sizeof(set));
                            }
                            fm106_set_banner("DELETED ", code_buf);
                        }
                        g_save_del_confirm = 0;
                        dirty_screen = 1;
                    }
                }
            } else if (g_ui_mode == UI_MODE_MENU) {
                if (g_menu_sel == 0) {
                    g_ui_mode = UI_MODE_MIDI;
                    g_midi_sel = 0;
                } else if (g_menu_sel == 1) {
                    g_ui_mode = UI_MODE_CC_MAP;
                    g_cc_map_sel = 0;
                    g_cc_map_scroll = 0;
                } else if (g_menu_sel == 2) {
                    g_ui_mode = UI_MODE_ABOUT;
                }
                dirty_screen = 1;
            } else if (g_ui_mode == UI_MODE_ABOUT) {
                g_ui_mode = UI_MODE_MENU;
                dirty_screen = 1;
            } else if (g_ui_mode == UI_MODE_MIDI) {
                if (g_midi_sel == 5) {
                    g_ui_mode = UI_MODE_CC_MAP;
                    g_cc_map_sel = 0;
                    g_cc_map_scroll = 0;
                } else {
                    g_ui_mode = UI_MODE_MENU;
                }
                dirty_screen = 1;
            } else if (g_ui_mode == UI_MODE_CC_MAP) {
                if (g_cc_map_sel == CC_MAP_IDX_RESET) {
                    memcpy(g_param_cc, g_param_cc_default, sizeof(g_param_cc));
                    memcpy(g_seq_cc, g_seq_cc_default, sizeof(g_seq_cc));
                    set.cc_custom = 0;
                    fm106_set_banner("CC MAP ", "RESET");
                    dirty_save = 1;
                    t_change = fm1_ms;
                } else {
                    g_ui_mode = (g_menu_sel == 1) ? UI_MODE_MENU : UI_MODE_MIDI;
                }
                dirty_screen = 1;
            } else {
                g_synth.vca_mode ^= 1u;
                dirty_screen = 1;
            }
        }
        if (b & (1u << BTN_ARP)) {
            if (g_ui_mode == UI_MODE_NORMAL) {
                g_current_page = PAGE_ARP;
                dirty_screen = 1;
            }
        }
        if (b & (1u << BTN_SEQ)) {
            if (g_ui_mode == UI_MODE_NORMAL) {
                g_current_page = PAGE_SEQ;
                dirty_screen = 1;
            }
        }
        if (b & (1u << BTN_PLAY)) {   /* transport: start/stop the sequencer */
            if (g_count_in) {
                count_in_cancel();    /* second press aborts the count-in */
            } else if (!g_synth.seq.playing && g_seq_rec_arm) {
                count_in_start();     /* REC armed: click in four beats first */
            } else {
                seq_transport_cmd(3);
            }
        }
        if (b & (1u << BTN_SAVE)) {
            if (g_ui_mode == UI_MODE_SAVE) {
                /* Commit save to g_save_slot */
                char clean_name[16];
                memcpy(clean_name, g_save_name, 15);
                clean_name[15] = '\0';
                int32_t last = 14;
                while (last >= 0 && clean_name[last] == ' ') {
                    clean_name[last] = '\0';
                    last--;
                }
                if (clean_name[0] == '\0') {
                    str_cpy(clean_name, "Untitled", sizeof(clean_name));
                }
                fm106_save_engine_to_preset(&g_user_presets[g_save_slot], &g_synth, clean_name);
                if (fm106_user_presets_save_flash() != 0) {
                    fm106_set_banner("SAVE FAILED!", "FLASH ERR");
                    g_ui_mode = UI_MODE_NORMAL;
                    save_held_active = 1;
                    dirty_screen = 1;
                } else {
                    g_synth.patch_idx = (uint8_t)(128u + g_save_slot);
                    str_cpy(g_synth.patch_name, clean_name, sizeof(g_synth.patch_name));
                    set.patch = g_synth.patch_idx;
                    set.octave = (int8_t)g_synth.octave_shift;
                    settings_save(&set, sizeof(set));
                    char code_buf[8];
                    fm106_format_slot_code(g_synth.patch_idx, code_buf);
                    fm106_set_banner("SAVED TO ", code_buf);
                    g_ui_mode = UI_MODE_NORMAL;
                    save_held_active = 1; /* suppress short save on release */
                    dirty_screen = 1;
                }
            } else if (g_ui_mode == UI_MODE_NORMAL) {
                t_save_down = fm1_ms;
                save_held_active = 0;
            }
        }

        /* Check for held SAVE button (long press >= 500 ms) in NORMAL mode */
        if (g_ui_mode == UI_MODE_NORMAL) {
            if (!save_held_active && (fm1_in.buttons & (1u << BTN_SAVE))) {
                if (fm1_ms - t_save_down >= 500u) {
                    save_held_active = 1;
                    g_ui_mode = UI_MODE_SAVE;
                    fm106_enter_save_menu();
                    dirty_screen = 1;
                }
            }
            if (rel & (1u << BTN_SAVE)) {
                if (!save_held_active) {
                    fm106_do_short_save(&set);
                    dirty_screen = 1;
                }
            }
        }

        /* 3. Panel Encoders */
        /* Preset navigation encoder: steps within active bank, skipping empty user presets */
        int32_t enc_p = fm1_enc_take(ENC_PRESETS);
        if (enc_p != 0) {
            if (g_ui_mode == UI_MODE_SAVE) {
                int32_t cur_c = (int32_t)g_save_name[g_save_cursor];
                int32_t c_idx = 0;
                for (int32_t ci = 0; ci < CHAR_SET_LEN; ci++) {
                    if (CHAR_SET[ci] == cur_c) {
                        c_idx = ci;
                        break;
                    }
                }
                c_idx = (c_idx + enc_p) % CHAR_SET_LEN;
                if (c_idx < 0) c_idx += CHAR_SET_LEN;
                g_save_name[g_save_cursor] = CHAR_SET[c_idx];
                dirty_screen = 1;
            } else if (g_ui_mode == UI_MODE_NORMAL) {
                int np = fm106_find_next_preset(g_synth.patch_idx, enc_p);
                if (np != (int)g_synth.patch_idx) {
                    fm106_load_user_preset(&g_synth, (uint32_t)np);
                    set.patch = (uint8_t)np;
                    dirty_screen = 1;
                    midi_send(0xC0u | ((g_synth.midi_out_ch - 1u) & 0x0Fu), (uint8_t)(np % 64u), 0);
                }
            }
        }

        /* Select / Page encoder */
        int32_t enc_s = fm1_enc_take(ENC_SELECT);
        if (enc_s != 0) {
            if (g_ui_mode == UI_MODE_NORMAL && g_current_page == PAGE_SEQ && g_seq_menu_on) {
                int32_t nm = (int32_t)g_seq_menu_sel + enc_s;   /* glo-menu nav: SELECT moves rows */
                while (nm < 0) nm += SEQ_MENU_COUNT;
                while (nm >= (int32_t)SEQ_MENU_COUNT) nm -= SEQ_MENU_COUNT;
                g_seq_menu_sel = (uint8_t)nm;
                dirty_screen = 1;
            } else if (g_ui_mode == UI_MODE_NORMAL && g_current_page == PAGE_SEQ && g_note_menu_on) {
                int32_t nm = (int32_t)g_note_menu_sel + enc_s;
                while (nm < 0) nm += NOTE_MENU_COUNT;
                while (nm >= (int32_t)NOTE_MENU_COUNT) nm -= NOTE_MENU_COUNT;
                g_note_menu_sel = (uint8_t)nm;
                dirty_screen = 1;
            } else if (g_ui_mode == UI_MODE_SAVE) {
                int32_t ns = (int32_t)g_save_slot + enc_s;
                while (ns < 0) ns += (int32_t)FM106_NUM_USER_PRESETS;
                while (ns >= (int32_t)FM106_NUM_USER_PRESETS) ns -= (int32_t)FM106_NUM_USER_PRESETS;
                g_save_slot = (uint8_t)ns;
                g_save_del_confirm = 0;
                dirty_screen = 1;
            } else if (g_ui_mode == UI_MODE_MENU) {
                int32_t nm = (int32_t)g_menu_sel + enc_s;
                if (nm < 0) nm = MENU_MAIN_COUNT - 1;
                if (nm >= (int32_t)MENU_MAIN_COUNT) nm = 0;
                g_menu_sel = (uint8_t)nm;
                dirty_screen = 1;
            } else if (g_ui_mode == UI_MODE_MIDI) {
                int32_t nm = (int32_t)g_midi_sel + enc_s;
                if (nm < 0) nm = MENU_MIDI_COUNT - 1;
                if (nm >= (int32_t)MENU_MIDI_COUNT) nm = 0;
                g_midi_sel = (uint8_t)nm;
                dirty_screen = 1;
            } else if (g_ui_mode == UI_MODE_CC_MAP) {
                int32_t ns = (int32_t)g_cc_map_sel + enc_s;
                if (ns < 0) ns = CC_MAP_COUNT - 1;
                if (ns >= (int32_t)CC_MAP_COUNT) ns = 0;
                g_cc_map_sel = (uint8_t)ns;
                if (g_cc_map_sel < g_cc_map_scroll) {
                    g_cc_map_scroll = g_cc_map_sel;
                } else if (g_cc_map_sel >= g_cc_map_scroll + CC_MAP_VISIBLE) {
                    g_cc_map_scroll = g_cc_map_sel - (CC_MAP_VISIBLE - 1);
                }
                dirty_screen = 1;
            } else {
                int32_t npage = (int32_t)g_current_page + enc_s;
                if (npage < 0) npage = PAGE_COUNT - 1;
                if (npage >= PAGE_COUNT) npage = 0;
                g_current_page = (uint8_t)npage;
                dirty_screen = 1;
            }
        }

        /* ALGORITHM knob: bank switching in NORMAL mode, cursor in SAVE mode, menu editing in MENU mode */
        int32_t ealgo = fm1_enc_take(ENC_ALGORITHM);
        if (ealgo != 0) {
            if (g_ui_mode == UI_MODE_NORMAL && g_current_page == PAGE_SEQ && g_seq_menu_on) {
                seq_ui_menu_adjust(ealgo > 0 ? 1 : -1);          /* glo-menu nav: ALGORITHM edits value */
                dirty_screen = 1;
            } else if (g_ui_mode == UI_MODE_NORMAL && g_current_page == PAGE_SEQ && g_note_menu_on) {
                seq_ui_note_adjust(ealgo > 0 ? 1 : -1);
                dirty_screen = 1;
            } else if (g_ui_mode == UI_MODE_SAVE) {
                int32_t ncur = (int32_t)g_save_cursor + ealgo;
                if (ncur < 0) ncur = 0;
                if (ncur > 14) ncur = 14;
                g_save_cursor = (uint8_t)ncur;
                dirty_screen = 1;
            } else if (g_ui_mode == UI_MODE_NORMAL && !g_seq_menu_on && !g_note_menu_on) {
                int32_t cur_bank = (int32_t)g_synth.patch_idx / 64;
                int32_t cur_slot = (int32_t)g_synth.patch_idx % 64;
                int32_t nb = cur_bank + ealgo;
                while (nb < 0) nb += 4;
                while (nb >= 4) nb -= 4;
                uint32_t np = fm106_bank_first_preset((uint32_t)nb, (uint32_t)cur_slot);
                fm106_load_user_preset(&g_synth, np);
                set.patch = (uint8_t)np;
                dirty_screen = 1;
                midi_send(0xB0u | ((g_synth.midi_out_ch - 1u) & 0x0Fu), 0, (uint8_t)nb);
                midi_send(0xC0u | ((g_synth.midi_out_ch - 1u) & 0x0Fu), (uint8_t)(np % 64u), 0);
            } else if (g_ui_mode == UI_MODE_MIDI) {
                if (g_midi_sel == 0) {          /* MIDI IN: 0 = All, 1..16 */
                    int32_t nc = (int32_t)g_synth.midi_channel + ealgo;
                    if (nc < 0) nc = 16;
                    if (nc > 16) nc = 0;
                    g_synth.midi_channel = (uint8_t)nc;
                } else if (g_midi_sel == 1) {   /* MIDI OUT: 1..16 */
                    int32_t nc = (int32_t)g_synth.midi_out_ch + ealgo;
                    if (nc < 1) nc = 16;
                    if (nc > 16) nc = 1;
                    g_synth.midi_out_ch = (uint8_t)nc;
                } else if (g_midi_sel == 2) {   /* SYNC */
                    int32_t ns = (int32_t)g_synth.sync_mode + (ealgo > 0 ? 1 : -1);
                    if (ns < 0) ns = 2;
                    if (ns > 2) ns = 0;
                    g_synth.sync_mode = (uint8_t)ns;
                    seq_sync_apply();
                } else if (g_midi_sel == 3) {   /* LOCAL: keys play engine */
                    g_synth.midi_local ^= (uint8_t)(ealgo > 0 ? 1u : 0u);
                    if (!g_synth.midi_local) {   /* going local-off: release any held voices */
                        fm1_irq_off();
                        for (uint8_t v = 0; v < FM106_NUM_VOICES; v++)
                            if (g_synth.voice[v].note != 255u) fm106_note_off(&g_synth, g_synth.voice[v].note);
                        fm1_irq_on();
                    }
                } else if (g_midi_sel == 4) {   /* PC IN: accept program change */
                    g_synth.pc_in ^= (uint8_t)(ealgo > 0 ? 1u : 0u);
                } else if (g_midi_sel == 5) {   /* CC MAP sub-menu */
                    g_ui_mode = UI_MODE_CC_MAP;
                    g_cc_map_sel = 0;
                    g_cc_map_scroll = 0;
                }
                dirty_screen = 1;
                dirty_save = 1;
                t_change = fm1_ms;
            } else if (g_ui_mode == UI_MODE_CC_MAP) {
                if (g_cc_map_sel < CC_MAP_COUNT) {
                    const fm106_cc_entry_t *e = &g_cc_map_entries[g_cc_map_sel];
                    if (e->type == 0 || e->type == 1) {
                        uint8_t cur_c = (e->type == 0) ? g_param_cc[e->idx] : g_seq_cc[e->idx];
                        int32_t st = (cur_c == 0xFFu) ? 128 : (int32_t)cur_c;
                        st += ealgo;
                        while (st < 0) st += 129;
                        while (st > 128) st -= 129;
                        uint8_t new_c = (st == 128) ? 0xFFu : (uint8_t)st;
                        if (e->type == 0) {
                            g_param_cc[e->idx] = new_c;
                        } else {
                            g_seq_cc[e->idx] = new_c;
                        }
                        set.cc_custom = 1u;
                        dirty_screen = 1;
                        dirty_save = 1;
                        t_change = fm1_ms;
                    }
                }
            }
        }

        /* 4 macro knobs (ENC_KNOB1..4) tweaking the active fader parameters */
        int32_t k1 = fm1_enc_take(ENC_KNOB1);
        int32_t k2 = fm1_enc_take(ENC_KNOB2);
        int32_t k3 = fm1_enc_take(ENC_KNOB3);
        int32_t k4 = fm1_enc_take(ENC_KNOB4);

        if (k1 || k2 || k3 || k4) {
            if (g_ui_mode == UI_MODE_NORMAL) {
            switch (g_current_page) {
            case PAGE_VCF:
                if (k1) g_synth.vcf_cutoff = (uint8_t)fm106_clamp(g_synth.vcf_cutoff + k1 * 3, 0, 127);
                if (k2) g_synth.vcf_resonance = (uint8_t)fm106_clamp(g_synth.vcf_resonance + k2 * 3, 0, 127);
                if (k3) g_synth.vcf_env_mod = (uint8_t)fm106_clamp(g_synth.vcf_env_mod + k3 * 3, 0, 127);
                if (k4) g_synth.vcf_lfo_mod = (uint8_t)fm106_clamp(g_synth.vcf_lfo_mod + k4 * 3, 0, 127);
                break;
            case PAGE_VCF2:
                if (k1) g_synth.vcf_kbd_track = (uint8_t)fm106_clamp(g_synth.vcf_kbd_track + k1 * 3, 0, 127);
                if (k2) g_synth.vcf_env_polarity = k2 > 0 ? 1u : 0u;
                if (k3) g_synth.bender_dco = (uint8_t)fm106_clamp(g_synth.bender_dco + k3 * 3, 0, 127);
                if (k4) g_synth.bender_vcf = (uint8_t)fm106_clamp(g_synth.bender_vcf + k4 * 3, 0, 127);
                break;
            case PAGE_DCO:
                if (k1) {
                    int32_t m = (int32_t)g_synth.dco_range + (k1 > 0 ? 1 : -1);
                    if (m < 0) m = 2;
                    if (m > 2) m = 0;
                    g_synth.dco_range = (uint8_t)m;
                    for (uint32_t v = 0; v < FM106_NUM_VOICES; v++)
                        if (g_synth.voice[v].note != 255u)
                            g_synth.voice[v].base_inc =
                                fm106_calc_inc(g_synth.voice[v].note, 0, g_synth.dco_range);
                }
                if (k2) {
                    g_synth.dco_pulse_on = k2 > 0 ? 1u : 0u;
                    for (uint32_t v = 0; v < FM106_NUM_VOICES; v++)
                        g_synth.voice[v].dco.pulse_on = g_synth.dco_pulse_on;
                }
                if (k3) {
                    g_synth.dco_saw_on = k3 > 0 ? 1u : 0u;
                    for (uint32_t v = 0; v < FM106_NUM_VOICES; v++)
                        g_synth.voice[v].dco.saw_on = g_synth.dco_saw_on;
                }
                if (k4) {
                    g_synth.dco_sub_on = k4 > 0 ? 1u : 0u;
                    if (g_synth.dco_sub_on && g_synth.dco_sub_level == 0) {
                        g_synth.dco_sub_level = 64;
                    }
                    for (uint32_t v = 0; v < FM106_NUM_VOICES; v++) {
                        g_synth.voice[v].dco.sub_on = g_synth.dco_sub_on;
                        g_synth.voice[v].dco.sub_level = g_synth.dco_sub_level;
                    }
                }
                break;
            case PAGE_DCO2:
                if (k1) {
                    g_synth.dco_sub_level = (uint8_t)fm106_clamp(g_synth.dco_sub_level + k1 * 3, 0, 127);
                    g_synth.dco_sub_on = g_synth.dco_sub_level > 0 ? 1u : 0u;
                    for (uint32_t v = 0; v < FM106_NUM_VOICES; v++) {
                        g_synth.voice[v].dco.sub_level = g_synth.dco_sub_level;
                        g_synth.voice[v].dco.sub_on = g_synth.dco_sub_on;
                    }
                }
                if (k2) {
                    g_synth.dco_noise_level = (uint8_t)fm106_clamp(g_synth.dco_noise_level + k2 * 3, 0, 127);
                    for (uint32_t v = 0; v < FM106_NUM_VOICES; v++)
                        g_synth.voice[v].dco.noise_level = g_synth.dco_noise_level;
                }
                if (k3) g_synth.dco_pwm_depth = (uint8_t)fm106_clamp(g_synth.dco_pwm_depth + k3 * 3, 0, 127);
                if (k4) g_synth.dco_lfo_depth = (uint8_t)fm106_clamp(g_synth.dco_lfo_depth + k4 * 3, 0, 127);
                break;
            case PAGE_ENVELOPE:
                if (k1) g_synth.env_a = (uint8_t)fm106_clamp(g_synth.env_a + k1 * 3, 0, 127);
                if (k2) g_synth.env_d = (uint8_t)fm106_clamp(g_synth.env_d + k2 * 3, 0, 127);
                if (k3) g_synth.env_s = (uint8_t)fm106_clamp(g_synth.env_s + k3 * 3, 0, 127);
                if (k4) g_synth.env_r = (uint8_t)fm106_clamp(g_synth.env_r + k4 * 3, 0, 127);
                for (uint32_t v = 0; v < FM106_NUM_VOICES; v++)
                    fm106_adsr_params(&g_synth.voice[v].adsr, g_synth.env_a, g_synth.env_d, g_synth.env_s, g_synth.env_r);
                break;
            case PAGE_LFO:
                if (k1) g_synth.lfo_rate = (uint8_t)fm106_clamp(g_synth.lfo_rate + k1 * 3, 0, 127);
                if (k2) g_synth.lfo_delay = (uint8_t)fm106_clamp(g_synth.lfo_delay + k2 * 3, 0, 127);
                if (k3) g_synth.dco_pwm_mode = k3 > 0 ? 1u : 0u;
                if (k4) {
                    int32_t vm = (int32_t)g_synth.voice_mode + (k4 > 0 ? 1 : -1);
                    if (vm < 0) vm = 2;
                    if (vm > 2) vm = 0;
                    fm106_set_voice_mode(&g_synth, (uint8_t)vm);
                    set.voice_mode = g_synth.voice_mode;
                    dirty_save = 1;
                    t_change = fm1_ms;
                    fm106_set_banner("ASSIGN ", N_POLY[g_synth.voice_mode]);
                }
                break;
            case PAGE_ARP:
                if (k1) fm106_arp_enable(&g_synth, k1 > 0);
                if (k2) {
                    int32_t m = (int32_t)g_synth.arp_mode + (k2 > 0 ? 1 : -1);
                    if (m < 0) m = 2;
                    if (m > 2) m = 0;
                    g_synth.arp_mode = (uint8_t)m;
                }
                if (k3) g_synth.arp_rate = (uint8_t)fm106_clamp(g_synth.arp_rate + k3 * 3, 0, 127);
                if (k4) g_synth.arp_range = (uint8_t)fm106_clamp(g_synth.arp_range + (k4 > 0 ? 1 : -1), 1, 3);
                break;
            case PAGE_FX:
                if (k1) {
                    int32_t m = (int32_t)g_synth.chorus.mode + (k1 > 0 ? 1 : -1);
                    if (m < 0) m = 3;
                    if (m > 3) m = 0;
                    g_synth.chorus.mode = (uint8_t)m;
                }
                if (k2) {
                    int32_t m = (int32_t)g_synth.hpf.mode + (k2 > 0 ? 1 : -1);
                    if (m < 0) m = 3;
                    if (m > 3) m = 0;
                    g_synth.hpf.mode = (uint8_t)m;
                }
                if (k3) g_synth.vca_mode = k3 > 0 ? 1u : 0u;
                if (k4) g_synth.vca_level = (uint8_t)fm106_clamp(g_synth.vca_level + k4 * 3, 0, 127);
                break;
            case PAGE_TAPE: {
                fm106_fx_t *fx = &g_synth.fx[0];
                if (k1) {
                    int32_t m = (int32_t)fx->type + (k1 > 0 ? 1 : -1);
                    if (m < 0) m = FM106_FX_COUNT - 1;
                    if (m >= FM106_FX_COUNT) m = 0;
                    fx->type = (uint8_t)m;
                }
                if (k2) fx->mix = (uint8_t)fm106_clamp(fx->mix + k2 * 3, 0, 127);
                if (k3) fx->rate = (uint8_t)fm106_clamp(fx->rate + k3 * 3, 0, 127);
                if (k4) fx->character = (uint8_t)fm106_clamp(fx->character + k4 * 3, 0, 127);
                break;
            }
            case PAGE_SEQ: {
                if (!g_seq_menu_on && !g_note_menu_on) {   /* menus use SELECT/ALGORITHM (glo scheme) */
                    if (k1) seq_ui_move_cursor(k1 > 0 ? 1 : -1);
                    if (k2) seq_ui_move_row(k2 > 0 ? 1 : -1);
                    if (k3) seq_ui_adjust_gate(k3 > 0 ? 1 : -1);
                    if (k4) seq_ui_adjust_vel(k4 > 0 ? 1 : -1);
                }
                break;
            }
            }
            dirty_screen = 1;
            }
        }

        /* 4. USB-MIDI In */
        while (midi_get(&st, &d1, &d2)) {
            /* System Realtime (clock/transport) — arrives interleaved on the
             * status byte, so handle before the channel-type nibble. */
            if (st >= 0xF8u) {
                if (st == 0xF8u) {
                    fm106_midi_clock_pulse(&g_synth.seq, fm106_now_ms());
                } else if (st == 0xFAu || st == 0xFBu || st == 0xFCu) {
                    fm106_midi_clock_transport(&g_synth.seq, st);
                    if (st == 0xFCu) {
                        /* Realtime Stop: cleanly kill sounding sequencer notes */
                        fm1_irq_off();
#if DINGHY_DUAL_CORE
                        if (audio_worker_online) audio_worker_join();
#endif
                        for (uint8_t i = 0; i < g_synth.seq.seq_n; i++)
                            fm106_note_off(&g_synth, g_synth.seq.seq_notes[i]);
                        g_synth.seq.seq_n = 0;
                        g_synth.seq.seq_hold = 0;
                        g_synth.seq.seq_off = 0;
                        g_synth.seq.playing = 0;
                        g_synth.seq.msg_tail = g_synth.seq.msg_head;
                        fm1_irq_on();
                        dirty_screen = 1;
                    } else if (st == 0xFAu || st == 0xFBu) {
                        dirty_screen = 1;
                    }
                }
                continue;
            }
            /* channel filter: IN = All (0) hears everything, 1..16 only their
             * channel. System Common (0xF0..0xF7) passes unfiltered. */
            if (g_synth.midi_channel && (st & 0xF0u) < 0xF0u
                && ((st & 0x0Fu) + 1u) != g_synth.midi_channel)
                continue;
            uint8_t type = st & 0xF0u;
            if (type == 0x90u && d2 > 0) {
                fm106_key_on(&g_synth, d1, d2);
                seq_note_entry(d1, d2); /* armed -> live record anywhere; else acid entry on SEQ page */
            } else if (type == 0x80u || (type == 0x90u && d2 == 0)) {
                fm106_key_off(&g_synth, d1);
                seq_note_release(d1); /* live-rec: stamp held length as gate */
            } else if (type == 0xE0u) {  /* Pitch Bend */
                int32_t raw = ((int32_t)d2 << 7) | (int32_t)d1;
                g_synth.pitch_bend_val = (int16_t)(raw - 8192);
            } else if (type == 0xC0u) {  /* Program Change (gated by PC IN) */
                if (!g_synth.pc_in) continue;
                int32_t cur_bank = (int32_t)g_synth.patch_idx / 64;
                uint32_t target_patch = (uint32_t)(cur_bank * 64 + (d1 % 64u));
                fm106_load_user_preset(&g_synth, target_patch);
                set.patch = (uint8_t)target_patch;
                dirty_screen = 1;
            } else if (type == 0xB0u) {  /* Control Change */
                if (d1 == 0 || d1 == 32) { /* Bank Select MSB/LSB */
                    uint32_t nb = d2 & 3u;
                    int32_t cur_slot = (int32_t)g_synth.patch_idx % 64;
                    uint32_t target_patch = nb * 64u + (uint32_t)cur_slot;
                    fm106_load_user_preset(&g_synth, target_patch);
                    set.patch = (uint8_t)target_patch;
                    dirty_screen = 1;
                } else if (g_param_cc[10] != 0xFFu && d1 == g_param_cc[10]) {  /* Brightness / Cutoff */
                    g_synth.vcf_cutoff = d2;
                    dirty_screen = 1;
                } else if (g_param_cc[11] != 0xFFu && d1 == g_param_cc[11]) {  /* Resonance */
                    g_synth.vcf_resonance = d2;
                    dirty_screen = 1;
                } else if (g_param_cc[12] != 0xFFu && d1 == g_param_cc[12]) {  /* VCF Env Mod */
                    g_synth.vcf_env_mod = d2;
                    dirty_screen = 1;
                } else if (g_param_cc[13] != 0xFFu && d1 == g_param_cc[13]) {  /* VCF LFO Mod */
                    g_synth.vcf_lfo_mod = d2;
                    dirty_screen = 1;
                } else if (g_param_cc[14] != 0xFFu && d1 == g_param_cc[14]) {  /* VCF Key Track */
                    g_synth.vcf_kbd_track = d2;
                    dirty_screen = 1;
                } else if (g_param_cc[16] != 0xFFu && d1 == g_param_cc[16]) {  /* Attack */
                    g_synth.env_a = d2;
                    for (uint32_t vi = 0; vi < FM106_NUM_VOICES; vi++)
                        fm106_adsr_params(&g_synth.voice[vi].adsr, g_synth.env_a, g_synth.env_d, g_synth.env_s, g_synth.env_r);
                    dirty_screen = 1;
                } else if (g_param_cc[17] != 0xFFu && d1 == g_param_cc[17]) {  /* Decay */
                    g_synth.env_d = d2;
                    for (uint32_t vi = 0; vi < FM106_NUM_VOICES; vi++)
                        fm106_adsr_params(&g_synth.voice[vi].adsr, g_synth.env_a, g_synth.env_d, g_synth.env_s, g_synth.env_r);
                    dirty_screen = 1;
                } else if (g_param_cc[18] != 0xFFu && d1 == g_param_cc[18]) {  /* Sustain */
                    g_synth.env_s = d2;
                    for (uint32_t vi = 0; vi < FM106_NUM_VOICES; vi++)
                        fm106_adsr_params(&g_synth.voice[vi].adsr, g_synth.env_a, g_synth.env_d, g_synth.env_s, g_synth.env_r);
                    dirty_screen = 1;
                } else if (g_param_cc[19] != 0xFFu && d1 == g_param_cc[19]) {  /* Release */
                    g_synth.env_r = d2;
                    for (uint32_t vi = 0; vi < FM106_NUM_VOICES; vi++)
                        fm106_adsr_params(&g_synth.voice[vi].adsr, g_synth.env_a, g_synth.env_d, g_synth.env_s, g_synth.env_r);
                    dirty_screen = 1;
                } else if (g_param_cc[5] != 0xFFu && d1 == g_param_cc[5]) {    /* DCO LFO */
                    g_synth.dco_lfo_depth = d2;
                    dirty_screen = 1;
                } else if (g_param_cc[6] != 0xFFu && d1 == g_param_cc[6]) {    /* DCO PWM Depth */
                    g_synth.dco_pwm_depth = d2;
                    dirty_screen = 1;
                } else if (g_param_cc[33] != 0xFFu && d1 == g_param_cc[33]) {  /* PWM Mode */
                    g_synth.dco_pwm_mode = (d2 >= 64u);
                    dirty_screen = 1;
                } else if (g_param_cc[7] != 0xFFu && d1 == g_param_cc[7]) {    /* Sub Level */
                    g_synth.dco_sub_level = d2;
                    g_synth.dco_sub_on = d2 > 0 ? 1u : 0u;
                    for (uint32_t vi = 0; vi < FM106_NUM_VOICES; vi++) {
                        g_synth.voice[vi].dco.sub_level = d2;
                        g_synth.voice[vi].dco.sub_on = g_synth.dco_sub_on;
                    }
                    dirty_screen = 1;
                } else if (g_param_cc[25] != 0xFFu && d1 == g_param_cc[25]) {  /* Sub On */
                    g_synth.dco_sub_on = (d2 >= 64u);
                    for (uint32_t vi = 0; vi < FM106_NUM_VOICES; vi++)
                        g_synth.voice[vi].dco.sub_on = g_synth.dco_sub_on;
                    dirty_screen = 1;
                } else if (g_param_cc[8] != 0xFFu && d1 == g_param_cc[8]) {    /* Noise Level */
                    g_synth.dco_noise_level = d2;
                    for (uint32_t vi = 0; vi < FM106_NUM_VOICES; vi++)
                        g_synth.voice[vi].dco.noise_level = d2;
                    dirty_screen = 1;
                } else if (g_param_cc[23] != 0xFFu && d1 == g_param_cc[23]) {  /* Pulse On */
                    g_synth.dco_pulse_on = (d2 >= 64u);
                    for (uint32_t vi = 0; vi < FM106_NUM_VOICES; vi++)
                        g_synth.voice[vi].dco.pulse_on = g_synth.dco_pulse_on;
                    dirty_screen = 1;
                } else if (g_param_cc[24] != 0xFFu && d1 == g_param_cc[24]) {  /* Saw On */
                    g_synth.dco_saw_on = (d2 >= 64u);
                    for (uint32_t vi = 0; vi < FM106_NUM_VOICES; vi++)
                        g_synth.voice[vi].dco.saw_on = g_synth.dco_saw_on;
                    dirty_screen = 1;
                } else if (g_param_cc[29] != 0xFFu && d1 == g_param_cc[29]) {  /* DCO Range */
                    g_synth.dco_range = d2 / 42u > 2u ? 2u : d2 / 42u;
                    for (uint32_t vi = 0; vi < FM106_NUM_VOICES; vi++)
                        if (g_synth.voice[vi].note != 255u)
                            g_synth.voice[vi].base_inc = fm106_calc_inc(g_synth.voice[vi].note, 0, g_synth.dco_range);
                    dirty_screen = 1;
                } else if ((g_param_cc[15] != 0xFFu && d1 == g_param_cc[15]) || (g_param_cc[15] != 7 && d1 == 7)) { /* VCA Level / Volume */
                    g_synth.vca_level = d2;
                    dirty_screen = 1;
                } else if (g_param_cc[35] != 0xFFu && d1 == g_param_cc[35]) {  /* VCA Mode */
                    g_synth.vca_mode = (d2 >= 64u);
                    dirty_screen = 1;
                } else if (g_param_cc[34] != 0xFFu && d1 == g_param_cc[34]) {  /* Env Polarity */
                    g_synth.vcf_env_polarity = (d2 >= 64u);
                    dirty_screen = 1;
                } else if (g_param_cc[3] != 0xFFu && d1 == g_param_cc[3]) {    /* LFO Rate */
                    g_synth.lfo_rate = d2;
                    dirty_screen = 1;
                } else if (g_param_cc[4] != 0xFFu && d1 == g_param_cc[4]) {    /* LFO Delay */
                    g_synth.lfo_delay = d2;
                    dirty_screen = 1;
                } else if (g_param_cc[9] != 0xFFu && d1 == g_param_cc[9]) {    /* HPF Mode */
                    g_synth.hpf.mode = (d2 / 32u) > 3 ? 3 : (d2 / 32u);
                    dirty_screen = 1;
                } else if (g_param_cc[27] != 0xFFu && d1 == g_param_cc[27]) {  /* Chorus Mode */
                    g_synth.chorus.mode = d2 < 32u ? 0u : d2 < 64u ? 1u : d2 < 96u ? 2u : 3u;
                    dirty_screen = 1;
                } else if (g_param_cc[0] != 0xFFu && d1 == g_param_cc[0]) {    /* Bender DCO */
                    g_synth.bender_dco = d2;
                    dirty_screen = 1;
                } else if (g_param_cc[1] != 0xFFu && d1 == g_param_cc[1]) {    /* Bender VCF */
                    g_synth.bender_vcf = d2;
                    dirty_screen = 1;
                } else if (g_param_cc[2] != 0xFFu && d1 == g_param_cc[2]) {    /* Arp Rate */
                    g_synth.arp_rate = d2;
                    dirty_screen = 1;
                } else if (g_param_cc[30] != 0xFFu && d1 == g_param_cc[30]) {  /* Arp Mode */
                    g_synth.arp_mode = (d2 / 42u > 2u) ? 2u : (d2 / 42u);
                    dirty_screen = 1;
                } else if (g_param_cc[31] != 0xFFu && d1 == g_param_cc[31]) {  /* Arp Range */
                    g_synth.arp_range = (d2 >= 85u) ? 3u : (d2 >= 42u ? 2u : 1u);
                    dirty_screen = 1;
                } else if (g_param_cc[36] != 0xFFu && d1 == g_param_cc[36]) {  /* Tape Type */
                    fm106_cc_fx(&g_synth.fx[0], 0, d2);
                    dirty_screen = 1;
                } else if (g_param_cc[37] != 0xFFu && d1 == g_param_cc[37]) {  /* Tape Mix */
                    fm106_cc_fx(&g_synth.fx[0], 1, d2);
                    dirty_screen = 1;
                } else if (g_param_cc[38] != 0xFFu && d1 == g_param_cc[38]) {  /* Tape Rate */
                    fm106_cc_fx(&g_synth.fx[0], 2, d2);
                    dirty_screen = 1;
                } else if (g_param_cc[39] != 0xFFu && d1 == g_param_cc[39]) {  /* Tape Char */
                    fm106_cc_fx(&g_synth.fx[0], 3, d2);
                    dirty_screen = 1;
                } else if (g_param_cc[40] != 0xFFu && d1 == g_param_cc[40]) {  /* Voice Assign Mode */
                    uint8_t vm = (d2 < 43u) ? FM106_MODE_MONO : (d2 < 86u) ? FM106_MODE_POLY1 : FM106_MODE_POLY2;
                    fm106_set_voice_mode(&g_synth, vm);
                    set.voice_mode = g_synth.voice_mode;
                    dirty_save = 1;
                    t_change = fm1_ms;
                    fm106_set_banner("ASSIGN ", N_POLY[g_synth.voice_mode]);
                    dirty_screen = 1;
                } else if (d1 == 99u || d1 == 100u || d1 == 101u) {
                    /* step accent / slide / ratchet on the edit step */
                    seq_ui_cc_flag(d1 - 99u, d2);
                } else if (g_seq_cc[0] != 0xFFu && d1 == g_seq_cc[0]) {        /* Seq BPM */
                    uint32_t bpm = (uint32_t)d2 * 2u + 40u;
                    g_synth.seq.bpm = (uint8_t)(bpm > 240u ? 240u : bpm);
                    dirty_screen = 1;
                } else if (g_seq_cc[1] != 0xFFu && d1 == g_seq_cc[1]) {        /* Seq Div */
                    g_synth.seq.sdiv = d2 > 9u ? 3u : d2;
                    dirty_screen = 1;
                } else if (g_seq_cc[2] != 0xFFu && d1 == g_seq_cc[2]) {        /* Seq Len */
                    fm1_irq_off();
                    g_synth.seq.slen = (uint16_t)((uint32_t)d2 + 1u);
                    if (g_seq_cursor >= g_synth.seq.slen) g_seq_cursor = (uint16_t)(g_synth.seq.slen - 1u);
                    fm1_irq_on();
                    dirty_screen = 1;
                } else if (g_seq_cc[3] != 0xFFu && d1 == g_seq_cc[3]) {        /* Seq Gate */
                    g_synth.seq.sgate = d2 ? d2 : 1u;
                    dirty_screen = 1;
                } else if (g_seq_cc[4] != 0xFFu && d1 == g_seq_cc[4]) {        /* Seq Swing */
                    g_synth.seq.swing = d2 > 100u ? 100u : d2;
                    dirty_screen = 1;
                } else if (g_seq_cc[5] != 0xFFu && d1 == g_seq_cc[5]) {        /* Seq Transport */
                    seq_transport_cmd(d2 == 0u ? 0u : d2 == 1u ? 1u : d2 == 2u ? 2u : 3u);
                } else if (d1 == 108u) {        /* Record arm */
                    g_seq_rec_arm = d2 >= 64u;
                    dirty_screen = 1;
                } else if (d1 == 109u && d2 >= 64u) {  /* Clear all p-locks */
                    fm1_irq_off();
                    fm106_plock_clear(&g_synth.seq);
                    fm1_irq_on();
                } else if (d1 == 110u) {        /* Edit cursor step */
                    g_seq_cursor = (uint16_t)((uint32_t)d2 % (g_synth.seq.slen ? g_synth.seq.slen : 1u));
                    dirty_screen = 1;
                } else if (d1 == 111u) {        /* Edit cursor pitch row (1..127) */
                    uint8_t row = d2 < 1u ? 1u : d2;
                    g_seq_row = row;
                    if (g_seq_row < g_seq_view_lo) g_seq_view_lo = g_seq_row;
                    if (g_seq_row > g_seq_view_lo + SEQ_ROWS - 1u) g_seq_view_lo = g_seq_row - SEQ_ROWS + 1u;
                    if (g_seq_view_lo > 114u) g_seq_view_lo = 114u;
                    dirty_screen = 1;
                } else if (d1 == 112u && d2 >= 64u) {   /* Clear cursor step */
                    seq_ui_clear_step();
                    dirty_screen = 1;
                } else if (d1 == 113u) {        /* Sync mode: 0 off, 1 master, 2 slave */
                    g_synth.sync_mode = (uint8_t)(d2 % 3u);
                    seq_sync_apply();
                    dirty_save = 1;
                    dirty_screen = 1;
                } else if (d1 == 119) {  /* Debug telemetry inquiry */
                    uint8_t tch = (uint8_t)(0xB0u | ((g_synth.midi_out_ch - 1u) & 0x0Fu));
                    midi_send(tch, 114, (uint8_t)(uac.alt & 0x7Fu));
                    midi_send(tch, 115, (uint8_t)(uac.starts & 0x7Fu));
                    midi_send(tch, 116, (uint8_t)(uac.stalls & 0x7Fu));
                    midi_send(tch, 117, (uint8_t)(uac.rate_sets & 0x7Fu));
                    midi_send(tch, 118, (uint8_t)(uac.flowing & 0x7Fu));
                    midi_send(tch, 119, (uint8_t)g_cpu0_load_pct);
                    midi_send(tch, 120, (uint8_t)g_cpu1_load_pct);
                    midi_send(tch, 121, (uint8_t)(g_cpu0_ticks / 1000u));
                    midi_send(tch, 122, (uint8_t)(g_cpu1_ticks / 1000u));
                    midi_send(tch, 123, (uint8_t)(uac.overruns & 0x7Fu));
                    midi_send(tch, 124, (uint8_t)(uac.underruns & 0x7Fu));
#if DINGHY_DUAL_CORE
                    midi_send(tch, 125, (uint8_t)(audio_worker.timeouts & 0x7Fu));
#else
                    midi_send(tch, 125, 0);
#endif
                    midi_send(tch, 126, (uint8_t)(uac.missed & 0x7Fu));
                    midi_send(tch, 127, (uint8_t)(audio_lates & 0x7Fu));
                } else if (d1 == 64) {   /* Sustain pedal */
                    fm1_irq_off();
#if DINGHY_DUAL_CORE
                    if (audio_worker_online) audio_worker_join();
#endif
                    uint8_t old_hold = g_synth.hold_pedal;
                    g_synth.hold_pedal = (d2 >= 64);
                    if (old_hold && !g_synth.hold_pedal) {
                        for (uint32_t vi = 0; vi < FM106_NUM_VOICES; vi++) {
                            if (!g_synth.voice[vi].gate) {
                                fm106_adsr_gate_off(&g_synth.voice[vi].adsr);
                            }
                        }
                    }
                    fm1_irq_on();
                } else if (d1 == 120u) { /* All Sound Off */
                    fm1_irq_off();
#if DINGHY_DUAL_CORE
                    if (audio_worker_online) audio_worker_join();
#endif
                    fm106_all_sound_off(&g_synth);
                    fm1_irq_on();
                    dirty_screen = 1;
                } else if (d1 == 121u) { /* Reset All Controllers */
                    fm1_irq_off();
#if DINGHY_DUAL_CORE
                    if (audio_worker_online) audio_worker_join();
#endif
                    g_synth.hold_pedal = 0;
                    g_synth.pitch_bend_val = 0;
                    fm1_irq_on();
                    dirty_screen = 1;
                } else if (d1 == 123u) { /* All Notes Off */
                    fm1_irq_off();
#if DINGHY_DUAL_CORE
                    if (audio_worker_online) audio_worker_join();
#endif
                    fm106_all_notes_off(&g_synth);
                    fm1_irq_on();
                    dirty_screen = 1;
                }
            }
        }

        /* 5. Voice LEDs + keyboard lit tracking */
        uint32_t voice_active = 0;
        for (k = 0; k < FM106_NUM_VOICES; k++) {
            if (g_synth.voice[k].gate || g_synth.voice[k].adsr.stage != FM106_ENV_IDLE)
                voice_active |= 1u << k;
        }
        lit = 0;
        for (k = 0; k < FM106_NUM_VOICES; k++) {
            if (voice_active & (1u << k)) {
                if (g_synth.voice[k].note >= base && g_synth.voice[k].note < base + 27u)
                    lit |= 1u << (g_synth.voice[k].note - base);
            }
        }
        for (k = 0; k < 27u; k++) {
            fm1_led_key(KEY_LED(k), (lit >> k) & 1u);
        }
        fm1_led_key(BTN_OCT_DOWN, g_synth.octave_shift < 0);
        fm1_led_key(BTN_OCT_UP, g_synth.octave_shift > 0);
        fm1_led_key(BTN_FX, g_synth.chorus.mode != FM106_CHORUS_OFF ||
                            g_synth.fx[0].type != FM106_FX_OFF);
        fm1_led_key(BTN_ARP, g_synth.arp_on);
        fm1_led_key(BTN_SEQ, g_synth.seq.playing);
        fm1_led_key(BTN_PLAY, g_synth.seq.playing);
        fm1_led_key(BTN_SAVE, (g_ui_mode == UI_MODE_SAVE));
        fm1_led_key(BTN_REC, g_seq_rec_arm);

        if (g_banner_msg[0] != '\0' && fm1_ms >= g_banner_until) {
            g_banner_msg[0] = '\0';
            dirty_screen = 1;
        }

        if (g_dirty_screen) {
            dirty_screen = 1;
            g_dirty_screen = 0;
        }

        /* 6. Screen Redraws */
        static uint32_t t_last_top = 0;
        static uint32_t t_last_batt_chk = 0;
        if (fm1_ms - t_last_batt_chk >= 3000u) {
            t_last_batt_chk = fm1_ms;
            dirty_screen = 1;
        }
        static uint8_t prev_ui_mode = UI_MODE_NORMAL;
        static uint8_t prev_page = 0xFF;
        int ui_mode_changed = (g_ui_mode != prev_ui_mode);
        int page_changed = (g_current_page != prev_page);
        prev_ui_mode = g_ui_mode;
        prev_page = g_current_page;
        int voice_changed = (voice_active != prev_voice_active);

        if (dirty_screen) {
            if (g_ui_mode == UI_MODE_MENU) {
                draw_menu_main();
            } else if (g_ui_mode == UI_MODE_MIDI) {
                draw_menu_midi();
            } else if (g_ui_mode == UI_MODE_CC_MAP) {
                draw_menu_cc_map();
            } else if (g_ui_mode == UI_MODE_ABOUT) {
                draw_menu_about();
            } else if (g_ui_mode == UI_MODE_SAVE) {
                draw_save_menu();
            } else if (g_current_page == PAGE_SEQ) {
                if (g_seq_menu_on) draw_seq_menu();
                else if (g_note_menu_on) draw_note_menu();
                else draw_seq_page();
            } else {
                draw_top_section(g_synth.patch_idx, g_synth.patch_name);
                draw_faders_section(g_current_page);
                if (ui_mode_changed || page_changed) {
                    draw_waveform_section();
                }
            }
            dirty_screen = 0;
            t_last_top = fm1_ms;
            prev_voice_active = voice_active;
        } else if (voice_changed && (fm1_ms - t_last_top >= 50u) && (g_ui_mode == UI_MODE_NORMAL)
                   && g_current_page != PAGE_SEQ) {   /* SEQ page owns its header — never overdraw it */
            t_last_top = fm1_ms;
            prev_voice_active = voice_active;
            draw_top_section(g_synth.patch_idx, g_synth.patch_name);
        }

        /* SEQ page: periodic redraw for playhead — roll bands only (fast), and
         * follow the playhead when the pattern is longer than the window. */
        if ((g_ui_mode == UI_MODE_NORMAL) && (g_current_page == PAGE_SEQ) && !g_seq_menu_on && !g_note_menu_on) {
            fm106_seq_t *sq = &g_synth.seq;
            if (sq->playing) {
                uint16_t ph_idx = seq_ph_idx(sq);
                if (ph_idx < g_seq_view_step ||
                    ph_idx >= (uint16_t)(g_seq_view_step + 16u)) {
                    int32_t vs = (int32_t)ph_idx - 8;
                    int32_t len = sq->slen ? (int32_t)sq->slen : 1;
                    if (vs > len - 16) vs = len - 16;
                    if (vs < 0) vs = 0;
                    g_seq_view_step = (uint16_t)vs;
                }
            }
            if ((sq->playing && fm1_ms - t_last_seq >= 40u) || g_seq_dirty) {
                t_last_seq = fm1_ms;
                g_seq_dirty = 0;
                int32_t ph = sq->playing ? (int32_t)seq_ph_idx(sq) : -1; /* one snapshot: both bands agree */
                draw_seq_roll_band(0, 8, 22, ph);
                draw_seq_roll_band(8, 14, 118, ph);
            }
        }

        /* Continuous oscilloscope refresh in performance mode */
        if (g_ui_mode == UI_MODE_NORMAL && g_current_page != PAGE_SEQ) {
            uint32_t scope_active = (voice_active != 0) || !synth_quiet();
            if ((scope_active && (fm1_ms - t_last_scope >= 50u)) ||
                (!scope_active && scope_was_active && (fm1_ms - t_last_scope >= 50u))) {
                t_last_scope = fm1_ms;
                scope_was_active = (uint8_t)scope_active;
                draw_waveform_section();
            }
        }

        /* 7. Flash Settings Persistence (only when synth is completely silent) */
        if (dirty_save && synth_quiet() && (fm1_ms - t_change > 1000u)) {
            /* menu edits live in g_synth; mirror them into set before the flush
             * (the old code never did — MIDI menu changes were lost on reboot) */
            set.midi_channel = g_synth.midi_channel;
            set.midi_out_ch  = g_synth.midi_out_ch;
            set.midi_local   = g_synth.midi_local;
            set.pc_in        = g_synth.pc_in;
            set.sync_mode    = g_synth.sync_mode;
            set.voice_mode   = g_synth.voice_mode;
            memcpy(set.param_cc, g_param_cc, sizeof(g_param_cc));
            memcpy(set.seq_cc, g_seq_cc, sizeof(g_seq_cc));
            settings_save(&set, sizeof(set));
            dirty_save = 0;
        }
}

#ifndef FM106_WEB
static void app_main(void)
{
    fm106_app_init();
    for (;;) {
        fm106_app_frame();
    }
}
#endif

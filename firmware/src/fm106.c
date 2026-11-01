/* SPDX-License-Identifier: GPL-3.0-only */
/* FM-106: Authentic 6-Voice Roland Juno-106 Synthesizer Emulator
 * Ported from ultramaster_kr106 DSP with dual-core acceleration,
 * 1980s Roland styling, 128 factory presets, and MN3009 BBD Stereo Chorus. */

#define APP_NAME "FM-106"
#define FELUCCA_ID "FM-1_907"

#include "platform.c"
#if FELUCCA_BLE
#include "ble/ble_core.h"
#endif
#include "fm106_engine.h"

/* UI Modes */
enum {
    UI_MODE_NORMAL = 0,
    UI_MODE_MENU,
    UI_MODE_MIDI,
    UI_MODE_CC_MAP,
#if FELUCCA_BLE
    UI_MODE_BLUETOOTH,
    UI_MODE_BT_SCAN,
#endif
    UI_MODE_ABOUT,
    UI_MODE_SAVE,
    UI_MODE_COUNT
};

static uint8_t g_ui_mode = UI_MODE_NORMAL;
#if FELUCCA_BLE
static uint32_t g_bt_scan_sel = 0;
static uint8_t g_bt_device_menu_on = 0;
static uint8_t g_bt_device_menu_sel = 0;
#define BT_DEV_MENU_COUNT 4
#endif

#include "fm106_sysex.h"
#if FELUCCA_BLE
#include "bt_glue.h"
#include "core_ble_api.c"
#endif

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
    PAGE_LFO,           /* RATE DELAY PWM MD ASSIGN */
    PAGE_ARP,           /* ON MODE RATE RANGE */
    PAGE_FX,            /* CHO HPF VCA LEVEL */
    PAGE_TAPE,          /* TAPE: TYPE MIX RATE CHAR */
    PAGE_REVERB,        /* SHIMMER: REV MIX DECAY SHIM */
    PAGE_SEQ,           /* SEQ: STEP PITCH GATE VEL (piano-roll edit) */
    PAGE_COUNT = 11
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
    /* PAGE_REVERB: 1980s stereo shimmer reverb (post-chain) */
    {{"REV", SLOT_MODE, 2, N_BOOL}, {"MIX", SLOT_FADER, 0, 0},
     {"DECAY", SLOT_FADER, 0, 0},   {"SHIM", SLOT_FADER, 0, 0}},
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
    case PAGE_REVERB: {
        const fm106_reverb_t *rv = &g_synth.reverb;
        switch (i) {
        case 0: return rv->on & 1u;
        case 1: return rv->mix;
        case 2: return rv->decay;
        case 3: return rv->shimmer;
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

static uint8_t g_menu_sel = 0;
static uint8_t g_midi_sel = 0;
#if FELUCCA_BLE
static uint8_t g_ble_sel = 0;
#endif

static uint8_t g_save_slot = 0;          /* Target user slot (0..127 -> UA11..UB88) */
static char g_save_name[16];             /* 15 chars + null */
static uint8_t g_save_cursor = 0;        /* 0..14 */
static uint8_t g_save_del_confirm = 0;   /* 0 = normal, 1 = "Are you sure?" */
static uint32_t t_save_down = 0;         /* Timestamp when BTN_SAVE was pressed */
static uint8_t save_held_active = 0;     /* 1 if long press triggered UI_MODE_SAVE */
static char g_banner_msg[32] = {0};      /* Screen notification banner */
static uint32_t g_banner_until = 0;      /* Expiry timestamp for banner */
static void fm106_set_banner(const char *prefix, const char *suffix);

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
static uint8_t  g_seq_flash_dirty = 0;      /* pattern edited; auto-saved when quiet */
static uint32_t g_t_seq_edit   = 0;         /* ms timestamp of last pattern edit */
static uint8_t  g_seq_step_held = 0;        /* keys held during stopped chord step-entry */
static uint8_t  g_seq_step_active = 0;      /* 1 = actively building chord on cursor step */

static inline void seq_mark_modified(void)
{
    g_seq_dirty = 1;
    g_seq_flash_dirty = 1;
    g_t_seq_edit = fm106_now_ms();
}

/* SEQ options menu (long-press EDIT on the roll) */
static uint8_t  g_seq_menu_on   = 0;
static uint8_t  g_seq_menu_sel  = 0;
static uint32_t t_edit_down    = 0;
static uint8_t  edit_held_active= 0;
static uint32_t t_sel_down     = 0;
static uint8_t  sel_held_active= 0;
#define SEQ_MENU_COUNT 8u

/* note options menu (short-press EDIT on the roll) */
static uint8_t  g_note_menu_on  = 0;
static uint8_t  g_note_menu_sel = 0;
#define NOTE_MENU_COUNT 6u

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

#define CC_MAP_COUNT 46u

typedef struct {
    const char *group;
    const char *name;
    uint8_t default_cc;
    uint8_t param_idx;  /* index in g_param_cc (0..48) or 0xFF if not in g_param_cc */
} cc_entry_def_t;

static const cc_entry_def_t CC_ENTRIES[CC_MAP_COUNT] = {
    /* VCF */
    {"VCF", "CUTOFF",     74, 10},
    {"VCF", "RESONANCE",  71, 11},
    {"VCF", "ENV MOD",    76, 12},
    {"VCF", "LFO MOD",    77, 13},
    {"VCF", "KBD TRACK",  78, 14},
    {"VCF", "HPF MODE",   12,  9},
    {"VCF", "ENV POLAR",  97, 34},

    /* ENV & VCA */
    {"ENV", "ATTACK",     73, 16},
    {"ENV", "DECAY",      75, 17},
    {"ENV", "SUSTAIN",    70, 18},
    {"ENV", "RELEASE",    72, 19},
    {"VCA", "LEVEL",      11, 15},
    {"VCA", "VCA MODE",   98, 35},

    /* DCO */
    {"DCO", "LFO DEPTH",   1,  5},
    {"DCO", "PWM DEPTH",  79,  6},
    {"DCO", "PWM MODE",   96, 33},
    {"DCO", "SUB LEVEL",   9,  7},
    {"DCO", "NOISE",      13,  8},
    {"DCO", "RANGE",      95, 29},
    {"DCO", "PULSE SW",   91, 23},
    {"DCO", "SAW SW",     92, 24},
    {"DCO", "SUB SW",     94, 25},

    /* LFO */
    {"LFO", "RATE",       80,  3},
    {"LFO", "DELAY",      81,  4},

    /* FX */
    {"FX",  "CHORUS",     93, 27},
    {"FX",  "TAPE TYPE",  83, 36},
    {"FX",  "TAPE MIX",   84, 37},
    {"FX",  "TAPE RATE",  85, 38},
    {"FX",  "TAPE CHAR",  86, 39},
    {"FX",  "REV ON",     20, 45},
    {"FX",  "REV MIX",    21, 46},
    {"FX",  "REV DECAY",  22, 47},
    {"FX",  "REV SHIM",   23, 48},

    /* PERF & ARP */
    {"PRF", "BEND DCO",   87,  0},
    {"PRF", "BEND VCF",   88,  1},
    {"ARP", "ARP RATE",   82,  2},
    {"ARP", "ARP MODE",   89, 30},
    {"ARP", "ARP RANGE",  90, 31},
    {"PRF", "ASSIGN",     68, 49},

    /* SEQUENCER */
    {"SEQ", "TEMPO",     102, 0xFF},
    {"SEQ", "DIVISION",  103, 0xFF},
    {"SEQ", "LENGTH",    104, 0xFF},
    {"SEQ", "GATE",      105, 0xFF},
    {"SEQ", "SWING",     106, 0xFF},
    {"SEQ", "PLAY/STOP", 107, 0xFF},
    {"SEQ", "REC ARM",   108, 0xFF},
};

/* Mutable param -> CC for TX (plock_scan) */
static uint8_t g_param_cc[FM106_NPARAM];
/* Reverse lookup: CC (0..127) -> CC_ENTRIES index (0..44) or 0xFF */
static uint8_t g_cc_to_entry[128];

/* CC Configuration UI State */
static uint8_t g_cc_menu_sel = 0;
static uint8_t g_cc_scroll_top = 0;
static uint8_t g_cc_learn_active = 0;

/* Save / load settings */
typedef struct {
    int8_t octave;
    uint8_t patch;
    uint8_t midi_channel;   /* 0 = All (omni), 1..16 */
    uint8_t midi_out_ch;    /* 1..16: channel for everything we send */
    uint8_t midi_local;     /* 1 = panel keys play engine, 0 = send-only */
    uint8_t pc_in;          /* 1 = accept program change */
    uint8_t sync_mode;      /* 0 = off, 1 = master, 2 = slave */
    uint8_t cc_map[CC_MAP_COUNT];
} fm106_settings_t;

static fm106_settings_t set;

static void fm106_cc_map_init_defaults(void)
{
    for (uint32_t i = 0; i < CC_MAP_COUNT; i++) {
        set.cc_map[i] = CC_ENTRIES[i].default_cc;
    }
}

static void fm106_cc_map_update(void)
{
    for (uint32_t c = 0; c < 128u; c++) {
        g_cc_to_entry[c] = 0xFFu;
    }
    for (uint32_t p = 0; p < FM106_NPARAM; p++) {
        g_param_cc[p] = 0xFFu;
    }
    for (uint32_t i = 0; i < CC_MAP_COUNT; i++) {
        uint8_t cc = set.cc_map[i];
        if (cc < 128u) {
            g_cc_to_entry[cc] = (uint8_t)i;
            uint8_t pidx = CC_ENTRIES[i].param_idx;
            if (pidx < FM106_NPARAM) {
                g_param_cc[pidx] = cc;
                if (pidx == 27u) {
                    g_param_cc[28u] = cc; /* Chorus I & II share CC */
                }
            }
        }
    }
}

/* param raw value -> full-range CC out (modes/flags stretched bijectively for faders) */
static uint8_t param_cc_value(uint8_t i, uint8_t v)
{
    switch (i) {
    case 9: case 29: case 30: return (uint8_t)(v * 42u);            /* 0..3 / 0..2 enums */
    case 31: return v <= 1u ? 0u : (v == 2u ? 64u : 127u);          /* arp range 1..3 */
    case 36: return v ? 127u : 0u;                                  /* fx type */
    case 49: return (uint8_t)(v * 63u);                             /* assign mode 0..2 */
    case 23: case 24: case 25: case 27: case 28:
    case 33: case 34: case 35: case 45: return v ? 127u : 0u;       /* flags */
    default: return v;
    }
}

/* CC value (0-127) -> param raw value on receive */
static uint8_t cc_to_param_value(uint8_t idx, uint8_t v)
{
    switch (idx) {
    case 9:  return v / 32u > 3u ? 3u : v / 32u;                    /* hpf mode zones */
    case 23: case 24: case 25: case 33: case 34: case 35:
        return (uint8_t)(v >= 64u);
    case 29: return v / 42u > 2u ? 2u : v / 42u;                    /* dco range 0-2 */
    case 30: return v >= 84u ? 2u : (v >= 42u ? 1u : 0u);           /* arp mode 0-2 */
    case 31: return v >= 85u ? 3u : (v >= 42u ? 2u : 1u);           /* arp range 1-3 */
    case 49: return v >= 85u ? 2u : (v >= 42u ? 1u : 0u);           /* assign mode 0-2 */
    default: return v;
    }
}

/* ---- Panel TX Self-Echo Guard ----
 * Prevents DAW MIDI Thru from echoing panel-originated CC/PC bursts back into
 * FM-106 while turning physical encoders/knobs (eliminating value ping-pong). */
#define MIDI_ECHO_GUARD_MS 250u
static uint32_t g_cc_tx_ms[128];
static uint32_t g_cc_tx_armed[4];
static uint32_t g_pc_tx_ms;
static uint8_t  g_pc_tx_armed;

static inline void panel_send_cc(uint8_t ch_st, uint8_t cc, uint8_t val)
{
    uint8_t c = cc & 0x7Fu;
    g_cc_tx_ms[c] = fm106_now_ms();
    g_cc_tx_armed[c >> 5] |= (1u << (c & 31u));
    midi_send(ch_st, c, val);
}

static inline void panel_send_pc(uint8_t ch_st, uint8_t pc)
{
    g_pc_tx_ms = fm106_now_ms();
    g_pc_tx_armed = 1u;
    midi_send(ch_st, pc & 0x7Fu, 0u);
}

static inline int cc_is_self_echo(uint8_t cc)
{
    uint8_t c = cc & 0x7Fu;
    uint32_t mask = 1u << (c & 31u);
    if (!(g_cc_tx_armed[c >> 5] & mask)) return 0;
    if ((uint32_t)(fm106_now_ms() - g_cc_tx_ms[c]) < MIDI_ECHO_GUARD_MS) return 1;
    g_cc_tx_armed[c >> 5] &= ~mask;
    return 0;
}

static inline int pc_is_self_echo(void)
{
    if (!g_pc_tx_armed) return 0;
    if ((uint32_t)(fm106_now_ms() - g_pc_tx_ms) < MIDI_ECHO_GUARD_MS) return 1;
    g_pc_tx_armed = 0u;
    return 0;
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

/* transport: 0=stop, 1=start, 2=continue, 3=toggle (BTN_PLAY / CC 107) */
static void seq_transport_cmd(uint8_t cmd, int send_midi_out)
{
    fm106_seq_t *s = &g_synth.seq;
    uint8_t was = s->playing;
    count_in_cancel();             /* any transport command aborts a pending count-in */
    g_seq_step_active = 0;
    g_seq_step_held = 0;
    if (cmd == 0u || (cmd == 3u && was)) {
        if (was) {
            uint8_t nflush = s->seq_n;
            uint8_t flushed[4];
            for (uint8_t i = 0; i < nflush; i++) flushed[i] = s->seq_notes[i];
            uint8_t och = (g_synth.midi_out_ch - 1u) & 0x0Fu;
            fm1_irq_off();
            for (uint8_t i = 0; i < s->seq_n; i++) fm106_note_off(&g_synth, s->seq_notes[i]);
            s->seq_n = 0;
            s->seq_hold = 0;
            s->playing = 0;
            s->msg_tail = s->msg_head;  /* drop queued seq MIDI events */
            fm1_irq_on();
            /* release any externally-sounding seq notes (main loop → midi_send ok) */
            for (uint8_t i = 0; i < nflush; i++) midi_send(0x80u | och, flushed[i], 0u);
            if (send_midi_out) {
                seq_clock_out(0xFCu);
                uint8_t tr_cc = set.cc_map[43];
                if (tr_cc < 128u) panel_send_cc(0xB0u | och, tr_cc, 0u);
            }
        }
    } else if (cmd == 2u) {         /* continue: resume in place */
        if (!was) {
            s->playing = 1;
            if (send_midi_out) {
                seq_clock_out(0xFBu);
                uint8_t tr_cc = set.cc_map[43];
                if (tr_cc < 128u) panel_send_cc(0xB0u | ((g_synth.midi_out_ch - 1u) & 0x0Fu), tr_cc, 1u);
            }
        }
    } else if (cmd == 1u || (cmd == 3u && !was)) {
        fm1_irq_off();
        fm106_param_snapshot(&g_synth); /* p-lock diff starts from the live sound */
        fm106_seq_start(s);
        fm1_irq_on();
        if (send_midi_out) {
            seq_clock_out(0xFAu);
            uint8_t tr_cc = set.cc_map[43];
            if (tr_cc < 128u) panel_send_cc(0xB0u | ((g_synth.midi_out_ch - 1u) & 0x0Fu), tr_cc, 1u);
        }
    }
    g_seq_dirty = 1;
    g_dirty_screen = 1;
}

static void count_in_start(void)
{
    g_count_in = 4;
    g_count_next_ms = fm106_now_ms();  /* first click on the next loop pass */
    g_dirty_screen = 1;
}

static void count_in_cancel(void)
{
    if (g_count_in) g_dirty_screen = 1;
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
        seq_transport_cmd(1u, 1);
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

static void fm106_apply_cc(uint8_t entry, uint8_t d2)
{
    switch (entry) {
    /* VCF */
    case 0:
        g_synth.vcf_cutoff = d2;
        break;
    case 1:
        g_synth.vcf_resonance = d2;
        break;
    case 2:
        g_synth.vcf_env_mod = d2;
        break;
    case 3:
        g_synth.vcf_lfo_mod = d2;
        break;
    case 4:
        g_synth.vcf_kbd_track = d2;
        break;
    case 5:
        g_synth.hpf.mode = (d2 / 32u) > 3 ? 3 : (d2 / 32u);
        break;
    case 6:
        fm106_param_set(&g_synth, 34, cc_to_param_value(34, d2));
        break;

    /* ENV / VCA */
    case 7:
        g_synth.env_a = d2;
        for (uint32_t vi = 0; vi < FM106_NUM_VOICES; vi++)
            fm106_adsr_params(&g_synth.voice[vi].adsr, g_synth.env_a, g_synth.env_d, g_synth.env_s, g_synth.env_r);
        break;
    case 8:
        g_synth.env_d = d2;
        for (uint32_t vi = 0; vi < FM106_NUM_VOICES; vi++)
            fm106_adsr_params(&g_synth.voice[vi].adsr, g_synth.env_a, g_synth.env_d, g_synth.env_s, g_synth.env_r);
        break;
    case 9:
        g_synth.env_s = d2;
        for (uint32_t vi = 0; vi < FM106_NUM_VOICES; vi++)
            fm106_adsr_params(&g_synth.voice[vi].adsr, g_synth.env_a, g_synth.env_d, g_synth.env_s, g_synth.env_r);
        break;
    case 10:
        g_synth.env_r = d2;
        for (uint32_t vi = 0; vi < FM106_NUM_VOICES; vi++)
            fm106_adsr_params(&g_synth.voice[vi].adsr, g_synth.env_a, g_synth.env_d, g_synth.env_s, g_synth.env_r);
        break;
    case 11:
        g_synth.vca_level = d2;
        break;
    case 12:
        fm106_param_set(&g_synth, 35, cc_to_param_value(35, d2));
        break;

    /* DCO */
    case 13:
        g_synth.dco_lfo_depth = d2;
        break;
    case 14:
        g_synth.dco_pwm_depth = d2;
        break;
    case 15:
        fm106_param_set(&g_synth, 33, cc_to_param_value(33, d2));
        break;
    case 16:
        g_synth.dco_sub_level = d2;
        g_synth.dco_sub_on = d2 > 0 ? 1u : 0u;
        for (uint32_t vi = 0; vi < FM106_NUM_VOICES; vi++) {
            g_synth.voice[vi].dco.sub_level = d2;
            g_synth.voice[vi].dco.sub_on = g_synth.dco_sub_on;
        }
        break;
    case 17:
        g_synth.dco_noise_level = d2;
        for (uint32_t vi = 0; vi < FM106_NUM_VOICES; vi++)
            g_synth.voice[vi].dco.noise_level = d2;
        break;
    case 18:
        fm106_param_set(&g_synth, 29, cc_to_param_value(29, d2));
        break;
    case 19:
        fm106_param_set(&g_synth, 23, cc_to_param_value(23, d2));
        break;
    case 20:
        fm106_param_set(&g_synth, 24, cc_to_param_value(24, d2));
        break;
    case 21:
        fm106_param_set(&g_synth, 25, cc_to_param_value(25, d2));
        break;

    /* LFO */
    case 22:
        g_synth.lfo_rate = d2;
        break;
    case 23:
        g_synth.lfo_delay = d2;
        break;

    /* FX */
    case 24:
        g_synth.chorus.mode = d2 < 32u ? 0u : d2 < 64u ? 1u : d2 < 96u ? 2u : 3u;
        break;
    case 25:
        fm106_cc_fx(&g_synth.fx[0], 0, d2);
        break;
    case 26:
        fm106_cc_fx(&g_synth.fx[0], 1, d2);
        break;
    case 27:
        fm106_cc_fx(&g_synth.fx[0], 2, d2);
        break;
    case 28:
        fm106_cc_fx(&g_synth.fx[0], 3, d2);
        break;
    case 29:
        g_synth.reverb.on = (d2 >= 64u) ? 1u : 0u;
        break;
    case 30:
        g_synth.reverb.mix = d2;
        break;
    case 31:
        g_synth.reverb.decay = d2;
        break;
    case 32:
        g_synth.reverb.shimmer = d2;
        break;

    /* PERF & ARP */
    case 33:
        fm106_param_set(&g_synth, 0, cc_to_param_value(0, d2));
        break;
    case 34:
        fm106_param_set(&g_synth, 1, cc_to_param_value(1, d2));
        break;
    case 35:
        g_synth.arp_rate = d2;
        break;
    case 36:
        fm106_param_set(&g_synth, 30, cc_to_param_value(30, d2));
        break;
    case 37:
        fm106_param_set(&g_synth, 31, cc_to_param_value(31, d2));
        break;
    case 38:
        fm106_param_set(&g_synth, 49, cc_to_param_value(49, d2));
        break;

    /* SEQUENCER */
    case 39: {
        uint32_t bpm = (uint32_t)d2 * 2u + 40u;
        g_synth.seq.bpm = (uint8_t)(bpm > 240u ? 240u : bpm);
        seq_mark_modified();
        break;
    }
    case 40:
        g_synth.seq.sdiv = d2 > 9u ? 3u : d2;
        seq_mark_modified();
        break;
    case 41:
        fm1_irq_off();
        g_synth.seq.slen = (uint16_t)((uint32_t)d2 + 1u);
        if (g_seq_cursor >= g_synth.seq.slen) g_seq_cursor = (uint16_t)(g_synth.seq.slen - 1u);
        fm1_irq_on();
        seq_mark_modified();
        break;
    case 42:
        g_synth.seq.sgate = d2 ? d2 : 1u;
        seq_mark_modified();
        break;
    case 43:
        g_synth.seq.swing = d2 > 100u ? 100u : d2;
        seq_mark_modified();
        break;
    case 44:
        seq_transport_cmd(d2 == 0u ? 0u : d2 == 1u ? 1u : d2 == 2u ? 2u : 3u, 0);
        break;
    case 45:
        g_seq_rec_arm = d2 >= 64u;
        if (!g_seq_rec_arm) count_in_cancel();
        break;
    default:
        break;
    }
    g_dirty_screen = 1;
}

/* ---- Elektron-style p-lock record + CC out ----
 * send_cc_out == 1 after panel knobs/menus: transmits MIDI CCs (with self-echo
 * guard) and records p-locks when REC+PLAY is active.
 * send_cc_out == 0 after midi_get(): updates plock_cache (and records external
 * CC automation as p-locks if REC+PLAY) WITHOUT echoing CCs back to the DAW! */
static void plock_scan(int send_cc_out)
{
    fm106_seq_t *s = &g_synth.seq;
    uint8_t rec = (uint8_t)(g_seq_rec_arm && s->playing);
    uint8_t ch = (uint8_t)(0xB0u | ((g_synth.midi_out_ch - 1u) & 0x0Fu));
    static uint8_t primed = 0;
    static uint8_t seq_cache[5]; /* bpm sdiv slen sgate swing (CC 102-106 out) */
    static const uint8_t chorus_cc[4] = { 0u, 48u, 80u, 127u };
    uint8_t chorus_sent = 0;

    for (uint8_t i = 0; i < FM106_NPARAM; i++) {
        uint8_t v = fm106_param_get(&g_synth, i);
        if (!primed) { s->plock_cache[i] = v; continue; } /* boot: prime silently, no CC dump */
        if (v == s->plock_cache[i]) continue;
        s->plock_cache[i] = v;
        if (send_cc_out && g_param_cc[i] != 0xFFu) {
            if (i == 27u || i == 28u) {
                if (!chorus_sent) {
                    panel_send_cc(ch, g_param_cc[27], chorus_cc[g_synth.chorus.mode & 3u]);
                    chorus_sent = 1u;
                }
            } else {
                panel_send_cc(ch, g_param_cc[i], param_cc_value(i, v));
            }
        }
        if (rec) {
            uint16_t idx = (uint16_t)(seq_ph_idx(s) % (s->slen ? s->slen : 1u)); /* fresh per lock */
            fm1_irq_off();
            fm106_plock_set(s, idx, i, v);
            fm1_irq_on();
            seq_mark_modified();
            g_dirty_screen = 1;
        }
    }
    /* seq track params -> CC out (scaled to match CC in) */
    uint8_t now[5];
    now[0] = (uint8_t)((s->bpm > 40u ? s->bpm - 40u : 0u) / 2u);               /* 102 tempo */
    now[1] = s->sdiv;                                                          /* 103 division */
    now[2] = (uint8_t)(s->slen > 128u ? 127u : (s->slen ? s->slen - 1u : 0u)); /* 104 length */
    now[3] = s->sgate > 127u ? 127u : s->sgate;                                /* 105 gate */
    now[4] = s->swing;                                                         /* 106 swing */
    for (uint8_t j = 0; j < 5u; j++) {
        if (now[j] != seq_cache[j]) {
            uint8_t s_cc = set.cc_map[38u + j];
            if (primed && send_cc_out && s_cc < 128u) panel_send_cc(ch, s_cc, now[j]);
            seq_cache[j] = now[j];
        }
    }
    primed = 1;
}

/* wipe the entire sequence pattern + p-locks (shared by SEL hold and SEQ menu) */
static void seq_ui_clear_all(void)
{
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
    fm106_plock_clear(&g_synth.seq);
    fm1_irq_on();
    g_seq_step_active = 0;
    g_seq_step_held = 0;
    seq_mark_modified();
    fm106_set_banner("SEQ ", "CLEARED");
    g_dirty_screen = 1;
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
    g_seq_step_active = 0;
    g_seq_step_held = 0;
    seq_mark_modified();
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
    seq_mark_modified();
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
        seq_mark_modified();
        fm1_irq_on();
        return;
    }
    if (v > 127) v = 127;
    st->vel = (uint8_t)v;
    seq_mark_modified();
    fm1_irq_on();
}

static void seq_ui_toggle_flag(uint8_t flag)
{
    fm1_irq_off();
    fm106_step_t *st = &g_synth.seq.step[seq_ui_edit_idx()];
    st->flags ^= flag;
    seq_mark_modified();
    fm1_irq_on();
}

/* note options (short-press EDIT): ratchet / tie / accent / slide / prob / p-locks */
static void seq_ui_note_adjust(int32_t d)
{
    if (!d) return;
    fm1_irq_off();
    fm106_seq_t *s = &g_synth.seq;
    uint16_t len = s->slen ? s->slen : 1u;
    uint16_t eidx = seq_ui_edit_idx();
    fm106_step_t *st = &s->step[eidx];
    switch (g_note_menu_sel) {
    case 0: /* RATCHET 1..8 */
        if (d > 0) { if (st->ratchet < 8u) st->ratchet++; }
        else       { if (st->ratchet > 0u) st->ratchet--; }
        break;
    case 1: /* TIE (copy preceding notes if empty so tie bar renders visibly) */
        if (st->time == FM106_ST_TIE) {
            st->time = FM106_ST_NOTE;
        } else {
            st->time = FM106_ST_TIE;
            if (!st->n) {
                fm106_step_t *prev = &s->step[(eidx + len - 1u) % len];
                if (prev->n) {
                    st->n = prev->n > 4u ? 4u : prev->n;
                    for (uint8_t i = 0; i < st->n; i++) st->note[i] = prev->note[i];
                }
            }
        }
        break;
    case 2: /* ACCENT */
        st->flags ^= FM106_SF_ACCENT;
        break;
    case 3: /* SLIDE */
        st->flags ^= FM106_SF_SLIDE;
        break;
    case 4: { /* PROB: MUTE (101), 10%..90%, 100% (0) */
        int32_t pct = (st->probability == 0u) ? 100
                    : (st->probability > 100u ? 0 : (int32_t)st->probability);
        pct = ((pct + 5) / 10) * 10 + (d > 0 ? 10 : -10);
        if (pct < 0) pct = 0;
        if (pct > 100) pct = 100;
        st->probability = (pct == 100) ? 0u : (pct == 0 ? 101u : (uint8_t)pct);
        break;
    }
    case 5: /* P-LOCKS: clear locks on this step */
        if (d < 0 || fm106_plock_count_step(s, eidx) > 0u) {
            fm106_plock_clear_step(s, eidx);
        }
        break;
    }
    seq_mark_modified();
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
    g_seq_step_active = 0;
    g_seq_step_held = 0;
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

/* Key / MIDI note-on entry into the sequencer:
 * - Playing + REC armed: quantized live overdub on ANY page (without flooding
 *   all 39 params into plock[], so live filter/FX knobs stay unlocked!).
 * - Stopped on PAGE_SEQ (without menus): polyphonic chord-aware step entry
 *   (hold 1..4 keys to build a chord on the cursor step; cursor advances +1
 *   when the last held key is released). */
static void seq_note_entry(uint8_t pitch, uint8_t vel)
{
    fm106_seq_t *s = &g_synth.seq;
    if (s->playing && g_seq_rec_arm) {
        uint16_t len = s->slen ? s->slen : 1u;
        uint16_t idx;
        fm1_irq_off();
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
        fm106_step_t *st = &s->step[idx % len];
        if (st->time == FM106_ST_TIE) {
            st->n = 0;
            st->time = FM106_ST_NOTE;
        }
        uint8_t found = 0;
        for (uint8_t i = 0; i < st->n; i++) if (st->note[i] == pitch) { found = 1; break; }
        if (!found && st->n < 4u) {
            st->note[st->n++] = pitch;
            st->time = FM106_ST_NOTE;
            if (!st->vel) st->vel = vel ? vel : 100u;
            g_seq_last_pitch = pitch;
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
        if (pitch < g_seq_view_lo) g_seq_view_lo = pitch;
        if (pitch > g_seq_view_lo + SEQ_ROWS - 1u) g_seq_view_lo = pitch - SEQ_ROWS + 1u;
        if (g_seq_view_lo > 114u) g_seq_view_lo = 114u;
        seq_mark_modified();
        g_dirty_screen = 1;
    } else if (!s->playing && g_ui_mode == UI_MODE_NORMAL &&
               g_current_page == PAGE_SEQ && !g_seq_menu_on && !g_note_menu_on) {
        uint16_t len = s->slen ? s->slen : 1u;
        fm1_irq_off();
        fm106_step_t *st = &s->step[g_seq_cursor % len];
        if (!g_seq_step_active || g_seq_step_held == 0u) {
            /* First key of a new step gesture: overwrite step cleanly */
            st->n = 1u;
            st->note[0] = pitch;
            st->time = FM106_ST_NOTE;
            st->vel = vel ? vel : 100u;
            st->acc = 0u;
            g_seq_step_active = 1u;
            g_seq_step_held = 1u;
        } else {
            /* Additional key held simultaneously: build polyphonic chord (up to 4) */
            if (g_seq_step_held < 32u) g_seq_step_held++;
            uint8_t found = 0;
            for (uint8_t i = 0; i < st->n; i++) if (st->note[i] == pitch) { found = 1; break; }
            if (!found && st->n < 4u) {
                st->note[st->n++] = pitch;
            }
        }
        fm1_irq_on();
        g_seq_row = pitch;
        g_seq_last_pitch = pitch;
        if (pitch < g_seq_view_lo) g_seq_view_lo = pitch;
        if (pitch > g_seq_view_lo + SEQ_ROWS - 1u) g_seq_view_lo = pitch - SEQ_ROWS + 1u;
        if (g_seq_view_lo > 114u) g_seq_view_lo = 114u;
        seq_mark_modified();
        g_dirty_screen = 1;
    }
}

/* Note-off handler for both stopped chord step-entry and live-record gate/TIEs */
static void seq_note_release(uint8_t pitch)
{
    fm106_seq_t *s = &g_synth.seq;
    if (g_seq_step_active && g_seq_step_held > 0u) {
        g_seq_step_held--;
        if (g_seq_step_held == 0u) {
            g_seq_step_active = 0u;
            if (!s->playing && g_ui_mode == UI_MODE_NORMAL &&
                g_current_page == PAGE_SEQ && !g_seq_menu_on && !g_note_menu_on) {
                seq_ui_move_cursor(1);
            }
        }
    }
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
            uint32_t period_ms = base ? (base * 1000u) / 44100u : 0u;
            fm1_irq_off();
            if (!period_ms || held_ms < period_ms) {
                uint8_t acc = 255u;
                if (period_ms) {
                    acc = (uint8_t)((held_ms * 255u) / period_ms);
                    if (!acc) acc = 1u;
                }
                s->step[idx].acc = acc;
            } else {
                uint32_t full = held_ms / period_ms;
                uint32_t rem  = held_ms % period_ms;
                if (full >= len) full = (uint32_t)(len - 1u);
                fm106_step_t *orig = &s->step[idx];
                orig->acc = 255u;
                for (uint32_t k = 1u; k <= full; k++) {
                    fm106_step_t *st = &s->step[(idx + (uint16_t)k) % len];
                    if (st->n && st->time != FM106_ST_TIE) break; /* occupied note: don't clobber */
                    st->time = FM106_ST_TIE;
                    st->acc  = 255u;
                    st->n    = orig->n > 4u ? 4u : orig->n;
                    for (uint8_t ni = 0; ni < st->n; ni++) st->note[ni] = orig->note[ni];
                }
                if (rem && full > 0u && full < len) {
                    fm106_step_t *st = &s->step[(idx + (uint16_t)full) % len];
                    if (st->time == FM106_ST_TIE) {
                        uint8_t acc = (uint8_t)((rem * 255u) / period_ms);
                        if (acc) st->acc = acc;
                    }
                }
            }
            fm1_irq_on();
            seq_mark_modified();
            g_dirty_screen = 1;
            return;
        }
    }
}

/* MIDI CC 99/100/101: accent / slide / ratchet on the edit step */
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
    seq_mark_modified();
    g_dirty_screen = 1;
}

static const char CHAR_SET[] = " ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789*-_./#+!";
#define CHAR_SET_LEN ((int32_t)(sizeof(CHAR_SET) - 1))

#if FELUCCA_BLE
#define MENU_MAIN_COUNT 3
#define MENU_BLE_COUNT  4
#else
#define MENU_MAIN_COUNT 2
#endif
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
    if (tx < 0)
        tx = 0;
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
    g_where = 0xB0;
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
    case PAGE_FX:       title = "FX (CHORUS)";  break;
    case PAGE_TAPE:     title = "FX (TAPE)";    break;
    case PAGE_REVERB:   title = "FX (SHIMMER)"; break;
    case PAGE_SEQ:      title = "SEQUENCER";    break;
    default:            title = "FX (SHIMMER)"; break;
    }

    cv_begin(240, 76, C_BG);

    /* --- Header (y=0..43, 1980s Roland badge directly on chassis grey) --- */
    cv_rect(6, 4, 4, 12, C_ROLAND_RED);
    cv_text(16, 2, &FONT_S, "FM-106", C_STRIPE_CYAN);
#if FELUCCA_BLE
    if (btp.on) {
        uint32_t bst = ble_ui_state();
        int linked = (bst == BLE_ST_SUBSCRIBED || bst == BLE_ST_RECEIVING);
        cv_text(96, 2, &FONT_S, "BT", linked ? C_STRIPE_CYAN : C_TEXT_DIM);
    }
#endif
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

static void fmt_u32_compact(uint32_t v, char *out)
{
    if (v >= 100u) {
        out[0] = (char)('0' + (v / 100u) % 10u);
        out[1] = (char)('0' + (v / 10u) % 10u);
        out[2] = (char)('0' + (v % 10u));
        out[3] = '\0';
    } else if (v >= 10u) {
        out[0] = (char)('0' + (v / 10u));
        out[1] = (char)('0' + (v % 10u));
        out[2] = '\0';
    } else {
        out[0] = (char)('0' + v);
        out[1] = '\0';
    }
}

/* ---- SEQ options menu (long-press EDIT) ---- */
static void seq_ui_menu_adjust(int32_t d)
{
    if (!d) return;
    fm106_seq_t *s = &g_synth.seq;
    if (g_seq_menu_sel == 7u) { /* CLR ALL */
        if (d > 0) {
            seq_ui_clear_all();
            g_seq_menu_on = 0u;
        }
        return;
    }
    fm1_irq_off();
    switch (g_seq_menu_sel) {
    case 0: { /* STEPS (1..128, accelerates smoothly with encoder d) */
        int32_t nl = (int32_t)s->slen + d;
        if (nl < 1) nl = 1;
        if (nl > (int32_t)FM106_NSTEP) nl = (int32_t)FM106_NSTEP;
        s->slen = (uint16_t)nl;
        if (g_seq_cursor >= s->slen) g_seq_cursor = (uint16_t)(s->slen - 1u);
        seq_mark_modified();
        break;
    }
    case 1: { /* BPM (40..240) */
        int32_t nb = (int32_t)s->bpm + d;
        if (nb < 40) nb = 40;
        if (nb > 240) nb = 240;
        s->bpm = (uint8_t)nb;
        seq_mark_modified();
        break;
    }
    case 2: /* DIV */
        if (d > 0) { if (s->sdiv < 9u) s->sdiv++; else s->sdiv = 0u; }
        else       { if (s->sdiv > 0u) s->sdiv--; else s->sdiv = 9u; }
        seq_mark_modified();
        break;
    case 3: { /* GATE (8..128) */
        int32_t ng = (int32_t)s->sgate + (d > 0 ? 4 : -4);
        if (ng < 8) ng = 8;
        if (ng > 128) ng = 128;
        s->sgate = (uint8_t)ng;
        seq_mark_modified();
        break;
    }
    case 4: { /* SWING (0..100) */
        int32_t ns = (int32_t)s->swing + (d > 0 ? 4 : -4);
        if (ns < 0) ns = 0;
        if (ns > 100) ns = 100;
        s->swing = (uint8_t)ns;
        seq_mark_modified();
        break;
    }
    case 5: /* REC arm */
        g_seq_rec_arm = (d > 0) ? 1u : 0u;
        if (!g_seq_rec_arm) count_in_cancel();
        g_seq_dirty = 1;
        break;
    case 6: /* DUP BARS (16->32->64->128, copying steps + p-locks) */
        if (d > 0) {
            uint16_t nl = fm106_seq_duplicate(s);
            if (nl > 0u) {
                char nb[8];
                fmt_u32_compact(nl, nb);
                seq_mark_modified();
                fm106_set_banner("DUP TO ", nb);
            }
        }
        break;
    }
    fm1_irq_on();
}

/* value text for SEQ menu row i */
static void seq_menu_value(uint32_t i, char *val)
{
    fm106_seq_t *s = &g_synth.seq;
    switch (i) {
    case 0:                                     /* STEPS (1..128) */
        fmt_u32_compact(s->slen, val);
        break;
    case 1:                                     /* BPM */
        fmt_u32_compact(s->bpm, val);
        break;
    case 2: {                                   /* DIV */
        uint32_t j = 0;
        while (N_SDIV[s->sdiv][j] && j < 7u) { val[j] = N_SDIV[s->sdiv][j]; j++; }
        val[j] = '\0';
        break;
    }
    case 3:                                     /* GATE */
        fmt_u32_compact(s->sgate, val);
        break;
    case 4:                                     /* SWING */
        fmt_u32_compact(s->swing, val);
        break;
    case 5: {                                   /* REC */
        const char *v = g_seq_rec_arm ? "ON" : "OFF";
        uint32_t j = 0;
        while (v[j]) { val[j] = v[j]; j++; }
        val[j] = '\0';
        break;
    }
    case 6: {                                   /* DUP BARS */
        uint32_t cur = s->slen ? s->slen : 16u;
        if (cur >= FM106_NSTEP) {
            str_cpy(val, "MAX", 8);
        } else {
            uint32_t nxt = cur * 2u > FM106_NSTEP ? FM106_NSTEP : cur * 2u;
            char a[4], b[4];
            fmt_u32_compact(cur, a);
            fmt_u32_compact(nxt, b);
            uint32_t k = 0, j = 0;
            while (a[j] && k < 7u) val[k++] = a[j++];
            if (k < 7u) val[k++] = '>';
            j = 0;
            while (b[j] && k < 7u) val[k++] = b[j++];
            val[k] = '\0';
        }
        break;
    }
    default:                                    /* CLR ALL */
        str_cpy(val, "TURN>", 8);
        break;
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
    cv_text_plate(x, y, 138, buf, c, C_BLACK, 0);
    cv_text_plate(x + 144, y, 76, val, c, C_BLACK, 1);
}

static void draw_seq_fader_guide(void);

static void draw_seq_header(void)
{
    fm106_seq_t *s = &g_synth.seq;
    cv_begin(240, 22, C_BG);
    cv_text(6, 4, &FONT_S, "SEQ", C_ROLAND_AMBER);
    if (g_banner_until > fm1_ms && g_banner_msg[0] != '\0') {
        cv_text(44, 4, &FONT_S, g_banner_msg, C_ROLAND_AMBER);
    } else {
        if (g_count_in)       cv_text(38, 4, &FONT_S, "CNT", C_ROLAND_RED);
        else if (s->playing)  cv_text(38, 4, &FONT_S, "PLAY", C_STRIPE_CYAN);
        else                  cv_text(38, 4, &FONT_S, "STOP", C_TEXT_DIM);

        if (g_seq_rec_arm) {
            cv_rect(76, 3, 28, 15, C_ROLAND_RED);
            cv_text(78, 3, &FONT_S, "REC", C_TEXT_WHITE);
        }

        char buf[16];
        buf[0] = (char)('0' + s->bpm / 100);
        buf[1] = (char)('0' + (s->bpm / 10) % 10);
        buf[2] = (char)('0' + s->bpm % 10);
        buf[3] = '\0';
        cv_text(114, 4, &FONT_S, buf, C_TEXT_WHITE);
        cv_text(142, 4, &FONT_S, "BPM", C_TEXT_DIM);

        buf[0] = (char)('0' + s->swing / 100);
        buf[1] = (char)('0' + (s->swing / 10) % 10);
        buf[2] = (char)('0' + s->swing % 10);
        buf[3] = '\0';
        cv_text(176, 4, &FONT_S, buf, C_TEXT_WHITE);
        cv_text(204, 4, &FONT_S, "SWG", C_TEXT_DIM);
    }
    cv_blit(0, 0);
}

static void draw_seq_menu(void)
{
    static const char *const opts[SEQ_MENU_COUNT] = {
        "STEPS", "BPM", "DIV", "GATE", "SWING", "REC", "DUP BARS", "CLR ALL"
    };

    draw_seq_header();
    cv_begin(240, 96, C_BG);
    cv_text_plate(0, 0, 240, "SEQ OPTIONS", C_ROLAND_AMBER, C_BLACK, 1);
    for (uint32_t i = 0; i < 4u; i++) {
        char val[8];
        seq_menu_value(i, val);
        menu_row_draw(10, 20u + i * 18u, i == g_seq_menu_sel, opts[i], val);
    }
    cv_blit(0, 22);
    cv_begin(240, 72, C_BG);
    for (uint32_t i = 4u; i < SEQ_MENU_COUNT; i++) {
        char val[8];
        seq_menu_value(i, val);
        menu_row_draw(10, (i - 4u) * 18u, i == g_seq_menu_sel, opts[i], val);
    }
    cv_blit(0, 118);
    draw_seq_fader_guide();
}

/* ---- note options menu (short-press EDIT): glo-menu style ---- */
static void note_menu_value(uint32_t i, const fm106_step_t *st, uint16_t eidx, char *val)
{
    switch (i) {
    case 0: {
        uint32_t v = st->ratchet ? st->ratchet : 1u;
        val[0] = (char)('0' + v); val[1] = '\0';
        break;
    }
    case 1: {
        uint8_t tie = st->time == FM106_ST_TIE;
        str_cpy(val, tie ? "ON" : "OFF", 8);
        break;
    }
    case 2: {
        uint8_t on = (st->flags & FM106_SF_ACCENT) != 0u;
        str_cpy(val, on ? "ON" : "OFF", 8);
        break;
    }
    case 3: {
        uint8_t on = (st->flags & FM106_SF_SLIDE) != 0u;
        str_cpy(val, on ? "ON" : "OFF", 8);
        break;
    }
    case 4: { /* PROB */
        if (st->probability == 0u || st->probability == 100u) {
            str_cpy(val, "100%", 8);
        } else if (st->probability > 100u) {
            str_cpy(val, "MUTE", 8);
        } else {
            fmt_u32_compact(st->probability, val);
            uint32_t k = 0;
            while (val[k]) k++;
            val[k] = '%'; val[k + 1u] = '\0';
        }
        break;
    }
    default: { /* P-LOCKS count on this step */
        uint32_t c = fm106_plock_count_step(&g_synth.seq, eidx);
        if (c == 0u) str_cpy(val, "NONE", 8);
        else         fmt_u32_compact(c, val);
        break;
    }
    }
}

static void draw_note_menu(void)
{
    static const char *const opts[NOTE_MENU_COUNT] = {
        "RATCHET", "TIE", "ACCENT", "SLIDE", "PROB", "P-LOCKS"
    };
    uint16_t eidx = seq_ui_edit_idx();
    fm106_step_t *st = &g_synth.seq.step[eidx];

    draw_seq_header();
    cv_begin(240, 96, C_BG);
    cv_text_plate(0, 0, 240, "NOTE OPTIONS", C_ROLAND_AMBER, C_BLACK, 1);
    for (uint32_t i = 0; i < 4u; i++) {
        char val[8];
        note_menu_value(i, st, eidx, val);
        menu_row_draw(10, 20u + i * 18u, i == g_note_menu_sel, opts[i], val);
    }
    cv_blit(0, 22);
    cv_begin(240, 72, C_BG);
    for (uint32_t i = 4u; i < NOTE_MENU_COUNT; i++) {
        char val[8];
        note_menu_value(i, st, eidx, val);
        menu_row_draw(10, (i - 4u) * 18u, i == g_note_menu_sel, opts[i], val);
    }
    cv_blit(0, 118);
    draw_seq_fader_guide();
}

/* fader-style knob guide for the SEQ page bottom (matches draw_faders_section) */
static void draw_seq_fader_guide(void)
{
    fm106_seq_t *s = &g_synth.seq;
    uint16_t len = s->slen ? s->slen : 1u;
    uint16_t cur = (uint16_t)(g_seq_cursor % len);
    fm106_step_t *st = &s->step[cur];
    static const char *const lbl[4] = {"STEP", "NOTE", "GATE", "VEL"};
    int32_t val[4];
    int32_t minv[4] = {1, 0, 0, 0};
    int32_t max[4];

    val[0] = (int32_t)cur + 1;                      max[0] = (int32_t)len;
    val[1] = (int32_t)g_seq_row;                    max[1] = 127;
    val[2] = st->acc ? (int32_t)st->acc : (int32_t)s->sgate;           max[2] = 255;
    val[3] = (int32_t)st->vel;                      max[3] = 127;

    cv_begin(240, 50, C_BG);
    for (int i = 0; i < 4; i++) {
        int32_t fx = 12 + i * 58;
        int32_t v = val[i];
        if (v < minv[i]) v = minv[i];
        if (v > max[i]) v = max[i];

        /* fader track + center line (same geometry as draw_faders_section) */
        cv_rect(fx + 19, 10, 6, 20, C_FADER_BG);
        cv_line(fx + 21, 11, fx + 21, 29, C_FADER_LINE);

        int32_t travel = 14;
        int32_t span = max[i] - minv[i];
        int32_t cap_y = 10 + (travel - ((v - minv[i]) * travel) / (span > 0 ? span : 1));
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
            fmt_u32_compact((uint32_t)v, num_str);
        }
        int32_t nw = text_w(&FONT_S, num_str);
        cv_text(fx + (44 - nw) / 2, 38, &FONT_S, num_str, C_TEXT_WHITE);
    }
    cv_blit(0, 190);
}

/* ---- SEQ piano-roll page (Melodee single-track port) ---- */
static void draw_seq_roll_band(uint8_t row_lo, uint8_t row_hi, uint32_t y, int32_t play_idx)
{
    uint8_t rows = row_hi - row_lo;
    uint16_t plock_cols = 0;
    if (row_lo == 0u) {
        const fm106_seq_t *sq = &g_synth.seq;
        for (uint16_t li = 0; li < sq->nplock; li++) {
            uint16_t stp = sq->plock[li].step;
            if (stp >= g_seq_view_step && stp < (uint16_t)(g_seq_view_step + 16u)) {
                plock_cols |= (uint16_t)(1u << (stp - g_seq_view_step));
            }
        }
    }

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
                    cv_rect(x, yb + SEQ_ROW_H / 2 - 2, SEQ_STEP_W - 1, 4, C_STRIPE_CYAN);
                } else {
                    uint16_t bc = (st->flags & FM106_SF_ACCENT) ? C_TEXT_WHITE : C_ROLAND_AMBER;
                    cv_rect(x + 1, yb + 1, SEQ_STEP_W - 2, SEQ_ROW_H - 2, bc);
                    if (st->flags & FM106_SF_SLIDE) {
                        cv_rect(x + SEQ_STEP_W - 3, yb + 2, 2, SEQ_ROW_H - 4, C_STRIPE_CYAN);
                    }
                }
            }

            /* P-lock indicator pip along top margin of locked steps */
            if (r == 0u && (plock_cols & (1u << c))) {
                cv_rect(x + 4, 0, 4, 2, C_STRIPE_CYAN);
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

    draw_seq_header();

    /* roll bands: one playhead snapshot so both halves always agree */
    int32_t ph = s->playing ? (int32_t)seq_ph_idx(s) : -1;
    draw_seq_roll_band(0, 8, 22, ph);
    draw_seq_roll_band(8, 14, 118, ph);

    /* bottom: fader-style knob guide */
    draw_seq_fader_guide();
}

static void fmt_cpu_pct(uint32_t pct, char *out)
{
    if (pct > 99u) pct = 99u;
    out[0] = (pct >= 10u) ? (char)('0' + (pct / 10u)) : ' ';
    out[1] = (char)('0' + (pct % 10u));
    out[2] = '%';
    out[3] = '\0';
}

/* Live CPU load footer at bottom of OPT menu (y=160..239) */
static void draw_opt_cpu_footer(void)
{
    uint32_t c0 = g_cpu0_load_pct;
    if (c0 > 99u) c0 = 99u;

    char p0[4];
    fmt_cpu_pct(c0, p0);
    uint16_t col0 = (c0 >= 85u) ? C_ROLAND_RED : (c0 >= 65u) ? C_ROLAND_AMBER : C_STRIPE_CYAN;

#if DINGHY_DUAL_CORE
    uint32_t c1 = g_cpu1_load_pct;
    if (c1 > 99u) c1 = 99u;
    char p1[4];
    fmt_cpu_pct(c1, p1);
    uint16_t col1 = (c1 >= 85u) ? C_ROLAND_RED : (c1 >= 65u) ? C_ROLAND_AMBER : C_STRIPE_CYAN;
    int32_t w0 = (int32_t)((c0 * 124u) / 100u);
    int32_t w1 = (int32_t)((c1 * 124u) / 100u);

    cv_begin(240, 80, C_BG);
    cv_rect(6, 4, 228, 72, C_FADER_BG);
    cv_rect(6, 4, 228, 1, C_LINE_DIV);
    cv_rect(6, 75, 228, 1, C_LINE_DIV);

    cv_text(14, 8, &FONT_S, "CPU LOAD MONITOR", C_STRIPE_CYAN);
    cv_rect(14, 22, 212, 1, C_LINE_DIV);

    /* Core 0 row */
    cv_text(14, 28, &FONT_S, "CPU0 (SYNTH)", C_TEXT_DIM);
    cv_rect(106, 31, 80, 8, C_BLACK);
    cv_rect(106, 31, 80, 1, C_LINE_DIV);
    if (w0 > 0) {
        int32_t b0 = (w0 * 76) / 124;
        if (b0 > 76) b0 = 76;
        cv_rect(108, 33, b0, 4, col0);
    }
    cv_text(194, 28, &FONT_S, p0, C_TEXT_WHITE);

    /* Core 1 row */
    cv_text(14, 50, &FONT_S, "CPU1 (WORKER)", C_TEXT_DIM);
    cv_rect(106, 53, 80, 8, C_BLACK);
    cv_rect(106, 53, 80, 1, C_LINE_DIV);
    if (w1 > 0) {
        int32_t b1 = (w1 * 76) / 124;
        if (b1 > 76) b1 = 76;
        cv_rect(108, 55, b1, 4, col1);
    }
    cv_text(194, 50, &FONT_S, p1, C_TEXT_WHITE);

    cv_blit(0, 160);
#else
    cv_begin(240, 80, C_BG);
    cv_rect(6, 4, 228, 72, C_FADER_BG);
    cv_rect(6, 4, 228, 1, C_LINE_DIV);
    cv_rect(6, 75, 228, 1, C_LINE_DIV);

    cv_text(14, 8, &FONT_S, "CPU LOAD MONITOR", C_STRIPE_CYAN);
    cv_rect(14, 22, 212, 1, C_LINE_DIV);

    /* Single core row: clean label, wide meter bar, percentage */
    cv_text(14, 30, &FONT_S, "SYNTH ENGINE", C_TEXT_DIM);
    cv_text(194, 30, &FONT_S, p0, C_TEXT_WHITE);

    cv_rect(14, 48, 212, 14, C_BLACK);
    cv_rect(14, 48, 212, 1, C_LINE_DIV);
    cv_rect(14, 61, 212, 1, C_LINE_DIV);

    if (c0 > 0) {
        int32_t b0 = (int32_t)((c0 * 208u) / 100u);
        if (b0 > 208) b0 = 208;
        cv_rect(16, 50, b0, 10, col0);
    }

    cv_blit(0, 160);
#endif
}

/* 4. OPT Menu (Covering full screen y=0..239 in three 80px bands) */
static void draw_menu_main(void)
{
#if FELUCCA_BLE
    const char *items[MENU_MAIN_COUNT] = {"MIDI SETUP", "BLUETOOTH", "ABOUT"};
#else
    const char *items[MENU_MAIN_COUNT] = {"MIDI SETUP", "ABOUT"};
#endif

    /* Band 1: y=0..79 */
    cv_begin(240, 80, C_BG);
    cv_text_plate(0, 0, 240, "GLOBAL OPTIONS", C_ROLAND_AMBER, C_BLACK, 1);
    cv_rect(0, 16, 240, 1, C_LINE_DIV);

    cv_text(12, 22, &FONT_S, "SYSTEM SETTINGS", C_STRIPE_CYAN);
    cv_text(146, 22, &FONT_S, "[GLO] EXIT", C_TEXT_DIM);
    cv_rect(6, 36, 228, 1, C_LINE_DIV);

    /* Item 0 */
    uint16_t c0 = (g_menu_sel == 0) ? C_ROLAND_AMBER : C_TEXT_DIM;
    cv_rect(6, 44, 228, 26, (g_menu_sel == 0) ? C_LINE_DIV : C_BLACK);
    cv_rect(6, 44, 228, 1, (g_menu_sel == 0) ? C_ROLAND_AMBER : C_LINE_DIV);
    cv_rect(6, 69, 228, 1, (g_menu_sel == 0) ? C_ROLAND_AMBER : C_LINE_DIV);
    cv_text((g_menu_sel == 0) ? 14 : 18, 50, &FONT_S, (g_menu_sel == 0) ? "> MIDI SETUP" : "  MIDI SETUP", c0);
    cv_blit(0, 0);

    /* Band 2: y=80..159 */
    cv_begin(240, 80, C_BG);
    for (uint32_t i = 1; i < MENU_MAIN_COUNT; i++) {
        uint16_t c = (i == g_menu_sel) ? C_ROLAND_AMBER : C_TEXT_DIM;
        uint16_t ry = 2 + (i - 1u) * 26;
        cv_rect(6, ry, 228, 24, (i == g_menu_sel) ? C_LINE_DIV : C_BLACK);
        cv_rect(6, ry, 228, 1, (i == g_menu_sel) ? C_ROLAND_AMBER : C_LINE_DIV);
        cv_rect(6, ry + 23, 228, 1, (i == g_menu_sel) ? C_ROLAND_AMBER : C_LINE_DIV);
        char buf[24];
        buf[0] = (i == g_menu_sel) ? '>' : ' ';
        buf[1] = ' ';
        uint32_t j = 2;
        while (items[i][j-2] && j < sizeof(buf)-1) { buf[j] = items[i][j-2]; j++; }
        buf[j] = '\0';
        cv_text((i == g_menu_sel) ? 14 : 18, ry + 6, &FONT_S, buf, c);
    }

    cv_rect(6, 56, 228, 20, C_BLACK);
    cv_rect(6, 56, 228, 1, C_LINE_DIV);
    cv_rect(6, 75, 228, 1, C_LINE_DIV);
    cv_text(12, 60, &FONT_S, "[SEL] Move    [OCT+] Enter", C_TEXT_DIM);
    cv_blit(0, 80);

    /* Band 3: y=160..239 (CPU meter) */
    draw_opt_cpu_footer();
}

#if FELUCCA_BLE
static void draw_menu_bluetooth(void)
{
    g_where = 0x25;
    bt_set_t s;
    bt_set_get(&s);
    char st_str[24];
    bt_get_status_str(st_str, sizeof(st_str));

    const char *on_str = btp.on ? "ON" : "OFF";
    const char *auto_str = s.autostart ? "ON" : "OFF";
    const char *role_str = (s.role == BT_ROLE_DEVICE) ? "DEVICE" : "HOST";

    const char *labels[3] = {"BLUETOOTH", "AUTOSTART", "ROLE"};
    const char *vals[3]   = {on_str, auto_str, role_str};

    /* Band 1: y=0..79 (Header + Status + Row 0) */
    cv_begin(240, 80, C_BG);
    cv_text_plate(0, 0, 240, "BLUETOOTH", C_ROLAND_AMBER, C_BLACK, 1);
    cv_rect(0, 16, 240, 1, C_LINE_DIV);

    /* Recessed Status Bar */
    cv_rect(6, 20, 228, 18, C_BLACK);
    cv_rect(6, 20, 228, 1, C_LINE_DIV);
    cv_rect(6, 37, 228, 1, C_LINE_DIV);
    cv_text(12, 22, &FONT_S, "STATUS:", C_TEXT_DIM);
    cv_text(74, 22, &FONT_S, st_str, btp.on ? C_STRIPE_CYAN : C_TEXT_DIM);

    /* Row 0 */
    uint16_t c0 = (g_ble_sel == 0u) ? C_ROLAND_AMBER : C_TEXT_DIM;
    cv_rect(6, 44, 228, 26, (g_ble_sel == 0u) ? C_LINE_DIV : C_BLACK);
    cv_rect(6, 44, 228, 1, (g_ble_sel == 0u) ? C_ROLAND_AMBER : C_LINE_DIV);
    cv_rect(6, 69, 228, 1, (g_ble_sel == 0u) ? C_ROLAND_AMBER : C_LINE_DIV);
    cv_text(14, 50, &FONT_S, labels[0], C_TEXT_DIM);
    cv_text_plate(140, 48, 80, vals[0], c0, C_BLACK, 1);
    cv_blit(0, 0);

    /* Band 2: y=80..159 (Rows 1, 2) */
    cv_begin(240, 80, C_BG);
    for (uint32_t i = 1u; i < 3u; i++) {
        uint16_t c = (g_ble_sel == i) ? C_ROLAND_AMBER : C_TEXT_DIM;
        uint16_t ry = 8 + (i - 1u) * 34;
        cv_rect(6, ry, 228, 26, (g_ble_sel == i) ? C_LINE_DIV : C_BLACK);
        cv_rect(6, ry, 228, 1, (g_ble_sel == i) ? C_ROLAND_AMBER : C_LINE_DIV);
        cv_rect(6, ry + 25, 228, 1, (g_ble_sel == i) ? C_ROLAND_AMBER : C_LINE_DIV);
        cv_text(14, ry + 6, &FONT_S, labels[i], C_TEXT_DIM);
        cv_text_plate(140, ry + 4, 80, vals[i], c, C_BLACK, 1);
    }
    cv_blit(0, 80);

    /* Band 3: y=160..239 (Row 3 + Guide Footer) */
    cv_begin(240, 80, C_BG);
    uint16_t c3 = (g_ble_sel == 3u) ? C_ROLAND_AMBER : C_TEXT_DIM;
    cv_rect(6, 4, 228, 26, (g_ble_sel == 3u) ? C_LINE_DIV : C_BLACK);
    cv_rect(6, 4, 228, 1, (g_ble_sel == 3u) ? C_ROLAND_AMBER : C_LINE_DIV);
    cv_rect(6, 29, 228, 1, (g_ble_sel == 3u) ? C_ROLAND_AMBER : C_LINE_DIV);
    if (s.role == BT_ROLE_HOST) {
        cv_text_plate(10, 8, 220, "[OCT+] SCAN DEVICES", c3, C_BLACK, 1);
    } else {
        cv_text_plate(10, 8, 220, "ADVERTISING: FM-106", C_TEXT_DIM, C_BLACK, 1);
    }

    cv_rect(6, 36, 228, 36, C_BLACK);
    cv_rect(6, 36, 228, 1, C_LINE_DIV);
    cv_rect(6, 71, 228, 1, C_LINE_DIV);
    cv_text(12, 40, &FONT_S, "[SEL] Select   [ALGO] Edit", C_TEXT_DIM);
    cv_text(14, 54, &FONT_S, "[HOME] Back to Menu", C_ROLAND_AMBER);
    cv_blit(0, 160);
}

static void bt_device_menu_action(void)
{
    if (btp.an == 0 || g_bt_scan_sel >= btp.an) {
        g_bt_device_menu_on = 0;
        return;
    }
    const ble_ui_dev_t *d = &btp.adev[g_bt_scan_sel];
    int s_idx = bt_dev_find(d->addr);

    bt_dev_t dev;
    memset(&dev, 0, sizeof(dev));
    memcpy(dev.addr, d->addr, 6);
    dev.type = d->type & 1u;
    dev.kind = (uint8_t)bt_dev_kind(d->name);
    bt_name(dev.name, d->name, sizeof(dev.name));

    switch (g_bt_device_menu_sel) {
    case 0: /* CONNECT NOW */
        bt_devs_add(&dev);
        bt_add_close();
        bt_devs_page(1);
        ble_ui_slot_target(dev.kind, dev.addr, dev.type);
        g_bt_device_menu_on = 0;
        g_ui_mode = UI_MODE_BLUETOOTH;
        break;

    case 1: /* AUTO CONNECT TOGGLE */
        if (s_idx < 0) {
            bt_devs_add(&dev);
        } else if (s_idx == 0) {
            bt_devs_forget(0);
        } else {
            bt_devs_use((uint32_t)s_idx);
        }
        g_bt_device_menu_on = 0;
        break;

    case 2: /* FORGET DEVICE */
        if (s_idx >= 0) {
            bt_devs_forget((uint32_t)s_idx);
        }
        g_bt_device_menu_on = 0;
        break;

    case 3: /* CANCEL */
    default:
        g_bt_device_menu_on = 0;
        break;
    }
}

static void bt_connect_to_device(uint32_t sel_idx)
{
    if (btp.an == 0 || sel_idx >= btp.an) return;
    g_bt_scan_sel = sel_idx;
    g_bt_device_menu_on = 1;
    g_bt_device_menu_sel = 0;
}

static void draw_bt_device_row(const ble_ui_dev_t *dev_p, int is_sel, uint16_t row_y)
{
    cv_rect(6, row_y, 228, 24, is_sel ? C_LINE_DIV : C_BLACK);
    cv_rect(6, row_y, 228, 1, is_sel ? C_ROLAND_AMBER : C_LINE_DIV);
    cv_rect(6, row_y + 23, 228, 1, is_sel ? C_ROLAND_AMBER : C_LINE_DIV);

    char name_str[17];
    memset(name_str, 0, sizeof(name_str));
    if (dev_p->name[0]) {
        bt_name(name_str, dev_p->name, sizeof(name_str));
    } else {
        static const char H[] = "0123456789ABCDEF";
        name_str[0] = H[dev_p->addr[0] >> 4]; name_str[1] = H[dev_p->addr[0] & 15]; name_str[2] = ':';
        name_str[3] = H[dev_p->addr[1] >> 4]; name_str[4] = H[dev_p->addr[1] & 15]; name_str[5] = ':';
        name_str[6] = H[dev_p->addr[2] >> 4]; name_str[7] = H[dev_p->addr[2] & 15]; name_str[8] = ':';
        name_str[9] = H[dev_p->addr[3] >> 4]; name_str[10] = H[dev_p->addr[3] & 15]; name_str[11] = '\0';
    }
    cv_text(14, row_y + 5, &FONT_S, name_str, is_sel ? C_ROLAND_AMBER : C_TEXT_WHITE);

    if (dev_p->midi) {
        if (bt_saved(dev_p->addr)) {
            cv_text(142, row_y + 5, &FONT_S, "[SAVED]", C_ROLAND_AMBER);
        } else {
            cv_text(148, row_y + 5, &FONT_S, "[MIDI]", C_STRIPE_CYAN);
        }
    } else {
        cv_text(148, row_y + 5, &FONT_S, "[BLE]", C_TEXT_DIM);
    }

    char rssi_str[8];
    memset(rssi_str, 0, sizeof(rssi_str));
    int8_t rs = dev_p->rssi;
    if (rs < 0) {
        rssi_str[0] = '-';
        uint8_t pos = (uint8_t)(-rs);
        if (pos > 99) pos = 99;
        rssi_str[1] = (char)('0' + pos / 10);
        rssi_str[2] = (char)('0' + pos % 10);
        rssi_str[3] = '\0';
    }
    cv_text(190, row_y + 5, &FONT_S, rssi_str, C_TEXT_DIM);
}

static void draw_menu_bt_scan(void)
{
    g_where = 0xA0;
    static char status_buf[32];

    g_where = 0xA1;
    if (!g_bt_device_menu_on) {
        bt_add_list();
    }
    g_where = 0xA2;

    uint32_t n = btp.an;
    if (n > 0 && g_bt_scan_sel >= n) g_bt_scan_sel = n - 1;

    if (g_bt_device_menu_on && n > 0 && g_bt_scan_sel < n) {
        const ble_ui_dev_t *d = &btp.adev[g_bt_scan_sel];
        int s_idx = bt_dev_find(d->addr);

        /* Band 1: y=0..79 (Header + Selected Device Details) */
        cv_begin(240, 80, C_BG);
        cv_text_plate(0, 0, 240, "DEVICE OPTIONS", C_ROLAND_AMBER, C_BLACK, 1);
        cv_rect(0, 16, 240, 1, C_LINE_DIV);

        /* Recessed Device Box */
        cv_rect(6, 20, 228, 54, C_BLACK);
        cv_rect(6, 20, 228, 1, C_LINE_DIV);
        cv_rect(6, 73, 228, 1, C_LINE_DIV);

        char name_str[17];
        memset(name_str, 0, sizeof(name_str));
        if (d->name[0]) {
            bt_name(name_str, d->name, sizeof(name_str));
        } else {
            static const char H[] = "0123456789ABCDEF";
            name_str[0] = H[d->addr[0] >> 4]; name_str[1] = H[d->addr[0] & 15]; name_str[2] = ':';
            name_str[3] = H[d->addr[1] >> 4]; name_str[4] = H[d->addr[1] & 15]; name_str[5] = ':';
            name_str[6] = H[d->addr[2] >> 4]; name_str[7] = H[d->addr[2] & 15]; name_str[8] = ':';
            name_str[9] = H[d->addr[3] >> 4]; name_str[10] = H[d->addr[3] & 15]; name_str[11] = '\0';
        }
        cv_text(16, 26, &FONT_S, name_str, C_ROLAND_AMBER);

        if (d->midi) {
            cv_text(16, 46, &FONT_S, "TYPE: BLE MIDI CONTROLLER", C_STRIPE_CYAN);
        } else {
            cv_text(16, 46, &FONT_S, "TYPE: BLE PERIPHERAL", C_TEXT_DIM);
        }

        if (s_idx >= 0) {
            cv_text(164, 26, &FONT_S, "[SAVED]", C_STRIPE_CYAN);
        } else {
            cv_text(164, 26, &FONT_S, "[NEW]", C_TEXT_DIM);
        }
        cv_blit(0, 0);

        /* Band 2: y=80..159 (Actions 0, 1, 2) */
        cv_begin(240, 80, C_BG);
        const char *opt_labels[3];
        opt_labels[0] = "CONNECT NOW";
        if (s_idx < 0) {
            opt_labels[1] = "AUTO CONNECT: OFF";
        } else if (s_idx == 0) {
            opt_labels[1] = "AUTO CONNECT: ON (#1)";
        } else {
            opt_labels[1] = "AUTO CONNECT: ON (TOP)";
        }
        opt_labels[2] = (s_idx >= 0) ? "FORGET DEVICE" : "FORGET (NOT SAVED)";

        for (uint32_t opt = 0; opt < 3; opt++) {
            uint16_t ry = 2 + opt * 26;
            int is_sel = (g_bt_device_menu_sel == opt);
            cv_rect(6, ry, 228, 24, is_sel ? C_LINE_DIV : C_BLACK);
            cv_rect(6, ry, 228, 1, is_sel ? C_ROLAND_AMBER : C_LINE_DIV);
            cv_rect(6, ry + 23, 228, 1, is_sel ? C_ROLAND_AMBER : C_LINE_DIV);
            cv_text(16, ry + 6, &FONT_S, opt_labels[opt], is_sel ? C_ROLAND_AMBER : C_TEXT_WHITE);
        }
        cv_blit(0, 80);

        /* Band 3: y=160..239 (Action 3 + Footer) */
        cv_begin(240, 80, C_BG);
        int is_sel3 = (g_bt_device_menu_sel == 3);
        cv_rect(6, 2, 228, 24, is_sel3 ? C_LINE_DIV : C_BLACK);
        cv_rect(6, 2, 228, 1, is_sel3 ? C_ROLAND_AMBER : C_LINE_DIV);
        cv_rect(6, 25, 228, 1, is_sel3 ? C_ROLAND_AMBER : C_LINE_DIV);
        cv_text(16, 8, &FONT_S, "CANCEL", is_sel3 ? C_ROLAND_AMBER : C_TEXT_WHITE);

        cv_rect(6, 34, 228, 38, C_BLACK);
        cv_rect(6, 34, 228, 1, C_LINE_DIV);
        cv_rect(6, 71, 228, 1, C_LINE_DIV);
        cv_text(12, 38, &FONT_S, "[SEL] Confirm  [OCT-] Back", C_ROLAND_AMBER);
        cv_text(14, 54, &FONT_S, "[SELECT] Move Cursor", C_TEXT_DIM);
        cv_blit(0, 160);
        return;
    }

    if (n == 0) {
        if (fm1_ms - btp.add_t0 < BT_ADD_MS) {
            str_cpy(status_buf, "SCANNING...", sizeof(status_buf));
        } else {
            str_cpy(status_buf, "DONE (NONE)", sizeof(status_buf));
        }
    } else {
        status_buf[0] = 'F'; status_buf[1] = 'O'; status_buf[2] = 'U'; status_buf[3] = 'N'; status_buf[4] = 'D';
        status_buf[5] = ':'; status_buf[6] = ' ';
        uint32_t k = 7;
        if (n > 9) { status_buf[k++] = (char)('0' + (n / 10)); }
        status_buf[k++] = (char)('0' + (n % 10));
        status_buf[k++] = ' '; status_buf[k++] = 'D'; status_buf[k++] = 'E'; status_buf[k++] = 'V'; status_buf[k++] = '\0';
    }

    uint32_t top = 0;
    if (n > 5 && g_bt_scan_sel >= 5) {
        top = g_bt_scan_sel - 4;
    }

    /* Band 1: y=0..79 (Header + Status + Slot 0) */
    g_where = 0xA3;
    cv_begin(240, 80, C_BG);
    cv_text_plate(0, 0, 240, "BLE MIDI SCAN", C_ROLAND_AMBER, C_BLACK, 1);
    cv_rect(0, 16, 240, 1, C_LINE_DIV);

    /* Recessed Status Bar */
    cv_rect(6, 20, 228, 18, C_BLACK);
    cv_rect(6, 20, 228, 1, C_LINE_DIV);
    cv_rect(6, 37, 228, 1, C_LINE_DIV);
    cv_text(12, 22, &FONT_S, "STATUS:", C_TEXT_DIM);
    cv_text(74, 22, &FONT_S, status_buf, C_STRIPE_CYAN);

    if (n == 0) {
        if (fm1_ms - btp.add_t0 < BT_ADD_MS) {
            cv_text(16, 48, &FONT_S, "SCANNING FOR BLE DEVS...", C_TEXT_DIM);
            cv_text(16, 64, &FONT_S, "SET KEYBOARD TO PAIRING", C_TEXT_DIM);
        } else {
            cv_text(16, 48, &FONT_S, "NO DEVICES FOUND", C_ROLAND_AMBER);
            cv_text(16, 64, &FONT_S, "[OCT+] TO SCAN AGAIN", C_TEXT_DIM);
        }
    } else {
        if (top < n) {
            draw_bt_device_row(&btp.adev[top], (top == g_bt_scan_sel), 46);
        }
    }
    g_where = 0xA4;
    cv_blit(0, 0);
    g_where = 0xA5;

    /* Band 2: y=80..159 (Slots 1, 2, 3) */
    cv_begin(240, 80, C_BG);
    if (n == 0) {
        if (fm1_ms - btp.add_t0 < BT_ADD_MS) {
            cv_text(16, 16, &FONT_S, "KEEP DEVICE CLOSE", C_TEXT_DIM);
            cv_text(16, 36, &FONT_S, "[OCT-] CANCEL", C_TEXT_DIM);
        } else {
            cv_text(16, 16, &FONT_S, "[OCT-] RETURN TO MENU", C_TEXT_DIM);
        }
    } else {
        for (uint32_t s = 1; s <= 3; s++) {
            uint32_t idx = top + s;
            if (idx < n) {
                draw_bt_device_row(&btp.adev[idx], (idx == g_bt_scan_sel), 2 + (s - 1u) * 26);
            }
        }
    }
    cv_blit(0, 80);

    /* Band 3: y=160..239 (Slot 4 + Footer) */
    cv_begin(240, 80, C_BG);
    if (n > 0 && (top + 4 < n)) {
        draw_bt_device_row(&btp.adev[top + 4], (top + 4 == g_bt_scan_sel), 2);
    }

    cv_rect(6, 34, 228, 38, C_BLACK);
    cv_rect(6, 34, 228, 1, C_LINE_DIV);
    cv_rect(6, 71, 228, 1, C_LINE_DIV);
    if (n > 0) {
        cv_text(14, 38, &FONT_S, "[OCT+/SEL] DEVICE OPTIONS", C_ROLAND_AMBER);
        cv_text(12, 54, &FONT_S, "[SEL] Scroll   [OCT-] Back", C_TEXT_DIM);
    } else {
        cv_text(14, 38, &FONT_S, "[OCT+] SCAN AGAIN", C_ROLAND_AMBER);
        cv_text(14, 54, &FONT_S, "[OCT-] RETURN TO MENU", C_TEXT_DIM);
    }
    g_where = 0xA6;
    cv_blit(0, 160);
    g_where = 0xA7;
}
#endif

static void draw_menu_about(void)
{
    /* Band 1: y=0..79 */
    cv_begin(240, 80, C_BG);
    cv_text_plate(0, 0, 240, "ABOUT", C_ROLAND_AMBER, C_BLACK, 1);
    cv_rect(0, 16, 240, 1, C_LINE_DIV);

    cv_rect(6, 22, 228, 52, C_BLACK);
    cv_rect(6, 22, 228, 1, C_LINE_DIV);
    cv_rect(6, 73, 228, 1, C_LINE_DIV);
    cv_text(48, 28, &FONT_S, "FM-106 SYNTHESIZER", C_STRIPE_CYAN);
    cv_rect(24, 46, 192, 1, C_LINE_DIV);
    cv_text(60, 54, &FONT_S, "Firmware: ", C_TEXT_DIM);
    cv_text(136, 54, &FONT_S, "Beta7", C_ROLAND_AMBER);
    cv_blit(0, 0);

    /* Band 2: y=80..159 */
    cv_begin(240, 80, C_BG);
    cv_rect(6, 4, 228, 70, C_BLACK);
    cv_rect(6, 4, 228, 1, C_LINE_DIV);
    cv_rect(6, 73, 228, 1, C_LINE_DIV);
    cv_text(16, 16, &FONT_S, "ENGINE : 6-Voice Poly DCO", C_TEXT_WHITE);
    cv_text(16, 32, &FONT_S, "FILTER : 4-Pole 24dB IR3109", C_TEXT_WHITE);
    cv_text(16, 48, &FONT_S, "AUDIO  : 44.1kHz / USB UAC", C_TEXT_WHITE);
    cv_blit(0, 80);

    /* Band 3: y=160..239 */
    cv_begin(240, 80, C_BG);
    cv_rect(6, 4, 228, 46, C_BLACK);
    cv_rect(6, 4, 228, 1, C_LINE_DIV);
    cv_rect(6, 49, 228, 1, C_LINE_DIV);
    cv_text(96, 9, &FONT_S, "GITHUB", C_ROLAND_AMBER);
    cv_text(24, 28, &FONT_S, "github.com/PatttF/fm-106", C_TEXT_WHITE);

    cv_text(44, 58, &FONT_S, "[HOME] Back to Menu", C_TEXT_DIM);
    cv_blit(0, 160);
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

    const char *labels[6] = {"MIDI IN", "MIDI OUT", "SYNC", "LOCAL", "PC IN", "CC CONFIG"};
    const char *vals[6]   = {in_str, out_str, sync_str,
                             g_synth.midi_local ? yes : no,
                             g_synth.pc_in ? yes : no,
                             "OPEN >"};

    /* Band 1: y=0..79 (Header + Row 0) */
    cv_begin(240, 80, C_BG);
    cv_text_plate(0, 0, 240, "MIDI SETUP", C_ROLAND_AMBER, C_BLACK, 1);
    cv_rect(0, 16, 240, 1, C_LINE_DIV);

    cv_text(12, 22, &FONT_S, "COMMUNICATIONS", C_STRIPE_CYAN);
    cv_text(138, 22, &FONT_S, "[HOME] BACK", C_TEXT_DIM);
    cv_rect(6, 36, 228, 1, C_LINE_DIV);

    uint16_t c0 = (g_midi_sel == 0u) ? C_ROLAND_AMBER : C_TEXT_DIM;
    cv_rect(6, 44, 228, 26, (g_midi_sel == 0u) ? C_LINE_DIV : C_BLACK);
    cv_rect(6, 44, 228, 1, (g_midi_sel == 0u) ? C_ROLAND_AMBER : C_LINE_DIV);
    cv_rect(6, 69, 228, 1, (g_midi_sel == 0u) ? C_ROLAND_AMBER : C_LINE_DIV);
    cv_text(14, 50, &FONT_S, labels[0], C_TEXT_DIM);
    cv_text_plate(140, 48, 80, vals[0], c0, C_BLACK, 1);
    cv_blit(0, 0);

    /* Band 2: y=80..159 (Rows 1, 2, 3) */
    cv_begin(240, 80, C_BG);
    for (uint32_t i = 1u; i < 4u; i++) {
        uint16_t c = (g_midi_sel == i) ? C_ROLAND_AMBER : C_TEXT_DIM;
        uint16_t ry = 2 + (i - 1u) * 26;
        cv_rect(6, ry, 228, 24, (g_midi_sel == i) ? C_LINE_DIV : C_BLACK);
        cv_rect(6, ry, 228, 1, (g_midi_sel == i) ? C_ROLAND_AMBER : C_LINE_DIV);
        cv_rect(6, ry + 23, 228, 1, (g_midi_sel == i) ? C_ROLAND_AMBER : C_LINE_DIV);
        cv_text(14, ry + 6, &FONT_S, labels[i], C_TEXT_DIM);
        cv_text_plate(140, ry + 4, 80, vals[i], c, C_BLACK, 1);
    }
    cv_blit(0, 80);

    /* Band 3: y=160..239 (Row 4 + Row 5 + Guide Footer) */
    cv_begin(240, 80, C_BG);
    uint16_t c4 = (g_midi_sel == 4u) ? C_ROLAND_AMBER : C_TEXT_DIM;
    cv_rect(6, 2, 228, 22, (g_midi_sel == 4u) ? C_LINE_DIV : C_BLACK);
    cv_rect(6, 2, 228, 1, (g_midi_sel == 4u) ? C_ROLAND_AMBER : C_LINE_DIV);
    cv_rect(6, 23, 228, 1, (g_midi_sel == 4u) ? C_ROLAND_AMBER : C_LINE_DIV);
    cv_text(14, 6, &FONT_S, labels[4], C_TEXT_DIM);
    cv_text_plate(140, 4, 80, vals[4], c4, C_BLACK, 1);

    uint16_t c5 = (g_midi_sel == 5u) ? C_ROLAND_AMBER : C_TEXT_DIM;
    cv_rect(6, 26, 228, 22, (g_midi_sel == 5u) ? C_LINE_DIV : C_BLACK);
    cv_rect(6, 26, 228, 1, (g_midi_sel == 5u) ? C_ROLAND_AMBER : C_LINE_DIV);
    cv_rect(6, 47, 228, 1, (g_midi_sel == 5u) ? C_ROLAND_AMBER : C_LINE_DIV);
    cv_text(14, 30, &FONT_S, labels[5], C_TEXT_DIM);
    cv_text_plate(140, 28, 80, vals[5], c5, C_BLACK, 1);

    cv_rect(6, 51, 228, 26, C_BLACK);
    cv_rect(6, 51, 228, 1, C_LINE_DIV);
    cv_rect(6, 76, 228, 1, C_LINE_DIV);
    cv_text(10, 56, &FONT_S, "[SEL] Edit/Open [HOME] Back", C_ROLAND_AMBER);
    cv_blit(0, 160);
}

static void draw_cc_row(uint16_t ry, uint32_t idx)
{
    if (idx >= CC_MAP_COUNT) return;
    const cc_entry_def_t *e = &CC_ENTRIES[idx];
    uint8_t is_sel = (idx == g_cc_menu_sel);
    uint16_t border_c = is_sel ? C_ROLAND_AMBER : C_LINE_DIV;
    uint16_t bg_c = is_sel ? C_LINE_DIV : C_BLACK;

    cv_rect(6, ry, 228, 24, bg_c);
    cv_rect(6, ry, 228, 1, border_c);
    cv_rect(6, ry + 23, 228, 1, border_c);

    /* Group tag: e.g. [VCF] */
    char grp[8];
    grp[0] = '[';
    uint32_t gi = 0;
    while (e->group[gi] && gi < 4) { grp[gi + 1] = e->group[gi]; gi++; }
    grp[gi + 1] = ']';
    grp[gi + 2] = '\0';
    cv_text(12, ry + 5, &FONT_S, grp, is_sel ? C_ROLAND_AMBER : C_STRIPE_CYAN);

    /* Parameter name */
    cv_text(58, ry + 5, &FONT_S, e->name, is_sel ? C_TEXT_WHITE : C_TEXT_DIM);

    /* Value box */
    if (is_sel && g_cc_learn_active) {
        uint8_t flash = (uint8_t)((fm1_ms / 250u) & 1u);
        cv_text_plate(146, ry + 3, 82, flash ? "LEARN..." : "MOVE CC", C_LED_ON, C_BLACK, 1);
    } else {
        uint8_t val = set.cc_map[idx];
        char val_str[12];
        if (val == 0xFFu) {
            val_str[0] = '-'; val_str[1] = '-'; val_str[2] = '-'; val_str[3] = '\0';
        } else {
            val_str[0] = 'C'; val_str[1] = 'C'; val_str[2] = ' ';
            if (val >= 100u) {
                val_str[3] = (char)('0' + (val / 100u));
                val_str[4] = (char)('0' + ((val / 10u) % 10u));
                val_str[5] = (char)('0' + (val % 10u));
                val_str[6] = '\0';
            } else if (val >= 10u) {
                val_str[3] = (char)('0' + (val / 10u));
                val_str[4] = (char)('0' + (val % 10u));
                val_str[5] = '\0';
            } else {
                val_str[3] = (char)('0' + val);
                val_str[4] = '\0';
            }
        }
        cv_text_plate(146, ry + 3, 82, val_str, is_sel ? C_ROLAND_AMBER : C_TEXT_WHITE, C_BLACK, 1);
    }
}

static void draw_menu_cc_map(void)
{
    /* Band 1: y=0..79 (Header + Subtitle + Row 0) */
    cv_begin(240, 80, C_BG);
    if (g_cc_learn_active) {
        cv_text_plate(0, 0, 240, "MIDI LEARN: SEND CC", C_LED_ON, C_BLACK, 1);
    } else {
        cv_text_plate(0, 0, 240, "MIDI CC CONFIG", C_ROLAND_AMBER, C_BLACK, 1);
    }
    cv_rect(0, 16, 240, 1, C_LINE_DIV);

    char idx_str[16];
    uint32_t cur_num = g_cc_menu_sel + 1u;
    idx_str[0] = (char)('0' + (cur_num / 10u));
    idx_str[1] = (char)('0' + (cur_num % 10u));
    idx_str[2] = '/';
    idx_str[3] = '4';
    idx_str[4] = '5';
    idx_str[5] = ' ';
    idx_str[6] = 'P';
    idx_str[7] = 'A';
    idx_str[8] = 'R';
    idx_str[9] = 'A';
    idx_str[10] = 'M';
    idx_str[11] = '\0';
    cv_text(12, 22, &FONT_S, idx_str, C_STRIPE_CYAN);
    cv_text(138, 22, &FONT_S, "[HOME] BACK", C_TEXT_DIM);
    cv_rect(6, 36, 228, 1, C_LINE_DIV);

    draw_cc_row(44, (uint32_t)g_cc_scroll_top);
    cv_blit(0, 0);

    /* Band 2: y=80..159 (Rows 1, 2, 3) */
    cv_begin(240, 80, C_BG);
    for (uint32_t r = 1u; r < 4u; r++) {
        uint16_t ry = 2u + (r - 1u) * 26u;
        draw_cc_row(ry, (uint32_t)g_cc_scroll_top + r);
    }
    cv_blit(0, 80);

    /* Band 3: y=160..239 (Row 4 + Guide Footer) */
    cv_begin(240, 80, C_BG);
    draw_cc_row(2, (uint32_t)g_cc_scroll_top + 4u);

    cv_rect(6, 30, 228, 46, C_BLACK);
    cv_rect(6, 30, 228, 1, C_LINE_DIV);
    cv_rect(6, 75, 228, 1, C_LINE_DIV);
    if (g_cc_learn_active) {
        cv_text(14, 35, &FONT_S, "MOVE CONTROLLER KNOB/CC", C_LED_ON);
        cv_text(14, 52, &FONT_S, "[SEL] Cancel   [HOME] Back", C_TEXT_DIM);
    } else {
        cv_text(14, 35, &FONT_S, "[SEL] Learn   [ALGO] Value", C_TEXT_DIM);
        cv_text(12, 52, &FONT_S, "[EDIT] Reset   [HOME] Back", C_ROLAND_AMBER);
    }
    cv_blit(0, 160);
}

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
        int rc = fm106_user_presets_save_flash();
        if (rc != 0) {
            fm106_set_banner("SAVE FAILED!", "FLASH ERR");
        } else {
            g_synth.patch_idx = (uint8_t)(128u + (uint32_t)first_blank);
            str_cpy(g_synth.patch_name, target_name, sizeof(g_synth.patch_name));
            set->patch = g_synth.patch_idx;
            set->octave = (int8_t)g_synth.octave_shift;
            settings_save(set, sizeof(*set));
            if (g_seq_flash_dirty) {
                fm106_seq_save_flash(&g_synth.seq);
                g_seq_flash_dirty = 0;
            }
            fm106_format_slot_code(g_synth.patch_idx, code_buf);
            fm106_set_banner("SAVED TO ", code_buf);
        }
    } else {
        uint32_t uidx = g_synth.patch_idx - 128u;
        const char *name_to_use = g_synth.patch_name[0] ? g_synth.patch_name : "User Patch";
        fm106_save_engine_to_preset(&g_user_presets[uidx], &g_synth, name_to_use);
        int rc = fm106_user_presets_save_flash();
        if (rc != 0) {
            fm106_set_banner("SAVE FAILED!", "FLASH ERR");
        } else {
            str_cpy(g_synth.patch_name, name_to_use, sizeof(g_synth.patch_name));
            set->patch = g_synth.patch_idx;
            set->octave = (int8_t)g_synth.octave_shift;
            settings_save(set, sizeof(*set));
            if (g_seq_flash_dirty) {
                fm106_seq_save_flash(&g_synth.seq);
                g_seq_flash_dirty = 0;
            }
            fm106_format_slot_code(g_synth.patch_idx, code_buf);
            fm106_set_banner("SAVED TO ", code_buf);
        }
    }
}

static void draw_save_menu(void)
{
    /* Canvas 1 (y=0..75, height 76): Header + Target Slot */
    cv_begin(240, 76, C_BG);

    cv_rect(6, 4, 4, 12, C_ROLAND_RED);
    cv_text(16, 2, &FONT_S, "SAVE PRESET", C_STRIPE_CYAN);
    cv_text(134, 2, &FONT_S, "[SEL] Slot", C_ROLAND_AMBER);
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
    cv_text(74, 2, &FONT_S, "ALGO:< > PRESET:A-Z", C_STRIPE_CYAN);

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
            cv_text(12, 52, &FONT_S, "[SEL] Delete   [HOME] Back", C_TEXT_WHITE);
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
    if (g_synth.reverb.on && g_synth.reverb.mix > 0u && g_synth.reverb.tail_active && g_synth.reverb.silent_blocks < 32u)
        return 0;
    return 1;
}

#include "splash_data.h"

static uint32_t g_splash_until_ms = 0;

static void draw_splash_screen(void)
{
    for (uint32_t s = 0; s < 3u; s++) {
        lcd_sync();
        splash_lz4_decompress(splash_strips[s], (uint8_t *)cv_px, 240u * 80u * 2u);
        lcd_blit(0, s * 80u, 240, 80, cv_px);
    }
    lcd_sync();
}

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
        fm106_cc_map_init_defaults();
    }
    if (set.octave < -2 || set.octave > 2) set.octave = 0;
    if (set.midi_channel > 16) set.midi_channel = 0;    /* 0 = All (omni) */
    if (set.midi_out_ch < 1 || set.midi_out_ch > 16) set.midi_out_ch = 1;
    if (set.midi_local > 1) set.midi_local = 1;
    if (set.pc_in > 1) set.pc_in = 1;
    if (set.sync_mode > 2) set.sync_mode = 0;

    /* Validate CC map in case loaded from uninitialized / corrupt flash */
    uint8_t cc_all_zero = 1;
    for (uint32_t i = 0; i < CC_MAP_COUNT; i++) {
        if (set.cc_map[i] != 0) cc_all_zero = 0;
        if (set.cc_map[i] > 127u && set.cc_map[i] != 0xFFu) {
            set.cc_map[i] = CC_ENTRIES[i].default_cc;
        }
    }
    if (cc_all_zero) {
        fm106_cc_map_init_defaults();
    }
    fm106_cc_map_update();
    g_synth.octave_shift = set.octave;
    g_synth.midi_channel = set.midi_channel;
    g_synth.midi_out_ch = set.midi_out_ch;
    g_synth.midi_local = set.midi_local;
    g_synth.pc_in = set.pc_in;
    g_synth.sync_mode = set.sync_mode;
    seq_sync_apply();
    fm106_load_user_preset(&g_synth, set.patch);
    fm106_seq_load_flash(&g_synth.seq);
    if (g_seq_cursor >= g_synth.seq.slen) g_seq_cursor = 0;
    fm106_param_snapshot(&g_synth);

    draw_splash_screen();
    g_splash_until_ms = fm1_ms + 1800u;

    /* Dim ambient backlight for all buttons (labels readable in the dark) */
    for (uint32_t col = 0; col < FM1_NCOL; col++)
        for (uint32_t row = 1; row < 5u; row++)
            if (FM1_KEYMAP[row][col] >= 0 && FM1_KEYMAP[row][col] < 14)
                fm1_led_bg[col] |= (uint8_t)(1u << row);
    fm1_led_bg_ns = 10000u;  /* dim pulse: ~1/6 of a fully lit LED */
#if FELUCCA_BLE
    bt_init();
#endif
}

/* One frame / poll iteration of the application */
static void fm106_app_frame(void)
{
    g_where = 0x02;
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

        g_where = 0x03;
        platform_poll();
        g_where = 0x04;
#if FELUCCA_BLE
        g_where = 0x05;
        bt_service();
        g_where = 0x06;
        bt_autostart_tick();
        g_where = 0x07;
#endif

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

        g_where = 0x20;
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
                if (g_ui_mode == UI_MODE_NORMAL) {
                    seq_note_entry(held[k], 100); /* rec+play -> live record anywhere; else chord/acid entry on PAGE_SEQ */
                    if (g_current_page == PAGE_SEQ) dirty_screen = 1;
                }
            }
            if (up >> k & 1u) {
                if (g_synth.midi_local) fm106_key_off(&g_synth, held[k]);
                midi_send(0x80u | ((g_synth.midi_out_ch - 1u) & 0x0Fu), held[k], 0);
                seq_note_release(held[k]); /* live-rec: stamp held length as gate */
            }
        }

        g_where = 0x21;
        /* 2. Panel Buttons */
        b = fm1_input_edges(&rel);

        /* Octave shift / Bluetooth Scan */
        if (g_ui_mode == UI_MODE_NORMAL) {
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
        }
        else if (g_ui_mode == UI_MODE_MENU) {
            if (b & (1u << BTN_OCT_UP)) {
                if (g_menu_sel == 0) {
                    g_ui_mode = UI_MODE_MIDI;
                    g_midi_sel = 0;
#if FELUCCA_BLE
                } else if (g_menu_sel == 1) {
                    g_ui_mode = UI_MODE_BLUETOOTH;
                    g_ble_sel = 0;
                    bt_page_open();
                } else if (g_menu_sel == 2) {
                    g_ui_mode = UI_MODE_ABOUT;
                }
#else
                } else if (g_menu_sel == 1) {
                    g_ui_mode = UI_MODE_ABOUT;
                }
#endif
                dirty_screen = 1;
            }
        }
#if FELUCCA_BLE
        else if (g_ui_mode == UI_MODE_BLUETOOTH) {
            if (b & (1u << BTN_OCT_UP)) {
                bt_set_t bs;
                bt_set_get(&bs);
                if (bs.role == BT_ROLE_HOST) {
                    bt_page_on();
                    bt_host_scan_start();
                    g_ui_mode = UI_MODE_BT_SCAN;
                    g_bt_scan_sel = 0;
                    g_bt_device_menu_on = 0;
                    g_bt_device_menu_sel = 0;
                    dirty_screen = 1;
                }
            }
        }
        else if (g_ui_mode == UI_MODE_BT_SCAN) {
            if (b & (1u << BTN_OCT_UP)) {
                if (g_bt_device_menu_on) {
                    bt_device_menu_action();
                } else if (btp.an == 0) {
                    bt_host_scan_start();
                } else {
                    g_bt_device_menu_on = 1;
                    g_bt_device_menu_sel = 0;
                }
                dirty_screen = 1;
            } else if (b & (1u << BTN_OCT_DOWN)) {
                if (g_bt_device_menu_on) {
                    g_bt_device_menu_on = 0;
                } else {
                    bt_add_close();
                    g_ui_mode = UI_MODE_BLUETOOTH;
                }
                dirty_screen = 1;
            }
        }
#endif

        /* OPT/GLO button: toggle menu */
        if (b & (1u << BTN_GLO)) {
            if (g_ui_mode == UI_MODE_NORMAL) {
                g_ui_mode = UI_MODE_MENU;
                g_menu_sel = 0;
            } else {
#if FELUCCA_BLE
                if (g_ui_mode == UI_MODE_BLUETOOTH || g_ui_mode == UI_MODE_BT_SCAN) {
                    bt_page_close();
                }
#endif
                g_cc_learn_active = 0;
                g_ui_mode = UI_MODE_NORMAL;
            }
            dirty_screen = 1;
        }

        if (g_ui_mode == UI_MODE_NORMAL) {
            /* FX button: cycle Chorus -> Tape -> Shimmer Reverb (jumps to Chorus from outside FX) */
            if (b & (1u << BTN_FX)) {
                if (g_current_page == PAGE_FX)          g_current_page = PAGE_TAPE;
                else if (g_current_page == PAGE_TAPE)   g_current_page = PAGE_REVERB;
                else                                    g_current_page = PAGE_FX;
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
                if (g_ui_mode == UI_MODE_CC_MAP) {
                    g_cc_learn_active = 0;
                    g_ui_mode = UI_MODE_MIDI;
                } else if (g_ui_mode == UI_MODE_MIDI
#if FELUCCA_BLE
                    || g_ui_mode == UI_MODE_BLUETOOTH
                    || g_ui_mode == UI_MODE_BT_SCAN
#endif
                    || g_ui_mode == UI_MODE_ABOUT) {
#if FELUCCA_BLE
                    if (g_ui_mode == UI_MODE_BT_SCAN) {
                        if (g_bt_device_menu_on) {
                            g_bt_device_menu_on = 0;
                        } else {
                            bt_add_close();
                            g_ui_mode = UI_MODE_BLUETOOTH;
                        }
                    } else {
                        if (g_ui_mode == UI_MODE_BLUETOOTH) bt_page_close();
                        g_ui_mode = UI_MODE_MENU;
                    }
#else
                    g_ui_mode = UI_MODE_MENU;
#endif
                } else g_ui_mode = UI_MODE_NORMAL;
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
            } else if (g_ui_mode == UI_MODE_CC_MAP) {
                uint8_t def_cc = CC_ENTRIES[g_cc_menu_sel].default_cc;
                for (uint32_t k = 0; k < CC_MAP_COUNT; k++) {
                    if (k != g_cc_menu_sel && set.cc_map[k] == def_cc) set.cc_map[k] = 0xFFu;
                }
                set.cc_map[g_cc_menu_sel] = def_cc;
                g_cc_learn_active = 0;
                fm106_cc_map_update();
                dirty_save = 1;
                dirty_screen = 1;
                t_change = fm1_ms;
                fm106_set_banner("RESET TO", "DEFAULT CC");
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
                    seq_ui_clear_all();
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
                /* In normal mode, SEL is a clean no-op (never toggle VCA) */
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
                        int rc = fm106_user_presets_save_flash();
                        if (rc != 0) {
                            fm106_set_banner("DELETE ERR", "FLASH ERR");
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
#if FELUCCA_BLE
                } else if (g_menu_sel == 1) {
                    g_ui_mode = UI_MODE_BLUETOOTH;
                    g_ble_sel = 0;
                    bt_page_open();
                } else if (g_menu_sel == 2) {
                    g_ui_mode = UI_MODE_ABOUT;
                }
#else
                } else if (g_menu_sel == 1) {
                    g_ui_mode = UI_MODE_ABOUT;
                }
#endif
                dirty_screen = 1;
#if FELUCCA_BLE
            } else if (g_ui_mode == UI_MODE_BLUETOOTH) {
                bt_set_t bs;
                bt_set_get(&bs);
                if (g_ble_sel == 0) {
                    if (btp.on) bt_page_off();
                    else bt_page_on();
                } else if (g_ble_sel == 1) {
                    bs.autostart ^= 1u;
                    bt_set_put(&bs);
                } else if (g_ble_sel == 2) {
                    bs.role ^= 1u;
                    bt_set_put(&bs);
                    ble_ui_role(bs.role);
                    bt_devs_page(!bs.role);
                    if (bs.role == BT_ROLE_DEVICE && g_ble_sel >= 3u) g_ble_sel = 2u;
                } else if (g_ble_sel == 3 && bs.role == BT_ROLE_HOST) {
                    bt_page_on();
                    bt_host_scan_start();
                    g_ui_mode = UI_MODE_BT_SCAN;
                    g_bt_scan_sel = 0;
                    g_bt_device_menu_on = 0;
                    g_bt_device_menu_sel = 0;
                }
                dirty_screen = 1;
            } else if (g_ui_mode == UI_MODE_BT_SCAN) {
                if (g_bt_device_menu_on) {
                    bt_device_menu_action();
                } else if (btp.an == 0) {
                    bt_host_scan_start();
                } else {
                    g_bt_device_menu_on = 1;
                    g_bt_device_menu_sel = 0;
                }
                dirty_screen = 1;
#endif
            } else if (g_ui_mode == UI_MODE_CC_MAP) {
                g_cc_learn_active ^= 1u;
                dirty_screen = 1;
            } else if (g_ui_mode == UI_MODE_MIDI) {
                if (g_midi_sel == 0) {
                    /* MIDI IN: cycle 0(All), 1..16 */
                    g_synth.midi_channel = (g_synth.midi_channel >= 16u) ? 0u : (g_synth.midi_channel + 1u);
                    set.midi_channel = g_synth.midi_channel;
                    dirty_save = 1;
                    t_change = fm1_ms;
                } else if (g_midi_sel == 1) {
                    /* MIDI OUT: cycle 1..16 */
                    g_synth.midi_out_ch = (g_synth.midi_out_ch >= 16u) ? 1u : (g_synth.midi_out_ch + 1u);
                    set.midi_out_ch = g_synth.midi_out_ch;
                    dirty_save = 1;
                    t_change = fm1_ms;
                } else if (g_midi_sel == 2) {
                    /* SYNC: cycle OFF -> MASTER -> SLAVE */
                    g_synth.sync_mode = (g_synth.sync_mode + 1u) % 3u;
                    set.sync_mode = g_synth.sync_mode;
                    seq_sync_apply();
                    dirty_save = 1;
                    t_change = fm1_ms;
                } else if (g_midi_sel == 3) {
                    /* LOCAL: toggle ON / OFF */
                    g_synth.midi_local ^= 1u;
                    set.midi_local = g_synth.midi_local;
                    if (!g_synth.midi_local) {
                        fm1_irq_off();
                        for (uint8_t v = 0; v < FM106_NUM_VOICES; v++) {
                            if (g_synth.voice[v].note != 255u) fm106_note_off(&g_synth, g_synth.voice[v].note);
                        }
                        fm1_irq_on();
                    }
                    dirty_save = 1;
                    t_change = fm1_ms;
                } else if (g_midi_sel == 4) {
                    /* PC IN: toggle ON / OFF */
                    g_synth.pc_in ^= 1u;
                    set.pc_in = g_synth.pc_in;
                    dirty_save = 1;
                    t_change = fm1_ms;
                } else if (g_midi_sel == 5) {
                    /* CC CONFIG: open CC Map submenu */
                    g_ui_mode = UI_MODE_CC_MAP;
                    g_cc_menu_sel = 0;
                    g_cc_scroll_top = 0;
                    g_cc_learn_active = 0;
                }
                dirty_screen = 1;
            } else if (g_ui_mode == UI_MODE_ABOUT) {
                g_ui_mode = UI_MODE_MENU;
                dirty_screen = 1;
            } else {
                /* Safe default: do nothing */
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
                seq_transport_cmd(3u, 1);
            }
            dirty_screen = 1;
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
                int rc = fm106_user_presets_save_flash();
                if (rc != 0) {
                    fm106_set_banner("SAVE FAILED!", "FLASH ERR");
                } else {
                    g_synth.patch_idx = (uint8_t)(128u + g_save_slot);
                    str_cpy(g_synth.patch_name, clean_name, sizeof(g_synth.patch_name));
                    set.patch = g_synth.patch_idx;
                    set.octave = (int8_t)g_synth.octave_shift;
                    settings_save(&set, sizeof(set));
                    if (g_seq_flash_dirty) {
                        fm106_seq_save_flash(&g_synth.seq);
                        g_seq_flash_dirty = 0;
                    }
                    char code_buf[8];
                    fm106_format_slot_code(g_synth.patch_idx, code_buf);
                    fm106_set_banner("SAVED TO ", code_buf);
                }
                g_ui_mode = UI_MODE_NORMAL;
                save_held_active = 1; /* suppress short save on release */
                dirty_screen = 1;
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

        g_where = 0x22;
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
                    panel_send_pc(0xC0u | ((g_synth.midi_out_ch - 1u) & 0x0Fu), (uint8_t)(np % 64u));
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
                int32_t ns = (int32_t)g_cc_menu_sel + enc_s;
                if (ns < 0) ns = (int32_t)CC_MAP_COUNT - 1;
                if (ns >= (int32_t)CC_MAP_COUNT) ns = 0;
                g_cc_menu_sel = (uint8_t)ns;
                g_cc_learn_active = 0;
                if (g_cc_menu_sel < g_cc_scroll_top) {
                    g_cc_scroll_top = g_cc_menu_sel;
                } else if (g_cc_menu_sel >= g_cc_scroll_top + 5u) {
                    g_cc_scroll_top = (uint8_t)(g_cc_menu_sel - 4u);
                }
                dirty_screen = 1;
#if FELUCCA_BLE
            } else if (g_ui_mode == UI_MODE_BLUETOOTH) {
                bt_set_t bs;
                bt_set_get(&bs);
                int32_t count = (bs.role == BT_ROLE_HOST) ? (int32_t)MENU_BLE_COUNT : ((int32_t)MENU_BLE_COUNT - 1);
                int32_t nm = (int32_t)g_ble_sel + enc_s;
                if (nm < 0) nm = count - 1;
                if (nm >= count) nm = 0;
                g_ble_sel = (uint8_t)nm;
                dirty_screen = 1;
            } else if (g_ui_mode == UI_MODE_BT_SCAN) {
                if (g_bt_device_menu_on) {
                    int32_t sel = (int32_t)g_bt_device_menu_sel + enc_s;
                    if (sel < 0) sel = BT_DEV_MENU_COUNT - 1;
                    if (sel >= (int32_t)BT_DEV_MENU_COUNT) sel = 0;
                    g_bt_device_menu_sel = (uint8_t)sel;
                    dirty_screen = 1;
                } else {
                    uint32_t n = btp.an;
                    if (n > 0) {
                        int32_t sel = (int32_t)g_bt_scan_sel + enc_s;
                        if (sel < 0) sel = (int32_t)n - 1;
                        if (sel >= (int32_t)n) sel = 0;
                        g_bt_scan_sel = (uint32_t)sel;
                        dirty_screen = 1;
                    }
                }
#endif
            } else if (g_ui_mode == UI_MODE_ABOUT) {
                /* static about screen */
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
                seq_ui_menu_adjust(ealgo);                       /* glo-menu nav: ALGORITHM edits value with encoder velocity */
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
                panel_send_cc(0xB0u | ((g_synth.midi_out_ch - 1u) & 0x0Fu), 0u, (uint8_t)nb);
                panel_send_pc(0xC0u | ((g_synth.midi_out_ch - 1u) & 0x0Fu), (uint8_t)(np % 64u));
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
                } else {                        /* 5 = CC CONFIG: open CC map */
                    g_ui_mode = UI_MODE_CC_MAP;
                    g_cc_menu_sel = 0;
                    g_cc_scroll_top = 0;
                    g_cc_learn_active = 0;
                }
                dirty_screen = 1;
                dirty_save = 1;
                t_change = fm1_ms;
            } else if (g_ui_mode == UI_MODE_CC_MAP) {
                uint8_t cur = set.cc_map[g_cc_menu_sel];
                uint8_t next = cur;
                if (ealgo > 0) {
                    if (cur == 0xFFu) next = 0u;
                    else if (cur < 127u) next = (uint8_t)(cur + 1u);
                    else next = 0xFFu;
                } else if (ealgo < 0) {
                    if (cur == 0xFFu) next = 127u;
                    else if (cur > 0u) next = (uint8_t)(cur - 1u);
                    else next = 0xFFu;
                }
                if (next != cur) {
                    if (next < 128u) {
                        for (uint32_t k = 0; k < CC_MAP_COUNT; k++) {
                            if (k != g_cc_menu_sel && set.cc_map[k] == next) set.cc_map[k] = 0xFFu;
                        }
                    }
                    set.cc_map[g_cc_menu_sel] = next;
                    g_cc_learn_active = 0;
                    fm106_cc_map_update();
                    dirty_save = 1;
                    dirty_screen = 1;
                    t_change = fm1_ms;
                }
#if FELUCCA_BLE
            } else if (g_ui_mode == UI_MODE_BLUETOOTH) {
                bt_set_t bs;
                bt_set_get(&bs);
                if (g_ble_sel == 0) {          /* BLUETOOTH ON/OFF */
                    if (ealgo > 0) bt_page_on();
                    else bt_page_off();
                } else if (g_ble_sel == 1) {   /* AUTOSTART ON/OFF */
                    bs.autostart ^= 1u;
                    bt_set_put(&bs);
                } else if (g_ble_sel == 2) {   /* ROLE DEVICE/HOST */
                    bs.role ^= 1u;
                    bt_set_put(&bs);
                    ble_ui_role(bs.role);
                    if (bs.role == BT_ROLE_DEVICE && g_ble_sel >= 3u) g_ble_sel = 2u;
                }
                dirty_screen = 1;
            } else if (g_ui_mode == UI_MODE_BT_SCAN) {
                if (g_bt_device_menu_on) {
                    int32_t sel = (int32_t)g_bt_device_menu_sel + (ealgo > 0 ? 1 : -1);
                    if (sel < 0) sel = BT_DEV_MENU_COUNT - 1;
                    if (sel >= (int32_t)BT_DEV_MENU_COUNT) sel = 0;
                    g_bt_device_menu_sel = (uint8_t)sel;
                    dirty_screen = 1;
                } else {
                    uint32_t n = btp.an;
                    if (n > 0) {
                        int32_t sel = (int32_t)g_bt_scan_sel + (ealgo > 0 ? 1 : -1);
                        if (sel < 0) sel = (int32_t)n - 1;
                        if (sel >= (int32_t)n) sel = 0;
                        g_bt_scan_sel = (uint32_t)sel;
                        dirty_screen = 1;
                    }
                }
#endif
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
                        if (g_synth.voice[v].note != 255u) {
                            if (g_synth.voice_mode == FM106_MODE_MONO) {
                                g_synth.voice[v].base_inc =
                                    fm106_calc_unison_inc(g_synth.voice[v].note, 0, g_synth.dco_range, v);
                            } else {
                                g_synth.voice[v].base_inc =
                                    fm106_calc_inc(g_synth.voice[v].note, 0, g_synth.dco_range);
                            }
                            if (!g_synth.voice[v].gliding)
                                g_synth.voice[v].curr_inc = g_synth.voice[v].base_inc;
                        }
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
                    /* the level fader changes the level only: SUBSW (PAGE_DCO k4) is the
                     * master on/off — deriving sub_on from the level here would switch the
                     * sub back on against the toggle (the gate already silences level 0) */
                    g_synth.dco_sub_level = (uint8_t)fm106_clamp(g_synth.dco_sub_level + k1 * 3, 0, 127);
                    for (uint32_t v = 0; v < FM106_NUM_VOICES; v++)
                        g_synth.voice[v].dco.sub_level = g_synth.dco_sub_level;
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
            case PAGE_REVERB: {
                fm106_reverb_t *rv = &g_synth.reverb;
                if (k1) {
                    rv->on = (k1 > 0) ? 1u : 0u;
                    if (rv->on && rv->mix == 0u) rv->mix = 48u;
                }
                if (k2) rv->mix     = (uint8_t)fm106_clamp((int32_t)rv->mix + k2 * 3, 0, 127);
                if (k3) rv->decay   = (uint8_t)fm106_clamp((int32_t)rv->decay + k3 * 3, 0, 127);
                if (k4) rv->shimmer = (uint8_t)fm106_clamp((int32_t)rv->shimmer + k4 * 3, 0, 127);
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

        /* Scan panel knob/menu parameter changes -> send MIDI CC out (with self-echo guard) + p-locks */
        plock_scan(1);

        g_where = 0x23;
        /* 4. USB-MIDI In */
        while (midi_get(&st, &d1, &d2)) {
            /* System Realtime (clock/transport) — arrives interleaved on the
             * status byte, so handle before the channel-type nibble. */
            if (st >= 0xF8u) {
                if (st == 0xF8u)      fm106_midi_clock_pulse(&g_synth.seq, fm106_now_ms());
                else if (st == 0xFAu || st == 0xFBu || st == 0xFCu)
                    fm106_midi_clock_transport(&g_synth.seq, st);
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
            } else if (type == 0xC0u) {  /* Program Change (gated by PC IN + self-echo guard) */
                if (!g_synth.pc_in || pc_is_self_echo()) continue;
                int32_t cur_bank = (int32_t)g_synth.patch_idx / 64;
                uint32_t target_patch = (uint32_t)(cur_bank * 64 + (d1 % 64u));
                fm106_load_user_preset(&g_synth, target_patch);
                set.patch = (uint8_t)target_patch;
                dirty_screen = 1;
            } else if (type == 0xB0u) {  /* Control Change */
                if (d1 != 119u && d1 != 64u && cc_is_self_echo(d1)) continue;

                /* MIDI Learn mode interception */
                if (g_ui_mode == UI_MODE_CC_MAP && g_cc_learn_active) {
                    if (d1 != 119u) {
                        for (uint32_t k = 0; k < CC_MAP_COUNT; k++) {
                            if (k != g_cc_menu_sel && set.cc_map[k] == d1) set.cc_map[k] = 0xFFu;
                        }
                        set.cc_map[g_cc_menu_sel] = d1;
                        g_cc_learn_active = 0;
                        fm106_cc_map_update();
                        dirty_save = 1;
                        dirty_screen = 1;
                        t_change = fm1_ms;
                        fm106_set_banner("MAPPED TO", "MIDI CC");
                        continue;
                    }
                }

                if (d1 == 0 || d1 == 32) { /* Bank Select MSB/LSB */
                    uint32_t nb = d2 & 3u;
                    int32_t cur_slot = (int32_t)g_synth.patch_idx % 64;
                    uint32_t target_patch = nb * 64u + (uint32_t)cur_slot;
                    fm106_load_user_preset(&g_synth, target_patch);
                    set.patch = (uint8_t)target_patch;
                    dirty_screen = 1;
                } else if (d1 == 119 && d2 == 1u) {  /* Peak-hold timing telemetry (read + clear) */
                    /* CC114: peak whole-ISR % of the 139,320-tick block budget (>100 = late)
                     * CC115: peak render % (fm106_audio_render only)
                     * CC116: peak post-render % (USB tap / 48k FIR + MASTER scaling)
                     * CC117/118: peak whole-ISR ticks >> 4 as 14 bits (hi7, lo7)
                     * CC119: last-block whole-ISR %   CC127: audio_lates */
                    uint8_t tch = (uint8_t)(0xB0u | ((g_synth.midi_out_ch - 1u) & 0x0Fu));
                    uint32_t ip = audio_isr_peak, rp = g_cpu0_peak, pp = audio_post_peak, il = audio_isr_ticks;
                    audio_isr_peak = 0; g_cpu0_peak = 0; audio_post_peak = 0;
                    uint32_t ip_pct = (uint32_t)((float)ip * (100.0f / 139320.0f));
                    uint32_t rp_pct = (uint32_t)((float)rp * (100.0f / 139320.0f));
                    uint32_t pp_pct = (uint32_t)((float)pp * (100.0f / 139320.0f));
                    uint32_t il_pct = (uint32_t)((float)il * (100.0f / 139320.0f));
                    uint32_t ip16 = ip >> 4;
                    if (ip16 > 0x3FFFu) ip16 = 0x3FFFu;
                    midi_send(tch, 114, (uint8_t)(ip_pct > 127u ? 127u : ip_pct));
                    midi_send(tch, 115, (uint8_t)(rp_pct > 127u ? 127u : rp_pct));
                    midi_send(tch, 116, (uint8_t)(pp_pct > 127u ? 127u : pp_pct));
                    midi_send(tch, 117, (uint8_t)((ip16 >> 7) & 0x7Fu));
                    midi_send(tch, 118, (uint8_t)(ip16 & 0x7Fu));
                    midi_send(tch, 119, (uint8_t)(il_pct > 127u ? 127u : il_pct));
                    midi_send(tch, 127, (uint8_t)(audio_lates & 0x7Fu));
                } else if (d1 == 119) {  /* Debug telemetry inquiry */
                    uint8_t tch = (uint8_t)(0xB0u | ((g_synth.midi_out_ch - 1u) & 0x0Fu));
#if defined(FELUCCA_UAC) && FELUCCA_UAC
                    midi_send(tch, 114, (uint8_t)(uac.alt & 0x7Fu));
                    midi_send(tch, 115, (uint8_t)(uac.starts & 0x7Fu));
                    midi_send(tch, 116, (uint8_t)(uac.stalls & 0x7Fu));
                    midi_send(tch, 117, (uint8_t)(uac.rate_sets & 0x7Fu));
                    midi_send(tch, 118, (uint8_t)(uac.flowing & 0x7Fu));
#endif
                    midi_send(tch, 119, (uint8_t)g_cpu0_load_pct);
                    midi_send(tch, 120, (uint8_t)g_cpu1_load_pct);
                    midi_send(tch, 121, (uint8_t)(g_cpu0_ticks / 1000u));
                    midi_send(tch, 122, (uint8_t)(g_cpu1_ticks / 1000u));
#if defined(FELUCCA_UAC) && FELUCCA_UAC
                    midi_send(tch, 123, (uint8_t)(uac.overruns & 0x7Fu));
                    midi_send(tch, 124, (uint8_t)(uac.underruns & 0x7Fu));
#endif
#if DINGHY_DUAL_CORE
                    midi_send(tch, 125, (uint8_t)(audio_worker.timeouts & 0x7Fu));
#else
                    midi_send(tch, 125, 0);
#endif
#if defined(FELUCCA_UAC) && FELUCCA_UAC
                    midi_send(tch, 126, (uint8_t)(uac.missed & 0x7Fu));
                    midi_send(tch, 127, (uint8_t)(audio_lates & 0x7Fu));
#endif
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
                } else {
                    uint8_t entry = g_cc_to_entry[d1 & 0x7Fu];
                    if (entry != 0xFFu) {
                        fm106_apply_cc(entry, d2);
                    } else if (d1 == 7u) {
                        fm106_apply_cc(11, d2); /* fallback: master volume CC 7 -> VCA level */
                    } else if (d1 == 99u || d1 == 100u || d1 == 101u) {
                        /* step accent / slide / ratchet on the edit step */
                        seq_ui_cc_flag(d1 - 99u, d2);
                    } else if (d1 == 109u && d2 >= 64u) {  /* Clear all p-locks */
                        fm1_irq_off();
                        fm106_plock_clear(&g_synth.seq);
                        fm1_irq_on();
                        seq_mark_modified();
                        dirty_screen = 1;
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
                    }
                }
            }
        }

        /* Sync plock_cache + record incoming MIDI CC automation WITHOUT echoing CCs out */
        plock_scan(0);

        g_where = 0x24;
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
                            g_synth.fx[0].type != FM106_FX_OFF ||
                            g_synth.reverb.on != 0u);
        fm1_led_key(BTN_ARP, g_synth.arp_on);
        fm1_led_key(BTN_SEQ, g_synth.seq.playing);
        fm1_led_key(BTN_PLAY, g_synth.seq.playing);
        fm1_led_key(BTN_SAVE, (g_ui_mode == UI_MODE_SAVE));
        fm1_led_key(BTN_REC, g_seq_rec_arm);
        if (g_ui_mode == UI_MODE_CC_MAP && g_cc_learn_active) {
            fm1_led_key(BTN_SEL, (uint8_t)((fm1_ms / 250u) & 1u));
        }

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

        if (g_splash_until_ms != 0) {
            if ((int32_t)(fm1_ms - g_splash_until_ms) >= 0 || down != 0 || b != 0 || enc_s != 0 ||
                k1 != 0 || k2 != 0 || k3 != 0 || k4 != 0 || fm1_in.notes != 0 || g_dirty_screen) {
                g_splash_until_ms = 0;
                dirty_screen = 1;
            } else {
                return;
            }
        }

        if (dirty_screen) {
            if (g_ui_mode == UI_MODE_MENU) {
                draw_menu_main();
            } else if (g_ui_mode == UI_MODE_MIDI) {
                draw_menu_midi();
            } else if (g_ui_mode == UI_MODE_CC_MAP) {
                draw_menu_cc_map();
#if FELUCCA_BLE
            } else if (g_ui_mode == UI_MODE_BLUETOOTH) {
                draw_menu_bluetooth();
            } else if (g_ui_mode == UI_MODE_BT_SCAN) {
                draw_menu_bt_scan();
#endif
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

        /* Live per-core CPU% refresh while viewing OPT menu */
        static uint32_t t_last_cpu_ui = 0;
        if (g_ui_mode == UI_MODE_MENU && (fm1_ms - t_last_cpu_ui >= 150u)) {
            t_last_cpu_ui = fm1_ms;
            draw_opt_cpu_footer();
        }

        /* Periodic refresh during MIDI Learn to flash badge */
        static uint32_t t_last_cc_ui = 0;
        if (g_ui_mode == UI_MODE_CC_MAP && g_cc_learn_active && (fm1_ms - t_last_cc_ui >= 200u)) {
            t_last_cc_ui = fm1_ms;
            dirty_screen = 1;
        }

#if FELUCCA_BLE
        /* Live Bluetooth status refresh while in BLUETOOTH menu or SCAN menu */
        static uint32_t t_last_bt_ui = 0;
        g_where = 0x27;
        if ((g_ui_mode == UI_MODE_BLUETOOTH || g_ui_mode == UI_MODE_BT_SCAN) && (fm1_ms - t_last_bt_ui >= 150u)) {
            t_last_bt_ui = fm1_ms;
            if (g_ui_mode == UI_MODE_BLUETOOTH) draw_menu_bluetooth();
            else draw_menu_bt_scan();
        }
#endif

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
            settings_save(&set, sizeof(set));
            dirty_save = 0;
        }
        if (g_seq_flash_dirty && !g_synth.seq.playing && !g_count_in &&
            synth_quiet() && (fm1_ms - g_t_seq_edit > 1500u)) {
            fm106_seq_save_flash(&g_synth.seq);
            g_seq_flash_dirty = 0;
        }
        g_where = 0x28;
}

#ifndef FM106_WEB
static void app_main(void)
{
    fm106_app_init();
    for (;;) {
        g_where = 0x29;
        fm106_app_frame();
    }
}
#endif


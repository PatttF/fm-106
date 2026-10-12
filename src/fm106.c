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
    PAGE_COUNT = 8
};

static uint8_t g_current_page = PAGE_VCF;

/* ---- Edit-page slot descriptors (KR-106 plugin param set) ---- */
enum { SLOT_FADER = 0, SLOT_MODE, SLOT_EMPTY };

static const char *const N_BOOL[2]   = {"OFF", "ON"};
static const char *const N_RNG[3]    = {"16'", "8'", "4'"};
static const char *const N_PWMD[2]   = {"LFO", "MAN"};
static const char *const N_INV[2]    = {"NORM", "INV"};
static const char *const N_ARMD[3]   = {"UP", "U&D", "DN"};
static const char *const N_ARNG[3]   = {"1OC", "2OC", "3OC"};
static const char *const N_CHOR[4]   = {"OFF", "I", "II", "I+II"};
static const char *const N_HPF[4]    = {"BST", "FLT", "236", "754"};
static const char *const N_VCA[2]    = {"ENV", "GATE"};

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
     {"PWM MD", SLOT_MODE, 2, N_PWMD}, {"-", SLOT_EMPTY, 0, 0}},
    /* PAGE_ARP */
    {{"ARP", SLOT_MODE, 2, N_BOOL}, {"MODE", SLOT_MODE, 3, N_ARMD},
     {"ARAT", SLOT_FADER, 0, 0},    {"ARNG", SLOT_MODE, 3, N_ARNG}},
    /* PAGE_FX */
    {{"CHO", SLOT_MODE, 4, N_CHOR}, {"HPF", SLOT_MODE, 4, N_HPF},
     {"VCA", SLOT_MODE, 2, N_VCA},  {"LEVEL", SLOT_FADER, 0, 0}},
};

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
    default:
        switch (i) {
        case 0: return g_synth.chorus.mode & 3u;
        case 1: return g_synth.hpf.mode & 3u;
        case 2: return g_synth.vca_mode & 1u;
        case 3: return g_synth.vca_level;
        }
        break;
    }
    return 0;
}

/* UI Modes */
enum {
    UI_MODE_NORMAL = 0,
    UI_MODE_MENU,
    UI_MODE_MIDI,
    UI_MODE_SAVE,
    UI_MODE_COUNT
};

static uint8_t g_ui_mode = UI_MODE_NORMAL;
static uint8_t g_menu_sel = 0;
static uint8_t g_midi_sel = 0;

static uint8_t g_save_slot = 0;          /* Target user slot (0..127 -> UA11..UB88) */
static char g_save_name[16];             /* 15 chars + null */
static uint8_t g_save_cursor = 0;        /* 0..14 */
static uint8_t g_save_del_confirm = 0;   /* 0 = normal, 1 = "Are you sure?" */
static uint32_t t_save_down = 0;         /* Timestamp when BTN_SAVE was pressed */
static uint8_t save_held_active = 0;     /* 1 if long press triggered UI_MODE_SAVE */
static char g_banner_msg[32] = {0};      /* Screen notification banner */
static uint32_t g_banner_until = 0;      /* Expiry timestamp for banner */

static const char CHAR_SET[] = " ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789*-_./#+!";
#define CHAR_SET_LEN ((int32_t)(sizeof(CHAR_SET) - 1))

#define MENU_MAIN_COUNT 2
#define MENU_MIDI_COUNT 2

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
    case PAGE_FX:
    default:            title = "FX (CHORUS)"; break;
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

/* 4. OPT Menu (y=76..171, single blit) */
static void draw_menu_main(void)
{
    const char *items[MENU_MAIN_COUNT] = {"MIDI SETUP", "ABOUT"};
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
        cv_text_plate(20, 20 + i*20, 200, buf, c, C_BLACK, 0);
    }
    cv_blit(0, 76);
    cv_begin(240, 68, C_BG);
    cv_blit(0, 172);
}

static void draw_menu_midi(void)
{
    char ch_str[8], sync_str[8];
    uint32_t ch = g_synth.midi_channel;
    if (ch > 9) { ch_str[0] = (char)('0' + ch/10); ch_str[1] = (char)('0' + ch%10); ch_str[2] = '\0'; }
    else { ch_str[0] = (char)('0' + ch); ch_str[1] = '\0'; }
    const char *sync_names[3] = {"OFF", "MASTER", "SLAVE"};
    uint32_t s = 0;
    while (sync_names[g_synth.sync_mode][s] && s < 7) { sync_str[s] = sync_names[g_synth.sync_mode][s]; s++; }
    sync_str[s] = '\0';

    cv_begin(240, 96, C_BG);
    cv_text_plate(0, 0, 240, "MIDI SETUP", C_ROLAND_AMBER, C_BLACK, 1);

    uint16_t c0 = (g_midi_sel == 0) ? C_ROLAND_AMBER : C_TEXT_DIM;
    uint16_t c1 = (g_midi_sel == 1) ? C_ROLAND_AMBER : C_TEXT_DIM;

    cv_text_plate(20, 24, 80, "CHANNEL", C_TEXT_DIM, C_BLACK, 0);
    cv_text_plate(140, 24, 60, ch_str, c0, C_BLACK, 1);

    cv_text_plate(20, 48, 80, "SYNC", C_TEXT_DIM, C_BLACK, 0);
    cv_text_plate(140, 48, 80, sync_str, c1, C_BLACK, 1);

    cv_blit(0, 76);
    cv_begin(240, 68, C_BG);
    cv_blit(0, 172);
}

/* Save / load settings */
typedef struct {
    int8_t octave;
    uint8_t patch;
    uint8_t midi_channel;   /* 1..16 */
    uint8_t sync_mode;      /* 0 = off, 1 = master, 2 = slave */
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
        fm106_user_presets_save_flash();
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
        fm106_user_presets_save_flash();
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

/* Main application entry loop */
static void app_main(void)
{
    fm106_settings_t set = {0, 0};
    uint32_t prev_notes = 0, k;
    uint32_t dirty_screen = 1;
    uint32_t prev_voice_active = 0;
    uint32_t t_change = 0, dirty_save = 0;
    uint32_t t_last_scope = 0;
    uint8_t scope_was_active = 1;
    uint8_t held[27] = {0};
    uint32_t lit = 0;

    fm106_engine_init(&g_synth);
    fm106_user_presets_init();

    settings_load(&set, sizeof(set));
    if (set.octave < -2 || set.octave > 2) set.octave = 0;
    if (set.midi_channel < 1 || set.midi_channel > 16) set.midi_channel = 1;
    if (set.sync_mode > 2) set.sync_mode = 0;
    g_synth.octave_shift = set.octave;
    g_synth.midi_channel = set.midi_channel;
    g_synth.sync_mode = set.sync_mode;
    fm106_load_user_preset(&g_synth, set.patch);

    lcd_fill(0, 0, 240, 240, C_BG);

    /* Dim ambient backlight for all buttons (labels readable in the dark) */
    for (uint32_t col = 0; col < FM1_NCOL; col++)
        for (uint32_t row = 1; row < 5u; row++)
            if (FM1_KEYMAP[row][col] >= 0 && FM1_KEYMAP[row][col] < 14)
                fm1_led_bg[col] |= (uint8_t)(1u << row);
    fm1_led_bg_ns = 10000u;  /* dim pulse: ~1/6 of a fully lit LED */

    for (;;) {
        uint32_t keys, down, up, rel, b;
        uint32_t base = (uint32_t)(KEY0 + 12 * g_synth.octave_shift);
        uint8_t st, d1, d2;

        platform_poll();

        /* 1. Panel Keys (27 note keyboard) */
        keys = fm1_in.notes;
        down = keys & ~prev_notes;
        up = prev_notes & ~keys;
        prev_notes = keys;

        for (k = 0; k < 27u; k++) {
            if (down >> k & 1u) {
                held[k] = (uint8_t)(base + k);
                fm106_key_on(&g_synth, held[k], 100);
                midi_send(0x90, held[k], 100);
            }
            if (up >> k & 1u) {
                fm106_key_off(&g_synth, held[k]);
                midi_send(0x80, held[k], 0);
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
            } else {
                g_ui_mode = UI_MODE_NORMAL;
            }
            dirty_screen = 1;
        }

        if (g_ui_mode == UI_MODE_NORMAL) {
            /* FX button: jump to FX page (chorus / HPF / VCA) */
            if (b & (1u << BTN_FX)) {
                g_current_page = PAGE_FX;
                dirty_screen = 1;
            }
        }

        /* Page jump buttons */
        if (b & (1u << BTN_HOME)) {
            if (g_ui_mode == UI_MODE_SAVE) {
                if (g_save_del_confirm) {
                    g_save_del_confirm = 0;
                    dirty_screen = 1;
                } else {
                    g_ui_mode = UI_MODE_NORMAL;
                    dirty_screen = 1;
                }
            } else if (g_ui_mode != UI_MODE_NORMAL) {
                if (g_ui_mode == UI_MODE_MIDI) g_ui_mode = UI_MODE_MENU;
                else g_ui_mode = UI_MODE_NORMAL;
                dirty_screen = 1;
            } else {
                g_current_page = PAGE_VCF;
                dirty_screen = 1;
            }
        }
        if (b & (1u << BTN_EDIT)) {
            if (g_ui_mode == UI_MODE_NORMAL) {
                g_current_page = (g_current_page + 1u) % PAGE_COUNT;
                dirty_screen = 1;
            }
        }
        if (b & (1u << BTN_ENV)) {
            if (g_ui_mode == UI_MODE_NORMAL) {
                g_current_page = PAGE_ENVELOPE;
                dirty_screen = 1;
            }
        }
        if (b & (1u << BTN_LFO)) {
            if (g_ui_mode == UI_MODE_NORMAL) {
                g_current_page = PAGE_LFO;
                dirty_screen = 1;
            }
        }
        if (b & (1u << BTN_SEL)) {
            if (g_ui_mode == UI_MODE_SAVE) {
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
                        fm106_user_presets_save_flash();
                        if (g_synth.patch_idx == (uint8_t)(128u + (uint32_t)g_save_slot)) {
                            fm106_load_user_preset(&g_synth, g_synth.patch_idx);
                            set.patch = g_synth.patch_idx;
                            settings_save(&set, sizeof(set));
                        }
                        fm106_set_banner("DELETED ", code_buf);
                        g_save_del_confirm = 0;
                        dirty_screen = 1;
                    }
                }
            } else if (g_ui_mode == UI_MODE_MENU) {
                if (g_menu_sel == 0) {
                    g_ui_mode = UI_MODE_MIDI;
                    g_midi_sel = 0;
                }
                dirty_screen = 1;
            } else if (g_ui_mode == UI_MODE_MIDI) {
                g_ui_mode = UI_MODE_MENU;
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
                fm106_user_presets_save_flash();
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
                    midi_send(0xC0u | ((g_synth.midi_channel - 1u) & 0x0Fu), (uint8_t)(np % 64u), 0);
                }
            }
        }

        /* Select / Page encoder */
        int32_t enc_s = fm1_enc_take(ENC_SELECT);
        if (enc_s != 0) {
            if (g_ui_mode == UI_MODE_SAVE) {
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
            if (g_ui_mode == UI_MODE_SAVE) {
                int32_t ncur = (int32_t)g_save_cursor + ealgo;
                if (ncur < 0) ncur = 0;
                if (ncur > 14) ncur = 14;
                g_save_cursor = (uint8_t)ncur;
                dirty_screen = 1;
            } else if (g_ui_mode == UI_MODE_NORMAL) {
                int32_t cur_bank = (int32_t)g_synth.patch_idx / 64;
                int32_t cur_slot = (int32_t)g_synth.patch_idx % 64;
                int32_t nb = cur_bank + ealgo;
                while (nb < 0) nb += 4;
                while (nb >= 4) nb -= 4;
                uint32_t np = fm106_bank_first_preset((uint32_t)nb, (uint32_t)cur_slot);
                fm106_load_user_preset(&g_synth, np);
                set.patch = (uint8_t)np;
                dirty_screen = 1;
                midi_send(0xB0u | ((g_synth.midi_channel - 1u) & 0x0Fu), 0, (uint8_t)nb);
                midi_send(0xC0u | ((g_synth.midi_channel - 1u) & 0x0Fu), (uint8_t)(np % 64u), 0);
            } else if (g_ui_mode == UI_MODE_MIDI) {
                if (g_midi_sel == 0) {
                    int32_t nc = (int32_t)g_synth.midi_channel + ealgo;
                    if (nc < 1) nc = 16;
                    if (nc > 16) nc = 1;
                    g_synth.midi_channel = (uint8_t)nc;
                } else {
                    int32_t ns = (int32_t)g_synth.sync_mode + (ealgo > 0 ? 1 : -1);
                    if (ns < 0) ns = 2;
                    if (ns > 2) ns = 0;
                    g_synth.sync_mode = (uint8_t)ns;
                }
                dirty_screen = 1;
                dirty_save = 1;
                t_change = fm1_ms;
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
            }
            dirty_screen = 1;
            }
        }

        /* 4. USB-MIDI In */
        while (midi_get(&st, &d1, &d2)) {
            uint8_t type = st & 0xF0u;
            if (type == 0x90u && d2 > 0) {
                fm106_key_on(&g_synth, d1, d2);
            } else if (type == 0x80u || (type == 0x90u && d2 == 0)) {
                fm106_key_off(&g_synth, d1);
            } else if (type == 0xE0u) {  /* Pitch Bend */
                int32_t raw = ((int32_t)d2 << 7) | (int32_t)d1;
                g_synth.pitch_bend_val = (int16_t)(raw - 8192);
            } else if (type == 0xC0u) {  /* Program Change */
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
                } else if (d1 == 74) {          /* Brightness / Cutoff */
                    g_synth.vcf_cutoff = d2;
                    dirty_screen = 1;
                } else if (d1 == 71) {   /* Resonance */
                    g_synth.vcf_resonance = d2;
                    dirty_screen = 1;
                } else if (d1 == 76) {   /* VCF Env Mod */
                    g_synth.vcf_env_mod = d2;
                    dirty_screen = 1;
                } else if (d1 == 77) {   /* VCF LFO Mod */
                    g_synth.vcf_lfo_mod = d2;
                    dirty_screen = 1;
                } else if (d1 == 78) {   /* VCF Key Track */
                    g_synth.vcf_kbd_track = d2;
                    dirty_screen = 1;
                } else if (d1 == 73) {   /* Attack */
                    g_synth.env_a = d2;
                    for (uint32_t vi = 0; vi < FM106_NUM_VOICES; vi++)
                        fm106_adsr_params(&g_synth.voice[vi].adsr, g_synth.env_a, g_synth.env_d, g_synth.env_s, g_synth.env_r);
                    dirty_screen = 1;
                } else if (d1 == 75) {   /* Decay */
                    g_synth.env_d = d2;
                    for (uint32_t vi = 0; vi < FM106_NUM_VOICES; vi++)
                        fm106_adsr_params(&g_synth.voice[vi].adsr, g_synth.env_a, g_synth.env_d, g_synth.env_s, g_synth.env_r);
                    dirty_screen = 1;
                } else if (d1 == 70) {   /* Sustain */
                    g_synth.env_s = d2;
                    for (uint32_t vi = 0; vi < FM106_NUM_VOICES; vi++)
                        fm106_adsr_params(&g_synth.voice[vi].adsr, g_synth.env_a, g_synth.env_d, g_synth.env_s, g_synth.env_r);
                    dirty_screen = 1;
                } else if (d1 == 72) {   /* Release */
                    g_synth.env_r = d2;
                    for (uint32_t vi = 0; vi < FM106_NUM_VOICES; vi++)
                        fm106_adsr_params(&g_synth.voice[vi].adsr, g_synth.env_a, g_synth.env_d, g_synth.env_s, g_synth.env_r);
                    dirty_screen = 1;
                } else if (d1 == 1) {    /* Mod Wheel -> DCO LFO depth */
                    g_synth.dco_lfo_depth = d2;
                    dirty_screen = 1;
                } else if (d1 == 79) {   /* DCO PWM Depth */
                    g_synth.dco_pwm_depth = d2;
                    dirty_screen = 1;
                } else if (d1 == 9) {    /* Sub Level */
                    g_synth.dco_sub_level = d2;
                    g_synth.dco_sub_on = d2 > 0 ? 1u : 0u;
                    for (uint32_t vi = 0; vi < FM106_NUM_VOICES; vi++) {
                        g_synth.voice[vi].dco.sub_level = d2;
                        g_synth.voice[vi].dco.sub_on = g_synth.dco_sub_on;
                    }
                    dirty_screen = 1;
                } else if (d1 == 13) {   /* Noise Level */
                    g_synth.dco_noise_level = d2;
                    for (uint32_t vi = 0; vi < FM106_NUM_VOICES; vi++)
                        g_synth.voice[vi].dco.noise_level = d2;
                    dirty_screen = 1;
                } else if (d1 == 80) {   /* LFO Rate */
                    g_synth.lfo_rate = d2;
                    dirty_screen = 1;
                } else if (d1 == 81) {   /* LFO Delay */
                    g_synth.lfo_delay = d2;
                    dirty_screen = 1;
                } else if (d1 == 82) {   /* Arp Rate */
                    g_synth.arp_rate = d2;
                    dirty_screen = 1;
                } else if (d1 == 11 || d1 == 7) { /* VCA Level / Volume */
                    g_synth.vca_level = d2;
                    dirty_screen = 1;
                } else if (d1 == 12) {   /* HPF Mode */
                    g_synth.hpf.mode = (d2 / 32u) > 3 ? 3 : (d2 / 32u);
                    dirty_screen = 1;
                } else if (d1 == 93) {   /* Chorus send */
                    g_synth.chorus.mode = d2 > 80 ? FM106_CHORUS_II : (d2 > 40 ? FM106_CHORUS_I : FM106_CHORUS_OFF);
                    dirty_screen = 1;
                } else if (d1 == 119) {  /* Debug telemetry inquiry */
                    midi_send(0xB0, 114, (uint8_t)(uac.alt & 0x7Fu));
                    midi_send(0xB0, 115, (uint8_t)(uac.starts & 0x7Fu));
                    midi_send(0xB0, 116, (uint8_t)(uac.stalls & 0x7Fu));
                    midi_send(0xB0, 117, (uint8_t)(uac.rate_sets & 0x7Fu));
                    midi_send(0xB0, 118, (uint8_t)(uac.flowing & 0x7Fu));
                    midi_send(0xB0, 119, (uint8_t)g_cpu0_load_pct);
                    midi_send(0xB0, 120, (uint8_t)g_cpu1_load_pct);
                    midi_send(0xB0, 121, (uint8_t)(g_cpu0_ticks / 1000u));
                    midi_send(0xB0, 122, (uint8_t)(g_cpu1_ticks / 1000u));
                    midi_send(0xB0, 123, (uint8_t)(uac.overruns & 0x7Fu));
                    midi_send(0xB0, 124, (uint8_t)(uac.underruns & 0x7Fu));
#if DINGHY_DUAL_CORE
                    midi_send(0xB0, 125, (uint8_t)(audio_worker.timeouts & 0x7Fu));
#else
                    midi_send(0xB0, 125, 0);
#endif
                    midi_send(0xB0, 126, (uint8_t)(uac.missed & 0x7Fu));
                    midi_send(0xB0, 127, (uint8_t)(audio_lates & 0x7Fu));
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
        fm1_led_key(BTN_FX, g_synth.chorus.mode != FM106_CHORUS_OFF);
        fm1_led_key(BTN_ARP, g_synth.arp_on);
        fm1_led_key(BTN_SAVE, (g_ui_mode == UI_MODE_SAVE));

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
        int ui_mode_changed = (g_ui_mode != prev_ui_mode);
        prev_ui_mode = g_ui_mode;
        int voice_changed = (voice_active != prev_voice_active);

        if (dirty_screen) {
            if (g_ui_mode == UI_MODE_MENU) {
                draw_menu_main();
            } else if (g_ui_mode == UI_MODE_MIDI) {
                draw_menu_midi();
            } else if (g_ui_mode == UI_MODE_SAVE) {
                draw_save_menu();
            } else {
                draw_top_section(g_synth.patch_idx, g_synth.patch_name);
                draw_faders_section(g_current_page);
                if (ui_mode_changed) {
                    draw_waveform_section();
                }
            }
            dirty_screen = 0;
            t_last_top = fm1_ms;
            prev_voice_active = voice_active;
        } else if (voice_changed && (fm1_ms - t_last_top >= 50u) && (g_ui_mode == UI_MODE_NORMAL)) {
            t_last_top = fm1_ms;
            prev_voice_active = voice_active;
            draw_top_section(g_synth.patch_idx, g_synth.patch_name);
        }

        /* Continuous oscilloscope refresh in performance mode */
        if (g_ui_mode == UI_MODE_NORMAL) {
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
            settings_save(&set, sizeof(set));
            dirty_save = 0;
        }
    }
}

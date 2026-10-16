/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 * FM-106 WebAssembly Emulator
 * Complete FM-106 DCO Polyphonic Synthesizer in the browser.
 */
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define FM106_WEB 1
#define APP_NAME "FM-106"
#define FELUCCA_ID "FM-1_907"
#define FS 44100u
#define HALF_FRAMES 256u
#define FM1_TICKS_PER_US 24u

/* Simulated NOR flash: 1MB */
static uint8_t nor[0x100000];
static uint32_t web_flash_writes;

/* Clock */
static volatile uint32_t fm1_ms;
static uint32_t web_ticks;
static inline uint32_t fm1_ticks(void) { return web_ticks; }
static inline uint32_t fm106_now_ms(void) { return fm1_ms; }
static inline void fm1_irq_off(void) {}
static inline void fm1_irq_on(void) {}

/* Hardware controls & state */
#define FM1_NCOL 11u
static const int8_t FM1_KEYMAP[6][FM1_NCOL] = {
    {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    { 5, 11,  4, 10,  3,  9,  2,  8, -1, -1, -1},
    {34, 35, 36, 37, 38, 40, 39, 13,  7,  6, 12},
    {23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33},
    { 0,  1, 15, 14, 17, 16, 19, 18, 20, 21, 22},
    {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
};
static uint8_t fm1_led_bg[FM1_NCOL];
static volatile uint16_t fm1_led_bg_ns;
static struct {
    uint32_t overruns, underruns;
    uint8_t alt, starts, stalls, rate_sets, flowing, missed;
} uac;
static volatile uint32_t audio_lates = 0;

static struct {
    uint32_t notes, buttons;
} fm1_in;

static uint32_t web_pressed, web_released, web_notes_pressed;
static int32_t web_enc_steps[7];
static uint32_t web_leds_btn, web_leds_key;
static uint32_t web_master_adc = 700;

static uint32_t fm1_input_edges(uint32_t *released)
{
    uint32_t p = web_pressed;
    web_pressed = 0;
    if (released) {
        *released = web_released;
        web_released = 0;
    }
    return p;
}

static uint32_t fm1_input_note_edges(void)
{
    uint32_t n = web_notes_pressed;
    web_notes_pressed = 0;
    return n;
}

static int32_t fm1_enc_take(uint32_t e)
{
    int32_t s = web_enc_steps[e % 7u];
    web_enc_steps[e % 7u] = 0;
    return s;
}

static void fm1_led_key(uint32_t id, int on)
{
    if (id < 14u) {
        if (on) web_leds_btn |= (1u << id);
        else web_leds_btn &= ~(1u << id);
    } else if (id < 14u + 27u) {
        uint32_t k = id - 14u;
        if (on) web_leds_key |= (1u << k);
        else web_leds_key &= ~(1u << k);
    }
}

enum { FM1_ADC_BATT = 3, FM1_ADC_MASTER = 4 };
static int32_t fm1_adc_read(uint32_t ch)
{
    return ch == FM1_ADC_MASTER ? (int32_t)web_master_adc : 620;
}

static void str_cpy(char *dst, const char *src, uint32_t n)
{
    if (!n) return;
    uint32_t i;
    for (i = 0; i + 1 < n && src[i]; i++) dst[i] = src[i];
    dst[i] = '\0';
}

/* Screen */
static uint16_t web_fb[240 * 240];
static uint32_t web_draws;
static void lcd_fill(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint16_t c)
{
    uint32_t i, j;
    uint16_t sw = (uint16_t)((c >> 8) | (c << 8));
    for (j = 0; j < h && y + j < 240u; j++)
        for (i = 0; i < w && x + i < 240u; i++)
            web_fb[(y + j) * 240u + x + i] = sw;
    web_draws++;
}

static void lcd_blit(uint32_t x, uint32_t y, uint32_t w, uint32_t h, const uint16_t *px)
{
    uint32_t i, j;
    for (j = 0; j < h && y + j < 240u; j++)
        for (i = 0; i < w && x + i < 240u; i++)
            web_fb[(y + j) * 240u + x + i] = px[j * w + i];
    web_draws++;
}
static void lcd_sync(void) {}

#define CV_MAX (240u * 96u)
#include "../../firmware/src/gfx.c"

/* Flash Storage */
#define SETTINGS_FLASH_OFFSET 0xFC000u
#define PRESETS_FLASH_OFFSET  0x97000u

static int settings_load(void *dst, uint32_t n)
{
    if (n > 4096u) return -1;
    uint8_t *s = nor + SETTINGS_FLASH_OFFSET;
    if (s[0] == 0xFF && s[1] == 0xFF) return -1;
    memcpy(dst, s, n);
    return 0;
}

static int settings_save(const void *src, uint32_t n)
{
    if (n > 4096u) return -1;
    memset(nor + SETTINGS_FLASH_OFFSET, 0xFF, 4096u);
    memcpy(nor + SETTINGS_FLASH_OFFSET, src, n);
    web_flash_writes++;
    return 0;
}

static int presets_flash_read(void *dst, uint32_t n)
{
    uint8_t *s = nor + PRESETS_FLASH_OFFSET;
    if (s[0] == 0xFF && s[1] == 0xFF) return -1;
    memcpy(dst, s, n);
    return 0;
}

static int presets_flash_save(const void *src, uint32_t n)
{
    if (n > 4096u * 8u) return -1;
    memset(nor + PRESETS_FLASH_OFFSET, 0xFF, 4096u * 8u);
    memcpy(nor + PRESETS_FLASH_OFFSET, src, n);
    web_flash_writes++;
    return 0;
}

/* MIDI ring queue */
static uint8_t midi_q[256][3];
static uint32_t midi_w, midi_r;
static int midi_get(uint8_t *st, uint8_t *d1, uint8_t *d2)
{
    if (midi_r == midi_w) return 0;
    *st = midi_q[midi_r & 255u][0];
    *d1 = midi_q[midi_r & 255u][1];
    *d2 = midi_q[midi_r & 255u][2];
    midi_r++;
    return 1;
}
static void midi_send(uint32_t st, uint32_t d1, uint32_t d2)
{
    (void)st; (void)d1; (void)d2;
}
static int fm106_sysex_send(const uint8_t *p, uint32_t n)
{
    (void)p; (void)n;
    return 0;
}

static void platform_poll(void) {}

#define RING_PUBLISH() ((void)0)

/* Include the synthesizer engine and UI */
#include "../../firmware/src/fm106.c"

/* ---------------- Audio & Time loop ---------------- */
#define RING 4096u
static float ring[RING * 2];
static uint32_t ring_w, ring_r, web_samples_due;
static uint32_t web_booted, web_last_frame;

static void web_ms(void)
{
    static int32_t abuf[2u * HALF_FRAMES];
    fm1_ms++;
    web_ticks += 1000u * FM1_TICKS_PER_US;
    web_samples_due += FS;
    while (web_samples_due >= 1000u * HALF_FRAMES) {
        web_samples_due -= 1000u * HALF_FRAMES;
        memset(abuf, 0, sizeof abuf);
        app_audio(abuf, HALF_FRAMES);
        for (uint32_t k = 0; k < HALF_FRAMES; k++) {
            uint32_t i = (ring_w++ % RING) * 2u;
            ring[i]     = (float)abuf[2u * k]      / 32768.0f;
            ring[i + 1] = (float)abuf[2u * k + 1u] / 32768.0f;
        }
    }
    if (!web_booted) return;
    if (fm1_ms - web_last_frame >= 15u) {
        web_last_frame = fm1_ms;
        fm106_app_frame();
    }
}

#define EXPORT __attribute__((used, visibility("default")))

static float out_l[1024], out_r[1024];
EXPORT float *web_out_l(void) { return out_l; }
EXPORT float *web_out_r(void) { return out_r; }

EXPORT void web_render(uint32_t n)
{
    if (n > 1024u) n = 1024u;
    while (ring_w - ring_r < n)
        web_ms();
    for (uint32_t k = 0; k < n; k++) {
        uint32_t i = (ring_r++ % RING) * 2u;
        out_l[k] = ring[i];
        out_r[k] = ring[i + 1];
    }
}

EXPORT uint8_t *web_nor(void) { return nor; }
EXPORT uint32_t web_nor_size(void) { return sizeof nor; }
EXPORT void web_nor_erase(void) { memset(nor, 0xFF, sizeof nor); }
EXPORT uint32_t web_flash_writes_count(void) { return web_flash_writes; }

EXPORT void web_boot(void)
{
    if (!web_booted) {
        fm106_app_init();
        web_booted = 1;
        web_last_frame = fm1_ms;
    }
}

static const uint8_t WEB_BTN_MAP[14] = {
    [0]  = BTN_FX,        /* FX */
    [1]  = BTN_SEL,       /* SCL / SEL */
    [2]  = BTN_ENV,       /* ENV */
    [3]  = BTN_LFO,       /* LFO */
    [4]  = BTN_EDIT,      /* EDIT */
    [5]  = BTN_GLO,       /* GLO / MENU */
    [6]  = BTN_HOME,      /* HOME */
    [7]  = BTN_SAVE,      /* SAVE */
    [8]  = BTN_ARP,       /* ARP */
    [9]  = BTN_SEQ,       /* SEQ */
    [10] = BTN_PLAY,      /* PLAY */
    [11] = BTN_REC,       /* REC */
    [12] = BTN_OCT_DOWN,  /* OCT- */
    [13] = BTN_OCT_UP,    /* OCT+ */
};

EXPORT void web_buttons(uint32_t labels)
{
    uint32_t m = 0, old = fm1_in.buttons;
    for (uint32_t b = 0; b < 14u; b++) {
        if ((labels >> b) & 1u)
            m |= (1u << WEB_BTN_MAP[b]);
    }
    web_pressed |= (m & ~old);
    web_released |= (old & ~m);
    fm1_in.buttons = m;
}

EXPORT void web_keys(uint32_t mask)
{
    uint32_t old = fm1_in.notes;
    mask &= (1u << 27) - 1u;
    web_notes_pressed |= (mask & ~old);
    fm1_in.notes = mask;
}

static const uint8_t WEB_ENC_MAP[7] = {
    [0] = ENC_SELECT,    /* SELECT: 0 -> 0 */
    [1] = ENC_ALGORITHM, /* ALGORITHM: 1 -> 1 */
    [2] = ENC_PRESETS,   /* PRESETS: 2 -> 6 */
    [3] = ENC_KNOB1,     /* KNOB 1: 3 -> 2 */
    [4] = ENC_KNOB2,     /* KNOB 2: 4 -> 3 */
    [5] = ENC_KNOB3,     /* KNOB 3: 5 -> 4 */
    [6] = ENC_KNOB4,     /* KNOB 4: 6 -> 5 */
};

EXPORT void web_enc(uint32_t role, int32_t n)
{
    if (role < 7u)
        web_enc_steps[WEB_ENC_MAP[role]] += n;
}

EXPORT void web_master(uint32_t v)
{
    web_master_adc = v > 1023u ? 1023u : v;
}

EXPORT uint32_t web_midi(uint32_t status, uint32_t d1, uint32_t d2)
{
    if (status < 0x80u) return 1;
    uint32_t next = (midi_w + 1u) & 255u;
    if (next == (midi_r & 255u)) return 0; /* full */
    midi_q[midi_w & 255u][0] = (uint8_t)status;
    midi_q[midi_w & 255u][1] = (uint8_t)(d1 & 0x7Fu);
    midi_q[midi_w & 255u][2] = (uint8_t)(d2 & 0x7Fu);
    midi_w = next;
    return 1;
}

EXPORT uint32_t web_lit_buttons(void)
{
    uint32_t out = 0;
    for (uint32_t b = 0; b < 14u; b++) {
        uint32_t btn_id = WEB_BTN_MAP[b];
        if ((web_leds_btn >> btn_id) & 1u)
            out |= (1u << b);
    }
    if ((web_leds_btn >> BTN_PLAY) & 1u)
        out |= (1u << 14); /* PLAY green */
    return out;
}

EXPORT uint32_t web_lit_keys(void) { return web_leds_key; }
EXPORT uint32_t web_dim_buttons(void) { return 0x3FFF; /* subtle ambient button backlight */ }
EXPORT uint32_t web_dim_keys(void) { return 0; }
EXPORT uint32_t web_dim_level(void) { return 20; }
EXPORT uint32_t web_breath_buttons(void) { return 0; }
EXPORT uint32_t web_breath_keys(void) { return 0; }
EXPORT uint32_t web_mid_keys(void) { return 0; }
EXPORT uint8_t *web_anim_levels(void) { return 0; }

EXPORT uint16_t *web_screen(void) { return web_fb; }
EXPORT uint32_t web_screen_draws(void) { return web_draws; }
EXPORT uint32_t web_now_ms(void) { return fm1_ms; }
EXPORT uint32_t web_sample_rate(void) { return FS; }

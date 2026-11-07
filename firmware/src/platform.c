/* SPDX-License-Identifier: GPL-3.0-only */
/* Dinghy's platform: everything an FM-1 firmware needs before it can be fun, taken unchanged from
 * SLOOP / Felucca / sloopDX and wired up here. You should not need to touch this file.
 *
 *   boot + boot guard   a firmware that crashes at boot falls back to the USB rescue, then to the ROM
 *   USB rescue          OCT- held at power-on: no audio, only the updater (the web installer still works)
 *   updater             the M-UPGRADE SysEx path: the web installer / the hub can always reach the FM-1
 *   USB-MIDI            in and out (midi_get, midi_send)
 *   USB audio           the output as a class-compliant 16-bit stereo input, 44.1 or 48 kHz
 *   keys, knobs, LEDs   fm1_in.notes / buttons, fm1_input_edges, fm1_enc_take, fm1_led_key (hal/fm1_input.h)
 *   screen              240 x 240, lcd_fill, draw_text_box with FONT_S (16 px) / FONT_L (32 px) (gfx.c)
 *   settings            one 4 KiB flash sector with a CRC (settings_load / settings_save)
 *   crash screen        the PC of a fault, then a reboot
 *
 * The app (dinghy.c) includes this file first and supplies:
 *   static void app_audio(int32_t *out, uint32_t n)   audio ISR: n stereo frames, L R L R, Q15 (+-32767), out is zero
 *   static void app_main(void)                        never returns; calls platform_poll() often (every few ms)
 */
/* the panel by name: buttons (fm1_in.buttons, fm1_input_edges, fm1_led_key: bit / id), the encoders
 * (fm1_enc_take); the 27 note keys are fm1_in.notes bit 0 (F3) .. 26 (G5), LED id 14 + key */
enum { BTN_OCT_DOWN = 0, BTN_OCT_UP = 1, BTN_FX = 2, BTN_SEL = 3, BTN_ENV = 4, BTN_LFO = 5, BTN_EDIT = 6,
       BTN_GLO = 7, BTN_HOME = 8, BTN_SAVE = 9, BTN_ARP = 10, BTN_SEQ = 11, BTN_PLAY = 12, BTN_REC = 13 };
enum { ENC_SELECT = 0, ENC_ALGORITHM = 1, ENC_KNOB1 = 2, ENC_KNOB2 = 3, ENC_KNOB3 = 4, ENC_KNOB4 = 5, ENC_PRESETS = 6 };
#define KEY_LED(k) (14u + (k))

#if defined(FM106_WEB)
/* In web emulator: platform provided by fm106_web.c */
#elif defined(DINGHY_HOST)
#include "../../tests/host.c"                  /* on a computer: a fake FM-1 (tests/host.c) */
#else
#ifndef DINGHY_DUAL_CORE
#define DINGHY_DUAL_CORE 0     /* Single-core release: 100% stable audio engine */
#endif
#include <stdint.h>
#include "fm1_time.h"
#include "fm1_sys.h"
#include "fm1_stub.h"
#include "fm1_irq.h"
#include "fm1_guard.h"
#if DINGHY_DUAL_CORE
#include "fm1_multicore.h"
#endif
#include "fm1_input.h"
#include "fm1_timer.h"
#include "fm1_lcd_hw.h"
#include "fm1_audio.h"
#include "fm1_adc.h"
#include "fm1_uart.h"
#include "bootguard.h"

#ifndef APP_NAME
#define APP_NAME "FM-106"
#endif
#ifndef FELUCCA_ID
#define FELUCCA_ID "FM-1_907"                   /* package identity: 907 = FM-106 */
#endif
#ifndef APP_VER
#define APP_VER "0.0"
#endif

bootguard_t bootguard __attribute__((section(".noinit")));
static volatile uint32_t fm1_ms;                /* milliseconds since power-on (TIMER5) */

#define RING_PUBLISH() __asm__ volatile("" ::: "memory")
#include "libc.c"
#include "lcd.c"
#define CV_MAX (240u * 96u)                     /* the biggest text box drawn at once */
#include "gfx.c"

#define FS 44100u                               /* the sample rate */
#define HALF_FRAMES 256u                        /* I2S half buffer: 5.8 ms (as sloopDX: the USB audio band is set for it) */
#define HALF_WORDS (HALF_FRAMES * 2u)
#define FELUCCA_OTA 1
#define FELUCCA_CDC 0
#define FELUCCA_UAC 1
#define FELUCCA_UAC_48K 1
#define FELUCCA_UAC_AS_FIRST 1                  /* Audio Streaming first in AC header (macOS <=15 AppleUSBAudio fix) */
#define FELUCCA_UAC_DEEP 1                      /* 1024-frame ring (+5.8 ms margin for heavy polyphonic + FX renders) */
#define FELUCCA_UAC_RING_ATTR __attribute__((section(".pool")))
#define FELUCCA_UAC_Q15 1                       /* app_audio() delivers full-scale Q15 (+-32767) directly to uac_tap() */
#define FELUCCA_USB_PRODUCT "FM-106"            /* Enumerate as "FM-106" in DAWs and host audio/MIDI panels */
#define FELUCCA_USB_SERIAL "FM106"              /* Distinct USB serial so CoreMIDI/Windows don't reuse cached "Felucca" */
#define FELUCCA_BCD_HI 0x09                     /* Distinct bcdDevice (9.28) so hosts refresh cached descriptors */
#define FELUCCA_UART 1
#define SXQ 256u
#include "usb.c"
#if FELUCCA_UART
#include "midi_uart.c"
#endif

/* ---- flash: read anywhere; erase / program only the update's staging area and the settings sector */
#define SETTINGS_AT 0x000FC000u                 /* one 4 KiB sector of Felucca's globals area */
#define FM106_PRESETS_FLASH_ADDR 0x00098000u
#define FM106_PRESETS_FLASH_SIZE 0x4000u        /* 16 KiB (4 x 4 KiB sectors) */
#define FM106_SEQ_FLASH_ADDR     0x0009C000u
#define FM106_SEQ_FLASH_SIZE     0x1000u        /* 4 KiB (1 sector: 128-step pattern + p-locks) */
#define BT_ST_A                  0x0009F000u    /* 4 KiB sector: Bluetooth Store A */
#define BT_ST_B                  0x000FE000u    /* 4 KiB sector: Bluetooth Store B */
#define FL_RANGE_OK(off, n) (FL_IN(off, n, FL_OTA_LO, FL_OTA_HI) || \
                             FL_IN(off, n, SETTINGS_AT, SETTINGS_AT + 0x1000u) || \
                             FL_IN(off, n, FM106_PRESETS_FLASH_ADDR, FM106_PRESETS_FLASH_ADDR + FM106_PRESETS_FLASH_SIZE) || \
                             FL_IN(off, n, FM106_SEQ_FLASH_ADDR, FM106_SEQ_FLASH_ADDR + FM106_SEQ_FLASH_SIZE) || \
                             FL_IN(off, n, BT_ST_A, BT_ST_A + 0x1000u) || \
                             FL_IN(off, n, BT_ST_B, BT_ST_B + 0x1000u))
/* CPU1 may run an audio job from XIP: done before the flash leaves XIP (interrupts already off: no new job) */
#if DINGHY_DUAL_CORE
#define FL_QUIESCE() do { if (audio_worker_online) audio_worker_join(); } while (0)
#else
#define FL_QUIESCE() do {} while (0)
#endif
#include "fm1_flash.h"
#include "fm1_clock.h"
static uint8_t flash_ok;
static int st_read(uint32_t off, void *dst, uint32_t n)
{
    uint8_t *d = dst;
    while (n) {
        uint32_t k = n > 256u ? 256u : n, f = irq_save();
        FL_QUIESCE();
        int rc = FL_FAR(fl_read_ram)(off, d, k);
        irq_restore(f);
        if (rc)
            return rc;
        off += k;
        d += k;
        n -= k;
    }
    return 0;
}

static int st_erase(uint32_t off)
{
    uint32_t took;
    return fl_erase4k(off, &took);
}

static int st_prog(uint32_t off, const void *p, uint32_t n)
{
    return fl_write(off, p, n);
}

static uint32_t st_crc32(const void *p, uint32_t n)
{
    static const uint32_t T[16] = {
        0x00000000u, 0x1DB71064u, 0x3B6E20C8u, 0x26D930ACu, 0x76DC4190u, 0x6B6B51F4u, 0x4DB26158u, 0x5005713Cu,
        0xEDB88320u, 0xF00F9344u, 0xD6D6A3E8u, 0xCB61B38Cu, 0x9B64C2B0u, 0x86D3D2D4u, 0xA00AE278u, 0xBDBDF21Cu};
    const uint8_t *b = (const uint8_t *)p;
    uint32_t c = 0xFFFFFFFFu;
    while (n--) {
        c ^= *b++;
        c = (c >> 4) ^ T[c & 15u];
        c = (c >> 4) ^ T[c & 15u];
    }
    return ~c;
}

static uint8_t recovery_active;
#define OTA_IDENTITY (recovery_active ? "FM-1_000" : FELUCCA_ID)
#include "ota.c"
static void recovery_poll(void);
static uint32_t ota_now_ms(void) { return fm1_ms; }
uint32_t fm106_now_ms(void) { return fm1_ms; }   /* public ms timebase (MIDI clock) */
static void ota_idle(void)
{
    fm1_wdt_feed();
    if (recovery_active) recovery_poll();
}
static int ota_in_area(uint32_t off, uint32_t n) { return FL_IN(off, n, OTA_AREA, OTA_AREA + OTA_AREA_LEN); }
static int ota_erase(uint32_t off)
{
    uint32_t took;
    if (!ota_in_area(off, 0x1000u) || (off & 0xFFFu))
        return -8;
    return fl_erase4k(off, &took);
}
static int ota_prog(uint32_t off, const void *p, uint32_t n)
{
    if (!ota_in_area(off, n))
        return -8;
    return fl_write(off, p, n);
}
static int ota_fread(uint32_t off, void *p, uint32_t n) { return st_read(off, p, n); }
static void ota_show(uint32_t step, int32_t code)
{
    static const char *const STEP[] = {"", "PACKAGE", "CHECK HEAD", "LOADER", "CONFIRM", "RESTART"};
    if (recovery_active) return;
    lcd_fill(0, 0, 240, 240, C_BLACK);
    draw_text_box(0, 92, 240, &FONT_S, "UPDATE", C_WHITE, 1);
    if (step < 9u)
        draw_text_box(0, 124, 240, &FONT_S, STEP[step < 6u ? step : 0], C_WHITE, 1);
    else
        draw_text_box(0, 124, 240, &FONT_S, code == 1 ? "DRY RUN OK" : "FAILED", C_WHITE, 1);
}
static void ota_commit(const uint8_t *parm)
{
    bootguard_clear(&bootguard);
    usb_detach();
    fm1_delay_ms(30);
    fm1_enter_update(parm);
}
#include "recovery.c"

/* ---- settings: [magic][length][crc32] then the bytes; settings_save erases the sector (~50 ms with
 * the interrupts off: the sound drops out), so save when nothing plays, not on every knob step */
#define SETTINGS_MAGIC 0x59474E44u              /* "DNGY" */
static uint32_t crc32(const uint8_t *p, uint32_t n)
{
    uint32_t c = 0xFFFFFFFFu, k;
    while (n--) {
        c ^= *p++;
        for (k = 0; k < 8u; k++)
            c = c & 1u ? (c >> 1) ^ 0xEDB88320u : c >> 1;
    }
    return ~c;
}
static int settings_load(void *p, uint32_t n)   /* 0: loaded; else p is untouched */
{
    static uint8_t buf[256];
    uint32_t h[3];
    if (!flash_ok || n > sizeof buf || st_read(SETTINGS_AT, h, sizeof h) || h[0] != SETTINGS_MAGIC || h[1] != n)
        return -1;
    if (st_read(SETTINGS_AT + sizeof h, buf, n) || crc32(buf, n) != h[2])
        return -1;
    memcpy(p, buf, n);
    return 0;
}
static int settings_save(const void *p, uint32_t n)   /* p in RAM, n <= 256 */
{
    uint32_t h[3] = {SETTINGS_MAGIC, n, 0}, took;
    int rc;
    if (!flash_ok || n > 256u)
        return -1;
    h[2] = crc32(p, n);
    if ((rc = fl_erase4k(SETTINGS_AT, &took)) != 0)
        return rc;
    if ((rc = fl_write(SETTINGS_AT + sizeof h, p, n)) != 0)
        return rc;
    return fl_write(SETTINGS_AT, (const uint8_t *)h, sizeof h);   /* the header last: a cut save stays invalid */
}

static int presets_flash_read(void *dst, uint32_t n)
{
    return st_read(FM106_PRESETS_FLASH_ADDR, dst, n);
}

static int presets_flash_save(const void *p, uint32_t n)
{
    uint32_t took;
    int rc;
    if (!flash_ok || n > FM106_PRESETS_FLASH_SIZE)
        return -1;
    for (uint32_t off = 0; off < n; off += 0x1000u) {
        if ((rc = fl_erase4k(FM106_PRESETS_FLASH_ADDR + off, &took)) != 0)
            return rc;
    }
    return fl_write(FM106_PRESETS_FLASH_ADDR, p, n);
}

static int seq_flash_read(void *dst, uint32_t n)
{
    if (!flash_ok || n > FM106_SEQ_FLASH_SIZE)
        return -1;
    return st_read(FM106_SEQ_FLASH_ADDR, dst, n);
}

static int seq_flash_save(const void *p, uint32_t n)
{
    uint32_t took;
    int rc;
    if (!flash_ok || n < 8u || n > FM106_SEQ_FLASH_SIZE)
        return -1;
    if ((rc = fl_erase4k(FM106_SEQ_FLASH_ADDR, &took)) != 0)
        return rc;
    if ((rc = fl_write(FM106_SEQ_FLASH_ADDR + 8u, (const uint8_t *)p + 8u, n - 8u)) != 0)
        return rc;
    return fl_write(FM106_SEQ_FLASH_ADDR, p, 8u);   /* magic + crc last: power-fail safe */
}

static int fm106_sysex_send(const uint8_t *p, uint32_t n)
{
#if FELUCCA_OTA
    if (!*(volatile uint8_t *)&usb.config)
        return -1;
    uint32_t needed = (n + 2u) / 3u + 1u;
    if ((so_w - so_r) + needed >= SXQ)
        return -1;
    return ota_wire_send(p, n);
#else
    (void)p; (void)n;
    return -1;
#endif
}

/* ---- MIDI over USB: 4-byte USB-MIDI packets, cable 0 */
static int midi_get(uint8_t *st, uint8_t *d1, uint8_t *d2)   /* 1: a message (status, data 1, data 2) */
{
    uint32_t pk;
    if (mi_r == mi_w)
        return 0;
    pk = midi_in_q[mi_r % MQ];
    mi_r++;
    *st = (uint8_t)(pk >> 8);
    *d1 = (uint8_t)(pk >> 16);
    *d2 = (uint8_t)(pk >> 24);
    return 1;
}
static void midi_send(uint32_t st, uint32_t d1, uint32_t d2)   /* from the main loop only */
{
    midi_out_event((st >> 4) | st << 8 | (d1 & 0x7Fu) << 16 | (d2 & 0x7Fu) << 24);
}

/* ---- the timer: 10 kHz key scan, milliseconds, USB at 2 kHz. It outranks the audio ISR; nested in a
 * render it only scans, counts and feeds the USB audio stream (as sloopDX main.c) */
static volatile uint8_t in_audio;

/* ---- the hang detector: the main loop feeds the watchdog (fm1_wdt_feed, fm1_wdt_beat); when it has not for
 * FM1_HANG_MS, the timer reports where the interrupted code was (the red screen, VEC 0x3ww: ww = g_where, the main
 * loop's last step) instead of the watchdog's silent reset seconds later. A hang with interrupts off still ends in
 * the watchdog (fm1_boot.p3_rst bit 2, the sysex crash query) */
#define FM1_HANG_MS 2500u
static volatile uint8_t fm1_hang_armed;
volatile uint32_t g_where __attribute__((section(".noinit")));   /* the main loop's step (fm106.c / bt_glue.h) */
static uint32_t g_where_boot;                  /* g_where as the last run left it (a watchdog reset: where it hung) */
static void __attribute__((noinline)) fm1_hang(const uint32_t *f)
{
    fm1_hang_armed = 0;
    fm1_hang_c(g_where, f);
}

void fm1_timer5_irq(uint32_t *f)
{
    static uint32_t sub, owed, last, acc;
    uint32_t t0 = fm1_ticks(), usb_due = sub % 5u == 0u;
    fm1_timer5_ack();
    if (fm1_hang_armed && (uint32_t)(t0 - fm1_wdt_beat) >= FM1_HANG_MS * 1000u * FM1_TICKS_PER_US)
        fm1_hang(f);
    fm1_input_tick();
    acc += t0 - last;
    last = t0;
    while (acc >= 1000u * FM1_TICKS_PER_US) {
        acc -= 1000u * FM1_TICKS_PER_US;
        fm1_ms++;
    }
    if (usb_due)
        owed |= 1u;
#if FELUCCA_UART
    if (sub % 5u == 2u)
        owed |= 2u;
#endif
    if (++sub == 10u)
        sub = 0;
#if FELUCCA_UAC
    if (in_audio || !(owed & 1u))
        uac_service_safe();                     /* on usb_poll ticks, usb_poll() services EP0 first, then UAC */
#endif
    if (in_audio)
        return;
    if (owed & 1u) {
        usb_sie_owner = 1;
        usb_poll();
        usb_sie_owner = 0;
    }
#if FELUCCA_UART
    if (owed & 2u)
        uart_midi_poll();
#endif
    owed = 0;
}
extern void isr_timer5(void);

/* ---- the sound: ALNK0 I2S, two halves; the app renders, the MASTER knob scales, Q15 -> 24 bit (-6 dBFS ceiling) */
static int32_t abuf[2u * HALF_WORDS] __attribute__((aligned(4)));
static volatile int32_t master_q12 = 2048;
static volatile uint32_t audio_lates = 0;
/* Whole-ISR timing (24 MHz TIMER4 ticks; budget = 256 frames @ 44.1 kHz = 139,320):
 * covers render + scope + USB tap (incl. 48 kHz FIR) + MASTER scaling + nested TIMER5.
 * Peaks are held until read (and cleared) by the CC 119 / value 1 telemetry query. */
static volatile uint32_t audio_isr_ticks = 0;     /* last block, whole ISR */
static volatile uint32_t audio_isr_peak = 0;      /* peak-hold, whole ISR */
static volatile uint32_t audio_post_peak = 0;     /* peak-hold, everything after app_audio (USB tap + scaling) */
static void app_audio(int32_t *out, uint32_t n);
static void app_main(void);

void fm1_alnk0_irq(void)                       /* via isr_alnk0 (hal/fm1_isr.S) */
{
    uint8_t p;
    in_audio = 1;
    p = fm1_audio_pending();
    fm1_audio_ack_aux(p);
    if (p & FM1_AUDIO_HALF) {
        uint32_t t_isr0 = fm1_ticks();
        uint32_t half = fm1_audio_free_half();
        int32_t *o = &abuf[half * HALF_WORDS];
        uint32_t i;
        for (i = 0; i < HALF_WORDS; i++)
            o[i] = 0;
        uac_render_start();
        fm1_irq_on();                           /* Allow TIMER5 (priority 4) to nest during audio render & 48k FIR */
        app_audio(o, HALF_FRAMES);
        uint32_t t_post0 = fm1_ticks();
        uac_tap(o, HALF_FRAMES);                /* Lock-free SPSC ring: pristine full-scale digital engine output */
        for (i = 0; i < HALF_WORDS; i++) {
            int32_t v = (o[i] * master_q12) >> 12;
            if (v > 32767) v = 32767;
            else if (v < -32767) v = -32767;
            o[i] = v << 7;
        }
        fm1_audio_ack_half();
        if (fm1_audio_free_half() != half)
            audio_lates++;
        uint32_t t_end = fm1_ticks();
        uint32_t isr_t = t_end - t_isr0, post_t = t_end - t_post0;
        audio_isr_ticks = isr_t;
        if (isr_t > audio_isr_peak) audio_isr_peak = isr_t;
        if (post_t > audio_post_peak) audio_post_peak = post_t;
    }
    in_audio = 0;
}
extern void isr_alnk0(void);

static void fm106_sysex_handle(const uint8_t *p, uint32_t n);
extern uint32_t g_dirty_screen;

/* ---- the main loop's duties: watchdog, USB, the installer's update request, the MASTER knob */
static void platform_poll(void)
{
    static int32_t knob_avg = 512 * 16;
    int32_t a;
    fm1_wdt_feed();
    fm1_hang_armed = 1;
    g_where = 1;
    usb_retry(fm1_ms);
    const uint8_t *sx_p;
    uint32_t sx_n;
    while (ota_frame_get(&sx_p, &sx_n)) {
        if (sx_n >= 2 && sx_p[0] == 0x41) {
            fm106_sysex_handle(sx_p, sx_n);
            ota_frame_done();
        } else {
            ota_service();
            break;
        }
    }
    if (usb.ota_req) {
        usb.ota_req = 0;
        if (flash_ok)
            ota_session();                      /* returns only if nothing was committed */
    }
    if (usb.uboot_req) {
        fm1_audio_stop();
        lcd_fill(0, 0, 240, 240, C_BLACK);
        draw_text_box(0, 110, 240, &FONT_S, "UBOOT (USB)", C_WHITE, 1);
        usb_detach();
        fm1_delay_ms(30);
        bootguard.pending = 0;
        fm1_enter_uboot();
    }
    /* Runtime update mode gesture: OCT- + OCT+ + HOME held 5 seconds */
    static uint32_t upd_t0;
    static uint8_t upd_countdown = 0;
    uint32_t upd_mask = (1u << BTN_OCT_DOWN) | (1u << BTN_OCT_UP) | (1u << BTN_HOME);
    if ((fm1_in.buttons & upd_mask) == upd_mask) {
        uint32_t held = fm1_ms - upd_t0;
        if (held > 2000u && held <= 5000u) {
            uint8_t left = (uint8_t)((5000u - held + 999u) / 1000u);
            if (left != upd_countdown) {
                upd_countdown = left;
                char msg[16];
                msg[0] = 'U'; msg[1] = 'P'; msg[2] = 'D'; msg[3] = 'A'; msg[4] = 'T'; msg[5] = 'E'; msg[6] = ' ';
                msg[7] = 'I'; msg[8] = 'N'; msg[9] = ' '; msg[10] = (char)('0' + left); msg[11] = '\0';
                draw_text_box(0, 110, 240, &FONT_S, msg, RGB(255, 60, 60), 1);
            }
        } else if (held > 5000u) {
            fm1_audio_stop();
            lcd_fill(0, 0, 240, 240, C_BLACK);
            draw_text_box(0, 110, 240, &FONT_S, "UPDATE MODE", C_WHITE, 1);
            usb_detach();
            fm1_delay_ms(30);
            bootguard.pending = 0;
            fm1_stub_request(STUB_REQ_RESCUE);
        }
    } else {
        if (upd_countdown) {
            upd_countdown = 0;
            g_dirty_screen = 1;
        }
        upd_t0 = fm1_ms;
    }
    if (fm1_ms >= 30000u && bootguard.pending)
        bootguard_clear(&bootguard);            /* 30 s up: this firmware boots */
    if ((a = fm1_adc_read(FM1_ADC_MASTER)) >= 0) {
        uint32_t k10;
        knob_avg += (a * 16 - knob_avg) / 8;
        k10 = (uint32_t)(knob_avg / 16);
        master_q12 = (int32_t)((k10 * k10) >> 8);
    }
}

static void fm1_fault(const fm1_crash_t *c)
{
    char b[24];
    uint32_t i, t0;
    const char *lbl[6] = {"VEC : ", "PC  : ", "RETS: ", "SP  : ", "EMU : ", "DBG : "};
    uint32_t v[6] = {c->vec, c->pc, c->rets, c->sp, c->emu, c->dbg};
    fm1_audio_stop();
    fm1_lcd_wait();
    fm1_lcd_deselect();
    lcd_busy = 0;
    lcd_fill(0, 0, 240, 240, RGB(160, 0, 0));
    draw_text_box(0, 15, 240, &FONT_S, APP_NAME " CRASH", C_WHITE, 1);
    for (i = 0; i < 6u; i++) {
        uint32_t k;
        for (k = 0; k < 6u; k++) b[k] = lbl[i][k];
        for (k = 0; k < 8u; k++) b[6 + k] = "0123456789ABCDEF"[(v[i] >> (28u - 4u * k)) & 15u];
        b[14] = 0;
        draw_text_box(10, 45 + i * 20, 220, &FONT_S, b, C_WHITE, 0);
    }
    t0 = fm1_ticks();
    while ((uint32_t)(fm1_ticks() - t0) < 15000u * 1000u * FM1_TICKS_PER_US)
        fm1_wdt_feed();
    fm1_reboot();
}

void fm1_cstart(void)
{
    extern uint32_t _data_start[], _data_end[], _data_load[], _bss_start[], _bss_end[];
    extern uint32_t _pool_start[], _pool_end[], _rt_start[], _rt_end[], _rt_load[];
    uint32_t *s, *d, p3, boot_mode;
    fm1_time_init();
    fm1_reset_reason();
    p3 = fm1_boot.p3_rst;
    fm1_wdt_arm(0x0D);
    fm1_cpu1_stop();                          /* hold CPU1 before clearing/copying application RAM */
    if ((p3 & 1u) && !(p3 & (4u | 0x40u)))
        bootguard_clear(&bootguard);
    boot_mode = bootguard_begin(&bootguard);
    if (boot_mode == BOOT_ROM)
        fm1_enter_uboot();
    fm1_irq_init();
    for (d = _bss_start; d < _bss_end; d++) *d = 0;
    for (d = _pool_start; d < _pool_end; d++) *d = 0;
    for (s = _data_load, d = _data_start; d < _data_end; s++, d++) *d = *s;
    for (s = _rt_load, d = _rt_start; d < _rt_end; s++, d++) *d = *s;
    (void)fm1_clock_stock();                    /* guarantee 360 MHz CPU / 60 MHz LSB across bootloaders */
    fm1_mailbox_clear();
    fm1_guard_enable(FM1_GUARD_STACK | FM1_GUARD_WRITE | FM1_GUARD_BUS
#if !DINGHY_DUAL_CORE && !FELUCCA_BLE
                     | FM1_GUARD_PC
#endif
                     );                          /* CPU1 boots through mask-ROM: PC limits after its handshake */
    fm1_boot.p3_rst = (uint8_t)p3;
    g_where_boot = g_where;
    g_where = 0;
    if (boot_mode == BOOT_RECOVERY || recovery_key())
        recovery_main();                        /* OCT- at power-on: the USB rescue */
    flash_ok = FL_JEDEC_OK(FL_FAR(fl_jedec_ram)());
    ota_boot_cleanup();
    lcd_init();
    lcd_fill(0, 0, 240, 240, C_BLACK);
    fm1_input_init();
    fm1_adc_init();
#if DINGHY_DUAL_CORE
    fm1_multicore_start();                  /* bounded handshake; failure keeps serial rendering */
#if !FELUCCA_BLE
    /* CPU1 starts through mask-ROM before it reaches our RAM worker. Restrict
     * instruction fetch only after that bootstrap has returned (or been
     * stopped on timeout), or the ready handshake bootloops with DBG 0x400
     * (c1_pc_limit_err_r). BLE builds omit this because JieLi BLE controller
     * calls Mask-ROM routines during scan/link. */
    fm1_guard_enable(FM1_GUARD_PC);
#endif
#endif
    fm1_audio_init(abuf, HALF_WORDS, isr_alnk0, 3);   /* (abuf is zero: .bss) */
    usb_start();
#if FELUCCA_UART
    uart_midi_init();
#endif
    fm1_timer5_start(isr_timer5, 4);
    fm1_guard_lock_top();
    fm1_irq_enable_all();
    app_main();
    for (;;)
        platform_poll();
}
#endif


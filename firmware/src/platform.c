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

#ifdef DINGHY_HOST
#include "../../tests/host.c"                  /* on a computer: a fake FM-1 (tests/host.c) */
#else
#ifndef DINGHY_DUAL_CORE
#define DINGHY_DUAL_CORE 1     /* Dual-core CPU0/CPU1 parallel audio engine */
#endif
#include <stdint.h>
#include "fm1_time.h"
#include "fm1_sys.h"
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
#define FELUCCA_UART 1
#include "usb.c"
#if FELUCCA_UART
#include "midi_uart.c"
#endif

/* ---- flash: read anywhere; erase / program only the update's staging area and the settings sector */
#define SETTINGS_AT 0x000FC000u                 /* one 4 KiB sector of Felucca's globals area */
#define FM106_PRESETS_FLASH_ADDR 0x00098000u
#define FM106_PRESETS_FLASH_SIZE 0x2000u        /* 8 KiB (2 x 4 KiB sectors) */
#define FL_RANGE_OK(off, n) (FL_IN(off, n, FL_OTA_LO, FL_OTA_HI) || \
                             FL_IN(off, n, SETTINGS_AT, SETTINGS_AT + 0x1000u) || \
                             FL_IN(off, n, FM106_PRESETS_FLASH_ADDR, FM106_PRESETS_FLASH_ADDR + FM106_PRESETS_FLASH_SIZE))
#include "fm1_flash.h"
static uint8_t flash_ok;
static int st_read(uint32_t off, void *dst, uint32_t n)
{
    uint8_t *d = dst;
    while (n) {
        uint32_t k = n > 256u ? 256u : n, f = irq_save();
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
    if ((rc = fl_erase4k(FM106_PRESETS_FLASH_ADDR, &took)) != 0)
        return rc;
    if ((rc = fl_erase4k(FM106_PRESETS_FLASH_ADDR + 0x1000u, &took)) != 0)
        return rc;
    return fl_write(FM106_PRESETS_FLASH_ADDR, p, n);
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
void fm1_timer5_irq(void)
{
    static uint32_t sub, owed, last, acc;
    uint32_t t0 = fm1_ticks(), usb_due = sub % 5u == 0u;
    fm1_timer5_ack();
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
    uac_service_safe();
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
static void app_audio(int32_t *out, uint32_t n);
static void app_main(void);

void fm1_alnk0_irq(void)                       /* via isr_alnk0 (hal/fm1_isr.S) */
{
    uint8_t p;
    in_audio = 1;
    p = fm1_audio_pending();
    fm1_audio_ack_aux(p);
    if (p & FM1_AUDIO_HALF) {
        uint32_t half = fm1_audio_free_half();
        int32_t *o = &abuf[half * HALF_WORDS];
        uint32_t i;
        for (i = 0; i < HALF_WORDS; i++)
            o[i] = 0;
        uac_render_start();
        fm1_irq_on();                           /* Allow TIMER5 (priority 4) to nest during audio render */
        app_audio(o, HALF_FRAMES);
        fm1_irq_off();
        uac_tap(o, HALF_FRAMES);                /* USB Audio records pristine digital engine output */
        fm1_irq_on();
        for (i = 0; i < HALF_WORDS; i++) {
            int32_t v = (o[i] * master_q12) >> 12;
            if (v > 32767) v = 32767;
            else if (v < -32767) v = -32767;
            o[i] = v << 7;
        }
        fm1_audio_ack_half();
        if (fm1_audio_free_half() != half)
            audio_lates++;
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
    char b[12];
    uint32_t i, t0, v[3] = {c->vec, c->pc, c->rets};
    fm1_audio_stop();
    lcd_fill(0, 0, 240, 240, RGB(160, 0, 0));
    draw_text_box(0, 20, 240, &FONT_S, APP_NAME " CRASH", C_WHITE, 1);
    for (i = 0; i < 3u; i++) {
        uint32_t k;
        for (k = 0; k < 8u; k++) b[k] = "0123456789ABCDEF"[(v[i] >> (28u - 4u * k)) & 15u];
        b[8] = 0;
        draw_text_box(10, 60 + i * 20, 220, &FONT_S, b, C_WHITE, 0);
    }
    t0 = fm1_ticks();
    while ((uint32_t)(fm1_ticks() - t0) < 8000u * 1000u * FM1_TICKS_PER_US)
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
    fm1_core1_stop();                          /* hold CPU1 before clearing/copying application RAM */
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
    fm1_mailbox_clear();
    fm1_guard_enable(FM1_GUARD_STACK | FM1_GUARD_WRITE | FM1_GUARD_BUS
#if !DINGHY_DUAL_CORE
                     | FM1_GUARD_PC
#endif
                     );                          /* CPU1 boots through mask-ROM: PC limits after its handshake */
    fm1_boot.p3_rst = (uint8_t)p3;
    if (boot_mode == BOOT_RECOVERY || recovery_key())
        recovery_main();                        /* OCT- at power-on: the USB rescue */
    flash_ok = FL_FAR(fl_jedec_ram)() == 0x856014u;
    ota_boot_cleanup();
    lcd_init();
    lcd_fill(0, 0, 240, 240, C_BLACK);
    fm1_input_init();
    fm1_adc_init();
#if DINGHY_DUAL_CORE
    fm1_multicore_start();                  /* bounded handshake; failure keeps serial rendering */
    /* CPU1 starts through mask-ROM before it reaches our RAM worker. Restrict
     * instruction fetch only after that bootstrap has returned (or been
     * stopped on timeout), or the ready handshake bootloops with DBG 0x400
     * (c1_pc_limit_err_r). */
    fm1_guard_enable(FM1_GUARD_PC);
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

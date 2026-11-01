/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 * Copyright (C) 2026 Charles Vestal (VDC14 and SYSVDD, the second core held on reset) */
/* FM-1 system HAL: P33 access, watchdog, reset reason, reboot / UBOOT entry
 * No vendor code; call with interrupts off
 * (no IRQs are enabled yet, so nothing else touches P33).
 *
 *   fm1_reset_reason()   snapshot P3_RST_SRC / RST_SRC (call first thing)
 *   fm1_wdt_arm(t)       reset-mode watchdog, t = 0xA 1 s .. 0xF 32 s
 *   fm1_wdt_feed()       in the main loop
 *   fm1_reboot()         chip reset through P33 (P3_PR_PWR bit 4)
 *   fm1_enter_uboot()    "usb_update_mode" at 0x01C7FD80 + chip reset
 *   fm1_enter_update(p)  112-byte UPDATA_PARM record at 0x01C7FD88 + core reset
 *                        (the SPL then runs the staged update loader)
 *   fm1_core_reset()     PWR_CON core reset
 *   fm1_updata_parm_clear()  zero the record (CRC 0: ignored on a warm reset)
 *   fm1_mailbox_clear()  zero 0x01C7FD80..0x01C7FDFF (boot info, mailbox) at cold start
 *   fm1_mem_readable(a, n) / fm1_peek8(a)   debug reads of RAM / XIP only
 *   fm1_vdc14_raise(l)   VDC14 up to level l, a step at a time; fm1_vdc14() reads it
 *   fm1_sysvdd_raise(l)  SYSVDD (the core and SRAM rail) the same; fm1_sysvdd() reads it
 */
#pragma once
#include <stdint.h>
#include "fm1_cc.h"
#include "fm1_time.h"
#include "fm1_cpu1_stop.h"

#define FM1_P33_CON (*(volatile uint32_t *)0x13E08u)
#define FM1_P33_DAT (*(volatile uint32_t *)0x13E0Cu)
#define FM1_RST_SRC (*(volatile uint32_t *)0x100C0u)
#define FM1_BOOT_STATE ((volatile uint8_t *)0x01C7FD80u)
#define FM1_PWR_CON (*(volatile uint32_t *)0x10000u)
#define FM1_UPDATA_PARM ((volatile uint8_t *)0x01C7FD88u)
#define FM1_UPDATA_PARM_LEN 112u

static uint32_t fm1_p33_timeouts;

static uint8_t fm1__p33_xfer(uint8_t b)
{
    uint32_t n = 100000u;
    FM1_P33_DAT = b;
    FM1_P33_CON |= 1u << 4;
    (void)FM1_P33_CON;                           /* stands in for csync */
    while (FM1_P33_CON & (1u << 1))
        if (--n == 0) {
            fm1_p33_timeouts++;
            break;
        }
    return (uint8_t)FM1_P33_DAT;
}

static void fm1__p33_cs(uint32_t a)
{
    if (a & 0x8000u) {
        FM1_P33_CON |= (1u << 0) | (1u << 8);    /* RTC domain */
    } else {
        FM1_P33_CON &= ~(1u << 8);
        FM1_P33_CON |= 1u << 0;
    }
}

static void fm1__p33_op(uint32_t op, uint32_t a, uint8_t d)   /* 0 wr, 1 or, 2 and, 3 xor */
{
    fm1__p33_cs(a);
    fm1__p33_xfer((uint8_t)((op << 5) | ((a >> 8) & 3u)));
    fm1__p33_xfer((uint8_t)a);
    fm1__p33_xfer(d);
    FM1_P33_CON &= ~((1u << 0) | (1u << 8));
}

static uint8_t fm1_p33_read(uint32_t a)
{
    uint8_t v;
    fm1__p33_cs(a);
    fm1__p33_xfer((uint8_t)(0x80u | ((a >> 8) & 3u)));
    fm1__p33_xfer((uint8_t)a);
    v = fm1__p33_xfer(0x5E);
    FM1_P33_CON &= ~((1u << 0) | (1u << 8));
    return v;
}
static void fm1_p33_write(uint32_t a, uint8_t d) { fm1__p33_op(0, a, d); }
static void fm1_p33_or(uint32_t a, uint8_t d) { fm1__p33_op(1, a, d); }
static void fm1_p33_and(uint32_t a, uint8_t d) { fm1__p33_op(2, a, d); }

/* ------------------------------------------------------- VDC14, SYSVDD --- */
/* VDC14 (P3_ANA_CON6 [2:0]): 1.25 V + 50 mV a level; SYSVDD (P3_ANA_CON9 [3:0], the core and SRAM):
 * 0.93 V + 30 mV a level. The boot loader leaves 3 (1.40 V) and 11 (1.26 V); M-VAVE's own firmware
 * (V15) raises them to 7 (1.60 V) and 15 (1.38 V) as it starts, at the same 360 MHz (fm1-x0x#21,
 * fc79's reading of its __hw_power_init). At the boot loader's, under load (both cores, or WiFi in
 * fm1-radio), some units read flash and SRAM wrong. Up only, a level per 100 us, VDC14 first (as
 * Jangada's fm1_core_supply): SYSVDD is regulated from it. */
#define FM1_P3_ANA_CON6 0x06u
#define FM1_P3_ANA_CON9 0x09u
static uint32_t fm1_vdc14(void) { return fm1_p33_read(FM1_P3_ANA_CON6) & 7u; }
static void fm1_vdc14_raise(uint32_t level)
{
    uint32_t v = fm1_vdc14();
    while (v < level && v < 7u) {
        v++;
        fm1_p33_write(FM1_P3_ANA_CON6, (uint8_t)((fm1_p33_read(FM1_P3_ANA_CON6) & ~7u) | v));
        fm1_delay_us(100);
    }
}
static uint32_t fm1_sysvdd(void) { return fm1_p33_read(FM1_P3_ANA_CON9) & 15u; }
static void fm1_sysvdd_raise(uint32_t level)
{
    uint32_t v = fm1_sysvdd();
    while (v < level && v < 15u) {
        v++;
        fm1_p33_write(FM1_P3_ANA_CON9, (uint8_t)((fm1_p33_read(FM1_P3_ANA_CON9) & ~15u) | v));
        fm1_delay_us(100);
    }
}

/* ----------------------------------------------------- the power setup --- */
/* The rest of M-VAVE's power setup (V15's __hw_power_init, the SDK's cpu.a power_hw.c read with llvm-dis: its
 * power_dcdc_ldo_config and the writes after it). The boot loaders leave the 1.3 V supply on its linear regulator
 * (P3_ANA_CON0 bit 2) and the DC-DC (bit 3) off; V15 moves it onto the DC-DC and tunes it (ANA_CON7 0xA5, ANA_CON8
 * 0x96), sets VDDIOM 4 (3.2 V), and puts the WiFi 1.2 V regulator on automatic (WLDO12_AUTO 0xC0). The always-on P33
 * keeps them across a reset, so an FM-1 that last ran V15 has them and one that has not lacks them: on an FM-1
 * (2026-10-23) WiFi heard far less without them (scans empty, 1-4 frames a second against 12-21). The order is the
 * SDK's: the regulator on, 1 ms, the DC-DC on, 1 ms, the regulator off, 1 ms; then the tuning. Not taken: the sleep
 * path's settings (low-voltage detect, wake-up pins, the low-power timers). After fm1_vdc14_raise / fm1_sysvdd_raise
 * (VDC14 and SYSVDD are part of the same setup). Returns 1 if the DC-DC was off (and is on now), 0 if it was on. */
#define FM1_P3_ANA_CON0 0x00u                   /* bit 2 LDO13_EN, bit 3 DCDC13_EN, bit 7 RC_250k_EN */
#define FM1_P3_ANA_CON1 0x01u
#define FM1_P3_ANA_CON2 0x02u                   /* bit 6 VDDOK_DIS */
#define FM1_P3_ANA_CON5 0x05u                   /* [3:0] VDDIOM, [5:4] VDDIOW, [7:6] VDDIO drive */
#define FM1_P3_ANA_CON7 0x07u                   /* the DC-DC's bias, duty, oscillator */
#define FM1_P3_ANA_CON8 0x08u                   /* the DC-DC's V2I, current sense, comparator, zero, ramp */
#define FM1_P3_ANA_KEEP 0x16u
#define FM1_P3_WLDO12_AUTO 0x74u
static int fm1_power_stock(void)
{
    int moved = 0;
    if (!(fm1_p33_read(FM1_P3_ANA_CON0) & 0x08u)) {
        fm1_p33_or(FM1_P3_ANA_CON0, 0x04u);             /* the regulator on (it is) */
        fm1_delay_us(1000);
        fm1_p33_or(FM1_P3_ANA_CON0, 0x08u);             /* the DC-DC on beside it */
        fm1_delay_us(1000);
        fm1_p33_and(FM1_P3_ANA_CON0, (uint8_t)~0x04u);  /* the regulator off */
        fm1_delay_us(1000);
        moved = 1;
    }
    fm1_p33_write(FM1_P3_ANA_CON1, 0x20u);
    fm1_p33_write(FM1_P3_ANA_CON7, 0xA5u);
    fm1_p33_write(FM1_P3_ANA_CON8, 0x96u);
    fm1_p33_write(FM1_P3_ANA_CON5, (uint8_t)((fm1_p33_read(FM1_P3_ANA_CON5) & 0xC0u) | 4u));   /* VDDIOM 4, VDDIOW 0 */
    fm1_p33_or(FM1_P3_ANA_CON0, 0x80u);
    fm1_p33_and(FM1_P3_ANA_KEEP, 0xC0u);
    fm1_p33_or(FM1_P3_ANA_CON2, 0x40u);
    fm1_p33_write(FM1_P3_WLDO12_AUTO, 0xC0u);
    return moved;
}

/* ------------------------------------------------------------ watchdog --- */
#define FM1_P3_WDT_CON 0x80u
#define FM1_P3_VLD_KEEP 0x17u
#define FM1_P3_RST_SRC 0x12u
#define FM1_P3_PR_PWR 0xA0u

static void fm1_wdt_arm(uint8_t t)
{
    fm1_p33_write(FM1_P3_WDT_CON, 0);
    fm1_p33_and(FM1_P3_VLD_KEEP, (uint8_t)~0x40u);           /* WDT_EXPT_EN = 0: plain reset */
    fm1_p33_write(FM1_P3_WDT_CON, (uint8_t)((t & 0x0Fu) | 0x40u));   /* timeout, reset mode, clear */
    fm1_p33_or(FM1_P3_WDT_CON, 0x10);                        /* enable */
}
/* the last feed (TIMER4): a hang detector in the timer ISR reports a main loop that stopped feeding (fm1_hang) */
static volatile uint32_t fm1_wdt_beat;
static void fm1_wdt_feed(void)
{
    fm1_wdt_beat = FM1_T4_CNT;
    fm1_p33_or(FM1_P3_WDT_CON, 0x40);
}
static void fm1_wdt_stop(void) { fm1_p33_write(FM1_P3_WDT_CON, 0); }

/* the watchdog feed and a busy wait with nothing called, for code that runs from RAM (.ram_text) while the flash
 * must not be touched: the Bluetooth radio's trim (JieLi's, ~500 ms of high current) runs its delays here */
FM1_INLINE void fm1__p33_xfer_inl(uint8_t b)
{
    uint32_t n = 100000u;
    FM1_P33_DAT = b;
    FM1_P33_CON |= 1u << 4;
    (void)FM1_P33_CON;
    while ((FM1_P33_CON & (1u << 1)) && --n)
        ;
}
FM1_INLINE void fm1_wdt_feed_inl(void)            /* fm1_wdt_feed: P33 "or" WDT_CON, 0x40 */
{
    fm1_wdt_beat = FM1_T4_CNT;
    FM1_P33_CON &= ~(1u << 8);
    FM1_P33_CON |= 1u << 0;
    fm1__p33_xfer_inl((uint8_t)((1u << 5) | ((FM1_P3_WDT_CON >> 8) & 3u)));
    fm1__p33_xfer_inl((uint8_t)FM1_P3_WDT_CON);
    fm1__p33_xfer_inl(0x40);
    FM1_P33_CON &= ~((1u << 0) | (1u << 8));
}
FM1_INLINE void fm1_busy_us_inl(uint32_t us)      /* TIMER4; the watchdog fed every millisecond */
{
    uint32_t t0 = FM1_T4_CNT, fed = t0, span = us * FM1_TICKS_PER_US;
    while ((uint32_t)(FM1_T4_CNT - t0) < span)
        if ((uint32_t)(FM1_T4_CNT - fed) >= 1000u * FM1_TICKS_PER_US) {
            fed = FM1_T4_CNT;
            fm1_wdt_feed_inl();
        }
}

/* ---------------------------------------------------- reset and reboot --- */
static struct {
    uint8_t p3_rst;      /* bit0 power-on, 1 VDDIO low, 2 WDT, 3 VCM, 4 long press, 5 1.2 V, 6 soft (P33) */
    uint32_t rst_src;    /* bit5 soft (PWR_CON) */
    uint8_t wdt_con;     /* as found at boot */
} fm1_boot;

static void fm1_reset_reason(void)
{
    fm1_boot.p3_rst = fm1_p33_read(FM1_P3_RST_SRC);
    fm1_boot.rst_src = FM1_RST_SRC;
    fm1_boot.wdt_con = fm1_p33_read(FM1_P3_WDT_CON);
}

static void fm1_reboot(void)
{
    __asm__ volatile("cli");
    fm1_cpu1_stop();                                    /* the second core held (fm1_cpu1_stop.h) */
    fm1_p33_or(FM1_P3_PR_PWR, 0x10);
    for (;;)
        ;
}

static void fm1_enter_uboot(void)
{
    static const char k[16] = "usb_update_mode";
    uint32_t i;
    __asm__ volatile("cli");
    fm1_cpu1_stop();
    /* CPU0 write limits (fm1_guard.h) may cover the mailbox: drop them all */
    if (!(*(volatile uint32_t *)0x1EEE240u & 1u))
        *(volatile uint32_t *)0x1EEE240u = 0xE7u;
    *(volatile uint32_t *)0x1EEE348u = 0;
    for (i = 0; i < 16u; i++)
        FM1_BOOT_STATE[i] = (uint8_t)k[i];
    fm1_p33_or(FM1_P3_PR_PWR, 0x10);
    for (;;)
        ;
}

FM1_INLINE void fm1_core_reset(void)
{
    fm1_cpu1_stop();                                    /* the second core held first: it must not run old
                                                         * code while the loader writes */
    FM1_PWR_CON |= 0x10u;
    for (;;)
        ;
}

FM1_INLINE void fm1_enter_update(const uint8_t *parm)
{
    uint32_t i;
    __asm__ volatile("cli");
    fm1_cpu1_stop();                                    /* before writing the bootloader's update record */
    if (!(*(volatile uint32_t *)0x1EEE240u & 1u))       /* drop CPU0 write limits, as fm1_enter_uboot */
        *(volatile uint32_t *)0x1EEE240u = 0xE7u;
    *(volatile uint32_t *)0x1EEE348u = 0;
    for (i = 0; i < FM1_UPDATA_PARM_LEN; i++)
        FM1_UPDATA_PARM[i] = parm[i];
    fm1_core_reset();
}

FM1_INLINE void fm1_updata_parm_clear(void)
{
    uint32_t i;
    for (i = 0; i < FM1_UPDATA_PARM_LEN; i++)
        FM1_UPDATA_PARM[i] = 0;
}

FM1_INLINE void fm1_mailbox_clear(void)   /* before fm1_guard_enable (it write-protects the top) */
{
    uint32_t *s;
    for (s = (uint32_t *)0x01C7FD80u; s < (uint32_t *)0x01C7FE00u; s++)
        *s = 0;
}

/* RAM and the XIP window only: SFR reads can have side effects */
static int fm1_mem_readable(uint32_t a, uint32_t n)
{
    return (a >= 0x01C00000u && a + n <= 0x01C80000u && a + n >= a) ||
           (a >= 0x02000000u && a + n <= 0x02100000u && a + n >= a);
}
FM1_INLINE uint8_t fm1_peek8(uint32_t a) { return *(const volatile uint8_t *)a; }


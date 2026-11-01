/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 * Copyright (C) 2026 Charles Vestal (fm1_irq_unmask, fm1_uptime_ms, fm1_crash_ctx) */
/* FM-1 interrupt / exception HAL.
 *
 *   fm1_irq_init()        first thing in cstart: all ICFG off, pendings and
 *                         exception causes cleared, all 128 vectors -> fatal
 *                         stubs (fm1_vec.S), vector 1 (CPU
 *                         exception) enabled at prio 7, ETM on, div0 trap
 *                         off (see below).
 *   fm1_irq_attach(n, h, prio)   h = asm wrapper (fm1_isr.S), prio 0..7
 *   fm1_irq_enable_all()  icfg bit 8 + sti, after every source is set up
 *
 * The application supplies fm1_fault(const fm1_crash_t *) which reports the
 * crash (LCD) and resets; the record survives in .noinit for the next boot.
 * fm1_crash_ctx keeps the registers of the last FM1_CRASH_CTX_N crashes beside
 * it (an entry is one of them when its count is within the last
 * FM1_CRASH_CTX_N of fm1_crash.count).
 */
#pragma once
#include <stdint.h>
#define FM1_IRQ_TARGET 1

#define FM1_VEC ((volatile uint32_t *)0x01C7FE00u)
#define FM1_ICFG(n) (*(volatile uint32_t *)(0x1EEF100u + 4u * ((n) >> 3)))
#define FM1_IPND(i) (*(volatile uint32_t *)(0x1EEF180u + 4u * (i)))
#define FM1_ILAT_SET (*(volatile uint32_t *)0x1EEF1A0u)
#define FM1_ILAT_CLR (*(volatile uint32_t *)0x1EEF1A4u)
#define FM1_EMU_CON (*(volatile uint32_t *)0x1EEF0D0u)
#define FM1_EMU_MSG (*(volatile uint32_t *)0x1EEF0D4u)
#define FM1_C1_EMU_MSG (*(volatile uint32_t *)0x1EEF2D4u)
#define FM1_ETM_CON (*(volatile uint32_t *)0x1EEF1C0u)
#define FM1_ETM_PC(i) (*(volatile uint32_t *)(0x1EEF1C4u + 4u * (i)))
#define FM1_DBG_WR_EN (*(volatile uint32_t *)0x1EEE240u)
#define FM1_DBG_MSG (*(volatile uint32_t *)0x1EEE244u)
#define FM1_DBG_MSG_CLR (*(volatile uint32_t *)0x1EEE248u)
#define FM1_DBG_EN (*(volatile uint32_t *)0x1EEE340u)
/* DBG_MSG's bits by core (Melodee's reading): CPU1's PC limit, bus and write errors, then CPU0's */
#define FM1_DBG_C1 ((1u << 6) | (1u << 8) | (1u << 10) | (1u << 11) | (7u << 16))
#define FM1_DBG_C0 ((1u << 7) | (1u << 9) | (1u << 12) | (1u << 13) | (7u << 19))

enum { FM1_IRQ_EXCEPTION = 1, FM1_IRQ_ALNK0 = 11, FM1_IRQ_SPI1 = 16, FM1_IRQ_UART1 = 20,
       FM1_IRQ_SARADC = 24, FM1_IRQ_SPI2 = 37, FM1_IRQ_TIMER5 = 63, FM1_IRQ_SOFT0 = 120 };

static inline uint32_t fm1_icfg(void) { uint32_t v; __asm__ volatile("%0 = icfg" : "=r"(v)); return v; }
static inline void fm1_icfg_set(uint32_t v) { __asm__ volatile("icfg = %0" ::"r"(v) : "memory"); }
static inline void fm1_irq_off(void) { __asm__ volatile("cli" ::: "memory"); }
static inline void fm1_irq_on(void) { __asm__ volatile("csync\n\tsti" ::: "memory"); }
static inline uint32_t fm1_cnum(void) { uint32_t v; __asm__ volatile("%0 = cnum" : "=r"(v)); return v; }   /* 0 / 1 */
static inline uint32_t fm1_usp(void) { uint32_t v; __asm__ volatile("%0 = usp" : "=r"(v)); return v; }
static inline uint32_t fm1_ssp(void) { uint32_t v; __asm__ volatile("%0 = ssp" : "=r"(v)); return v; }

#define FM1_CRASH_MAGIC 0x43525348u          /* "CRSH" */
typedef struct {
    uint32_t magic, count, vec, pc, rets, emu, dbg, sp, psr, icfg, uptime_ms;
    uint32_t etm[4];
    uint32_t early;                          /* consecutive crashes < 3 s after boot */
} fm1_crash_t;
/* Not static: .noinit holds whatever the last run (or power-on) left, and a
 * file-local object would let the compiler assume its zero initial value and
 * fold the magic check away. */
fm1_crash_t fm1_crash __attribute__((section(".noinit")));

/* the registers at the last crashes, the oldest overwritten: which register held what when the CPU faulted (a
 * pointer one bit off with the memory it came from intact, sp a word off). count is the crash's fm1_crash.count */
#ifndef FM1_CRASH_CTX_N
#define FM1_CRASH_CTX_N 4u                        /* a firmware short of .noinit may keep fewer (80 B each) */
#endif
typedef struct {
    uint32_t count, pc, r[16], usp, ssp;
} fm1_crash_ctx_t;
fm1_crash_ctx_t fm1_crash_ctx[FM1_CRASH_CTX_N] __attribute__((section(".noinit")));

extern const char fm1_fatal_stubs[];
static void fm1_fault(const fm1_crash_t *c);   /* application: report, then reset */

static void fm1_irq_init(void)
{
    uint32_t i;
    for (i = 0; i < 32u; i++)
        *(volatile uint32_t *)(0x1EEF100u + 4u * i) = 0;   /* every source off (CPU0 bank) */
    FM1_ILAT_CLR = 0xFFu;
    FM1_EMU_MSG = 0xFFFFFFFFu;
    if (!(FM1_DBG_WR_EN & 1u))
        FM1_DBG_WR_EN = 0xE7u;                              /* unlock */
    FM1_DBG_MSG_CLR = 0xFFFFFFFFu;
    for (i = 0; i < 128u; i++)
        FM1_VEC[i] = (uint32_t)(uintptr_t)(fm1_fatal_stubs + 6u * i);
    FM1_ICFG(1) = (FM1_ICFG(1) & ~0xF0u) | 0xF0u;           /* exception: enable, prio 7 */
    /* EMU traps (unaligned access bit 0, divide-by-zero bit 2, stack limit bit 3,
     * and FPU IEEE flags bits 16..20): off by default, following AC79 SDK debug.c.
     * Compiler and Mask-ROM Bluetooth libraries may use unaligned accesses and
     * single-precision FPU flags. */
    FM1_EMU_CON = 0;
    FM1_ETM_CON |= 1u;                                      /* branch trace for the report */
}

static void fm1_irq_attach(uint32_t n, void (*h)(void), uint32_t prio)   /* IRQs off */
{
    uint32_t sh = (n & 7u) * 4u;
    FM1_VEC[n] = (uint32_t)(uintptr_t)h;
    FM1_ICFG(n) = (FM1_ICFG(n) & ~(0xFu << sh)) | ((((prio & 7u) << 1) | 1u) << sh);
}
static void fm1_irq_mask(uint32_t n) { if (n >= 128u) return; FM1_ICFG(n) &= ~(1u << ((n & 7u) * 4u)); }
static inline uint32_t fm1_uptime_ms(void) { return *(volatile uint32_t *)0x10804u / 24000u; }   /* TIMER4, as fm1_fault_c */

static void fm1_irq_enable_all(void)
{
    fm1_icfg_set(fm1_icfg() | 0x100u);
    fm1_irq_on();
}

#if FM1_CPU1
static int fm1_cpu1_fault_c(const uint32_t *f);   /* fm1_cpu1.h: 1 = the second core's fault, handled */
#endif

/* called from fm1_fatal_common (fm1_vec.S) with the saved frame:
 * f[0..15] r0..r15, f[16] reti, f[17] rete, f[18] retx, f[19] stub+6,
 * f[20] psr, f[21] icfg, f[22] usp, f[23] ssp, f[24] sp, f[25] interrupted rets.
 * Returns only to resume CPU0 after a fault of the second core's (FM1_CPU1) */
void fm1_fault_c(uint32_t *f)
{
    static volatile uint32_t in_fault;
    uint32_t i, count = fm1_crash.magic == FM1_CRASH_MAGIC ? fm1_crash.count : 0;
    uint32_t early = fm1_crash.magic == FM1_CRASH_MAGIC ? fm1_crash.early : 0;
#if FM1_CPU1
    if (fm1_cpu1_fault_c(f))
        return;
#elif DINGHY_DUAL_CORE
    uint32_t cpu = fm1_cnum(), dbg = FM1_DBG_MSG;
    if (cpu != 0) {
        /* CPU1 took exception; record into crash context before parking */
        fm1_crash.magic = FM1_CRASH_MAGIC;
        fm1_crash.count = count + 1u;
        fm1_crash.vec = 0x100u | ((f[19] - (uint32_t)(uintptr_t)fm1_fatal_stubs) / 6u - 1u);
        fm1_crash.pc = f[16];
        fm1_crash.rets = f[25];
        fm1_crash.emu = FM1_C1_EMU_MSG;
        fm1_crash.dbg = dbg;
        fm1_crash.sp = f[24];
        fm1_crash.psr = f[20];
        fm1_crash.icfg = f[21];
        fm1_crash.uptime_ms = *(volatile uint32_t *)0x10804u / 24000u;
        fm1_crash_ctx_t *x = &fm1_crash_ctx[fm1_crash.count % FM1_CRASH_CTX_N];
        x->count = fm1_crash.count;
        x->pc = f[16];
        for (i = 0; i < 16u; i++) x->r[i] = f[i];
        x->usp = f[22];
        x->ssp = f[23];
        for (;;) {
            __asm__ volatile("nop; nop; nop; nop");
        }
    }
    if (!cpu && (dbg & FM1_DBG_C1) && !(dbg & FM1_DBG_C0)) {
        if (!(FM1_DBG_WR_EN & 1u))
            FM1_DBG_WR_EN = 0xE7u;
        FM1_DBG_MSG_CLR = dbg;
        return;
    }
#endif
    fm1_irq_off();
    if (in_fault++)                           /* fault while reporting: reset at once */
        *(volatile uint32_t *)0x10000u |= 1u << 4;
    fm1_crash.magic = FM1_CRASH_MAGIC;
    fm1_crash.count = count + 1u;
    fm1_crash.vec = (f[19] - (uint32_t)(uintptr_t)fm1_fatal_stubs) / 6u - 1u;
    fm1_crash.pc = f[16];
    fm1_crash.rets = f[25];
    fm1_crash.emu = FM1_EMU_MSG;
    fm1_crash.dbg = FM1_DBG_MSG;
    fm1_crash.sp = f[24];
    fm1_crash.psr = f[20];
    fm1_crash.icfg = f[21];
    fm1_crash.uptime_ms = *(volatile uint32_t *)0x10804u / 24000u;   /* TIMER4 since boot */
    for (i = 0; i < 4u; i++)
        fm1_crash.etm[i] = FM1_ETM_PC(i);
    fm1_crash.early = fm1_crash.uptime_ms < 3000u ? early + 1u : 0u;
    {
        fm1_crash_ctx_t *x = &fm1_crash_ctx[fm1_crash.count % FM1_CRASH_CTX_N];
        x->count = fm1_crash.count;
        x->pc = f[16];
        for (i = 0; i < 16u; i++)
            x->r[i] = f[i];                   /* frame: r0..r15 first (fm1_vec.S) */
        x->usp = f[22];
        x->ssp = f[23];
    }
    fm1_fault(&fm1_crash);
    for (;;)
        ;
}

static void __attribute__((noinline)) fm1_hang_c(uint32_t where, const uint32_t *f)
{
    uint32_t i, count = fm1_crash.magic == FM1_CRASH_MAGIC ? fm1_crash.count : 0;
    uint32_t usp = fm1_usp(), ssp = fm1_ssp();
    fm1_irq_off();
    fm1_crash.magic = FM1_CRASH_MAGIC;
    fm1_crash.count = count + 1u;
    fm1_crash.vec = 0x300u | (where & 0xFFu);
    fm1_crash.pc = f[4];                        /* reti: where the main loop was */
    fm1_crash.rets = f[5];
    fm1_crash.emu = FM1_EMU_MSG;
    fm1_crash.dbg = FM1_DBG_MSG;
    fm1_crash.sp = usp;
    fm1_crash.psr = f[6];
    fm1_crash.icfg = fm1_icfg();
    fm1_crash.uptime_ms = *(volatile uint32_t *)0x10804u / 24000u;
    for (i = 0; i < 4u; i++)
        fm1_crash.etm[i] = FM1_ETM_PC(i);
    fm1_crash.early = 0;
    {
        fm1_crash_ctx_t *x = &fm1_crash_ctx[fm1_crash.count % FM1_CRASH_CTX_N];
        x->count = fm1_crash.count;
        x->pc = f[4];
        for (i = 0; i < 4u; i++)
            x->r[i] = f[i];
        for (i = 4; i < 16u; i++) {
            uint32_t ptr = usp + 4u * (i - 4u);
            x->r[i] = (ptr >= 0x01C00000u && ptr < 0x01C80000u)
                          ? *(const uint32_t *)(uintptr_t)ptr : 0u;
        }
        x->usp = usp;
        x->ssp = ssp;
    }
    fm1_fault(&fm1_crash);
    for (;;)
        ;
}


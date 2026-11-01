/* SPDX-License-Identifier: GPL-3.0-only
 * BLE MIDI test: 2026 DEADACTIVE
 * Drum machine fork: 2026 DEADACTIVE */
/* What the JieLi BT libraries (AC79 SDK V1.2.0, pinned in tools/ble_libs.py) call besides the OS (ble_os.c) and
 * the SDK members linked from their archives (system.a lbuf / circular_buf, cpu.a wlc / encryption, lib_ccm_aes,
 * newlib libcompiler_rt). Each group says why its definitions are safe; signatures are the SDK's (its headers, or
 * the libraries' own IR declarations where no header has them: fm1-lsdj 548ce73 docs/ble/LINK_NOTES.md,
 * docs/ble/undefined-step0.txt).
 * Rules: no flash writes anywhere (config writes are refused); no register access here (hal/fm1_ble_hal.h). */

/* ---- libc pieces the libraries call (ours: libc.c has memset / memcpy / memcmp only) ---- */
void *memmove(void *d, const void *s, unsigned n)
{
    uint8_t *dp = d;
    const uint8_t *sp = s;
    if (dp < sp)
        while (n--)
            *dp++ = *sp++;
    else
        while (n--)
            dp[n] = sp[n];
    return d;
}
unsigned strlen(const char *s) { unsigned n = 0; while (s[n]) n++; return n; }
char *strcpy(char *d, const char *s)
{
    char *r = d;
    do
        *d++ = *s;
    while (*s++);
    return r;
}
int strcmp(const char *a, const char *b)
{
    while (*a && *a == *b) {
        a++;
        b++;
    }
    return (int)(uint8_t)*a - (int)(uint8_t)*b;
}
/* logging: off, in ble_lto_stubs.c (bitcode: LTO sees the empty functions and drops the calls and their text) */

/* ---- interrupts: the libraries' critical sections nest; their radio IRQs go into our vector table ---- */
void local_irq_disable(void) { fm1_ble_irq_off(); }
void local_irq_enable(void) { fm1_ble_irq_on(); }
void __local_irq_disable(void) __attribute__((alias("local_irq_disable")));   /* (one body each: the timing of */
void __local_irq_enable(void) __attribute__((alias("local_irq_enable")));     /* the off stretches, fm1_ble_hal.h) */
void sys_local_irq_disable(void) __attribute__((alias("local_irq_disable")));
void sys_local_irq_enable(void) __attribute__((alias("local_irq_enable")));
int cpu_irq_disabled(void) { return fm1_ble_irq_depth != 0; }
int cpu_in_irq(void) { return 0; }                       /* (only CRC16's hardware path and asserts ask) */
void request_irq(unsigned char index, unsigned char priority, void (*handler)(void), unsigned char cpu_id)
{
    (void)cpu_id;                                        /* one core runs everything here */
    if (priority > 2u) {                                 /* BT stays below the audio (3) and the timer (4) (spec);
                                                          * the libraries ask for 2 (fm1-lsdj docs/ble/LINK_NOTES.md) */
        priority = 2u;
#if BLE_CONSOLE
        ble_diag.irq_lowered++;
#endif
    }
    local_irq_disable();
    core_irq_attach(index, handler, priority);           /* the SDK's handlers are interrupt functions (rti):
                                                          * checked in the link (fm1-lsdj docs/ble/LINK_NOTES.md) */
    local_irq_enable();
}
void unrequest_irq(uint8_t index, uint8_t cpu_id) { (void)cpu_id; core_irq_mask(index); }
void bit_set_ie(unsigned char index, uint8_t cpuid) { (void)cpuid; core_irq_unmask(index); }
void bit_clr_ie(unsigned char index, uint8_t cpuid) { (void)cpuid; core_irq_mask(index); }
uint8_t irq_read(uint32_t index) { return (uint8_t)!core_irq_is_masked(index); }

/* ---- delays and clocks (clock values read from the hardware, read-only) ---- */
/* in RAM (.ram_text): the radio calibration calls delay_us while the flash must not be read (app_ble.ld) */
#define BLE_RAM __attribute__((section(".ram_text.ble"), noinline))
/* As the SDK's (cpu.a delay.c): a counted loop, no timer. The radio calibration calls delay_us while it trims the
 * crystal and the clocks, so a delay must not depend on a timer still counting. Calibrated against TIMER4 once at
 * start-up (ble_delay_calibrate), rounded up: a delay is never shorter than asked. */
static uint32_t ble_loops_per_us = 64;
BLE_RAM static void ble_spin(uint32_t n)
{
    while (n--)
        fm1_ble_nop();
}
void ble_delay_calibrate(void)                           /* (interrupts off: they would only lengthen the run) */
{
    uint32_t t0, dt;
    local_irq_disable();
    t0 = fm1_ble_ticks();
    ble_spin(100000);
    dt = fm1_ble_ticks() - t0;                           /* 24 ticks per us */
    local_irq_enable();
    if (dt)
        ble_loops_per_us = (100000u * FM1_TICKS_PER_US + dt - 1u) / dt;
}
BLE_RAM void delay_us(unsigned int us)
{
    uint32_t t0 = fm1_ble_ticks();
    ble_spin(us * ble_loops_per_us);
#if BLE_CONSOLE
    ble_diag.delays++;
    ble_diag.last_us = us;
    if (us && fm1_ble_ticks() == t0)
        ble_diag.t4_stalls++;
#else
    (void)t0;
#endif
}
BLE_RAM void hw_udelay(unsigned int us) { delay_us(us); }
BLE_RAM void *__wrap_memcpy(void *d, const void *s, unsigned n)   /* (build.py --wrap=memcpy) */
{
    uint8_t *o = d;
    const uint8_t *i = s;
    while (n--)
        *o++ = *i++;
    return d;
}
BLE_RAM void delay(unsigned int n) { volatile unsigned int i = n; while (i--) ; }   /* (a spin count, as the SDK's) */
int clk_get(const char *name)
{
    if (!strcmp(name, "sys")) return (int)fm1_clk_hz(FM1_CLK_SYS);
    if (!strcmp(name, "hsb")) return (int)fm1_clk_hz(FM1_CLK_HSB);
    if (!strcmp(name, "lsb") || !strcmp(name, "spi")) return (int)fm1_clk_hz(FM1_CLK_LSB);
    if (!strcmp(name, "sfc")) return (int)fm1_clk_hz(FM1_CLK_SFC);
    if (!strcmp(name, "uart")) return 48000000;          /* UART is on PLL48M */
    if (!strcmp(name, "osc") || !strcmp(name, "timer")) return 24000000;
    return 0;                                            /* sdram, sd: none here */
}
int clk_get_osc_cap(void) { return 0; }                  /* the SDK's returns 0 too (cpu.a clock.c) */

/* ---- low power: never sleeps here; registrations are accepted and ignored ---- */
static uint32_t lp_dummy;
void *low_power_get(void *priv, const void *ops) { (void)priv; (void)ops; return &lp_dummy; }
void low_power_put(void *priv) { (void)priv; }
void low_power_request(char *name) { (void)name; }
void low_power_exit_request(void) {}
int low_power_on(void) { return 0; }
uint8_t low_power_get_default_osc_type(void) { return 0; }
uint8_t low_power_get_osc_type(void) { return 0; }
void low_power_reset_osc_type(uint8_t type) { (void)type; }
void low_power_hw_unsleep_lock(void) {}
void low_power_hw_unsleep_unlock(void) {}
int32_t low_power_trace_drift(uint32_t usec) { (void)usec; return 0; }
int power_is_poweroff_post(void) { return 0; }

/* ---- faults: a library assert or reset request is kept (.noinit ble_diag.fatal: 1 assert, 2 reset request; the
 * console's `ble` after the restart), then a clean reboot. Nothing else: this often runs with interrupts off, where
 * the console would never drain (and its full-ring wait would hang; tests/ble/check_ble_fatal.py). ---- */
static void ble_fatal(uint32_t why)
{
    ble_diag.fatal = why;
    ble_diag.heap_high = heap_high;               /* (ble_os.c's, as they were then: ble_service copies them only */
    ble_diag.alloc_fails = alloc_fails;           /* between runs; read, not called: interrupts may be off) */
    core_audio_stop();
    core_ble_fatal(why, ble_diag.task, ble_diag.stage, heap_high, alloc_fails);
    core_reboot();                                        /* fallback */
}
void cpu_assert_debug(void) { ble_fatal(1); }
void P33_SYSTEM_RESET(void) { ble_fatal(2); }
const int config_asser = 1;                              /* the libraries' asserts on: a fault shows, not corrupts */

/* ---- CRC and chip id (software; the same results as the SDK's cpu.a crc16.c: CRC-16/XMODEM, init 0) ---- */
uint16_t crc16_sw(const void *ptr, uint32_t len)
{
    const uint8_t *p = ptr;
    uint32_t crc = 0, k;
    while (len--) {
        crc ^= (uint32_t)*p++ << 8;
        for (k = 0; k < 8u; k++)
            crc = crc & 0x8000u ? ((crc << 1) ^ 0x1021u) & 0xFFFFu : (crc << 1) & 0xFFFFu;
    }
    return (uint16_t)crc;
}
uint16_t CRC16(const void *ptr, uint32_t len) { return crc16_sw(ptr, len); }
uint16_t chip_crc16(const void *ptr, uint32_t len) { return crc16_sw(ptr, len); }
uint16_t crc_get_16bit(const void *src, uint32_t len) { return crc16_sw(src, len); }
uint32_t crc_get_32bit(const char *src) { return src ? crc16_sw(src, strlen(src)) : 0u; }
static uint8_t ble_mac[6];                               /* random static address, made at ble_start */
uint16_t get_chip_id(void) { return (uint16_t)(ble_mac[0] | ble_mac[1] << 8); }

/* ---- config store: READ ONLY. The BT MAC (id 102) is ours; everything else "not stored" -> library defaults ---- */
#define CFG_BT_MAC_ADDR 102u
int syscfg_read(uint16_t item_id, void *buf, uint16_t len)
{
    if (item_id == CFG_BT_MAC_ADDR && len >= 6u) {
        memcpy(buf, ble_mac, 6);
        return 6;
    }
    return 0;
}
int syscfg_write(uint16_t item_id, void *buf, uint16_t len) { (void)item_id; (void)buf; (void)len; return 0; }
int norflash_ioctl(void *device, uint32_t cmd, uint32_t arg) { (void)device; (void)cmd; (void)arg; return -1; }
void *bt_vm_interface(void) { return 0; }                /* no VM: bonding off (TCFG_BLE_SECURITY_EN 0) */
uint32_t sdfile_get_disk_capacity(void) { return 0x100000u; }

/* ---- the radio's Wi-Fi-side calibration (SDK apps/common/net/wifi_conf.c, user_cfg.c; board demo_ble defaults).
 * The SDK versions read stored trims and write what they used back to VM; these never write: no stored trim, so
 * the radio calibrates fresh at each start. ---- */
#include "ble_rf_tables.c"
/* the radio calibration (wl_rf_common wf_rf_trim, ~500 ms) brackets itself with these. The SDK's (apps/common/net/
 * wifi_conf.c, CONFIG_RF_TRIM_CODE_AT_RAM) stop flash access (norflash_enter_spi_code); here: interrupts off, as
 * every interrupt handler runs from XIP, and the trim itself runs from RAM. The return value is the SDK's for code
 * linked at RAM: no moved copy. */
BLE_RAM uint8_t *enter_wifi_rf_trim_region(uint32_t **start_of_region)
{
    (void)start_of_region;
    fm1_ble_irq_off();
    ble_diag.trim = 1;                                   /* (a plain RAM store: no call into XIP) */
    return 0;
}
BLE_RAM void exit_wifi_rf_trim_region(uint32_t **start_of_region, uint8_t *rf_trim_code_run_addr)
{
    (void)start_of_region;
    (void)rf_trim_code_run_addr;
    ble_diag.trim = 2;
    fm1_ble_irq_on();
}
const uint8_t WIFI_PA_ENABLE = 0, wifi_temperature_drift_trim_on = 0;
/* the radio calibration the stock firmware stored (ble_vm.c) is used instead of running the trim, which hangs or
 * crashes here (fm1-lsdj docs/ble/DEVICE_STEPS.md, round 4): with a valid record, wf_rf_common_init skips wf_rf_trim */
const uint8_t RFIinitUseTrimValue = 1;
static uint8_t ble_rf_trim[BLE_VM_RF_TRIM_LEN];
static uint32_t ble_rf_trim_at;                          /* flash offset of the record used, 1 the kept copy, 0 none */
#define BLE_VM_FROM 0xE5000u                             /* the plain area the stock config store lives in */
#define BLE_VM_TO 0xFC000u

/* Stock JieLi VM RF calibration record (68 bytes) with valid inner CRC 0x750F */
static const uint8_t STOCK_RF_TRIM[BLE_VM_RF_TRIM_LEN] = {
    0xFF, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xE9, 0x03, 0x00, 0x00, 0xD2, 0xFF, 0xFF, 0xFF,
    0x17, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x2E, 0x37, 0xFF, 0xF8, 0x00, 0xF6, 0x00, 0x00,
    0x92, 0x00, 0x00, 0x00, 0xF6, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x89, 0x3B, 0xFF, 0xFF, 0x89, 0x3B,
    0xD0, 0x02, 0x28, 0x00, 0xE0, 0x02, 0x2C, 0x00, 0xB6, 0x02, 0x1A, 0x00, 0x92, 0x02, 0x0A, 0x00,
    0x0F, 0x75, 0x00, 0x00
};

/* scan the config store; 1 = a record for the radio (the store's, else the copy the firmware kept: core_rf_keep_get).
 * The store's is handed to the firmware to keep whenever its copy is missing or another (ble_vm_rf_pick) */
static __attribute__((unused)) int ble_rf_trim_load(void)
{
    static uint8_t buf[512];
    uint8_t kept[BLE_VM_RF_TRIM_LEN], found[BLE_VM_RF_TRIM_LEN];
    uint32_t off, from;
    int keep;
    ble_rf_trim_at = 0;
    for (off = BLE_VM_FROM; off < BLE_VM_TO; off += sizeof buf - (4u + BLE_VM_RF_TRIM_LEN)) {
        uint32_t n = BLE_VM_TO - off < sizeof buf ? BLE_VM_TO - off : sizeof buf;
        uint8_t rec[BLE_VM_RF_TRIM_LEN];
        core_wdt_feed();
        if (core_flash_read(off, buf, n))
            break;
        if (ble_vm_rf_trim(buf, n, rec)) {
            memcpy(found, rec, sizeof rec);
            ble_rf_trim_at = off;
        }
        if (n < sizeof buf)
            break;
    }
    from = ble_vm_rf_pick(ble_rf_trim_at ? found : 0, core_rf_keep_get(kept) ? kept : 0, ble_rf_trim, &keep);
    if (keep)
        core_rf_keep_put(ble_rf_trim);
    if (from == 2u)
        ble_rf_trim_at = 1;
    if (!from) {
        memcpy(ble_rf_trim, STOCK_RF_TRIM, sizeof STOCK_RF_TRIM);
        core_rf_keep_put(ble_rf_trim);
        ble_rf_trim_at = 1;
        return 1;
    }
    return 1;
}
uint8_t RTDebugLevel = 0;
int wifi_get_rf_trim_data(void *info, int size)
{
    if (!ble_rf_trim_at || size != (int)BLE_VM_RF_TRIM_LEN)
        return -1;
    memcpy(info, ble_rf_trim, BLE_VM_RF_TRIM_LEN);
    return 0;
}
int wifi_set_rf_trim_data(void *info, int size) { (void)info; (void)size; return -1; }
void wifi_get_xosc(uint8_t *xosc) { xosc[0] = 0x0B; xosc[1] = 0x0B; }   /* board.c demo default: crystal caps */
int wifi_get_pa_trim_data(uint8_t *pa)
{
    static const uint8_t PA[7] = {1, 7, 4, 7, 11, 1, 7};                 /* board.c demo default */
    memcpy(pa, PA, 7);
    return 1;                                                            /* (as the SDK: no auto tune) */
}
void wifi_get_mcs_dgain(uint8_t *g) { memset(g, 0, 20); }
uint32_t adc_add_sample_ch(uint32_t ch) { (void)ch; return 0; }         /* (temperature drift trim is off) */
uint32_t adc_get_value(uint32_t ch) { (void)ch; return 0; }
unsigned int gpio_set_uart1(unsigned int ch) { (void)ch; return 0; }    /* no HCI UART */
unsigned int gpio_close_uart1(void) { return 0; }
void aes_hw_lock(void) {}                                               /* one user: the stack's own task */
void aes_hw_unlock(void) {}

/* ---- features compiled out (classic BT, TWS, A2DP, SBC, BT-over-air update): never reached in BLE-only ---- */
void bredr_esco_standard_tws_link_open(void *a, void *b, int c, uint8_t d) { (void)a; (void)b; (void)c; (void)d; }
void bredr_esco_standard_tws_link_close(void *a) { (void)a; }
void channel_packet_clear(void) {}
int sbc_cal_energy(void *p, int n) { (void)p; (void)n; return 0; }
void *lmp_ch_update_resume_hdl;
uint8_t tws_auto_pair_enable;
const int sniff_long_interval = 0, CONFIG_BTSTACK_SUPPORT_AAC = 0, CONFIG_TWS_POWER_BALANCE_ENABLE = 0,
          config_update_mode = 0;
int CONFIG_TWS_SUPER_TIMEOUT;
int task_kill(const char *name) { (void)name; return 0; }

/* after each BT task run (ble_os_after_run): a task that blocked inside local_irq_disable would hand the core its
 * interrupts off (TIMER5, USB, the UBOOT key all dead until the watchdog). Count it and turn interrupts back on. */
static void ble_irq_check(int task)
{
    if (!ble_os_stack_intact((uint32_t)task))     /* its 2 KB stack overflowed: stop before the neighbour breaks */
        ble_fatal(3);
    if (fm1_ble_irq_depth) {
#if BLE_CONSOLE
        ble_diag.irq_leaks++;
#endif
        fm1_ble_irq_depth = 1;
        local_irq_enable();
    }
}

/* the SDK's application task: btstack_init runs here, not in the core's main loop, so the SDK calls that block
 * (os_sem_pend on a semaphore its tasks post later) really wait; then it serves its queue (callbacks posted to it) */
static void ble_app_core(void *arg)
{
    int msg[8];
    (void)arg;
    ble_stage(4);
    btstack_init();
    ble_stage(5);
    for (;;)
        os_taskq_pend("taskq", msg, 8);
}

/* ---- heap names the libraries use ---- */
void *ram_malloc(unsigned long size) { return ble_malloc((unsigned)size); }
void ram_free(void *p) { ble_free(p); }

/* ---- stack events (struct bt_event: ble_sdk.h) to ble_central.c; the last one is kept for the console ---- */
static volatile uint32_t ble_last_event, ble_events;
static void ble_on_event(int from, struct bt_event *e);
int bt_event_notify(int from, struct bt_event *event)
{
    ble_events++;
    ble_last_event = (uint32_t)from << 8 | event->event;
    ble_on_event(from, event);
    return 0;
}


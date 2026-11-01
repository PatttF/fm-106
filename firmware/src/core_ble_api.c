/* SPDX-License-Identifier: GPL-3.0-only */
/* FM-106: Core BLE API bridge for JieLi BLE stack */
#include "ble/ble_core.h"
#include "fm1_ble_hal.h"

extern uint32_t _pool_start[];
extern char _sstack_top[];

uint32_t core_ms(void) { return fm1_ms; }
void core_wdt_feed(void) { fm1_wdt_feed(); }
void core_reboot(void) { fm1_reboot(); }
void core_audio_stop(void) { fm1_audio_stop(); }

void core_ble_fatal(uint32_t why, uint32_t task, uint32_t stage, uint32_t heap_hi, uint32_t alloc_f)
{
    char b[32];
    uint32_t i, t0;
    const char *lbl[5] = {"WHY  : ", "TASK : ", "STAGE: ", "HEAP : ", "FAIL : "};
    uint32_t v[5] = {why, task, stage, heap_hi, alloc_f};
    fm1_audio_stop();
    fm1_lcd_wait();
    fm1_lcd_deselect();
    lcd_busy = 0;
    lcd_fill(0, 0, 240, 240, RGB(160, 0, 0));
    draw_text_box(0, 15, 240, &FONT_S, APP_NAME " BT FATAL", C_WHITE, 1);
    for (i = 0; i < 5u; i++) {
        uint32_t k;
        for (k = 0; k < 7u; k++) b[k] = lbl[i][k];
        for (k = 0; k < 8u; k++) b[7 + k] = "0123456789ABCDEF"[(v[i] >> (28u - 4u * k)) & 15u];
        b[15] = 0;
        draw_text_box(10, 45 + i * 22, 220, &FONT_S, b, C_WHITE, 0);
    }
    t0 = fm1_ticks();
    while ((uint32_t)(fm1_ticks() - t0) < 15000u * 1000u * FM1_TICKS_PER_US)
        fm1_wdt_feed();
    fm1_reboot();
}

void core_irq_attach(uint32_t n, void (*h)(void), uint32_t prio)
{
    fm1_guard_unlock_top();
    fm1_irq_attach(n, h, prio);
    fm1_guard_lock_top();
}

void core_ble_stack_window(void)
{
    fm1_guard_stack_window((uint32_t)(uintptr_t)_pool_start, (uint32_t)(uintptr_t)_sstack_top);
}

void core_irq_mask(uint32_t n) { fm1_irq_mask(n); }
void core_irq_unmask(uint32_t n) { fm1_irq_unmask(n); }
uint32_t core_irq_is_masked(uint32_t n) { return fm1_irq_is_masked(n); }

void core_midi_out(uint32_t pkt) { midi_out_event(pkt); }

int core_flash_read(uint32_t off, void *buf, uint32_t n)
{
    if (!flash_ok || off < 0x93000u || off > 0x100000u || n > 0x100000u - off)
        return -1;
    return st_read(off, buf, n);
}

#if BLE_CONSOLE
uint32_t core_audio_late(void) { return 0; }
uint32_t core_audio_gap_us(void) { return 0; }
uint32_t core_ui_frames(void) { return 0; }
uint32_t core_ble_in_drops(void) { return bt_in.drops; }
#endif

int core_ble_slow(void) { return bt_set_slow(g_synth.seq.playing); }
void core_con_puts(const char *s) { (void)s; }
void core_ble_mac(uint8_t mac[6]) { bt_mac(mac); }
int core_rf_keep_get(uint8_t rec[68]) { return bt_rf_io(rec, 0); }
void core_rf_keep_put(const uint8_t rec[68]) { (void)bt_rf_io((uint8_t *)rec, 1); }
void core_ble_midi_in(uint32_t pkt) { bt_midi_in(pkt); }
void core_fml_rx(const uint8_t *p, uint32_t len, uint32_t t) { (void)p; (void)len; (void)t; }

void core_ble_link(int up)
{
    bt_link(up);
}


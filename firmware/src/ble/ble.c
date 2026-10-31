/* SPDX-License-Identifier: GPL-3.0-only
 * BLE MIDI test: 2026 DEADACTIVE
 * Drum machine fork: 2026 DEADACTIVE */
/* The BLE module: its own compilation unit (tools/build.py, FELUCCA_BLE=1), so the core's unit compiles exactly as
 * without BLE (gate 5: tools/check_default_build.sh). It reaches the core only through ble_core.h
 * (firmware/src/core_ble_api.c). No screen of its own: the console's `ble` reports (ble_status, ble_central.c). */
#include <stdint.h>
#include "fm1_time.h"
#include "fm1_ble_hal.h"
#include "ble/ble_core.h"
#include "ble/ble_sdk_abi.h"
#include "ble/ble_sdk.h"
uint32_t ble_os_now_ms(void) { return core_ms(); }
#define BLE_DIAG_MAGIC 0xB1E0D1A6u
static struct {
    uint32_t magic, stage, task, starts, trim, delays, last_us, t4_stalls, irq_leaks, fatal;
    uint32_t heap_high, alloc_fails, irq_lowered;
} ble_diag __attribute__((section(".noinit"), unused));
static __attribute__((unused)) void ble_stage(uint32_t s)
{
    if (ble_diag.magic != BLE_DIAG_MAGIC) {
        ble_diag.magic = BLE_DIAG_MAGIC;
        ble_diag.starts = 0;
    }
    if (s == 1u) {
        ble_diag.starts++;
        ble_diag.trim = ble_diag.delays = ble_diag.last_us = ble_diag.t4_stalls = ble_diag.irq_leaks = ble_diag.fatal = 0;
        ble_diag.heap_high = ble_diag.alloc_fails = ble_diag.irq_lowered = 0;
    }
    ble_diag.stage = s;
}
#include "ble/ble_os.c"
#include "ble/ble_midi.c"
#include "ble/ble_scan.c"
#include "ble/ble_vm.c"
#include "ble/ble_port.c"
/* the BT libraries' heap, in .pool: the measured high-water + 25 % (budget, user-approved 2026-10-09): 10824 B on the
 * FM-1 started, connected to the SMC-Mixer and receiving (docs/ble/DEVICE_STEPS.md), so 13530, rounded up to 512 B;
 * with two links at once (ROLE HOST + LINK) 17920 B ran out (measured on the FM-1 2026-10-22: 17880 B high with the
 * controller and an FM-1 linked; a link lost, then the whole heap in use and a library assert, `fatal 1`): 23552 B,
 * what the pool keeps (8 KB spare) with the BT task table one slot smaller (ble_os.c NTASK) */
#define BLE_HEAP_BYTES 23552u
static uint8_t ble_heap[BLE_HEAP_BYTES] __attribute__((section(".pool")));
static uint8_t ble_on;
int ble_started(void) { return ble_on; }
/* how far the last run got, kept over a crash reboot (.noinit) for the console's `ble`: stage 1 OS ready, 2 stack
 * window, 3 MAC set, 4 in btstack_init, 5 btstack_init returned, 6 profile init, 7 BT_STATUS_INIT_OK; trim = 1 in
 * the radio calibration, 2 after it (ble_port.c's RAM hooks); task = the BT task running (+1), 0 = the core;
 * delays / last_us = delay_us calls and the last one's length; t4_stalls = delays during which TIMER4 did not move;
 * irq_leaks = times a BT task handed back with interrupts still off (ble_port.c re-enables them); fatal = 1 a
 * library assert, 2 a library reset request, 3 a task's stack overflow (`task` names it) (ble_port.c ble_fatal);
 * heap_high / alloc_fails = the heap's most in use and its refused requests; irq_lowered = BT IRQ requests above
 * priority 2, attached at 2 (ble_port.c request_irq) */

#include "ble/ble_gatt.c"                      /* the device role's GATT table and advertising data */
#include "ble/ble_central.c"
extern volatile uint32_t g_where;                  /* the core's hang report: the main loop's step */
void ble_service(void)                             /* the main loop (ed_service): a 5 ms slice for the BT tasks */
{
    static uint8_t kept;
    uint32_t pkt;
#if BLE_CONSOLE
    ble_loop_note(&loop, core_ui_frames(), core_ms());   /* BT off too: the period to compare with */
#endif
    if (!kept) {                                  /* once a power-on, BT off too: the radio calibration found in flash
                                                   * handed to the firmware to keep (core_rf_keep_put) before its own
                                                   * data can cover the store; a flash scan, no Bluetooth started */
        kept = 1;
        (void)ble_rf_trim_load();
    }
    if (!ble_on)
        return;
    g_where = 0x30;
    ble_os_service(5000);
    g_where = 0x31;
#if BLE_CONSOLE
    ble_diag.heap_high = ble_os_heap_high();       /* (.noinit: kept over a crash reboot, for the heap's size) */
    ble_diag.alloc_fails = ble_os_alloc_fails();
#endif
    ble_tick();                                    /* the start's deadline */
    g_where = 0x32;
    ble_auto();                                    /* the role; the host's slots and scanner or the device role; the
                                                    * MIDI link up / down to the core */
    g_where = 0x33;
    ble_out_flush();                               /* MIDI out (ble_midi_send) as write-without-response packets */
    while (ble_midi_take(&pkt))                    /* to the core while linked (after core_ble_link(1)); a drop's */
        if (ble_linked())                          /* leftovers are discarded (the core heard core_ble_link(0)) */
            core_ble_midi_in(pkt);
    for (; fq_t != fq_h; fq_t++)                   /* FM-Link in, with the tick each came at (the same while linked) */
        if (ble_fml_ready())
            core_fml_rx(fq[fq_t & 15u].b, fq[fq_t & 15u].len, fq[fq_t & 15u].t);
}

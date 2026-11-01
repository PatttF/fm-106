/* SPDX-License-Identifier: GPL-3.0-only
 * BLE MIDI test: 2026 DEADACTIVE
 * Drum machine fork: 2026 DEADACTIVE */
/* The core functions the BLE unit (firmware/src/ble/ble.c, its own object) may call: defined in
 * firmware/src/core_ble_api.c inside the core's unit. The BLE unit shares no static data with the core, so the core
 * compiles as it does without BLE (gate 5: tools/check_default_build.sh). And the BLE entry points the core calls:
 * the console's, MIDI out, and the Bluetooth page's (ble_ui_*). */
#ifndef BLE_CORE_H
#define BLE_CORE_H
#include <stdint.h>
#ifndef BLE_CONSOLE                     /* 1 (default): the console's `ble` words and the diagnostics they print; 0: none
                                          * of them (a fit tool: docs/ble/PORTING.md; the serial logger then reads no BT
                                          * state) */
#define BLE_CONSOLE 1
#endif
/* time, watchdog, power */
uint32_t core_ms(void);
void core_wdt_feed(void);
void core_reboot(void);
void core_audio_stop(void);
void core_ble_fatal(uint32_t why, uint32_t task, uint32_t stage, uint32_t heap_hi, uint32_t alloc_f);
/* interrupts (fm1_irq.h) */
void core_irq_attach(uint32_t n, void (*h)(void), uint32_t prio);
void core_irq_mask(uint32_t n);
void core_irq_unmask(uint32_t n);
uint32_t core_irq_is_masked(uint32_t n);
/* guards (fm1_guard.h): widen the stack-limit window to the BLE task stacks (before the first task runs) */
void core_ble_stack_window(void);
/* USB MIDI out (usb.c midi_out_event: one USB-MIDI event packet, cable 0) */
void core_midi_out(uint32_t pkt);
/* flash: read the plain area (0x93000..0xFFFFF) through the core's driver; 0 = ok (never writes) */
int core_flash_read(uint32_t off, void *buf, uint32_t n);
#if BLE_CONSOLE
/* audio: render blocks that missed the DMA since power-on (audio.c felucca_dbg.late), for the console's `ble` */
uint32_t core_audio_late(void);
/* the longest gap between two audio half interrupts since the last call, us (2902 normal: more, the audio held off) */
uint32_t core_audio_gap_us(void);
#endif
/* RECONNECT SLOW, or AUTO while the transport plays (bt_set.c bt_set_slow): a host link rests longer after a drop
 * or a failed connect (ble_fail_rest) */
int core_ble_slow(void);
#if BLE_CONSOLE
/* the core's UI frame counter (felucca_dbg.ui_frames: once per main-loop pass), for the loop period */
uint32_t core_ui_frames(void);
#endif
/* the core's crash record (fm1_irq.h fm1_crash_t, .noinit, same layout): read only, for the console's `ble`
 * (the core's unit has the real declaration) */
#ifndef FM1_CRASH_MAGIC
extern const struct {
    uint32_t magic, count, vec, pc, rets, emu, dbg, sp, psr, icfg, uptime_ms;
    uint32_t etm[4];
    uint32_t early;
} fm1_crash;
#endif
/* this FM-1's Bluetooth address: a random static address made once and kept in the settings (bt_devs.c bt_mac) */
void core_ble_mac(uint8_t mac[6]);
/* the radio calibration (68 bytes, the stock config store's item 187: ble_vm.c), kept in the firmware's settings:
 * get 1 and the copy (0 none), put keeps this one (saved). The unit uses the copy when the store no longer has it;
 * it looks once at each power-on (ble_service's first call, Bluetooth off too) and at each start */
int core_rf_keep_get(uint8_t rec[68]);
void core_rf_keep_put(const uint8_t rec[68]);
/* console (CDC) */
void core_con_puts(const char *s);
/* BLE MIDI in and the link, from ble_service (the main loop): each received USB-MIDI packet (cable 0) in order, while
 * linked; up = 1 once on entering SUBSCRIBED, 0 once on leaving SUBSCRIBED / RECEIVING */
void core_ble_midi_in(uint32_t pkt);
void core_ble_link(int up);
/* FM-Link in (ble_service): one message from the other FM-1 and the TIMER4 tick it came at (fmlink.c) */
void core_fml_rx(const uint8_t *p, uint32_t len, uint32_t t);
#if BLE_CONSOLE
/* MIDI mode: received packets the core's input ring refused (its last 8 slots are TRS MIDI's), for `ble` */
uint32_t core_ble_in_drops(void);
#endif
/* libc.c */
void *memset(void *d, int c, unsigned n);
void *memcpy(void *d, const void *s, unsigned n);
int memcmp(const void *a, const void *b, unsigned n);

/* the BLE unit's entry points (ble.c): the core calls ble_service (ed_service) and ble_console (the console's
 * "ble ..." line); ble_user_start is the user's trigger (gate 3: the only way to ble_start) */
void ble_service(void);
#if BLE_CONSOLE
void ble_console(const char *args);
void ble_list(void);
void ble_status(void);
#endif
void ble_user_start(void);
int ble_started(void);
/* MIDI out to the connected BLE-MIDI device: one message of 1..3 bytes (main loop only, never from an ISR);
 * 1 = queued, 0 = dropped (not connected, or the 64-message queue full: `ble`'s out_drops). A caller that retries
 * sends only what ble_midi_room allows, so out_drops counts real losses only */
int ble_midi_send(const uint8_t *m, uint32_t n);
uint32_t ble_midi_room(void);                          /* the queue's free entries; 0 when not connected */
/* FM-Link out (main loop only): one message (at most 20 bytes) to the other FM-1 on its own characteristic (ble_gatt.c);
 * 1 = sent, 0 = not linked to an FM-1 or no room now */
int ble_fml_send(const uint8_t *p, uint32_t len);
int ble_fml_ready(void);                               /* a link FM-Link may go on (the FM1 slot, or the device role's) */
/* the Bluetooth page and the saved devices (main loop only). None of these starts the stack: the page's BLUETOOTH ON
 * calls ble_user_start (gate 3). States = ble_central.c's. The host role holds two links, a slot each: CTRL (a
 * controller: BLE-MIDI, MODE) and FM1 (another FM-1: FM-Link only); each is driven to the target the core sets
 * (bt_devs.c), connected when the target is in the scan list (seen in the last 20 s), scanned for while it is not, and
 * reconnected after a drop (docs/superpowers/specs/2026-10-22-bt-devices-design.md). */
enum { BLE_ST_OFF, BLE_ST_STARTING, BLE_ST_IDLE, BLE_ST_SCANNING, BLE_ST_CONNECTING, BLE_ST_DISCOVERING,
       BLE_ST_SUBSCRIBED, BLE_ST_RECEIVING, BLE_ST_FAILED, BLE_ST_ADVERTISING };
typedef struct { uint8_t addr[6], type, midi; int8_t rssi; char name[24]; uint32_t seen_ms; } ble_ui_dev_t;
uint32_t ble_ui_state(void);
const char *ble_ui_err(uint32_t *code);                /* the last error text ("" none) and its code */
uint32_t ble_ui_devs(void);                            /* the scan list (seen in the last 30 s), strongest first */
int ble_ui_dev(uint32_t i, ble_ui_dev_t *out);         /* 0 = no such entry */
enum { BLE_SLOT_CTRL, BLE_SLOT_FM1, BLE_SLOTS };
enum { BLE_LS_IDLE, BLE_LS_CONNECTING, BLE_LS_DISCOVERING, BLE_LS_UP };
typedef struct { uint8_t st, addr[6], type; uint8_t last_reason; uint16_t drops; char name[24]; } ble_ui_slot_t;
void ble_ui_slot_target(uint32_t slot, const uint8_t addr[6], uint32_t type);   /* all-zero addr: none; another address
                                                                               * than the linked one drops it */
int ble_ui_slot(uint32_t slot, ble_ui_slot_t *out);   /* its state, its address (linked, else its target), its name */
void ble_ui_scan_want(int on);             /* the core wants scanning: 2 continuous (the page open), 1 in windows (a
                                            * better device to find), 0 none; the unit also scans by itself for a
                                            * target not listed */
void ble_ui_want(int on);                  /* BLUETOOTH ON: links wanted; 0 drops them, stops a scan / connect */
int ble_ui_conn_name(char out[24]);        /* the CTRL slot up (the device role: its host): 1 and the scanned name ("" none;
                                            * the device role: the host's address); else 0 */
void ble_ui_role(int device);              /* ROLE: 0 HOST (scan, connect: the central), 1 DEVICE (advertise: a host
                                            * connects to the FM-1). Applied once no scan, link or advertising is on */
uint32_t ble_ui_role_now(void);            /* the role in effect */
void ble_ui_get_diag(uint32_t out[16]);
#endif


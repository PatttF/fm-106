/* SPDX-License-Identifier: GPL-3.0-only
 * BLE MIDI test: 2026 DEADACTIVE
 * Drum machine fork: 2026 DEADACTIVE
 * FM-Link: 2026 DEADACTIVE */
/* BLE-MIDI central on the JieLi SDK's ble_op_* API (reference: SDK apps/common/ble/le_net_central.c), and (ROLE
 * DEVICE, ble_ui_role) a BLE-MIDI peripheral a host connects to (reference: le_trans_data.c; the GATT table in
 * ble_gatt.c). The state machine and the MIDI out queue at the top are host-tested (tests/ble/ble_central_test.c);
 * the SDK glue below. */
#include "ble_scan.h"
extern volatile uint32_t g_where;
enum { BLE_OFF, BLE_STARTING, BLE_IDLE, BLE_SCANNING, BLE_CONNECTING, BLE_DISCOVERING, BLE_SUBSCRIBED,
       BLE_RECEIVING, BLE_FAILED, BLE_ADVERTISING };  /* (ADVERTISING: the device role, waiting for a host) */
typedef enum { EV_INIT_OK, EV_INIT_FAIL, EV_SCAN_DONE, EV_CONNECTED, EV_CONN_FAIL, EV_FOUND_MIDI, EV_NO_MIDI,
               EV_SUBSCRIBED, EV_NOTIFY, EV_DISCONNECTED, EV_TIMEOUT } ble_ev_t;

uint32_t ble_next(uint32_t s, ble_ev_t ev)        /* the new state; unknown pairs keep the state */
{
    if (ev == EV_DISCONNECTED && s >= BLE_CONNECTING && s <= BLE_RECEIVING)
        return BLE_IDLE;
    switch (s) {
    case BLE_STARTING:
        return ev == EV_INIT_OK ? BLE_IDLE : (ev == EV_INIT_FAIL || ev == EV_TIMEOUT) ? BLE_FAILED : s;
    case BLE_SCANNING:
        return ev == EV_SCAN_DONE ? BLE_IDLE : s;
    case BLE_CONNECTING:
        return ev == EV_CONNECTED ? BLE_DISCOVERING : (ev == EV_CONN_FAIL || ev == EV_TIMEOUT) ? BLE_IDLE : s;
    case BLE_DISCOVERING:
        return ev == EV_FOUND_MIDI ? BLE_SUBSCRIBED : (ev == EV_NO_MIDI || ev == EV_TIMEOUT) ? BLE_IDLE : s;
    case BLE_SUBSCRIBED:
    case BLE_RECEIVING:
        return ev == EV_NOTIFY ? BLE_RECEIVING : s;
    case BLE_ADVERTISING:                          /* the device role: a host connected to the FM-1 */
        return ev == EV_CONNECTED ? BLE_SUBSCRIBED : s;
    default:
        return s;
    }
}

/* the device role's step (ble_service): advertise while wanted and idle; not wanted, stop advertising or let the host
 * go. Reconnecting is the host's: the FM-1 only waits for it */
enum { BV_NONE, BV_ADV_ON, BV_ADV_OFF, BV_DROP };
uint32_t ble_dev_step(int want, uint32_t s)
{
    if (want)
        return s == BLE_IDLE ? BV_ADV_ON : BV_NONE;
    return s == BLE_ADVERTISING ? BV_ADV_OFF : s == BLE_SUBSCRIBED || s == BLE_RECEIVING ? BV_DROP : BV_NONE;
}

/* the BLE-MIDI characteristic 7772E5DB-3868-4112-A1A9-F2669D106BF3 (MMA BLE-MIDI 1.0). The SDK hands UUIDs over as
 * 16 bytes in an order its headers do not state, so both orders match. */
static const uint8_t MIDI_CHAR_UUID[16] = {0x77, 0x72, 0xE5, 0xDB, 0x38, 0x68, 0x41, 0x12, 0xA1, 0xA9, 0xF2, 0x66,
                                           0x9D, 0x10, 0x6B, 0xF3};
/* the FM-Link characteristic F3110001-6E4C-4A5B-9C2D-1F3E5D7A9B0C (ble_gatt.c: another FM-1 in the device role) */
static const uint8_t FML_CHAR_UUID[16] = {0xF3, 0x11, 0x00, 0x01, 0x6E, 0x4C, 0x4A, 0x5B, 0x9C, 0x2D, 0x1F, 0x3E,
                                          0x5D, 0x7A, 0x9B, 0x0C};
static int ble_uuid_is(const uint8_t u[16], const uint8_t ref[16])
{
    uint32_t i, fwd = 1, rev = 1;
    for (i = 0; i < 16u; i++) {
        fwd &= u[i] == ref[i];
        rev &= u[i] == ref[15u - i];
    }
    return (int)(fwd | rev);
}
int ble_is_midi_char(const uint8_t u[16]) { return ble_uuid_is(u, MIDI_CHAR_UUID); }
int ble_is_fml_char(const uint8_t u[16]) { return ble_uuid_is(u, FML_CHAR_UUID); }

#if !defined(BLE_CONSOLE) || BLE_CONSOLE || defined(BLE_MONITOR)   /* (BLE_CONSOLE 0: no console, docs/ble/PORTING.md) */
/* the console's text (`ble`, `ble list`, the parser): built for size (as the core's main-loop code, tools/size_fns.py),
 * run only when typed */
#define BLE_CON __attribute__((minsize))
static char *put_s(char *o, const char *s) { while (*s) *o++ = *s++; return o; }
static char *put_u(char *o, uint32_t v)
{
    char b[10];
    uint32_t n = 0;
    do b[n++] = (char)('0' + v % 10u); while (v /= 10u);
    while (n)
        *o++ = b[--n];
    return o;
}
static char *put_x(char *o, uint32_t v)
{
    static const char H[] = "0123456789ABCDEF";
    *o++ = H[v >> 4 & 15u];
    *o++ = H[v & 15u];
    return o;
}
static char *put_dev(char *o, const ble_dev_t *d)  /* the name, or the address most significant byte first */
{
    uint32_t i;
    if (d->name[0]) {
        o = put_s(o, d->name);
        *o = 0;
        return o;
    }
    for (i = 6; i--;) {
        o = put_x(o, d->addr[i]);
        if (i)
            *o++ = ':';
    }
    *o = 0;
    return o;
}
#endif
#ifdef BLE_MONITOR
/* one monitor line for a USB-MIDI packet (cin | status << 8 | d1 << 16 | d2 << 24); 0 = no line (a SysEx start or
 * continuation: *sx counts its bytes, and the end packet prints them). Hortator's MIDI monitor screen; the FM-1 has
 * none, so only the host test builds it (BLE_MONITOR: 684 B of the slot) */
int ble_midi_text(uint32_t pkt, uint32_t *sx, char out[32])
{
    static const char *const NOTE[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    uint32_t cin = pkt & 15u, b0 = pkt >> 8 & 0xFFu, b1 = pkt >> 16 & 0xFFu, b2 = pkt >> 24, ch = (b0 & 15u) + 1u;
    char *o = out;
    if (cin == 4u) {
        *sx += 3u;
        return 0;
    }
    if (cin >= 5u && cin <= 7u && (*sx || cin > 5u || b0 == 0xF7u)) {
        o = put_u(put_s(o, "SYSEX "), *sx + cin - 4u);
        o = put_s(o, " BYTES");
        *sx = 0;
    } else if (cin == 9u || cin == 8u) {
        o = put_u(put_s(o, cin == 9u && b2 ? "NOTE ON  " : "NOTE OFF "), ch);
        o = put_s(put_s(o, " "), NOTE[b1 % 12u]);
        o = b1 < 12u ? put_s(o, "-1") : put_u(o, b1 / 12u - 1u);
        if (cin == 9u && b2)
            o = put_u(put_s(o, " v"), b2);
    } else if (cin == 0xBu) {
        o = put_u(put_s(put_u(put_s(put_u(put_s(o, "CC "), ch), " #"), b1), " = "), b2);
    } else if (cin == 0xCu) {
        o = put_u(put_s(put_u(put_s(o, "PC "), ch), " "), b1);
    } else if (cin == 0xEu) {
        int32_t v = (int32_t)(b2 << 7 | b1) - 8192;
        o = put_s(put_u(put_s(o, "PB "), ch), v < 0 ? " -" : " +");
        o = put_u(o, (uint32_t)(v < 0 ? -v : v));
    } else if (cin == 0xFu) {
        o = put_x(put_s(o, "RT "), b0);
    } else {
        o = put_x(put_s(put_x(put_s(put_x(put_s(o, "MIDI "), b0), " "), b1), " "), b2);
    }
    *o = 0;
    return 1;
}
#endif

#if !defined(BLE_CONSOLE) || BLE_CONSOLE
/* ---- the console's `ble` words and the states they need (part 1b; pure, host-tested) ---- */
enum { BLE_CMD_STATUS, BLE_CMD_START, BLE_CMD_SCAN, BLE_CMD_LIST, BLE_CMD_STOP, BLE_CMD_BAD };

/* the words after `ble`. (No `connect N` since the saved devices: the core picks each link's device every frame, so
 * a console pick was overridden at once; the page's + ADD A DEVICE is the way) */
BLE_CON uint32_t ble_cmd_parse(const char *s)
{
    static const char *const W[] = {"start", "scan", "list", "stop"};
    uint32_t i, k;
    while (*s == ' ')
        s++;
    if (!*s)
        return BLE_CMD_STATUS;
    for (i = 0; i < 4u; i++) {
        for (k = 0; W[i][k] && s[k] == W[i][k]; k++)
            ;
        if (W[i][k] || (s[k] && s[k] != ' '))
            continue;
        for (s += k; *s == ' '; s++)
            ;
        return *s ? BLE_CMD_BAD : BLE_CMD_START + i;
    }
    return BLE_CMD_BAD;
}

/* 0: the command may run in this state; else why not (one console line) */
BLE_CON const char *ble_cmd_refuse(uint32_t cmd, uint32_t state)
{
    switch (cmd) {
    case BLE_CMD_START:
        return state == BLE_OFF ? 0 : state == BLE_FAILED ? "ble: the start failed; power off and on to retry"
                                                          : "ble: already started";
    case BLE_CMD_SCAN:
        if (state == BLE_OFF)
            return "ble: not started (ble start)";
        return state != BLE_IDLE ? "ble: busy (this needs IDLE)" : 0;
    case BLE_CMD_STOP:
        return state >= BLE_CONNECTING && state <= BLE_RECEIVING ? 0 : "ble: not connected";
    default:
        return 0;
    }
}

/* the main loop's period: the core's UI frame counter changes once per pass (main.c), ble_service sees it */
typedef struct { uint32_t frames, at_ms, last_ms, max_ms, seen; } ble_loop_t;
void ble_loop_note(ble_loop_t *l, uint32_t frames, uint32_t now_ms)
{
    if (l->seen && frames == l->frames)
        return;
    if (l->seen) {
        l->last_ms = now_ms - l->at_ms;
        if (l->last_ms > l->max_ms)
            l->max_ms = l->last_ms;
    }
    l->seen = 1;
    l->frames = frames;
    l->at_ms = now_ms;
}
#endif

/* MIDI out (ble_midi_send, called by the core's main loop): whole messages of 1..3 bytes, queued only while
 * connected; ble_service sends them (ble_out_flush). A disconnect empties the queue (ble_ev). */
static uint32_t st;                                 /* BLE_OFF ... BLE_FAILED */
static struct {
    uint8_t msg[64][3], len[64];
    uint32_t h, t, sent, drops;                     /* sent / drops: the console's `out` / `out_drops` */
} mo;
static uint8_t ctrl_up;                             /* the host role: the CTRL slot is up (the glue) */
static int ble_linked(void) { return st == BLE_SUBSCRIBED || st == BLE_RECEIVING || ctrl_up; }   /* MIDI's link */
static void ble_ev(ble_ev_t ev)                     /* every event's state change; the queue lives only while linked */
{
    st = ble_next(st, ev);
    if (!ble_linked())
        mo.t = mo.h;
}
int ble_midi_send(const uint8_t *m, uint32_t n)    /* 1 = queued, 0 = dropped (not connected, or the queue full) */
{
    uint32_t i;
    if (!ble_linked() || n - 1u > 2u)
        return 0;
    if (mo.h - mo.t >= 64u) {
        mo.drops++;
        return 0;
    }
    for (i = 0; i < n; i++)
        mo.msg[mo.h & 63u][i] = m[i];
    mo.len[mo.h & 63u] = (uint8_t)n;
    mo.h++;
    return 1;
}
uint32_t ble_midi_room(void) { return ble_linked() ? 64u - (mo.h - mo.t) : 0u; }   /* free entries; 0 unlinked */
/* the queue as packets while tx takes them (0 = sent): ble_service's ble_out_flush. A handler run inside tx may
 * empty the queue (a disconnect: ble_ev), so the tail moves only over messages still queued. */
static void ble_out_drain(uint32_t now_ms, int (*tx)(const uint8_t *pkt, uint32_t len))
{
    uint8_t pkt[20];                                /* the default ATT MTU's payload: at most 4 three-byte messages */
    while (mo.h != mo.t && ble_linked()) {
        uint32_t i = mo.t & 63u, n = mo.h - mo.t, len, taken;
        if (n > 64u - i)
            n = 64u - i;                            /* up to the ring's end: the rest in the next packet */
        len = ble_midi_pack(&mo.msg[i], &mo.len[i], n, now_ms, pkt, sizeof pkt, &taken);
        if (tx(pkt, len))
            return;                                 /* no room (or refused): the next pass */
        if (mo.h - mo.t < taken)
            return;                                 /* emptied by a disconnect inside tx */
        mo.t += taken;
        mo.sent += taken;
    }
}

/* ---- the host role: two link slots, CTRL (a controller: BLE-MIDI, MODE) and FM1 (another FM-1: FM-Link only), each
 * driven to the target address the core sets (bt_devs.c: the saved devices, the user's pick order), and one scanner
 * beside them (docs/superpowers/specs/2026-10-22-bt-devices-design.md). One decision per ble_service pass
 * (ble_host_step), applied by ble_host (the glue). One connect or discovery at a time, never beside a scan; a scan
 * runs beside links, never drops one ---- */
enum { HS_IDLE, HS_CONNECTING, HS_DISCOVERING, HS_UP, HS_CANCELLING };   /* (CANCELLING: a cancelled connect's
                                                    * completion awaited: busy, shown as IDLE) */
enum { BH_NONE, BH_SCAN_ON, BH_SCAN_OFF, BH_CONNECT, BH_CANCEL, BH_DROP };
typedef struct {
    uint8_t st;                                     /* HS_* */
    uint8_t target;                                 /* a target is set */
    uint8_t listed;                                 /* .. and it is in the scan list, seen in the last 20 s */
    uint8_t mismatch;                               /* linked / connecting to another address than its target (or
                                                     * to none) */
    uint8_t tried;                                  /* a connect to it failed and it has not been seen since */
    uint8_t dropping;                               /* a disconnect asked for: not again */
    uint32_t rest_ms;                               /* no connect before (2 s after a failure or a drop) */
    uint32_t deadline_ms;                           /* CONNECTING / DISCOVERING: given up then */
} ble_hslot_t;
typedef struct {
    ble_hslot_t s[2];
    uint8_t want;                                   /* BLUETOOTH ON, the host role, the stack up */
    uint8_t scan_on, scan_want;                     /* scanning; the core wants a continuous scan (the page) */
    uint8_t scan_bg;                                /* .. or a background one, in windows (a better device) */
    uint32_t scan_until, scan_rest;                 /* a background scan's window end; its rest's end */
} ble_host_t;
static int ble_due(uint32_t now, uint32_t at) { return (int32_t)(now - at) >= 0; }
/* the rest after the n-th failed connect (or a drop) in a row (from 0): 2 s doubling to 16 s; slow (RECONNECT SLOW,
 * or AUTO while the transport plays: core_ble_slow): 8 s, then 16 s.
 * Measured on the FM-1 (2026-10-22): each failed connect holds the interrupts off ~12 ms inside the libraries, the
 * audio late by several blocks; a mixer that dropped advertises at once but refuses for ~8 s */
uint32_t ble_fail_rest(uint32_t n, int slow)
{
    n += slow ? 2u : 0u;
    return 2000u << (n < 3u ? n : 3u);
}
/* the next action and its slot (*slot) */
uint32_t ble_host_step(const ble_host_t *h, uint32_t now_ms, uint32_t *slot)
{
    uint32_t i;
    int busy = 0, unlisted = 0, need;
    for (i = 0; i < 2u; i++) {
        const ble_hslot_t *s = &h->s[i];
        int off = !h->want || s->mismatch;
        *slot = i;
        if (s->st == HS_CONNECTING && (off || ble_due(now_ms, s->deadline_ms)))
            return BH_CANCEL;
        if (!s->dropping && ((s->st == HS_DISCOVERING && (off || ble_due(now_ms, s->deadline_ms))) ||
                             (s->st == HS_UP && off)))
            return BH_DROP;
        busy |= s->st == HS_CONNECTING || s->st == HS_CANCELLING || (s->st == HS_DISCOVERING && !s->dropping);
    }
    if (busy)
        return h->scan_on ? BH_SCAN_OFF : BH_NONE;
    for (i = 0; i < 2u; i++) {
        const ble_hslot_t *s = &h->s[i];
        if (h->want && s->st == HS_IDLE && s->target && s->listed && !s->tried && ble_due(now_ms, s->rest_ms)) {
            *slot = i;
            return h->scan_on ? BH_SCAN_OFF : BH_CONNECT;
        }
        unlisted |= s->st == HS_IDLE && s->target && (!s->listed || s->tried);
    }
    need = h->want && (h->scan_want || h->scan_bg || unlisted);
    if (h->scan_on)
        return !need || (!h->scan_want && ble_due(now_ms, h->scan_until)) ? BH_SCAN_OFF : BH_NONE;
    return need && (h->scan_want || ble_due(now_ms, h->scan_rest)) ? BH_SCAN_ON : BH_NONE;
}

#ifndef BLE_CENTRAL_STATE_ONLY
/* ---- the SDK glue: in ble.c's unit, after ble_heap / ble_on / ble_mac. The stack calls ble_profile_init and the
 * three handlers below from its own task (ble_os.c); the main loop calls ble_start, ble_tick and ble_auto (the host's
 * slots, the device role). Both run on the same CPU, one at a time, so they share these without locking. ---- */
#include "ble/ble_sdk.h"
#define ATT_MTU 128u                                /* le_net_central.c ATT_LOCAL_MTU_SIZE */

static ble_scan_t scan;
static ble_midi_t bm;
static uint16_t con_handle;                         /* the device role: the host's link */
static uint8_t role_want, role_now;                 /* the device role (1) or the central's (0): wanted, in effect */
static uint8_t want;                                /* BLUETOOTH ON: links wanted (ble_ui_want) */
static ble_host_t hs;                               /* the host role: its two slots and its scanner (ble_host_step) */
static struct {
    uint16_t con, midi, fml;                        /* its link; the BLE-MIDI (CTRL) / FM-Link (FM1) value handles */
    uint8_t addr[6], type;                          /* the address linked or being connected to */
    uint8_t tgt[6], tgt_type;                       /* its target (ble_ui_slot_target; all-zero: none) */
    uint8_t fail;                                   /* a connect / discovery failed: not again until seen anew */
    uint8_t nfail;                                  /* failed connects in a row (ble_fail_rest) */
    uint8_t last_reason;                            /* the HCI reason of its last disconnect */
    uint32_t fail_seen;                             /* the target's last sighting when it failed */
    uint16_t drops, tries;                          /* links lost; connects tried */
    char name[24];                                  /* the scanned name, at the connect */
} sl[2];
static uint8_t disc_slot;                           /* the slot whose discovery runs (one at a time) */
#if !defined(BLE_CONSOLE) || BLE_CONSOLE
static uint32_t console_scan_until;                 /* the console's `ble scan`: one window */
#endif
static uint8_t peer[6];                             /* the device role: the host's address, for the page */
static uint32_t deadline_ms;
static const char *err_text = "";                   /* the last error (ble_err) */
static uint32_t err_code;
static uint8_t att_ram[BLE_SDK_ATT_CTRL_BLOCK_SIZE + ATT_MTU + 1024u] __attribute__((aligned(4)));   /* (all links' sends) */
static uint8_t search_ram[512] __attribute__((aligned(4)));
static uint32_t ring[64], ring_h, ring_t, ring_lost;   /* BLE-MIDI as USB-MIDI packets, to the main loop */
/* FM-Link (the core's bt_fml.c, fmlink.c): its messages in, each with the TIMER4 tick it came at (the offset between
 * two FM-1s is measured from these), to the main loop (ble_service: core_fml_rx); out by ble_fml_send */
static struct { uint32_t t; uint8_t len, b[20]; } fq[16];   /* (8 overflowed with two links: `lost`, 2026-10-22) */
static uint32_t fq_h, fq_t, fq_lost, fml_in, fml_out;
static uint8_t fml_heard;                           /* the device role: the host wrote to FM-Link (it is an FM-1: it
                                                     * takes FM-Link notifications) */

const char *ble_err(uint32_t *code) { *code = err_code; return err_text; }
static void set_err(const char *text, uint32_t code) { err_text = text; err_code = code; }
static void set_deadline(uint32_t ms) { deadline_ms = core_ms() + ms; }

static void ring_put(void *ctx, uint32_t pkt)
{
    (void)ctx;
    if (ring_h - ring_t < 64u)
        ring[ring_h++ & 63u] = pkt;
    else
        ring_lost++;
}
static void fml_put(const uint8_t *p, uint32_t len)
{
    uint32_t t = fm1_ticks();                       /* (first: the time it came) */
    if (!len || len > 20u || fq_h - fq_t >= 16u) {
        fq_lost++;
        return;
    }
    fq[fq_h & 15u].t = t;
    fq[fq_h & 15u].len = (uint8_t)len;
    memcpy(fq[fq_h & 15u].b, p, len);
    fq_h++;
    fml_in++;
}
int ble_midi_take(uint32_t *pkt)
{
    if (ring_h == ring_t)
        return 0;
    *pkt = ring[ring_t++ & 63u];
    return 1;
}

static int ble_slot_of(uint16_t h)                 /* the host slot of a link handle, -1 none */
{
    return h && h == sl[0].con ? 0 : h && h == sl[1].con ? 1 : -1;
}
static void ble_slot_failed(uint32_t i)            /* a connect / discovery that failed (or a link lost): rest, longer
                                                    * each time; not again until seen anew */
{
    uint32_t k;
    hs.s[i].rest_ms = core_ms() + ble_fail_rest(sl[i].nfail, core_ble_slow());
    sl[i].nfail += sl[i].nfail < 3u;
    sl[i].fail = 1;
    sl[i].fail_seen = 0;
    for (k = 0; k < BLE_SCAN_MAX; k++)
        if (scan.dev[k].used && !memcmp(scan.dev[k].addr, sl[i].tgt, 6))
            sl[i].fail_seen = scan.dev[k].seen_ms;
}
static void cbk_packet_handler(uint8_t type, uint16_t channel, uint8_t *packet, uint16_t size)
{
    uint16_t h;
    int i;
    (void)channel;
    (void)size;
    if (type != BLE_SDK_HCI_EVENT_PACKET)
        return;
    switch (packet[0]) {
    case BLE_SDK_GAP_EVENT_ADVERTISING_REPORT:  /* one report: le_net_central.c 960 */
        if (hs.scan_on && packet[1] >= 10u) {
            const adv_report_t *r = (const adv_report_t *)(const void *)&packet[2];
            uint32_t max_len = (uint32_t)packet[1] - 10u;
            uint32_t len = (uint32_t)r->length;
            if (len > max_len) len = max_len;
            if (len > 31u) len = 31u;
            g_where = 0xD0;
            ble_scan_report(&scan, r->address, r->address_type, r->rssi, r->data, len, core_ms());
            g_where = 0xD1;
        }
        break;
    case BLE_SDK_HCI_EVENT_LE_META:              /* le_net_central.c 870-905: status packet[3], handle packet[4..5], */
        if (packet[2] != BLE_SDK_HCI_SUBEVENT_LE_CONNECTION_COMPLETE &&   /* role packet[6] (1: slave), the peer's */
            packet[2] != BLE_SDK_HCI_SUBEVENT_LE_ENHANCED_CONNECTION_COMPLETE)   /* address type [7], address [8..13] */
            break;
        h = (uint16_t)(packet[4] | packet[5] << 8);
        if (packet[6]) {                           /* the device role: a host connected */
            if (!packet[3] && (!role_now || st != BLE_ADVERTISING))
                ble_op_disconnect(h);              /* (not waiting for a host: not wanted) */
            if (packet[3] || !role_now || st != BLE_ADVERTISING)
                break;
            con_handle = h;
            memcpy(peer, &packet[8], 6);
            ble_midi_reset(&bm);
            ble_op_multi_att_send_conn_handle(con_handle, 0, 0);   /* (index 0, the server's) */
            multi_att_set_ccc_config(con_handle, BLE_GATT_MIDI_CCC, 0);   /* notifications off until this host */
            multi_att_set_ccc_config(con_handle, BLE_GATT_FML_CCC, 0);    /* turns them on */
            fml_heard = 0;
            set_err("", 0);
            ble_ev(EV_CONNECTED);
            break;
        }
        for (i = 0; i < 2; i++)                    /* the slot connecting or cancelling (one at a time), by the address
                                                    * it asked */
            if ((hs.s[i].st == HS_CONNECTING || hs.s[i].st == HS_CANCELLING) &&
                (packet[3] || !memcmp(&packet[8], sl[i].addr, 6)))
                break;
        if (i == 2) {
            if (!packet[3])
                ble_op_disconnect(h);              /* (completed after a cancel: not wanted) */
            break;
        }
        if (hs.s[i].st == HS_CANCELLING) {         /* the cancel's completion (or a link made just before it) */
            if (!packet[3])
                ble_op_disconnect(h);
            hs.s[i].st = HS_IDLE;
            break;
        }
        if (packet[3]) {
            set_err("CONNECT FAILED", packet[3]);
            hs.s[i].st = HS_IDLE;
            ble_slot_failed((uint32_t)i);
            break;
        }
        sl[i].con = h;
        sl[i].midi = sl[i].fml = 0;
        hs.s[i].dropping = 0;
        if (i == 0)
            ble_midi_reset(&bm);
        ble_op_multi_att_send_conn_handle(h, i, 1);   /* (client index: the slot) */
        user_client_init(h, search_ram, sizeof search_ram);
        disc_slot = (uint8_t)i;
        hs.s[i].st = HS_DISCOVERING;
        hs.s[i].deadline_ms = core_ms() + 10000u;
        ble_op_search_profile_all();
        break;
    case BLE_SDK_HCI_EVENT_DISCONNECTION_COMPLETE:   /* le_net_central.c 917: handle packet[3..4], reason packet[5] */
        h = (uint16_t)(packet[3] | packet[4] << 8);
        if ((i = ble_slot_of(h)) >= 0) {           /* a host slot's link */
            if (!hs.s[i].dropping) {               /* (not a drop asked for: another target, Bluetooth OFF) */
                if (hs.s[i].st == HS_UP)           /* lost: not again before it advertises again (its entry is from */
                    sl[i].nfail = 0;               /* before the link), from 2 s */
                ble_slot_failed((uint32_t)i);
            }
            sl[i].drops += hs.s[i].st == HS_UP;
            sl[i].last_reason = packet[5];
            sl[i].con = sl[i].midi = sl[i].fml = 0;
            ble_op_multi_att_send_conn_handle(0, i, 1);
            hs.s[i].st = HS_IDLE;
            hs.s[i].dropping = 0;
            if (i == 0) {
                ctrl_up = 0;
                mo.t = mo.h;                       /* (MIDI out lives only while linked) */
            }
            break;
        }
        if (!con_handle || h != con_handle)
            break;
        if (st >= BLE_CONNECTING && st <= BLE_RECEIVING)   /* the device role's host link */
            set_err("DISCONNECTED", packet[5]);
        multi_att_clear_ccc_config(con_handle);
        ble_op_multi_att_send_conn_handle(0, 0, 0);
        con_handle = 0;
        fml_heard = 0;
        ble_ev(EV_DISCONNECTED);                   /* (IDLE, then ble_dev advertises again) */
        break;
    default:
        break;
    }
}

/* called by the stack while it starts (btstack_main.c, before BT_STATUS_INIT_OK): as multi_demo/le_multi_common.c
 * 374-406, the client (ROLE HOST: up to two links) and the server (ROLE DEVICE: the GATT table, ble_gatt.c) both,
 * one send buffer for every link; no pairing required (a peer that asks gets Just Works) */
static uint16_t att_read_cb(uint16_t con, uint16_t handle, uint16_t offset, uint8_t *buffer, uint16_t size);
static int att_write_cb(uint16_t con, uint16_t handle, uint16_t mode, uint16_t offset, uint8_t *buffer, uint16_t size);
static void sm_cb(uint8_t type, uint16_t channel, uint8_t *packet, uint16_t size);
void ble_profile_init(void)
{
    ble_stage(6);
    le_device_db_init();
    ble_stack_gatt_role(2);
    sm_init();
    sm_set_io_capabilities(BLE_SDK_IO_CAPABILITY_NO_INPUT_NO_OUTPUT);
    sm_set_authentication_requirements(0);
    sm_set_encryption_key_size_range(7, 16);
    sm_set_master_request_pair(0);
    sm_set_request_security(0);                    /* (the FM-1 never asks a host to pair) */
    sm_event_callback_set(sm_cb);
    gatt_client_init();
    gatt_client_register_packet_handler(cbk_packet_handler);
    att_server_init(BLE_GATT_DB, att_read_cb, att_write_cb);
    att_server_register_packet_handler(cbk_packet_handler);
    hci_event_callback_set(cbk_packet_handler);
    le_l2cap_register_packet_handler(cbk_packet_handler);
    ble_vendor_set_default_att_mtu(ATT_MTU);
    ble_op_multi_att_send_init(att_ram, sizeof att_ram, ATT_MTU);
}

static void ble_on_event(int from, struct bt_event *e)  /* bt_event_notify (ble_port.c) */
{
    if (from == BLE_SDK_BT_EVENT_FROM_CON && e->event == BLE_SDK_BT_STATUS_INIT_OK && st == BLE_STARTING) {
        ble_stage(7);
        ble_ev(EV_INIT_OK);
    }
}

/* ---- the device role: the GATT server's callbacks (le_trans_data.c 586, 651), from the stack's task ---- */
static uint16_t att_read_cb(uint16_t con, uint16_t handle, uint16_t offset, uint8_t *buffer, uint16_t size)
{
    uint32_t n, v;
    if (handle == BLE_GATT_NAME) {                 /* called without a buffer for the length, then with one */
        n = sizeof BLE_GATT_NAME_STR - 1u;
        if (offset >= n)
            return 0;
        if (buffer)
            memcpy(buffer, BLE_GATT_NAME_STR + offset, n - offset < size ? n - offset : size);
        return (uint16_t)(n - offset);
    }
    if (handle == BLE_GATT_MIDI_CCC || handle == BLE_GATT_FML_CCC) {
        if (buffer && size >= 2u) {
            v = multi_att_get_ccc_config(con, handle);
            buffer[0] = (uint8_t)v;
            buffer[1] = (uint8_t)(v >> 8);
        }
        return 2;
    }
    return 0;                                      /* the MIDI value: read gives no data (BLE-MIDI 1.0); FM-Link's */
}
static int att_write_cb(uint16_t con, uint16_t handle, uint16_t mode, uint16_t offset, uint8_t *buffer, uint16_t size)
{
    (void)mode;
    (void)offset;
    if ((handle == BLE_GATT_MIDI_CCC || handle == BLE_GATT_FML_CCC) && size) {
        multi_att_set_ccc_config(con, handle, buffer[0]);   /* the host's notifications on / off */
    } else if (handle == BLE_GATT_FML && (st == BLE_SUBSCRIBED || st == BLE_RECEIVING)) {
        fml_heard = 1;                             /* FM-Link from the host (another FM-1) */
        fml_put(buffer, size);
    } else if (handle == BLE_GATT_MIDI && (st == BLE_SUBSCRIBED || st == BLE_RECEIVING)) {
        ble_midi_parse(&bm, buffer, size, ring_put, 0);   /* BLE-MIDI from the host: as a central's notification */
        ble_ev(EV_NOTIFY);
    }
    return 0;
}
static void sm_cb(uint8_t type, uint16_t channel, uint8_t *packet, uint16_t size)   /* a host asks to pair */
{
    (void)channel;
    (void)size;
    if (type == BLE_SDK_HCI_EVENT_PACKET && packet[0] == BLE_SDK_SM_EVENT_JUST_WORKS_REQUEST)
        sm_just_works_confirm((uint16_t)(packet[2] | packet[3] << 8));   /* (Just Works; no bond kept: no VM) */
}

/* the stack's GATT client reports (client_user.c weak defaults): le_net_central.c 386-415 and 453-480 */
void user_client_report_search_result(search_result_t *r)
{
    uint32_t i = disc_slot;
    if (role_now || hs.s[i].st != HS_DISCOVERING)
        return;
    if (r == 0 || r == (search_result_t *)-1) {  /* (void *)-1: the search is over */
        if (i == 0 && sl[0].midi) {
            static const uint8_t on[2] = {1, 0};   /* notifications on: the CCCD after the value */
            ble_op_multi_att_send_data(sl[0].con, sl[0].midi + 1u, (void *)on, 2, BLE_SDK_ATT_OP_WRITE);
            hs.s[0].st = HS_UP;
            sl[0].nfail = 0;
            ctrl_up = 1;
        } else if (i == 1 && sl[1].fml) {
            hs.s[1].st = HS_UP;
            sl[1].nfail = 0;                    /* (FM-Link: the FM-1 notifies once written to) */
        } else if (!hs.s[i].dropping) {
            set_err(i ? "NO FM-LINK" : "NO BLE-MIDI SERVICE", 0);
            ble_slot_failed(i);
            hs.s[i].dropping = 1;
            ble_op_disconnect(sl[i].con);
        }
        return;
    }
    if (!r->characteristic.uuid16 && ble_is_midi_char(r->characteristic.uuid128))
        sl[i].midi = r->characteristic.value_handle;
    else if (!r->characteristic.uuid16 && ble_is_fml_char(r->characteristic.uuid128))
        sl[i].fml = r->characteristic.value_handle;
}

void user_client_report_data_callback(att_data_report_t *d)
{
    int i = ble_slot_of(d->conn_handle);
    if (d->packet_type != BLE_SDK_GATT_EVENT_NOTIFICATION || i < 0 || hs.s[i].st != HS_UP)
        return;
    if (i == 0 && d->value_handle == sl[0].midi)
        ble_midi_parse(&bm, d->blob, d->blob_length, ring_put, 0);
    else if (i == 1 && d->value_handle == sl[1].fml)
        fml_put(d->blob, d->blob_length);
}

/* ---- the main loop's side ---- */
void ble_start(void)
{
    if (st != BLE_OFF)
        return;
    if (!ble_rf_trim_load()) {                     /* no stored radio calibration: do not start (the trim hangs) */
        set_err("NO RF TRIM IN FLASH", 0);
        st = BLE_FAILED;
        return;
    }
    core_ble_mac(ble_mac);                         /* this FM-1's own address, the same at every start (saved devices
                                                    * find it again: bt_devs.c bt_mac) */
    ble_os_init(ble_heap, sizeof ble_heap);
    ble_delay_calibrate();                         /* (counted-loop delays for the radio calibration) */
    ble_os_trace = &ble_diag.task;
    ble_stage(1);
    core_ble_stack_window();                       /* the task stacks are in .pool: inside the stack-limit window */
    ble_stage(2);
    ble_on = 1;
    st = BLE_STARTING;
    set_deadline(5000);
    le_controller_set_mac(ble_mac);
    ble_stage(3);
    ble_os_after_run = ble_irq_check;
    task_create(ble_app_core, 0, "app_core");      /* stages 4-5 there: btstack_init in a task */
}

static void ble_tick(void)                         /* the start's deadline */
{
    if (st == BLE_STARTING && (int32_t)(core_ms() - deadline_ms) >= 0) {
        set_err("BT START TIMEOUT", 0);
        ble_ev(EV_TIMEOUT);
    }
}

static int ble_out_tx(const uint8_t *pkt, uint32_t len)   /* ble_out_drain's sender: 0 = sent */
{
    uint32_t room = 0;
    if (role_now) {                                /* the device role: a notification, once the host turned them on */
        if (!multi_att_get_ccc_config(con_handle, BLE_GATT_MIDI_CCC)) {
            mo.drops++;                            /* (the host does not listen: dropped, counted) */
            return 0;
        }
        return ble_op_multi_att_get_remain(con_handle, &room) || room < len ||
               ble_op_multi_att_send_data(con_handle, BLE_GATT_MIDI, (void *)pkt, len, BLE_SDK_ATT_OP_NOTIFY);
    }
    return !ctrl_up || ble_op_multi_att_get_remain(sl[0].con, &room) || room < len ||
           ble_op_multi_att_send_data(sl[0].con, sl[0].midi, (void *)pkt, len, BLE_SDK_ATT_OP_WRITE_WITHOUT_RESPOND);
}
static void ble_out_flush(void) { ble_out_drain(core_ms(), ble_out_tx); }   /* ble_service: MIDI out */
/* one FM-Link message to the other FM-1 (main loop only): the host writes it on the FM1 slot's link, the device role
 * notifies its host (without the CCCD: the host's writes say it is an FM-1). 1 = sent; 0 = no such link, or no room
 * (the engine sends again) */
int ble_fml_send(const uint8_t *p, uint32_t len)
{
    uint32_t room = 0, op = BLE_SDK_ATT_OP_WRITE_WITHOUT_RESPOND;
    uint16_t con = sl[1].con, h = sl[1].fml;
    if (role_now) {
        con = con_handle;
        h = ble_linked() && (fml_heard || multi_att_get_ccc_config(con_handle, BLE_GATT_FML_CCC)) ? BLE_GATT_FML : 0u;
        op = BLE_SDK_ATT_OP_NOTIFY;
    } else if (hs.s[1].st != HS_UP) {
        return 0;
    }
    if (!h || ble_op_multi_att_get_remain(con, &room) || room < len ||
        ble_op_multi_att_send_data(con, h, (void *)p, len, op))
        return 0;
    fml_out++;
    return 1;
}
int ble_fml_ready(void) { return role_now ? ble_linked() : hs.s[1].st == HS_UP; }   /* a link FM-Link may go on */

/* ---- reconnecting and the Bluetooth page's entry points (ble_core.h ble_ui_*; the core's main loop). None of
 * them starts the stack: the page calls ble_user_start (gate 3). ---- */
_Static_assert(BLE_ST_OFF == BLE_OFF && BLE_ST_STARTING == BLE_STARTING && BLE_ST_IDLE == BLE_IDLE &&
               BLE_ST_SCANNING == BLE_SCANNING && BLE_ST_CONNECTING == BLE_CONNECTING &&
               BLE_ST_DISCOVERING == BLE_DISCOVERING && BLE_ST_SUBSCRIBED == BLE_SUBSCRIBED &&
               BLE_ST_RECEIVING == BLE_RECEIVING && BLE_ST_FAILED == BLE_FAILED &&
               BLE_ST_ADVERTISING == BLE_ADVERTISING, "ble_core.h's states");
static uint8_t link_seen;                           /* the MIDI link as core_ble_link last told the core */
static void ble_link_note(void)                    /* link up / down to the core, once each */
{
    uint8_t up = (uint8_t)ble_linked();
    if (up != link_seen) {
        link_seen = up;
        core_ble_link(up);
    }
}
static uint32_t ble_listed(void)
{
    uint8_t idx[BLE_SCAN_MAX];
    return ble_scan_sorted(&scan, idx);
}
static int ble_host_idle(void)                     /* the host side has nothing going on */
{
    return hs.s[0].st == HS_IDLE && hs.s[1].st == HS_IDLE && !hs.scan_on;
}
/* the role switched (ble_ui_role) only with nothing going on: no scan, no connect, no link, no advertising; until
 * then those are ended (ble_host / ble_dev: not wanted while switching). Both GATT roles are registered from the start
 * (ble_profile_init) */
static void ble_role_apply(void)
{
    if (role_want == role_now || st == BLE_OFF || st == BLE_STARTING || st == BLE_FAILED || !ble_host_idle())
        return;
    if (st == BLE_ADVERTISING) {
        ble_op_adv_enable(0);
        st = BLE_IDLE;
    }
    if (st != BLE_IDLE) {
        if (con_handle)
            ble_op_disconnect(con_handle);         /* (its disconnection-complete makes it IDLE) */
        return;
    }
    role_now = role_want;
}
static void ble_adv_on(void)                       /* le_trans_data.c 800-865: connectable, 100 ms, every channel */
{
    ble_op_set_adv_param(160, BLE_SDK_ADV_IND, BLE_SDK_ADV_CHANNEL_ALL);
    ble_op_set_adv_data(sizeof BLE_ADV_DATA, BLE_ADV_DATA);
    ble_op_set_rsp_data(sizeof BLE_RSP_DATA, BLE_RSP_DATA);
    ble_op_adv_enable(1);
    st = BLE_ADVERTISING;
}
static void ble_dev(void)                          /* ble_service, the device role: the step applied */
{
    static uint8_t dropping;
    switch (ble_dev_step(want && role_want, st)) {
    case BV_ADV_ON:
        ble_adv_on();
        break;
    case BV_ADV_OFF:
        ble_op_adv_enable(0);
        st = BLE_IDLE;
        break;
    case BV_DROP:
        if (!dropping && con_handle)
            ble_op_disconnect(con_handle);
        dropping = 1;
        break;
    default:
        break;
    }
    if (!ble_linked())
        dropping = 0;
}
static int ble_slot_listed(uint32_t i, uint32_t now, uint32_t *seen)   /* the target, seen in the last 20 s */
{
    uint32_t k;
    for (k = 0; k < BLE_SCAN_MAX; k++)
        if (scan.dev[k].used && scan.dev[k].addr_type == sl[i].tgt_type && !memcmp(scan.dev[k].addr, sl[i].tgt, 6)) {
            *seen = scan.dev[k].seen_ms;
            return now - scan.dev[k].seen_ms <= 20000u;
        }
    return 0;
}
static void ble_host(void)                         /* ble_service, the host role: the step applied */
{
    static struct create_conn_param_t c;           /* (static, as the SDK's scan_buffer) */
    uint32_t i, k, now = core_ms(), seen = 0, act;
    static const uint8_t Z[6];
    hs.want = want && !role_now && !role_want && st == BLE_IDLE;
    for (i = 0; i < 2u; i++)
        if (hs.s[i].st == HS_CANCELLING && ble_due(now, hs.s[i].deadline_ms))
            hs.s[i].st = HS_IDLE;                  /* (no completion came) */
#if !defined(BLE_CONSOLE) || BLE_CONSOLE
    hs.scan_want = hs.scan_want || (int32_t)(console_scan_until - now) > 0;   /* (the console's `ble scan`) */
#endif
    ble_scan_expire(&scan, now, 30000u);
    for (i = 0; i < 2u; i++) {
        ble_hslot_t *s = &hs.s[i];
        s->target = memcmp(sl[i].tgt, Z, 6) != 0;
        s->listed = s->target && ble_slot_listed(i, now, &seen);
        if (sl[i].fail && s->listed && seen != sl[i].fail_seen)
            sl[i].fail = 0;                        /* seen again since it failed */
        s->tried = sl[i].fail;
        s->mismatch = s->st != HS_IDLE && (!s->target || memcmp(sl[i].addr, sl[i].tgt, 6));
    }
    act = ble_host_step(&hs, now, &i);
    switch (act) {
    case BH_SCAN_ON:                               /* continuous with no link, 8 ms in 24 beside one (multi_demo) */
        g_where = 0xC1;
        k = hs.s[0].st == HS_UP || hs.s[1].st == HS_UP;
        ble_op_set_scan_param(BLE_SDK_SCAN_ACTIVE, k ? 38 : 48, k ? 13 : 48);
        g_where = 0xC2;
        ble_op_scan_enable2(1, 0);
        g_where = 0xC3;
        hs.scan_on = 1;
        hs.scan_until = now + 10000u;
        break;
    case BH_SCAN_OFF:                              /* rest 2 s; 20 s when only the background search wants it */
        ble_op_scan_enable2(0, 0);
        hs.scan_on = 0;
        for (k = 0, i = 0; i < 2u; i++)
            k |= hs.s[i].st == HS_IDLE && hs.s[i].target && (!hs.s[i].listed || hs.s[i].tried);
        hs.scan_rest = now + (k || !hs.scan_bg ? 2000u : 20000u);
        break;
    case BH_CONNECT:
        memcpy(sl[i].addr, sl[i].tgt, 6);
        sl[i].type = sl[i].tgt_type;
        sl[i].name[0] = 0;
        for (k = 0; k < BLE_SCAN_MAX; k++)
            if (scan.dev[k].used && !memcmp(scan.dev[k].addr, sl[i].addr, 6))
                memcpy(sl[i].name, scan.dev[k].name, 24);
        c.conn_interval = 12;                      /* 15 ms */
        c.conn_latency = 0;
        c.supervision_timeout = 400;               /* 4 s */
        c.peer_address_type = sl[i].type;
        memcpy(c.peer_address, sl[i].addr, 6);
        set_err("", 0);
        hs.s[i].st = HS_CONNECTING;
        hs.s[i].deadline_ms = now + 5000u;
        sl[i].tries++;
        ble_op_create_connection(&c);
        break;
    case BH_CANCEL:                                /* busy until its completion comes (1 s at most) */
        ble_op_create_connection_cancel();
        hs.s[i].st = HS_CANCELLING;
        hs.s[i].deadline_ms = now + 1000u;
        ble_slot_failed(i);
        break;
    case BH_DROP:                                  /* its disconnection-complete makes it HS_IDLE */
        if (hs.s[i].st == HS_DISCOVERING && hs.want && !hs.s[i].mismatch)
            ble_slot_failed(i);                    /* (a discovery given up) */
        hs.s[i].dropping = 1;
        ble_op_disconnect(sl[i].con);
        break;
    default:
        break;
    }
}
static void ble_auto(void)                         /* ble_service: the role, the host's slots or the device role */
{
    ble_role_apply();
    if (role_now)
        ble_dev();
    else
        ble_host();
    ble_link_note();
}
_Static_assert(BLE_LS_IDLE == HS_IDLE && BLE_LS_CONNECTING == HS_CONNECTING && BLE_LS_DISCOVERING == HS_DISCOVERING &&
               BLE_LS_UP == HS_UP, "ble_core.h's link states");
uint32_t ble_ui_state(void)
{
    if (st == BLE_IDLE && hs.scan_on)
        return BLE_ST_SCANNING;
    return st;
}
const char *ble_ui_err(uint32_t *code) { return ble_err(code); }
uint32_t ble_ui_devs(void) { return ble_listed(); }
int ble_ui_dev(uint32_t i, ble_ui_dev_t *out)
{
    uint8_t idx[BLE_SCAN_MAX];
    const ble_dev_t *d;
    if (i >= ble_scan_sorted(&scan, idx))
        return 0;
    d = &scan.dev[idx[i]];
    memcpy(out->addr, d->addr, 6);
    out->type = d->addr_type;
    out->midi = d->midi;
    out->rssi = d->rssi;
    memcpy(out->name, d->name, sizeof out->name);
    out->seen_ms = d->seen_ms;
    return 1;
}
void ble_ui_slot_target(uint32_t slot, const uint8_t addr[6], uint32_t type)
{
    if (slot < 2u) {
        memcpy(sl[slot].tgt, addr, 6);
        sl[slot].tgt_type = (uint8_t)type;
    }
}
int ble_ui_slot(uint32_t slot, ble_ui_slot_t *out)
{
    if (slot >= 2u)
        return 0;
    out->st = hs.s[slot].st == HS_CANCELLING ? (uint8_t)HS_IDLE : hs.s[slot].st;
    memcpy(out->addr, hs.s[slot].st == HS_IDLE ? sl[slot].tgt : sl[slot].addr, 6);
    out->type = hs.s[slot].st == HS_IDLE ? sl[slot].tgt_type : sl[slot].type;
    out->last_reason = sl[slot].last_reason;
    out->drops = sl[slot].drops;
    memcpy(out->name, sl[slot].name, 24);
    return 1;
}
void ble_ui_scan_want(int on)                      /* 2 continuous (the page), 1 in windows (a better device), 0 none */
{
    hs.scan_want = on == 2;
    hs.scan_bg = on == 1;
}
void ble_ui_want(int on) { want = on != 0; }
int ble_ui_conn_name(char out[24])
{
    static const char H[] = "0123456789ABCDEF";
    uint32_t i;
    if (!ble_linked())
        return 0;
    if (role_now) {                                /* the device role: the host's address (no scan, no name) */
        for (i = 0; i < 6u; i++) {
            out[i * 3u] = H[peer[5u - i] >> 4];
            out[i * 3u + 1u] = H[peer[5u - i] & 15u];
            out[i * 3u + 2u] = i < 5u ? ':' : 0;
        }
        memset(out + 18, 0, 6);
        return 1;
    }
    memcpy(out, sl[0].name, 24);
    return 1;
}
void ble_ui_role(int device) { role_want = device != 0; }   /* (applied by ble_service once nothing is going on) */
uint32_t ble_ui_role_now(void) { return role_now; }

#if !defined(BLE_CONSOLE) || BLE_CONSOLE
/* ---- the console's `ble` (moved here from fm1-lsdj's ble_ui.c: Hortator has no BLE screen) ---- */
static const char *const STATE_NAME[] = {"OFF", "STARTING", "IDLE", "SCANNING", "CONNECTING", "DISCOVERING",
                                         "SUBSCRIBED", "RECEIVING", "FAILED", "ADVERTISING"};
static char *put_i(char *o, int32_t v) { if (v < 0) { *o++ = '-'; v = -v; } return put_u(o, (uint32_t)v); }
static ble_loop_t loop;                             /* ble_service (ble.c): the main loop's period */
BLE_CON void ble_list(void)                        /* console `ble list`: numbered as `ble connect N` takes them */
{
    char b[96], *o;
    uint32_t i, n;
    uint8_t idx[BLE_SCAN_MAX];
    n = ble_scan_sorted(&scan, idx);
    if (!n)
        core_con_puts("  (no devices: ble scan)\r\n");
    for (i = 0; i < n; i++) {
        const ble_dev_t *d = &scan.dev[idx[i]];
        uint32_t k;
        o = put_s(put_u(put_s(b, "  "), i + 1u), "  ");
        for (k = 6; k--;)
            o = put_x(o, d->addr[k]);
        o = put_u(put_s(o, " t"), d->addr_type);
        o = put_i(put_s(o, " "), d->rssi);
        o = put_s(o, d->midi ? " MIDI " : " ");
        o = put_s(o, d->name);
        o = put_s(o, "\r\n");
        *o = 0;
        core_con_puts(b);
    }
}
BLE_CON void ble_status(void)                      /* console `ble` */
{
    char b[224], *o;
    uint32_t code, i;
    const char *err = ble_err(&code);
    o = put_s(put_s(b, "ble: state "), STATE_NAME[st]);
    o = put_u(put_s(o, " want "), want);
    o = put_u(put_s(o, " reports "), scan.reports);
    o = put_u(put_s(o, " msgs "), bm.msgs);
    o = put_u(put_s(o, " pkts "), bm.packets);
    o = put_u(put_s(o, " errs "), bm.errors);
    o = put_u(put_s(o, " lost "), ring_lost);
    o = put_u(put_s(o, " out "), mo.sent);
    o = put_u(put_s(o, " out_drops "), mo.drops);
    o = put_u(put_s(o, " in_drops "), core_ble_in_drops());
    o = put_u(put_s(o, " audio_late "), core_audio_late());
    o = put_s(o, "\r\n");
    *o = 0;
    core_con_puts(b);
    for (i = 0; i < 2u; i++) {                     /* the host's slots: state (0 idle 1 connecting 2 discovering 3 up),
                                                     * target, the last disconnect's reason, drops, connects tried */
        o = put_s(b, i ? "  fm1 " : "  ctrl ");
        o = put_u(put_s(o, "state "), hs.s[i].st);
        o = put_s(o, " target ");
        for (code = 6; code--;)
            o = put_x(o, sl[i].tgt[code]);
        o = put_s(o, sl[i].fail ? " failed" : "");
        o = put_x(put_s(o, " reason 0x"), sl[i].last_reason);
        o = put_u(put_s(o, " drops "), sl[i].drops);
        o = put_u(put_s(o, " tries "), sl[i].tries);
        o = put_s(put_s(o, " "), sl[i].name);
        o = put_s(o, "\r\n");
        *o = 0;
        core_con_puts(b);
    }
    o = put_u(put_s(b, "  scan "), hs.scan_on);
    o = put_u(put_s(o, " want "), hs.scan_want);
    o = put_s(o, "  fm-link ");                   /* another FM-1: the FM1 slot up, or the device role's host wrote */
    o = put_s(o, hs.s[1].st == HS_UP || (role_now && fml_heard) ? "peer" : "none");
    o = put_u(put_s(o, " in "), fml_in);
    o = put_u(put_s(o, " out "), fml_out);
    o = put_u(put_s(o, " lost "), fq_lost);
    o = put_s(o, "\r\n");
    *o = 0;
    core_con_puts(b);
    o = put_s(b, "  rf trim ");
    o = ble_rf_trim_at == 1u ? put_s(o, "kept copy")
      : ble_rf_trim_at ? put_x(put_x(put_x(put_s(o, "at 0x"), ble_rf_trim_at >> 16), ble_rf_trim_at >> 8), ble_rf_trim_at)
      : put_s(o, "not loaded");
    o = put_s(o, "\r\n");
    *o = 0;
    core_con_puts(b);
    o = put_s(b, "  mac ");
    for (i = 6; i--;)
        o = put_x(o, ble_mac[i]);
    if (err[0])
        o = put_x(put_s(put_s(put_s(o, "  error "), err), " 0x"), code);
    o = put_s(o, "\r\n");
    *o = 0;
    core_con_puts(b);
    if (ble_diag.magic == BLE_DIAG_MAGIC) {     /* kept over a reboot: where the last start got to */
        o = put_u(put_s(b, "  starts "), ble_diag.starts);
        o = put_u(put_s(o, " stage "), ble_diag.stage);
        o = put_u(put_s(o, " task "), ble_diag.task);
        o = put_u(put_s(o, " irq_leaks "), ble_diag.irq_leaks);
        o = put_u(put_s(o, " fatal "), ble_diag.fatal);   /* 1 assert, 2 reset request, 3 a stack overflow */
        o = put_u(put_s(o, " irq_lowered "), ble_diag.irq_lowered);
        o = put_s(o, "\r\n");
        *o = 0;
        core_con_puts(b);
    }
    if (fm1_crash.magic == 0x43525348u)          /* the core's crash record (fm1_irq.h): the console's `crash` */
        core_con_puts("  crash kept: `crash`\r\n");
    if (ble_diag.magic == BLE_DIAG_MAGIC) {     /* (.noinit: nothing to show before the first start) */
        o = put_u(put_s(b, "  heap high "), ble_diag.heap_high);
        o = put_u(put_s(o, " of "), sizeof ble_heap);
        o = put_u(put_s(o, " alloc fails "), ble_diag.alloc_fails);   /* (kept over a reboot) */
        o = put_s(o, "\r\n");
        *o = 0;
        core_con_puts(b);
    } else {
        core_con_puts("  heap: no record\r\n");
    }
    o = put_u(put_s(b, "  loop last "), loop.last_ms);
    o = put_u(put_s(o, " ms, max "), loop.max_ms);
    o = put_u(put_s(o, " ms; task max "), ble_os_run_max_us(1));   /* the longest BT task run */
    o = put_u(put_s(o, " us; irq_off max "), fm1_ble_irq_max / 24u);   /* the libraries' longest interrupts-off */
    o = put_u(put_s(o, " us; audio gap max "), core_audio_gap_us());   /* 2902 normal: more, the audio held off */
    o = put_s(o, " us\r\n");                  /* (all since the last `ble`) */
    *o = 0;
    core_con_puts(b);
    loop.max_ms = 0;
    fm1_ble_irq_max = 0;
    for (i = 0; i < 4u; i++) {                     /* the task stacks' unused bytes */
        const char *name;
        uint32_t f = ble_os_stack_free(i, &name);
        if (!name)
            continue;
        o = put_u(put_s(put_s(put_s(b, "  task "), name), " stack free "), f);
        o = put_s(o, "\r\n");
        *o = 0;
        core_con_puts(b);
    }
    ble_list();
}

#endif
/* ---- the console (part 1b): the core's `ble …` line comes here (ble_core.h ble_console) ---- */
/* the user's trigger, the only way to ble_start (gate 3: tools/check_ble_boot.py); not inlined, so the gate sees it */
__attribute__((noinline)) void ble_user_start(void) { ble_start(); }
#if !defined(BLE_CONSOLE) || BLE_CONSOLE
BLE_CON static void say_state(void)
{
    char b[96], *o;
    uint32_t code;
    const char *err = ble_err(&code);
    o = put_s(put_s(b, "ble: "), STATE_NAME[st]);
    if (err[0])
        o = put_x(put_s(put_s(put_s(o, "  error "), err), " 0x"), code);
    o = put_s(o, "\r\n");
    *o = 0;
    core_con_puts(b);
}
BLE_CON void ble_console(const char *args)
{
    uint32_t cmd = ble_cmd_parse(args);
    const char *no = ble_cmd_refuse(cmd, ctrl_up || hs.s[1].st == HS_UP ? BLE_SUBSCRIBED : st);
    if (no) {
        core_con_puts(no);
        core_con_puts("\r\n");
        return;
    }
    switch (cmd) {
    case BLE_CMD_STATUS: ble_status(); return;
    case BLE_CMD_LIST: ble_list(); return;
    case BLE_CMD_START: ble_user_start(); break;
    case BLE_CMD_SCAN: console_scan_until = core_ms() + 10000u; break;   /* (a 10 s scan beside the links) */
    case BLE_CMD_STOP: want = 0; break;            /* (BLUETOOTH ON on the page wants them again) */
    default:
        core_con_puts("ble [start | scan | list | stop]\r\n");
        return;
    }
    say_state();
}
#endif   /* BLE_CONSOLE */

void ble_ui_get_diag(uint32_t out[16])
{
    out[0] = ble_diag.magic;
    out[1] = ble_diag.stage;
    out[2] = ble_diag.task;
    out[3] = ble_diag.starts;
    out[4] = ble_diag.trim;
    out[5] = ble_diag.delays;
    out[6] = ble_diag.last_us;
    out[7] = ble_diag.t4_stalls;
    out[8] = ble_diag.irq_leaks;
    out[9] = ble_diag.fatal;
    out[10] = ble_diag.heap_high;
    out[11] = ble_diag.alloc_fails;
    out[12] = ble_diag.irq_lowered;
    out[13] = st;
    out[14] = hs.scan_on;
    out[15] = hs.scan_want;
}
#endif


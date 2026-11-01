/* SPDX-License-Identifier: GPL-3.0-only */
/* FM-106 Bluetooth Low Energy (BLE) MIDI Stack Glue
 * Ported from DeadActive/hortator (JieLi AC79 BLE stack)
 * Supports:
 *  - BLE-MIDI Peripheral ("FM-106" advertised to DAWs, Mac, iOS, Windows)
 *  - BLE-MIDI Central (connects wirelessly to MIDI keyboards e.g. M-VAVE SMK-25)
 *  - Dual-bank redundant NVRAM storage at flash 0x9F000 / 0xFE000
 */
#pragma once
#include <stdint.h>
#include "ble/ble_core.h"
#include "fm1_ble_hal.h"

#define BT_ST_MAGIC 0x31544248u   /* "HBT1" */
#define BT_ST_VER   1u
#define BT_NDEV     8u

typedef struct {
    uint8_t addr[6], flags, rsv;
    char name[16];
} bt_st_dev_t;

typedef struct {
    uint32_t magic;
    uint16_t ver, len;
    uint32_t seq;
    uint8_t set;        /* b0: role (0=HOST, 1=DEVICE), b1: autostart, b2..3: rate */
    uint8_t rsv[3];
    bt_st_dev_t dev[BT_NDEV];
    uint8_t mac[8];     /* MAC address, [6] 0xA5 when valid */
    uint8_t rf[72];     /* Factory RF calibration copy, [68] 0xA5 when valid */
    uint32_t crc;
} bt_st_rec_t;

static struct {
    uint8_t loaded, dirty, cur;
    uint8_t fails;
    bt_st_rec_t r;
} bt_st;

static struct {
    uint8_t up[2], up_addr[2][6];
    uint8_t claim[2], claim_addr[2][6];
    uint32_t claim_ms[2];
    uint8_t page;
} bt_dv;

static struct {
    uint8_t on;
    uint8_t sel;
    uint8_t add, asel, an;
    uint32_t add_t0;
    ble_ui_dev_t adev[8];
} btp;

enum { BT_KIND_CTRL = 0, BT_KIND_FM1 = 1 };
typedef struct { uint8_t addr[6], type, kind; char name[17]; } bt_dev_t;

enum { BT_ROLE_HOST = 0, BT_ROLE_DEVICE = 1 };
enum { BT_RATE_AUTO = 0, BT_RATE_FAST = 1, BT_RATE_SLOW = 2 };
typedef struct { uint8_t mode, autostart, role, rate; } bt_set_t;

static int synth_quiet(void);
static int bt_devs_get(uint32_t i, bt_dev_t *d);

/* ---- Flash Storage (0x9F000 / 0xFE000) ---- */
static inline uint32_t bt_st_sector(uint32_t copy) { return copy ? BT_ST_B : BT_ST_A; }

static inline int bt_st_valid(const bt_st_rec_t *r)
{
    return r->magic == BT_ST_MAGIC && r->ver == BT_ST_VER && r->len == sizeof(*r) &&
           r->crc == st_crc32(r, sizeof(*r) - 4u);
}

static void bt_st_load(void)
{
    if (bt_st.loaded) return;
    bt_st.loaded = 1;
    bt_st.cur = 2;
    if (flash_ok) {
        bt_st_rec_t c;
        int va = !st_read(bt_st_sector(0), &bt_st.r, sizeof(bt_st.r)) && bt_st_valid(&bt_st.r);
        int vb = !st_read(bt_st_sector(1), &c, sizeof(c)) && bt_st_valid(&c);
        if (vb && (!va || (c.seq != bt_st.r.seq && c.seq - bt_st.r.seq < 0x80000000u))) {
            bt_st.r = c;
            bt_st.cur = 1;
            return;
        }
        if (va) {
            bt_st.cur = 0;
            return;
        }
    }
    memset(&bt_st.r, 0, sizeof(bt_st.r));
    bt_st.r.set = 1u;   /* Default: DEVICE role (advertises as FM-106 to DAWs) */
}

static uint32_t bt_st_dirty_ms;
static inline void bt_st_save(void)
{
    bt_st.dirty = 1;
    bt_st_dirty_ms = fm1_ms;
}
static uint32_t bt_st_diag(void)
{
    return (uint32_t)bt_st.cur | (uint32_t)bt_st.fails << 8 | (uint32_t)bt_st.dirty << 16 | (uint32_t)bt_st.loaded << 24;
}

static void bt_st_poll(void)
{
    if (!bt_st.dirty || !flash_ok) return;
    /* Debounce: wait 300 ms after the last setting change before flashing */
    if (fm1_ms - bt_st_dirty_ms < 300u) return;
    /* Never erase flash while actively scanning for new devices */
    if (btp.add) return;
    /* Never erase flash while sequencer or arpeggiator is actively playing notes */
    if (g_synth.seq.playing || g_synth.arp_on) return;
    /* Do not erase flash while keys are actively held */
    if (fm1_in.notes != 0) return;
    for (uint32_t v = 0; v < FM106_NUM_VOICES; v++) {
        if (g_synth.voice[v].gate) return;
    }
#if DINGHY_DUAL_CORE
    if (audio_worker_online) audio_worker_join();
#endif

    uint32_t copy = bt_st.cur == 0u ? 1u : 0u;
    uint32_t base = bt_st_sector(copy);
    bt_st_rec_t chk;

    bt_st.dirty = 0;
    bt_st.r.magic = BT_ST_MAGIC;
    bt_st.r.ver = BT_ST_VER;
    bt_st.r.len = sizeof(bt_st.r);
    bt_st.r.seq++;
    bt_st.r.crc = st_crc32(&bt_st.r, sizeof(bt_st.r) - 4u);

    if (st_erase(base) ||
        st_prog(base + 4u, (const uint8_t *)&bt_st.r + 4u, 256u - 4u) ||
        st_prog(base + 256u, (const uint8_t *)&bt_st.r + 256u, sizeof(bt_st.r) - 256u) ||
        st_prog(base, &bt_st.r, 4u) ||   /* Commit magic word last */
        st_read(base, &chk, sizeof(chk)) || memcmp(&chk, &bt_st.r, sizeof(chk))) {
        bt_st.fails++;
        return;
    }
    bt_st.cur = (uint8_t)copy;
}

static void bt_set_get(bt_set_t *s)
{
    bt_st_load();
    uint8_t set = bt_st.r.set;
    s->role = set & 1u;             /* 0 = HOST, 1 = DEVICE */
    s->autostart = (set >> 1) & 1u; /* 0 = OFF, 1 = ON */
    s->rate = (set >> 2) & 3u;      /* 0 = AUTO, 1 = FAST, 2 = SLOW */
    s->mode = 0;                    /* MIDI */
}

static void bt_set_put(const bt_set_t *s)
{
    bt_st_load();
    uint8_t set = (s->role & 1u) | ((s->autostart & 1u) << 1) | ((s->rate & 3u) << 2);
    if (set != bt_st.r.set) {
        bt_st.r.set = set;
        bt_st_save();
    }
}

static int bt_set_slow(int playing)
{
    bt_set_t s;
    bt_set_get(&s);
    return s.rate == BT_RATE_SLOW || (s.rate == BT_RATE_AUTO && playing);
}

/* ---- Saved Devices & Factory RF Storage ---- */
static int bt_rf_io(uint8_t *rec, int put)
{
    bt_st_load();
    uint8_t *k = bt_st.r.rf;
    if (!put) {
        if (k[68] != 0xA5u) return 0;
        memcpy(rec, k, 68);
        return 1;
    }
    if (k[68] != 0xA5u || memcmp(k, rec, 68)) {
        memcpy(k, rec, 68);
        k[68] = 0xA5u;
        bt_st_save();
    }
    return 1;
}

static void bt_mac(uint8_t mac[6])
{
    bt_st_load();
    uint8_t *r = bt_st.r.mac;
    if (r[6] != 0xA5u) {
        uint32_t x = fm1_ticks() ^ (fm1_ms * 2654435761u);
        for (uint32_t i = 0; i < 6u; i++) {
            x = x * 1103515245u + 12345u;
            r[i] = (uint8_t)(x >> 16);
        }
        r[0] |= 0xC0u;
        r[5] |= 0xC0u;
        r[6] = 0xA5u;
        bt_st_save();
    }
    memcpy(mac, r, 6);
}

static int bt_dev_read(uint32_t i, bt_dev_t *d)
{
    bt_st_load();
    if (i >= BT_NDEV) return 0;
    const bt_st_dev_t *r = &bt_st.r.dev[i];
    if (!(r->flags & 0x80u)) return 0;
    memcpy(d->addr, r->addr, 6);
    d->type = r->flags & 1u;
    d->kind = (r->flags >> 1) & 1u;
    memcpy(d->name, r->name, 16);
    d->name[16] = 0;
    return 1;
}

static uint32_t bt_devs_all(bt_dev_t *l)
{
    uint32_t n = 0;
    for (uint32_t i = 0; i < BT_NDEV; i++) n += (uint32_t)bt_dev_read(i, &l[n]);
    return n;
}

static void bt_devs_put(const bt_dev_t *l, uint32_t n)
{
    bt_st_load();
    for (uint32_t i = 0; i < BT_NDEV; i++) {
        bt_st_dev_t *r = &bt_st.r.dev[i];
        memset(r, 0, sizeof(*r));
        if (i < n) {
            memcpy(r->addr, l[i].addr, 6);
            r->flags = (uint8_t)(0x80u | (l[i].type & 1u) | ((l[i].kind & 1u) << 1));
            memcpy(r->name, l[i].name, 16);
        }
    }
    bt_st_save();
}

static void bt_devs_add(const bt_dev_t *d)
{
    bt_dev_t l[BT_NDEV + 1u];
    uint32_t n = bt_devs_all(l + 1);
    l[0] = *d;
    l[0].name[16] = 0;
    uint32_t k = 1;
    for (uint32_t i = 1; i <= n; i++) {
        if (memcmp(l[i].addr, d->addr, 6)) l[k++] = l[i];
    }
    if (k > BT_NDEV) k = BT_NDEV;
    bt_devs_put(l, k);
}

static void bt_devs_forget(uint32_t i)
{
    bt_dev_t l[BT_NDEV];
    uint32_t n = bt_devs_all(l), k;
    if (i >= n)
        return;
    for (k = i; k + 1u < n; k++)
        l[k] = l[k + 1u];
    bt_devs_put(l, n - 1u);
}

static void bt_devs_use(uint32_t i)
{
    bt_dev_t d;
    if (bt_devs_get(i, &d))
        bt_devs_add(&d);
}

#define BT_AROUND_MS 20000u
#define BT_GRACE_MS 30000u
#define BT_SHOW 8u
#define BT_ADD_MS 8000u

static void bt_devs_page(int on) { bt_dv.page = (uint8_t)(on != 0); }

static int bt_devs_get(uint32_t i, bt_dev_t *d)
{
    bt_dev_t l[BT_NDEV];
    if (i >= bt_devs_all(l))
        return 0;
    *d = l[i];
    return 1;
}

static int bt_dev_find(const uint8_t a[6])
{
    bt_dev_t d;
    for (uint32_t i = 0; bt_devs_get(i, &d); i++) {
        if (!memcmp(d.addr, a, 6))
            return (int)i;
    }
    return -1;
}

static int bt_saved(const uint8_t a[6])
{
    return bt_dev_find(a) >= 0;
}

static int bt_dev_kind(const char *name)
{
    static const char N[] = "FM-1";
    uint32_t i;
    for (i = 0; N[i]; i++)
        if (name[i] != N[i])
            return BT_KIND_CTRL;
    return BT_KIND_FM1;
}

static void bt_name(char *o, const char *s, uint32_t n)
{
    uint32_t i;
    for (i = 0; i + 1u < n && s[i]; i++)
        o[i] = s[i] >= 0x20 && s[i] < 0x7F ? s[i] : '?';
    o[i] = 0;
}

static int bt_slot_on(uint32_t kind, const uint8_t addr[6])
{
    ble_ui_slot_t x;
    return ble_ui_slot(kind, &x) && x.st == BLE_LS_UP && !memcmp(x.addr, addr, 6);
}

static int bt_dev_scanned(const uint8_t addr[6], ble_ui_dev_t *s)
{
    uint32_t i;
    for (i = 0; ble_ui_dev(i, s); i++)
        if (!memcmp(s->addr, addr, 6))
            return 1;
    return 0;
}

static int bt_dev_around(const bt_dev_t *d)
{
    ble_ui_dev_t s;
    return bt_dev_scanned(d->addr, &s) && fm1_ms - s.seen_ms <= BT_AROUND_MS;
}

static int bt_devs_choose(uint32_t kind, const uint8_t *around, int linked, uint32_t now_ms)
{
    bt_dev_t l[BT_NDEV];
    uint32_t n = bt_devs_all(l), i;
    int claim = bt_dv.claim[kind] && now_ms - bt_dv.claim_ms[kind] < BT_GRACE_MS;
    for (i = 0; i < n; i++)
        if (l[i].kind == kind && (around[i] || (int)i == linked || (claim && !memcmp(l[i].addr, bt_dv.claim_addr[kind], 6))))
            return (int)i;
    return -1;
}

static void bt_devs_name(bt_dev_t *l, uint32_t n)
{
    ble_ui_dev_t s;
    uint32_t i, got = 0;
    for (i = 0; i < n; i++)
        if (!l[i].name[0] && bt_dev_scanned(l[i].addr, &s) && s.name[0]) {
            s.name[sizeof s.name - 1u] = 0;
            str_cpy(l[i].name, s.name, sizeof l[i].name);
            l[i].kind = (uint8_t)bt_dev_kind(s.name);
            got = 1;
        }
    if (got)
        bt_devs_put(l, n);
}

static void bt_devs_frame(void)
{
    static const uint8_t Z[6];
    bt_dev_t l[BT_NDEV];
    uint8_t around[BT_NDEV];
    uint32_t n = bt_devs_all(l), i, kind, scan = bt_dv.page ? 2u : 0u;
    bt_devs_name(l, n);
    for (i = 0; i < n; i++)
        around[i] = (uint8_t)bt_dev_around(&l[i]);
    for (kind = 0; kind < 2u; kind++) {
        ble_ui_slot_t x;
        int linked = -1, c, first = -1;
        int up = ble_ui_slot(kind, &x) && x.st == BLE_LS_UP;
        if (up) {
            memcpy(bt_dv.up_addr[kind], x.addr, 6);
            bt_dv.claim[kind] = 0;
        } else if (bt_dv.up[kind]) {
            bt_dv.claim[kind] = 1;
            bt_dv.claim_ms[kind] = fm1_ms;
            memcpy(bt_dv.claim_addr[kind], bt_dv.up_addr[kind], 6);
        }
        bt_dv.up[kind] = (uint8_t)up;
        for (i = 0; i < n; i++) {
            if (l[i].kind != kind)
                continue;
            if (first < 0)
                first = (int)i;
            if (up && !memcmp(l[i].addr, x.addr, 6))
                linked = (int)i;
        }
        c = bt_devs_choose(kind, around, linked, fm1_ms);
        if (c < 0)
            c = first;
        if (c >= 0 && !btp.add)
            ble_ui_slot_target(kind, l[c].addr, l[c].type);
        else
            ble_ui_slot_target(kind, Z, 0);
        if (up && linked != first && !scan)
            scan = 1;
        if (!up && n > 0 && !scan)
            scan = 1;
    }
    ble_ui_scan_want((int)scan);
}

/* ---- BLE MIDI In / Out Queue ---- */
#define BT_OUT_Q 64u
static struct {
    uint32_t drops;
    uint32_t out_drops;
    uint16_t held, release;
    uint8_t up;
    volatile uint8_t out_on;
} bt_in;

static uint32_t bt_out_q[BT_OUT_Q];
static volatile uint32_t bt_out_w, bt_out_r;

static void bt_link(int up)
{
    bt_in.up = (uint8_t)(up != 0);
    bt_in.out_on = (uint8_t)(up != 0);
    if (!up) {
        bt_in.release |= bt_in.held;
        bt_in.held = 0;
        bt_out_r = bt_out_w;
    }
}

static void bt_midi_in(uint32_t pkt)
{
    uint32_t cin = pkt & 15u, st = (pkt >> 8) & 0xFFu;
    if (cin == 0xFu) {
        if (st != 0xF8u && st != 0xFAu && st != 0xFBu && st != 0xFCu) return;
    } else if (cin < 8u || cin > 0xEu) {
        return;
    }

    uint32_t f = irq_save();
    if (MQ - (mi_w - mi_r) > 8u) {
        midi_in_q[mi_w % MQ] = pkt;
        RING_PUBLISH();
        mi_w++;
    } else {
        bt_in.drops++;
    }
    irq_restore(f);

    if ((cin == 9u && (pkt >> 24)) || (cin == 0xBu && ((pkt >> 16) & 0xFFu) == 64u && (pkt >> 24)))
        bt_in.held |= (uint16_t)(1u << (st & 15u));
}

static void bt_out_push(uint32_t pkt)
{
    if (bt_out_w - bt_out_r >= BT_OUT_Q) {
        bt_in.out_drops++;
        return;
    }
    bt_out_q[bt_out_w % BT_OUT_Q] = pkt;
    RING_PUBLISH();
    bt_out_w++;
}

static inline void bt_midi_out_event(uint32_t pkt)
{
    if (!bt_in.out_on) return;
    bt_out_push(pkt);
}

static void bt_midi_frame(void)
{
    /* Release dangling held notes if a link dropped */
    for (uint32_t ch = 0; ch < 16u && bt_in.release; ch++) {
        if (!((bt_in.release >> ch) & 1u)) continue;
        uint32_t f = irq_save();
        if (MQ - (mi_w - mi_r) > 9u) {
            midi_in_q[mi_w % MQ] = 0x0Bu | (0xB0u | ch) << 8 | 64u << 16;
            RING_PUBLISH();
            mi_w++;
            midi_in_q[mi_w % MQ] = 0x0Bu | (0xB0u | ch) << 8 | 123u << 16;
            RING_PUBLISH();
            mi_w++;
            bt_in.release &= (uint16_t)~(1u << ch);
        }
        irq_restore(f);
    }

    bt_in.out_on = bt_in.up;
    while (bt_out_r != bt_out_w && ble_midi_room()) {
        uint32_t pkt = bt_out_q[bt_out_r % BT_OUT_Q], cin = pkt & 15u;
        uint8_t m[3] = {(uint8_t)(pkt >> 8), (uint8_t)(pkt >> 16), (uint8_t)(pkt >> 24)};
        uint32_t len = (cin == 0xFu) ? 1u : (cin == 0xCu || cin == 0xDu) ? 2u : 3u;
        if (!ble_midi_send(m, len)) break;
        bt_out_r++;
    }
}

/* ---- User UI Control State ---- */
static void bt_page_on(void)
{
    if (btp.on) return;
    btp.on = 1;
    bt_set_t s;
    bt_set_get(&s);
    ble_ui_role((int)s.role);
    ble_ui_want(1);
    if (ble_ui_state() == BLE_ST_OFF) ble_user_start();
}

static void bt_page_off(void)
{
    btp.on = 0;
    ble_ui_want(0);
}

static void bt_service(void)
{
    g_where = 0x10;
    ble_service();
    g_where = 0x11;
    bt_devs_frame();
    g_where = 0x12;
    bt_midi_frame();
    g_where = 0x13;
    bt_st_poll();
    g_where = 0x14;
}

static uint8_t bt_auto_done;

static void bt_init(void)
{
    g_midi_out_hook = bt_midi_out_event;
    bt_st_load();
}

static void bt_autostart_tick(void)
{
    bt_set_t s;
    if (bt_auto_done || fm1_ms < 2000u)
        return;
    bt_auto_done = 1;
    bt_set_get(&s);
    if (!s.autostart || btp.on || ble_ui_state() != BLE_ST_OFF)
        return;
    bt_page_on();
}

static void bt_get_status_str(char *out, uint32_t max_len)
{
    uint32_t st = ble_ui_state();
    char conn[24] = {0};
    if (!btp.on || st == BLE_ST_OFF) {
        str_cpy(out, "OFF", max_len);
    } else if (st == BLE_ST_STARTING) {
        str_cpy(out, "STARTING...", max_len);
    } else if (st == BLE_ST_FAILED) {
        uint32_t code = 0;
        const char *err = ble_ui_err(&code);
        if (err && err[0]) {
            str_cpy(out, err, max_len);
        } else {
            str_cpy(out, "FAILED", max_len);
        }
    } else if (st == BLE_ST_ADVERTISING) {
        str_cpy(out, "ADV (FM-106)", max_len);
    } else if (st == BLE_ST_SUBSCRIBED || st == BLE_ST_RECEIVING) {
        if (ble_ui_conn_name(conn) && conn[0]) {
            out[0] = 'C'; out[1] = 'O'; out[2] = 'N'; out[3] = ':'; out[4] = ' ';
            uint32_t i = 0, j = 5;
            while (conn[i] && j < max_len - 1u) out[j++] = conn[i++];
            out[j] = '\0';
        } else {
            str_cpy(out, "CONNECTED", max_len);
        }
    } else if (st == BLE_ST_SCANNING) {
        str_cpy(out, "SCANNING...", max_len);
    } else if (st == BLE_ST_CONNECTING) {
        str_cpy(out, "CONNECTING...", max_len);
    } else {
        str_cpy(out, "READY", max_len);
    }
}

static inline int bt_has_proper_name(const char *name)
{
    if (!name || !name[0]) return 0;
    uint32_t printable = 0;
    for (uint32_t i = 0; name[i] && i < 16u; i++) {
        uint8_t c = (uint8_t)name[i];
        if (c >= 0x20 && c <= 0x7E && c != '?') {
            if (c > 0x20) printable++;
        } else {
            return 0; /* Reject non-printable or corrupt characters */
        }
    }
    return printable >= 1;
}

static void bt_add_list(void)
{
    ble_ui_dev_t d;
    uint8_t was[6];
    uint32_t i, k, pass, n = ble_ui_devs(), had = btp.asel < btp.an;
    if (fm1_ms - btp.add_t0 >= BT_ADD_MS)
        return;                                     /* (scan over: list stays as it is) */
    if (had)
        memcpy(was, btp.adev[btp.asel].addr, 6);
    btp.an = 0;
    for (pass = 0; pass < 2u; pass++)
        for (i = 0; i < n; i++) {
            if (!ble_ui_dev(i, &d) || (d.midi != 0) != (pass == 0u) || btp.an >= BT_SHOW)
                continue;
            d.name[sizeof d.name - 1u] = 0;
            if (!bt_has_proper_name(d.name))
                continue;                           /* Filter out nameless BLE devices */
            btp.adev[btp.an++] = d;
        }
    for (k = 0; had && k < btp.an; k++)
        if (!memcmp(btp.adev[k].addr, was, 6))
            btp.asel = (uint8_t)k;
    if (btp.asel >= btp.an)
        btp.asel = btp.an ? (uint8_t)(btp.an - 1u) : 0u;
}

static void bt_add_close(void)
{
    btp.add = 0;
}

static void bt_page_open(void)
{
    bt_set_t s;
    bt_set_get(&s);
    bt_add_close();
    bt_devs_page(!s.role);
}

static void bt_page_close(void)
{
    bt_add_close();
    bt_devs_page(0);
}

static void bt_host_scan_start(void)
{
    g_where = 0x84;
    btp.add = 1;
    btp.add_t0 = fm1_ms;
    btp.an = 0;
    btp.asel = 0;
    ble_ui_role(BT_ROLE_HOST);
    bt_devs_page(1);
}

static void bt_host_connect_first_available(void)
{
    uint32_t n = ble_ui_devs();
    ble_ui_dev_t d;
    for (uint32_t i = 0; i < n; i++) {
        if (ble_ui_dev(i, &d) && d.midi && bt_has_proper_name(d.name)) {
            bt_dev_t dev;
            memset(&dev, 0, sizeof(dev));
            memcpy(dev.addr, d.addr, 6);
            dev.type = d.type & 1u;
            dev.kind = BT_KIND_CTRL;
            uint32_t len = 0;
            while (d.name[len] && len < 16u) {
                dev.name[len] = d.name[len];
                len++;
            }
            dev.name[len] = '\0';
            bt_devs_add(&dev);
            ble_ui_slot_target(BT_KIND_CTRL, dev.addr, dev.type);
            break;
        }
    }
}


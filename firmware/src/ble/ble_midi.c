/* SPDX-License-Identifier: GPL-3.0-only
 * BLE MIDI test: 2026 DEADACTIVE */
/* BLE-MIDI parser. A packet: header (1 0 t12..t7), then per message a timestamp byte (1 t6..t0) and the MIDI
 * bytes. Running status may drop the status byte, and then the timestamp too. Inside SysEx only F7, real-time and
 * timestamp bytes have bit 7 set; a SysEx can run across packets (the next packet's bytes after its header are
 * data until a timestamp + F7). Malformed input is counted in errors and dropped. */
#include "ble_midi.h"

static uint32_t midi_len(uint8_t st)            /* total bytes of a message with this status */
{
    if (st < 0xC0u || (st >= 0xE0u && st < 0xF0u))
        return 3;
    if (st < 0xE0u)
        return 2;                                 /* Cx, Dx */
    switch (st) {
    case 0xF1: case 0xF3: return 2;
    case 0xF2: return 3;
    default: return 1;                            /* F6 and the undefined F4 / F5 */
    }
}

static uint32_t cin_of(uint8_t st, uint32_t len)
{
    if (st < 0xF0u)
        return st >> 4;
    if (st >= 0xF8u)
        return 0xFu;                              /* real-time: single byte */
    return len == 3u ? 3u : len == 2u ? 2u : 5u;  /* system common, 1..3 bytes */
}

void ble_midi_reset(ble_midi_t *m)
{
    uint8_t *p = (uint8_t *)m;
    uint32_t i;
    for (i = 0; i < sizeof *m; i++)
        p[i] = 0;
}

static void emit(ble_midi_t *m, ble_midi_out_fn out, void *ctx, uint8_t st, uint8_t a, uint8_t b)
{
    uint32_t len = midi_len(st);
    out(ctx, cin_of(st, len) | (uint32_t)st << 8 | (uint32_t)(len > 1u ? a : 0u) << 16 |
             (uint32_t)(len > 2u ? b : 0u) << 24);
    m->msgs++;
}

static void sx_byte(ble_midi_t *m, ble_midi_out_fn out, void *ctx, uint8_t b)
{
    m->sx_buf[m->sx_n++] = b;
    m->sx_len++;
    if (b == 0xF7u) {                             /* end: CIN 5/6/7 with 1/2/3 bytes */
        uint32_t cin = 4u + m->sx_n;
        out(ctx, cin | (uint32_t)m->sx_buf[0] << 8 | (m->sx_n > 1u ? (uint32_t)m->sx_buf[1] << 16 : 0u) |
                 (m->sx_n > 2u ? (uint32_t)m->sx_buf[2] << 24 : 0u));
        m->sx_n = 0;
        m->in_sysex = 0;
        m->sysex_done++;
        m->msgs++;
    } else if (m->sx_n == 3u) {                   /* start / continue: CIN 4 */
        out(ctx, 4u | (uint32_t)m->sx_buf[0] << 8 | (uint32_t)m->sx_buf[1] << 16 | (uint32_t)m->sx_buf[2] << 24);
        m->sx_n = 0;
    }
}

void ble_midi_parse(ble_midi_t *m, const uint8_t *p, uint32_t n, ble_midi_out_fn out, void *ctx)
{
    uint32_t i, want_ts, sx_ts = 0;               /* sx_ts: the last SysEx byte was a timestamp */
    if (n < 2u || (p[0] & 0xC0u) != 0x80u) {
        m->errors++;
        return;
    }
    m->packets++;
    want_ts = !m->in_sysex;                       /* a SysEx continuation starts with data */
    if (m->need) {                                /* a message cut off at the last packet's end */
        m->errors++;
        m->need = 0;
    }
    for (i = 1; i < n; i++) {
        uint8_t b = p[i];
        if (m->in_sysex) {                        /* inside SysEx every status byte follows a timestamp */
            if (!(b & 0x80u)) {
                sx_byte(m, out, ctx, b);
                sx_ts = 0;
                continue;
            }
            if (!sx_ts && !(b == 0xF7u && (i + 1u == n || !(p[i + 1u] & 0x80u)))) {
                sx_ts = 1;                        /* the timestamp (a bare F7 at the end or before data: the end) */
                continue;
            }
            sx_ts = 0;
            if (b == 0xF7u) {
                sx_byte(m, out, ctx, b);
                want_ts = 1;
                continue;
            }
            if (b >= 0xF8u) {
                emit(m, out, ctx, b, 0, 0);       /* real-time inside SysEx */
                continue;
            }
            m->in_sysex = 0;                      /* a new status: the SysEx never ended */
            m->sx_n = 0;
            m->errors++;
            want_ts = 0;                          /* (its timestamp was the byte before) */
        }
        if (b & 0x80u) {
            if (want_ts) {                        /* timestamp byte */
                want_ts = 0;
                continue;
            }
            if (b < 0xF8u && m->need && i + 1u < n && p[i + 1u] >= 0xF8u)
                continue;                         /* the timestamp of a real-time byte inside a message */
            if (b >= 0xF8u) {                     /* real-time: one byte, keeps running status */
                emit(m, out, ctx, b, 0, 0);
                want_ts = 1;
                continue;
            }
            if (b == 0xF0u) {
                m->in_sysex = 1;
                m->sx_n = 0;
                m->sx_len = 0;
                sx_byte(m, out, ctx, b);
                continue;
            }
            m->status = b;
            m->running = b < 0xF0u ? b : 0u;
            m->need = (uint8_t)(midi_len(b) - 1u);
            m->got = 0;
            if (!m->need) {
                emit(m, out, ctx, b, 0, 0);
                want_ts = 1;
            }
            continue;
        }
        if (!m->need) {                           /* data with no message open: running status */
            if (!m->running) {
                m->errors++;
                continue;
            }
            m->status = m->running;
            m->need = (uint8_t)(midi_len(m->status) - 1u);
            m->got = 0;
            want_ts = 0;
        }
        if (m->got == 0u)
            m->d0 = b;
        m->got++;
        if (m->got == m->need) {
            emit(m, out, ctx, m->status, m->d0, b);
            m->need = 0;
            want_ts = 1;
        }
    }
}

/* MIDI out: header 80|ts[12:7], then per message a timestamp 80|ts[6:0] and the whole message (no running status,
 * so every message stands alone); ts = milliseconds, 13 bits */
uint32_t ble_midi_pack(const uint8_t (*msg)[3], const uint8_t *len, uint32_t n, uint32_t ts_ms, uint8_t *out,
                       uint32_t max, uint32_t *taken)
{
    uint32_t i, k, o = 1;
    for (i = 0; i < n && o + 1u + len[i] <= max; i++) {
        out[o++] = (uint8_t)(0x80u | (ts_ms & 0x7Fu));
        for (k = 0; k < len[i]; k++)
            out[o++] = msg[i][k];
    }
    *taken = i;
    if (!i)
        return 0;
    out[0] = (uint8_t)(0x80u | (ts_ms >> 7 & 0x3Fu));
    return o;
}

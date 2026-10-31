/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio)
 * Copyright (C) 2026 Adrien Tibi (Recursive Field Instruments)
 * Ported from Melodee; source revisions and MPL permission: see NOTICE. */
/* USB host: what the host reads from an attached device, with no register access (usb_host.c uses it; the host
 * tests include it alone).
 *   uh_parse_midi   a configuration descriptor -> the first MIDI 1.0 streaming alternate setting with usable
 *                   IN and/or OUT endpoints (an output-only synth is usable too)
 *   uh_midi_ok      one USB-MIDI event packet -> 1 when it is a channel message or a clock / transport byte */
#pragma once
#include <stdint.h>

typedef struct {
    uint8_t found;               /* a usable endpoint of a MIDI streaming interface was found */
    uint8_t cfg;                 /* bConfigurationValue */
    uint8_t iface, alt;          /* its interface and alternate setting */
    uint8_t in_ep;               /* endpoint number (without the direction bit) */
    uint8_t in_type;             /* 2 bulk, 3 interrupt */
    uint8_t in_interval;         /* bInterval (interrupt only) */
    uint16_t in_mps;
    uint8_t out_ep;              /* the OUT endpoint of the same interface, 0 = none */
    uint8_t out_type, out_interval;
    uint16_t out_mps;
} uh_midi_t;

/* d / len: the configuration descriptor as read (possibly cut short: a descriptor that does not fit is ignored) */
static int uh_parse_midi(const uint8_t *d, uint32_t len, uh_midi_t *m)
{
    uint32_t i = 0;
    uint8_t cfg = 0, in_midi = 0;
    *m = (uh_midi_t){0};
    while (i + 2u <= len) {
        uint32_t l = d[i];
        if (l < 2u || i + l > len)
            break;
        if (d[i + 1] == 2u && l >= 9u) {
            cfg = d[i + 5];
        } else if (d[i + 1] == 4u && l >= 9u) {
            if (m->found) break;                        /* never combine interfaces or alternate settings */
            m->cfg = cfg;
            m->iface = d[i + 2];
            m->alt = d[i + 3];
            in_midi = d[i + 5] == 1u && d[i + 6] == 3u && d[i + 7] == 0u;
        } else if (d[i + 1] == 5u && l >= 7u && in_midi) {
            uint8_t ea = d[i + 2], type = d[i + 3] & 3u;
            uint16_t mps = (uint16_t)(d[i + 4] | d[i + 5] << 8);
            /* Full-speed USB-MIDI 1.0 events are four bytes. The buffers hold at most 64 bytes. */
            if ((ea & 15u) && !(ea & 0x70u) && mps >= 4u && mps <= 64u &&
                ((type == 2u && (mps == 8u || mps == 16u || mps == 32u || mps == 64u)) ||
                 (type == 3u && d[i + 6]))) {
                if ((ea & 0x80u) && !m->in_ep) {
                    m->found = 1;
                    m->in_ep = ea & 0x0Fu;
                    m->in_type = type;
                    m->in_interval = d[i + 6];
                    m->in_mps = mps;
                } else if (!(ea & 0x80u) && !m->out_ep) {
                    m->found = 1;
                    m->out_ep = ea & 0x0Fu;
                    m->out_type = type;
                    m->out_interval = d[i + 6];
                    m->out_mps = mps & ~3u;          /* send whole event packets only */
                }
            }
        }
        i += l;
    }
    return m->found;
}

/* A USB-MIDI event packet: cable / CIN, then the MIDI bytes. Channel messages (CIN 8..E whose status matches)
 * and the one-byte clock, start, continue and stop are passed on; SysEx and everything else is not: a
 * controller has no business with the SysEx commands (update, editor) that usb.c's own parser answers. The
 * cable number is dropped. Returns the packet to enqueue, or 0. */
static uint32_t uh_midi_ok(uint32_t pkt)
{
    uint32_t cin = pkt & 15u, st = (pkt >> 8) & 0xFFu;
    pkt &= ~0xF0u;
    if (cin == 0xFu)
        return st == 0xF8u || st == 0xFAu || st == 0xFBu || st == 0xFCu ? pkt : 0u;
    if (cin >= 8u && cin <= 0xEu && (st >> 4) == cin && !((pkt >> 16) & 0x80u) &&
        ((cin == 0xCu || cin == 0xDu) || !(pkt & 0x80000000u)))
        return pkt;
    return 0u;
}

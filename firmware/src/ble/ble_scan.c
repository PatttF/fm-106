/* SPDX-License-Identifier: GPL-3.0-only
 * BLE MIDI test: 2026 DEADACTIVE */
/* Scan results: one entry per address (advertising and scan response merge), up to BLE_SCAN_MAX; when full, a
 * stronger new device replaces the weakest. Entries are kept from scan to scan with the time of their last report and
 * age out (ble_scan_expire): the list is what has been around lately (the saved devices' AROUND, the page's ADD list). AD structures: [len][type][data len-1]; names 0x08 / 0x09; 128-bit
 * service lists 0x06 / 0x07 (little-endian UUIDs) for the BLE-MIDI tag. */
#include "ble_scan.h"

static const uint8_t MIDI_UUID_LE[16] = {0x00, 0xC7, 0xC4, 0x4E, 0xE3, 0x6C, 0x51, 0xA7,
                                         0x33, 0x4B, 0xE8, 0xED, 0x5A, 0x0E, 0xB8, 0x03};

static int same6(const uint8_t *a, const uint8_t *b)
{
    uint32_t i;
    for (i = 0; i < 6u; i++)
        if (a[i] != b[i])
            return 0;
    return 1;
}

void ble_scan_clear(ble_scan_t *s)
{
    uint8_t *p = (uint8_t *)s;
    uint32_t i;
    for (i = 0; i < sizeof *s; i++)
        p[i] = 0;
}

static void parse_ad(ble_dev_t *d, const uint8_t *ad, uint32_t n)
{
    if (!d || !ad || n == 0)
        return;
    uint32_t i = 0;
    while (i + 1u < n) {
        uint32_t len = ad[i], type, k;
        if (!len || i + 1u + len > n)
            return;                               /* malformed: stop, keep what was parsed */
        type = ad[i + 1u];
        if ((type == 0x08u || type == 0x09u) && (type == 0x09u || !d->name[0])) {
            uint32_t m = len - 1u < sizeof d->name - 1u ? len - 1u : sizeof d->name - 1u;
            for (k = 0; k < m; k++) {
                uint8_t c = ad[i + 2u + k];
                d->name[k] = (char)(c >= 0x20u && c < 0x7Fu ? c : '?');
            }
            d->name[m] = 0;
        } else if (type == 0x06u || type == 0x07u) {
            for (k = 0; k + 16u <= len - 1u; k += 16u) {
                uint32_t j, eq = 1;
                for (j = 0; j < 16u; j++)
                    eq &= ad[i + 2u + k + j] == MIDI_UUID_LE[j];
                if (eq)
                    d->midi = 1;
            }
        }
        i += 1u + len;
    }
}

void ble_scan_report(ble_scan_t *s, const uint8_t addr[6], uint8_t addr_type, int8_t rssi,
                     const uint8_t *ad, uint32_t ad_len, uint32_t now_ms)
{
    if (!s || !addr)
        return;
    ble_dev_t *d = 0, *weak = 0;
    uint32_t i;
    s->reports++;
    for (i = 0; i < BLE_SCAN_MAX; i++) {
        ble_dev_t *e = &s->dev[i];
        if (e->used && e->addr_type == addr_type && same6(e->addr, addr)) {
            d = e;
            break;
        }
    }
    if (!d) {
        for (i = 0; i < BLE_SCAN_MAX && !d; i++)
            if (!s->dev[i].used)
                d = &s->dev[i];
        if (!d) {
            for (i = 0; i < BLE_SCAN_MAX; i++)
                if (!weak || s->dev[i].rssi < weak->rssi)
                    weak = &s->dev[i];
            if (weak->rssi >= rssi)
                return;                           /* full and not stronger than the weakest */
            d = weak;
        }
        {
            uint8_t *p = (uint8_t *)d;
            for (i = 0; i < sizeof *d; i++)
                p[i] = 0;
        }
        for (i = 0; i < 6u; i++)
            d->addr[i] = addr[i];
        d->addr_type = addr_type;
        d->used = 1;
    }
    d->rssi = rssi;
    d->seen_ms = now_ms;
    parse_ad(d, ad, ad_len);
}

void ble_scan_expire(ble_scan_t *s, uint32_t now_ms, uint32_t max_age_ms)
{
    uint32_t i;
    for (i = 0; i < BLE_SCAN_MAX; i++)
        if (s->dev[i].used && now_ms - s->dev[i].seen_ms > max_age_ms)
            s->dev[i].used = 0;
}

uint32_t ble_scan_sorted(const ble_scan_t *s, uint8_t idx[BLE_SCAN_MAX])
{
    uint32_t n = 0, i, j;
    for (i = 0; i < BLE_SCAN_MAX; i++)
        if (s->dev[i].used)
            idx[n++] = (uint8_t)i;
    for (i = 1; i < n; i++)                       /* insertion sort, strongest first */
        for (j = i; j > 0u && s->dev[idx[j - 1u]].rssi < s->dev[idx[j]].rssi; j--) {
            uint8_t t = idx[j];
            idx[j] = idx[j - 1u];
            idx[j - 1u] = t;
        }
    return n;
}


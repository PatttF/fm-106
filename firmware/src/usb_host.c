/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio)
 * Copyright (C) 2026 Adrien Tibi (Recursive Field Instruments)
 * Ported from Melodee; source revisions and MPL permission: see NOTICE. */
/* USB MIDI host on USB0 (FELUCCA_USB_HOST), included by usb.c after the SIE helpers and ep1_take.
 *
 * The port is either the computer-facing device (usb.c) or, when usb_host_switch(1) is called, the host of a class
 * compliant USB-MIDI controller, alone or behind a hub. The initial state is always device mode, so
 * the update paths always work. Switching happens in the main loop (usb_host_task); usb.up is 0 while the port is a host.
 *
 *   main loop (usb_host_task)   waits for a device, resets the bus, and enumerates it with blocking EP0 transfers
 *                         (hub: powers the ports, resets those with something on them, and tries each device
 *                         until one has a USB-MIDI streaming interface). Failed direct probes wait for unplug;
 *                         idle hubs are polled for downstream connection changes. The ISR does nothing until uh.run.
 *   TIMER5 (uh_poll)      once uh.run is 1 the SIE is the ISR's alone: optional IN and OUT endpoints share host
 *                         EP1 in separate directions. IN is asked for a packet (ReqPkt), read when RxPktRdy, and its channel / clock events
 *                         go to the same ring as USB and TRS MIDI (source 1). A stall or a run of errors sets
 *                         uh.fault; the main loop then resets the bus and looks for a device again.
 *
 * Register sequences follow the AC79 SDK's USB0 host code (usb_h_sie_init, usb_h_chirp_and_reset, usb0_h_ep_read,
 * usb_ctlXfer): EP0 data moves through ep0buf / EP0_CNT as in the device driver, the target is FADDR (USB0 has no
 * per-endpoint function address), RXMAXP is 0xFF as everywhere in this firmware. Not driven: VBUS (the port
 * needs outside power, e.g. a powered hub), low-speed devices. OUT drains the existing MIDI output queue only
 * after the target ACKs; no SysEx/editor traffic is sent to attached devices.
 * Host OUT has SIE-model coverage; physical validation of this port is pending: see docs/USB_HOST.md. */
#if FELUCCA_USB_HOST
#include "usb_host_desc.h"

enum { S_DEVCTL = 15, S_TXTYPE = 24, S_TXINTERVAL = 25, S_RXTYPE = 26, S_RXINTERVAL = 27 };
enum { UH_MSG_NONE, UH_MSG_ON, UH_MSG_OFF, UH_MSG_MIDI, UH_MSG_NOMIDI, UH_MSG_LOW, UH_MSG_LOST };
#define UH_FIRST_ADDR 1u
#define UH_MAX_ERRORS 16u

static uint8_t uh_rx[64 + 4] __attribute__((aligned(4)));
static uint8_t uh_cfg[320];                                  /* a configuration, cut short if longer */
static struct {
    uint8_t want;                                       /* requested mode (main loop) */
    uint8_t on;                                         /* the port is a host */
    volatile uint8_t run;                               /* TIMER5 owns the active MIDI endpoints */
    volatile uint8_t fault;                             /* TIMER5 -> main loop: reset the bus and look again */
    uint8_t seen, next_addr, dev, tried;
    uint8_t hub_addr, hub_mps, hub_ports, hub_poll_port;
    uint8_t in_ep, out_mps, tx_count, tx_errors, line_idle;
    uint8_t msg;                                        /* one UH_MSG_* for the application */
    volatile uint8_t errors;
    uint32_t t_ms, retry_ms;
    uint32_t tx_ms, hub_poll_ms;
    volatile uint32_t pkts, events;
} uh;

/* Main-loop request only. usb_host_task performs the actual mode transition. */
static void usb_host_switch(uint32_t on) { if (!usb.detached) uh.want = on != 0; }
static uint32_t usb_host_active(void) { return uh.on; }
static uint32_t usb_host_connected(void) { return uh.run; }
static uint32_t usb_host_message(void)
{
    uint32_t msg = uh.msg;
    uh.msg = UH_MSG_NONE;
    return msg;
}

/* ------------------------------------------------ main loop: control --- */
static void uh_wait(uint32_t ms)                        /* main loop only */
{
    uint32_t t = fm1_ms;
    while (fm1_ms - t < ms)
        fm1_wdt_feed();
}

/* CSR0 (host) until want_set bits are all set and want_clr are all clear: >= 0 the CSR0, < 0 a stall / error / timeout */
static int uh_csr0(uint32_t want_set, uint32_t want_clr)
{
    uint32_t t = fm1_ms, v;
    for (;;) {
        v = sie_rd(S_CSR0);
        if (v & 0x04u) {                                /* RxStall */
            sie_wr(S_CSR0, 0);
            return -2;
        }
        if (v & 0x10u) {                                /* Error: no answer */
            sie_wr(S_CSR0, 0);
            return -1;
        }
        if ((v & want_set) == want_set && !(v & want_clr))
            return (int)v;
        if (fm1_ms - t > 500u)
            return -3;
        fm1_wdt_feed();
    }
}

/* One control transfer to `addr`: SETUP, the IN data stage if any, the status stage. Bytes read, or < 0.
 * Only requests without an OUT data stage (every one used here). */
static int uh_ctl(uint8_t addr, uint8_t rt, uint8_t rq, uint16_t val, uint16_t idx, uint16_t len, uint8_t *out,
                  uint8_t mps)
{
    uint32_t got = 0, n, i;
    int r;
    sie_wr(S_FADDR, addr);
    sie_wr(S_INDEX, 0);
    ep0buf[0] = rt;
    ep0buf[1] = rq;
    ep0buf[2] = (uint8_t)val;
    ep0buf[3] = (uint8_t)(val >> 8);
    ep0buf[4] = (uint8_t)idx;
    ep0buf[5] = (uint8_t)(idx >> 8);
    ep0buf[6] = (uint8_t)len;
    ep0buf[7] = (uint8_t)(len >> 8);
    fm1_usb_ep0_send(ep0buf, 8);
    sie_wr(S_CSR0, 0x0Au);                              /* SetupPkt | TxPktRdy */
    r = uh_csr0(0, 0x02u);
    if (r < 0)
        return r;
    if (len && (rt & 0x80u)) {
        while (got < len) {
            sie_wr(S_CSR0, 0x20u);                      /* ReqPkt */
            r = uh_csr0(0x01u, 0);
            if (r < 0)
                return r;
            n = sie_rd(S_COUNT0);
            if (n > 64u)
                n = 64u;
            fm1_usb_rx_sync();
            for (i = 0; i < n && got + i < len; i++)
                out[got + i] = ep0buf[i];
            sie_wr(S_CSR0, 0);                          /* RxPktRdy done */
            got += n;
            if (n < mps)
                break;
        }
        if (got > len)
            got = len;
        sie_wr(S_CSR0, 0x42u);                          /* status: StatusPkt | TxPktRdy (zero-length OUT) */
        r = uh_csr0(0, 0x02u);
    } else {
        sie_wr(S_CSR0, 0x60u);                          /* status: StatusPkt | ReqPkt (zero-length IN) */
        r = uh_csr0(0x01u, 0);
        if (r >= 0)
            sie_wr(S_CSR0, 0);
    }
    return r < 0 ? r : (int)got;
}

/* ---------------------------------------------------- enumeration --- */
static int uh_hub_probe(uint8_t addr, uint8_t mps, uint8_t cfgval, uh_midi_t *m);

/* The device at address 0 (just reset): give it an address, read its configuration, and if it has a USB-MIDI
 * streaming interface select it. 1 = found, 0 = no (including a hub below depth 0 and a device that fails). */
static int uh_probe(uint8_t depth, uh_midi_t *m)
{
    uint8_t d8[8], mps, cls, addr = uh.next_addr++;
    uint32_t total;
    int r;
    r = uh_ctl(0, 0x80, 6, 0x0100, 0, 8, d8, 8);        /* GET_DESCRIPTOR(device), first 8 bytes */
    if (r < 8)
        return 0;
    mps = d8[7] == 16u || d8[7] == 32u || d8[7] == 64u ? d8[7] : 8u;
    cls = d8[4];
    if (uh_ctl(0, 0x00, 5, addr, 0, 0, 0, mps) < 0)     /* SET_ADDRESS */
        return 0;
    uh_wait(10);                                        /* the device settles to its new address */
    r = uh_ctl(addr, 0x80, 6, 0x0200, 0, 9, uh_cfg, mps);
    if (r < 9)
        return 0;
    total = (uint32_t)(uh_cfg[2] | uh_cfg[3] << 8);
    if (total > sizeof uh_cfg)
        total = sizeof uh_cfg;
    if (total > 9u) {
        r = uh_ctl(addr, 0x80, 6, 0x0200, 0, (uint16_t)total, uh_cfg, mps);
        if (r < 9)
            return 0;
        total = (uint32_t)r;
    }
    if (cls == 9u)                                      /* a hub */
        return depth == 0u ? uh_hub_probe(addr, mps, uh_cfg[5], m) : 0;
    if (!uh_parse_midi(uh_cfg, total, m))
        return 0;
    if (uh_ctl(addr, 0x00, 9, m->cfg, 0, 0, 0, mps) < 0)   /* SET_CONFIGURATION */
        return 0;
    if (m->alt && uh_ctl(addr, 0x01, 11, m->alt, m->iface, 0, 0, mps) < 0)   /* SET_INTERFACE */
        return 0;
    uh.dev = addr;
    return 1;
}

/* Probe only this downstream port; keep the hub and the other devices configured. Each port reuses its
 * address on reconnect, so repeated hot-plugs cannot exhaust the USB address space. */
static int uh_hub_port_probe(uint8_t p, uint32_t st, uh_midi_t *m)
{
    uint8_t b[4], addr = uh.hub_addr, mps = uh.hub_mps;
    uint32_t t;
    if (!(st & 0x0001u) || (st & 0x0200u))              /* nothing there, or low speed */
        return 0;
    if (uh_ctl(addr, 0x23, 3, 4, p, 0, 0, mps) < 0)    /* SET_FEATURE(PORT_RESET) */
        return 0;
    t = fm1_ms;
    for (;;) {
        if (uh_ctl(addr, 0xA3, 0, 0, p, 4, b, mps) >= 4 && (b[2] & 0x10u))   /* C_PORT_RESET */
            break;
        if (fm1_ms - t > 500u)
            return 0;
        uh_wait(10);
    }
    uh_ctl(addr, 0x23, 1, 20, p, 0, 0, mps);            /* CLEAR_FEATURE(C_PORT_RESET) */
    if (uh_ctl(addr, 0xA3, 0, 0, p, 4, b, mps) < 4 || !(b[0] & 0x02u))   /* enabled? */
        return 0;
    uh_wait(20);                                        /* reset recovery */
    uh.next_addr = (uint8_t)(UH_FIRST_ADDR + p);
    return uh_probe(1, m);
}

/* Read and acknowledge connection changes, including disconnects and unsupported devices. An unresponsive
 * hub is left alone until root unplug/replug, rather than repeating a blocking control timeout forever. */
static int uh_hub_status(uint8_t p, uint8_t *b)
{
    if (uh_ctl(uh.hub_addr, 0xA3, 0, 0, p, 4, b, uh.hub_mps) < 4 ||
        ((b[2] & 1u) && uh_ctl(uh.hub_addr, 0x23, 1, 16, p, 0, 0, uh.hub_mps) < 0)) {
        uh.hub_addr = 0;
        return 0;
    }
    return 1;
}

/* Configure and power the root hub once. After the initial scan, idle polling uses connection-change bits. */
static int uh_hub_probe(uint8_t addr, uint8_t mps, uint8_t cfgval, uh_midi_t *m)
{
    uint8_t b[16], ports, p;                            /* (the hub descriptor is 9 bytes with <= 7 ports) */
    uint32_t good;
    if (uh_ctl(addr, 0x00, 9, cfgval, 0, 0, 0, mps) < 0)
        return 0;
    if (uh_ctl(addr, 0xA0, 6, 0x2900, 0, 9, b, mps) < 7)   /* hub descriptor */
        return 0;
    ports = b[2] > 7u ? 7u : b[2];
    good = (uint32_t)b[5] * 2u;                         /* bPwrOn2PwrGood, in 2 ms */
    for (p = 1; p <= ports; p++)
        uh_ctl(addr, 0x23, 3, 8, p, 0, 0, mps);         /* SET_FEATURE(PORT_POWER) */
    uh_wait((good < 100u ? 100u : good) + 100u);        /* power good, then the devices announce themselves */
    uh.hub_addr = addr;
    uh.hub_mps = mps;
    uh.hub_ports = ports;
    uh.hub_poll_port = 1;
    uh.hub_poll_ms = fm1_ms;
    for (p = 1; p <= ports; p++) {
        if (!uh_hub_status(p, b))
            return 0;
        if (uh_hub_port_probe(p, (uint32_t)(b[0] | b[1] << 8), m))
            return 1;
    }
    return 0;
}

/* Host EP1 has independent RX/TX DMA slots. Both target endpoints belong to this device and alternate setting. */
static void uh_start_ep(const uh_midi_t *m)
{
    sie_wr(S_FADDR, uh.dev);
    sie_wr(S_INDEX, 1);
    uh.in_ep = m->in_ep;
    uh.out_mps = (uint8_t)m->out_mps;
    uh.tx_count = uh.tx_errors = 0;
    uh.line_idle = 0;
    if (m->in_ep) {
        fm1_usb_ep_rxbuf(1, uh_rx);
        sie_wr(S_RXMAXP, 0xFF);
        sie_wr(S_RXCSR1, 0x90);                         /* clear data toggle, flush */
        sie_wr(S_RXCSR2, 0);
        sie_wr(S_RXTYPE, (uint32_t)(m->in_type << 4 | m->in_ep));
        sie_wr(S_RXINTERVAL, m->in_type == 3u ? m->in_interval : 0u);
        sie_wr(S_RXCSR1, 0x20u);                        /* ReqPkt */
    }
    if (m->out_ep) {
        fm1_usb_ep_txbuf(1, ep1tx);
        sie_wr(S_TXMAXP, 0xFF);                         /* AC79 USB0's DMA maximum, as in the device driver */
        sie_wr(S_TXCSR1, 0x48);                         /* ClrDataTog | FlushFIFO, only at endpoint start */
        sie_wr(S_TXCSR2, 0x20);                         /* MODE: TX half of a bidirectional endpoint */
        sie_wr(S_TXTYPE, (uint32_t)(m->out_type << 4 | m->out_ep));
        sie_wr(S_TXINTERVAL, m->out_type == 3u ? m->out_interval : 0u);
    }
    fm1_usb_ep_enable(1u << 1);
    uh.errors = 0;
    uh.fault = 0;
    midi_links++;
    RING_PUBLISH();
    uh.run = 1;
}

/* Stop admitting output before discarding queued notes. The app consumes midi_usb_lost to release USB
 * input owners and arrange output cleanup, including a packet with an uncertain ACK. */
static void uh_session_end(void)
{
    uh.run = 0;
    uh.out_mps = 0;
    uh.tx_count = 0;
    mo_r = mo_w;
    RING_PUBLISH();
    midi_usb_lost = 1;
}

/* the controller's port as a host with nothing attached yet */
static void uh_host_up(void)
{
    usb.timeouts = 0;
    fm1_usb_reset();
    uh_wait(25);
    fm1_usb_host_attach(ep0buf);
    uh_wait(25);
    sie_wr(S_INTRUSBE, 0);
    sie_wr(S_INTRTX1E, 0);
    sie_wr(S_INTRTX2E, 0);
    sie_wr(S_INTRRX1E, 0);
    sie_wr(S_INTRRX2E, 0);
    fm1_usb_ep_enable(0x1Fu);
    sie_wr(S_FADDR, 0);
    sie_wr(S_DEVCTL, 0x03u);                            /* session + host request */
    uh.seen = 0;
    uh.line_idle = 0;
    uh.hub_addr = 0;
}

/* a device is on the line: reset the bus and find the MIDI endpoint */
static void uh_connect(void)
{
    uh_midi_t m;
    if (sie_rd(S_DEVCTL) & 0x20u) {                     /* low speed (FSDev is bit 6, LSDev bit 5) */
        if (!uh.tried)
            uh.msg = UH_MSG_LOW;                        /* said once per attach, as NO MIDI is */
        uh.tried = 1;
        return;
    }
    sie_wr(S_POWER, 0x08u);                             /* bus reset */
    uh_wait(30);
    sie_wr(S_POWER, 0);
    uh_wait(30);
    uh.next_addr = UH_FIRST_ADDR;
    if (uh_probe(0, &m)) {
        uh_start_ep(&m);
        uh.msg = UH_MSG_MIDI;
        uh.tried = 0;
    } else {
        if (!uh.tried)
            uh.msg = UH_MSG_NOMIDI;                     /* said once per attach */
        uh.tried = 1;
    }
}

/* Main loop. usb_host_switch requests the mode; this finds and keeps a controller. */
static void usb_host_task(uint32_t now)
{
    if (usb.detached)
        return;
    if (uh.want != uh.on) {
        usb.up = 0;                                     /* TIMER5 leaves the SIE to this context */
        RING_PUBLISH();
        usb.config = 0;
        usb.suspended = 0;
        uh_session_end();
        uh.fault = 0;
        uh.tried = 0;
        if (uh.want) {
#if FELUCCA_UAC
            uac_stream(0);
#endif
#if FELUCCA_UAC_OUT
            uo_stream(0);
#endif
#if FELUCCA_CDC
            cdc.dtr = cdc.e0_rx = cdc.rx_pend = 0;
#endif
#if FELUCCA_OTA
            so_r = so_w;
            sx_collect = sx_ready = sx_busy = 0;
#endif
#if FELUCCA_MIRROR
            mr_r = mr_w;
            tx_mid = 0;
#endif
            usb.sx_on = 0;
            fm1_usb_off();
            uh.on = 1;
            RING_PUBLISH();
            uh_host_up();
            uh.retry_ms = now;
            uh.msg = UH_MSG_ON;
        } else {
            uh.on = 0;
            RING_PUBLISH();
            fm1_usb_off();
            usb_retry_ms = now - 1000u;                 /* usb_retry makes the port a device again at once */
            uh.msg = UH_MSG_OFF;
        }
        return;
    }
    if (!uh.on)
        return;
    if (uh.fault) {                                     /* stall, or the controller stopped answering (TIMER5 cleared run) */
        uh_session_end();
        uh.fault = 0;
        uh_host_up();
        uh.retry_ms = now + 500u;
        uh.msg = UH_MSG_LOST;
        return;
    }
    if (uh.run)
        return;
    if ((int32_t)(now - uh.t_ms) < 20 || (int32_t)(now - uh.retry_ms) < 0)
        return;
    uh.t_ms = now;
    if (!fm1_usb_line()) {
        uh.seen = 0;
        if (uh.line_idle < 5u) uh.line_idle++;
        if (uh.line_idle >= 5u) {                       /* debounce root disconnect while idle too */
            uh.tried = 0;
            uh.hub_addr = 0;
        }
        return;
    }
    uh.line_idle = 0;
    if (uh.tried) {
        if (uh.hub_addr && uh.hub_ports && (uint32_t)(now - uh.hub_poll_ms) >= 100u) {
            uint8_t b[4], p = uh.hub_poll_port;
            uh_midi_t m;
            uh.hub_poll_ms = now;
            uh.hub_poll_port = p == uh.hub_ports ? 1u : (uint8_t)(p + 1u);
            if (uh_hub_status(p, b) && (b[2] & 1u) && (b[0] & 1u) && !(b[1] & 2u)) {
                uh_wait(100);                          /* newly attached device settles before port reset */
                if (uh_hub_status(p, b) && uh_hub_port_probe(p, (uint32_t)(b[0] | b[1] << 8), &m)) {
                    uh_start_ep(&m);
                    uh.msg = UH_MSG_MIDI;
                    uh.tried = 0;
                }
            }
        }
        return;                                        /* direct failures wait for root unplug/replug */
    }
    if (uh.seen < 5u) {                                 /* 100 ms of a steady line before the bus reset */
        uh.seen++;
        return;
    }
    uh.seen = 0;
    uh_connect();
}

/* ------------------------------------------------------ TIMER5 ISR --- */
static void uh_fail(void)
{
    uh.run = 0;
    uh.fault = 1;
}

/* Retain ring slots and the DMA buffer until ACK. A NAK or transient error retries the same data toggle;
 * a stall, repeated errors or 500 ms without completion hands recovery to the main loop. Never wait here. */
static void uh_tx_poll(void)
{
    uint32_t csr = sie_rd(S_TXCSR1), n = 0;
    if (uh.tx_count) {
        if ((csr & 0x20u) || fm1_ms - uh.tx_ms >= 500u) {
            uh_fail();
            return;
        }
        if (csr & 0x84u) {                              /* Error / NAK timeout */
            if ((csr & 4u) && ++uh.tx_errors >= UH_MAX_ERRORS) {
                uh_fail();
                return;
            }
            sie_wr(S_TXCSR1, 0x01u);                    /* retry retained FIFO, preserving data toggle */
            return;
        }
        if (csr & 0x03u) return;                        /* TxPktRdy / FIFONotEmpty: no ACK yet */
        RING_PUBLISH();
        mo_r += uh.tx_count;
        uh.tx_count = uh.tx_errors = 0;
    }
    while (mo_r + n / 4u != mo_w && n + 4u <= uh.out_mps) {
        uint32_t p = midi_out_q[(mo_r + n / 4u) % MQ];
        ep1tx[n] = (uint8_t)p;
        ep1tx[n + 1] = (uint8_t)(p >> 8);
        ep1tx[n + 2] = (uint8_t)(p >> 16);
        ep1tx[n + 3] = (uint8_t)(p >> 24);
        n += 4u;
    }
    if (!n) return;
    uh.tx_count = (uint8_t)(n / 4u);
    uh.tx_ms = fm1_ms;
    fm1_usb_ep_send(1, ep1tx, n);
    sie_wr(S_TXCSR1, 0x01u);
}

static void uh_poll(void)
{
    uint32_t csr, n, i;
    if (!fm1_usb_line()) {
        /* EOP also drives SE0 briefly. Require 8 ms of idle samples, not one sample during a packet. */
        if (++uh.line_idle >= 16u) uh_fail();
        return;
    }
    uh.line_idle = 0;
    sie_wr(S_INDEX, 1);
    if (uh.out_mps) uh_tx_poll();
    if (!uh.run || !uh.in_ep) return;
    csr = sie_rd(S_RXCSR1);
    if (csr & 0x40u) {                                  /* RxStall: the endpoint is gone */
        uh_fail();
        return;
    }
    if (csr & 0x0Cu) {                                  /* Error / DataError: no usable answer */
        if (++uh.errors >= UH_MAX_ERRORS) {
            uh_fail();
            return;
        }
        sie_wr(S_RXCSR1, 0x20u);                        /* the flags clear (written 0) and the request goes again; the
                                                         * data toggle stays: it is reset when the endpoint starts only */
        return;
    }
    if (csr & 0x01u) {                                  /* a packet is in uh_rx */
        n = sie_rd(S_RXCOUNT1) | (sie_rd(S_RXCOUNT2) << 8);
        if (n > 64u)
            n = 64u;
        fm1_usb_rx_sync();
#ifndef FELUCCA_LOADER
        if (MQ - (mi_w - mi_r) < EP1_ROOM)
            return;                                     /* ring too full: the packet stays, retried next poll */
#endif
        for (i = 0; i + 3u < n; i += 4u) {
            uint32_t p = uh_midi_ok((uint32_t)uh_rx[i] | (uint32_t)uh_rx[i + 1] << 8 |
                                    (uint32_t)uh_rx[i + 2] << 16 | (uint32_t)uh_rx[i + 3] << 24);
            if (p) {
                midi_enqueue(p, 1u);
                uh.events++;
            }
        }
        uh.pkts++;
        uh.errors = 0;
        sie_wr(S_RXCSR1, 0x10u);                        /* release the packet */
        sie_wr(S_RXCSR1, 0x20u);                        /* and ask for the next */
        return;
    }
    if (!(csr & 0x20u))                                 /* nothing pending: ask */
        sie_wr(S_RXCSR1, 0x20u);
}
#endif /* FELUCCA_USB_HOST */

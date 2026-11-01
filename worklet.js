// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
// FM-106 in an AudioWorklet: fm106.wasm is the whole FM-106 firmware against a simulated FM-1
// (web/emu/fm106_web.c). Each render quantum asks it for 128 frames, which runs the device clock forward;
// between quanta the page's input goes in and, about 30 times a second (4 when hidden), the screen,
// the lights and any changed flash sectors go out. After X0X / Felucca web/emu/worklet.js (GPL-3.0).

export const STORE_AREAS = [[0x97000, 0xE0000], [0xE5000, 0xE8000], [0xFC000, 0x100000]];
export function storeSectors(nor) {
  const out = {};
  for (const [a, b] of STORE_AREAS)
    for (let off = a; off < b; off += 4096) {
      const s = nor.subarray(off, off + 4096);
      if (s.some((v) => v !== 0xFF)) out[off] = s.slice();
    }
  return out;
}
export function restoreSectors(nor, sectors) {
  for (const [k, v] of Object.entries(sectors || {})) {
    const off = +k, b = v instanceof Uint8Array ? v : new Uint8Array(v);
    if (STORE_AREAS.some(([a, e]) => off >= a && off + 4096 <= e && off % 4096 === 0) && b.length === 4096)
      nor.set(b, off);
  }
}

export const HOLD_MS = 24;

export const KNOB_CC = 20, MASTER_CC = 27, KNOB_MODES = ["abs", "bin", "twos", "sign"];
export function midiKnob(last, cc, value, mode = "abs") {
  if (cc === MASTER_CC) return { master: Math.round(value * 1023 / 127) };
  const role = cc - KNOB_CC;
  if (role < 0 || role >= 7) return null;
  if (mode === "bin") return { role, n: value - 64 };
  if (mode === "twos") return { role, n: value < 64 ? value : value - 128 };
  if (mode === "sign") return { role, n: value < 64 ? value : 64 - value };
  const was = last[role];
  last[role] = value;
  return { role, n: was === undefined ? 0 : value - was };
}

if (typeof registerProcessor === "function") {
  const clock = globalThis.performance ? () => globalThis.performance.now() : () => Date.now();
  class FM106Processor extends AudioWorkletProcessor {
    constructor() {
      super();
      this.ex = null;
      this.sentDraws = -1;
      this.sentWrites = -1;
      this.sentLeds = "";
      this.lastFrame = 0;
      this.period = 1 / 30;
      this.busy = 0;
      this.frames = 0;
      this.queue = [];
      this.nextOk = 0;
      this.port.onmessage = (e) => this.onMessage(e.data);
    }

    async onMessage(m) {
      if (m.type === "load") {
        try {
          const { instance } = await WebAssembly.instantiate(m.wasm, {});
          const ex = instance.exports;
          if (ex._initialize) ex._initialize();
          ex.web_nor_erase();
          restoreSectors(new Uint8Array(ex.memory.buffer, ex.web_nor(), ex.web_nor_size()), m.sectors);
          ex.web_master(m.master ?? 700);
          ex.web_boot();
          this.ex = ex;
          this.sentWrites = ex.web_flash_writes_count();
          this.port.postMessage({ type: "ready", rate: ex.web_sample_rate() });
        } catch (err) {
          this.port.postMessage({ type: "error", message: String(err && err.message || err) });
        }
        return;
      }
      const ex = this.ex;
      if (!ex) return;
      if (m.type === "buttons" || m.type === "keys") this.queue.push(m);
      else if (m.type === "enc") ex.web_enc(m.role, m.n | 0);
      else if (m.type === "master") ex.web_master(m.value | 0);
      else if (m.type === "midi") ex.web_midi(m.data[0] | 0, m.data[1] | 0, m.data[2] | 0);
      else if (m.type === "visible") this.period = m.on ? 1 / 30 : 1 / 4;
    }

    publish() {
      const ex = this.ex, mem = ex.memory.buffer;
      const msg = { type: "frame" };
      const leds = [
        ex.web_lit_buttons(), ex.web_lit_keys(),
        ex.web_dim_buttons(), ex.web_dim_keys(),
        ex.web_dim_level(),
        ex.web_breath_buttons(), ex.web_breath_keys(),
        ex.web_mid_keys ? ex.web_mid_keys() : 0
      ];
      const an = ex.web_anim_levels ? ex.web_anim_levels() : 0;
      const anim = an ? Array.from(new Uint8Array(mem, an, 41)) : null;
      const key = leds.join(",") + (anim ? ";" + anim.join(",") : "");
      if (key !== this.sentLeds) {
        this.sentLeds = key;
        msg.leds = leds;
        msg.anim = anim;
      }
      if (this.frames >= 44100) {
        msg.load = this.busy / (this.frames / 44.1);
        this.busy = this.frames = 0;
      }
      const transfer = [];
      const draws = ex.web_screen_draws();
      if (draws !== this.sentDraws) {
        this.sentDraws = draws;
        msg.fb = new Uint16Array(mem, ex.web_screen(), 240 * 240).slice();
        transfer.push(msg.fb.buffer);
      }
      const writes = ex.web_flash_writes_count();
      if (writes !== this.sentWrites) {
        this.sentWrites = writes;
        msg.sectors = storeSectors(new Uint8Array(mem, ex.web_nor(), ex.web_nor_size()));
      }
      if (msg.leds || msg.fb || msg.sectors || msg.load !== undefined) this.port.postMessage(msg, transfer);
    }

    input() {
      const ex = this.ex, now = ex.web_now_ms();
      while (this.queue.length && now - this.nextOk >= 0) {
        const m = this.queue.shift();
        if (m.type === "buttons") ex.web_buttons(m.mask >>> 0);
        else ex.web_keys(m.mask >>> 0);
        this.nextOk = now + HOLD_MS;
      }
    }

    process(inputs, outputs) {
      const ex = this.ex;
      if (!ex) return true;
      if (this.queue.length) this.input();
      const out = outputs[0], n = out[0].length;
      const t0 = clock();
      ex.web_render(n);
      this.busy += clock() - t0;
      this.frames += n;
      out[0].set(new Float32Array(ex.memory.buffer, ex.web_out_l(), n));
      if (out[1]) out[1].set(new Float32Array(ex.memory.buffer, ex.web_out_r(), n));
      if (currentTime - this.lastFrame >= this.period) {
        this.lastFrame = currentTime;
        this.publish();
      }
      return true;
    }
  }

  registerProcessor("fm106", FM106Processor);
  registerProcessor("felucca", FM106Processor);
}

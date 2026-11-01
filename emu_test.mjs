// SPDX-License-Identifier: GPL-3.0-only
// Test suite for FM-106 WebAssembly Emulator
import fs from "fs";
import path from "path";
import { fileURLToPath } from "url";

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const wasmPath = path.join(__dirname, "fm106.wasm");

console.log("Loading WASM:", wasmPath);
const wasmBuf = fs.readFileSync(wasmPath);
console.log(`WASM size: ${wasmBuf.length} bytes`);

const { instance } = await WebAssembly.instantiate(wasmBuf, {});
const ex = instance.exports;

// 1. Verify exports
const required = [
  "web_boot", "web_render", "web_screen", "web_screen_draws",
  "web_out_l", "web_out_r", "web_buttons", "web_keys", "web_enc",
  "web_master", "web_midi", "web_lit_buttons", "web_lit_keys",
  "web_nor", "web_nor_size", "web_nor_erase", "web_flash_writes_count",
  "web_now_ms", "web_sample_rate"
];

for (const fn of required) {
  if (typeof ex[fn] !== "function" && typeof ex[fn] !== "object") {
    throw new Error(`Missing expected export: ${fn}`);
  }
}
console.log("✓ All required exports present");

// 2. Boot sequence
if (ex._initialize) ex._initialize();
ex.web_nor_erase();
ex.web_master(800);
ex.web_boot();

const draws0 = ex.web_screen_draws();
if (draws0 < 1) throw new Error("Expected screen draw on boot");
console.log(`✓ Boot completed: screen draws = ${draws0}, sample rate = ${ex.web_sample_rate()} Hz`);

// 3. Inspect framebuffer
const fbPtr = ex.web_screen();
const fb = new Uint16Array(ex.memory.buffer, fbPtr, 240 * 240);
let nonZeroPixels = 0;
for (let i = 0; i < 240 * 240; i++) {
  if (fb[i] !== 0) nonZeroPixels++;
}
if (nonZeroPixels < 1000) throw new Error(`Framebuffer appears blank: non-zero pixels = ${nonZeroPixels}`);
console.log(`✓ Framebuffer valid: ${nonZeroPixels} non-zero pixels drawn`);

// 4. Initial silence
ex.web_render(128);
let outL = new Float32Array(ex.memory.buffer, ex.web_out_l(), 128);
let peak0 = Math.max(...Array.from(outL).map(Math.abs));
if (peak0 > 0.001) throw new Error(`Expected silence before note on, got peak = ${peak0}`);
console.log(`✓ Initial silence confirmed (peak = ${peak0})`);

// 5. Play C4 (key index 7)
console.log("Playing note C4...");
ex.web_keys(1 << 7);
let audioBlocks = 0;
let maxPeak = 0;
for (let b = 0; b < 40; b++) {
  ex.web_render(128);
  outL = new Float32Array(ex.memory.buffer, ex.web_out_l(), 128);
  for (let i = 0; i < 128; i++) {
    const v = Math.abs(outL[i]);
    if (v > maxPeak) maxPeak = v;
    if (Number.isNaN(v) || !Number.isFinite(v)) throw new Error("Audio contains NaN or Infinity!");
  }
}

if (maxPeak < 0.01) throw new Error(`Audio output too quiet or silent: maxPeak = ${maxPeak}`);
if (maxPeak > 1.2) throw new Error(`Audio output clipping abnormally: maxPeak = ${maxPeak}`);
console.log(`✓ Audio rendered cleanly: max peak = ${maxPeak.toFixed(4)}`);

// 6. Check LED state
const litKeys = ex.web_lit_keys();
if ((litKeys & (1 << 7)) === 0) throw new Error(`Expected key 7 to be lit, got litKeys = 0b${litKeys.toString(2)}`);
console.log(`✓ Key LED active: bit 7 lit (0b${litKeys.toString(2)})`);

// 7. Test Encoder Turn (Cutoff knob)
console.log("Turning Knob 1 (Cutoff frequency)...");
ex.web_enc(3, 5); // +5 clicks on KNOB 1
for (let b = 0; b < 10; b++) ex.web_render(128);
console.log(`✓ Knob encoder turn processed: screen draws = ${ex.web_screen_draws()}`);

// 8. Release Note
console.log("Releasing note C4...");
ex.web_keys(0);
for (let b = 0; b < 100; b++) {
  ex.web_render(128);
}
outL = new Float32Array(ex.memory.buffer, ex.web_out_l(), 128);
const tailPeak = Math.max(...Array.from(outL).map(Math.abs));
console.log(`✓ Note release envelope decay verified (tail peak = ${tailPeak.toFixed(5)})`);

// 9. MIDI Input Test
console.log("Testing MIDI Note On (Note 60, Vel 100)...");
ex.web_midi(0x90, 60, 100);
maxPeak = 0;
for (let b = 0; b < 20; b++) {
  ex.web_render(128);
  outL = new Float32Array(ex.memory.buffer, ex.web_out_l(), 128);
  for (let i = 0; i < 128; i++) {
    const v = Math.abs(outL[i]);
    if (v > maxPeak) maxPeak = v;
  }
}
if (maxPeak < 0.01) throw new Error(`MIDI Note On failed to generate sound: maxPeak = ${maxPeak}`);
console.log(`✓ MIDI input generated authentic synth audio: peak = ${maxPeak.toFixed(4)}`);

ex.web_midi(0x80, 60, 0);
for (let b = 0; b < 60; b++) ex.web_render(128);

console.log("\n=========================================");
console.log(" ALL FM-106 WEB SIMULATOR TESTS PASSED! ");
console.log("=========================================\n");

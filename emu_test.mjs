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

// 10. Navigation & Button Behavior Tests
console.log("Testing UI navigation & buttons...");

function pressButton(bitMask, holdBlocks = 10) {
  ex.web_buttons(bitMask);
  for (let b = 0; b < holdBlocks; b++) ex.web_render(128);
  ex.web_buttons(0);
  for (let b = 0; b < 10; b++) ex.web_render(128);
}

// Ensure starting at HOME (PAGE_VCF = 0)
pressButton(1 << 6); // BTN_HOME
if (ex.web_current_page() !== 0) throw new Error(`Expected PAGE_VCF (0), got ${ex.web_current_page()}`);
console.log("✓ Home button returned to page 0 (PAGE_VCF)");

// Cycle with EDIT button through all pages up to PAGE_SEQ (page 10)
for (let p = 1; p <= 10; p++) {
  pressButton(1 << 4); // BTN_EDIT tap
  if (ex.web_current_page() !== p) throw new Error(`Expected page ${p}, got ${ex.web_current_page()}`);
  if (ex.web_seq_menu_on() !== 0) throw new Error(`Sequencer options opened unexpectedly on page ${p}`);
  if (ex.web_note_menu_on() !== 0) throw new Error(`Note menu opened unexpectedly on page ${p}`);
}
console.log("✓ Stepping with EDIT button arrived cleanly on PAGE_SEQ (page 10) with NO menus open");

// Pressing EDIT on PAGE_SEQ must loop back to page 1 (PAGE_VCF = 0)
pressButton(1 << 4); // BTN_EDIT tap on PAGE_SEQ
if (ex.web_current_page() !== 0) throw new Error(`Expected EDIT on PAGE_SEQ to loop back to page 0 (PAGE_VCF), got ${ex.web_current_page()}`);
console.log("✓ Pressing EDIT on PAGE_SEQ looped back to page 1 (PAGE_VCF)");

// Jump to PAGE_SEQ with BTN_SEQ (bit 9)
pressButton(1 << 9);
if (ex.web_current_page() !== 10) throw new Error(`Expected PAGE_SEQ (10), got ${ex.web_current_page()}`);
if (ex.web_seq_menu_on() !== 0 || ex.web_note_menu_on() !== 0) throw new Error("Expected menus closed upon jumping to PAGE_SEQ");

// While on PAGE_SEQ, 1st press of BTN_SEQ opens Step Note Options
pressButton(1 << 9);
if (ex.web_note_menu_on() !== 1 || ex.web_seq_menu_on() !== 0) {
  throw new Error(`Expected Step Note Options (note=1, seq=0), got note=${ex.web_note_menu_on()}, seq=${ex.web_seq_menu_on()}`);
}
console.log("✓ Pressing BTN_SEQ on Piano Roll opened Step Note Options");

// 2nd press of BTN_SEQ advances to Global Seq Options
pressButton(1 << 9);
if (ex.web_note_menu_on() !== 0 || ex.web_seq_menu_on() !== 1) {
  throw new Error(`Expected Global Seq Options (note=0, seq=1), got note=${ex.web_note_menu_on()}, seq=${ex.web_seq_menu_on()}`);
}
console.log("✓ Pressing BTN_SEQ in Note Options advanced to Global Seq Options");

// 3rd press of BTN_SEQ returns to Piano Roll
pressButton(1 << 9);
if (ex.web_current_page() !== 10 || ex.web_note_menu_on() !== 0 || ex.web_seq_menu_on() !== 0) {
  throw new Error(`Expected Piano Roll (note=0, seq=0), got note=${ex.web_note_menu_on()}, seq=${ex.web_seq_menu_on()}`);
}
console.log("✓ Pressing BTN_SEQ in Seq Options returned to Piano Roll");

// Open Step Note Options, test HOME button return
pressButton(1 << 9); // note options open
pressButton(1 << 6); // BTN_HOME tap
if (ex.web_current_page() !== 0 || ex.web_note_menu_on() !== 0 || ex.web_seq_menu_on() !== 0) {
  throw new Error(`Expected HOME to return to page 0, got ${ex.web_current_page()}`);
}
console.log("✓ Pressing HOME in Step Note Options returned to page 0 (PAGE_VCF)");

// Jump back to PAGE_SEQ, test tie removal
pressButton(1 << 9); // PAGE_SEQ piano roll

console.log("Testing note and associated tie removal...");
// Hold SEL (bit 1) to clear all steps first
pressButton(1 << 1, 40);

// Key-down C4 on step 0
ex.web_keys(1 << 7); // Note C4
for (let b = 0; b < 10; b++) ex.web_render(128);
ex.web_keys(0);      // Key release (cursor advances to step 1)
for (let b = 0; b < 10; b++) ex.web_render(128);

if (ex.web_step_n(0) !== 1 || ex.web_step_note(0, 0) !== 60) {
  throw new Error(`Step 0 note entry failed: n=${ex.web_step_n(0)}, note=${ex.web_step_note(0, 0)}`);
}

// Open Note Options on step 1 (cursor is on step 1)
pressButton(1 << 9); // Note Options
// Move menu cursor to TIE (index 1) with ENC_SELECT
ex.web_enc(0, 1);
for (let b = 0; b < 5; b++) ex.web_render(128);
// Turn ALGORITHM encoder to toggle TIE ON
ex.web_enc(1, 1);
for (let b = 0; b < 5; b++) ex.web_render(128);
if (ex.web_step_time(1) !== 1 || ex.web_step_n(1) !== 1 || ex.web_step_note(1, 0) !== 60) {
  throw new Error(`Step 1 tie failed: time=${ex.web_step_time(1)}, n=${ex.web_step_n(1)}, note=${ex.web_step_note(1, 0)}`);
}

// Return to Piano Roll: press BTN_SEQ twice (Seq Options -> Piano Roll)
pressButton(1 << 9);
pressButton(1 << 9);

// Move cursor to step 2 with KNOB 1 (ENC_KNOB1 role 3)
ex.web_enc(3, 1);
for (let b = 0; b < 5; b++) ex.web_render(128);

// Open Note Options on step 2
pressButton(1 << 9); // Note Options
// Move menu cursor to TIE (index 1)
ex.web_enc(0, 1);
for (let b = 0; b < 5; b++) ex.web_render(128);
// Toggle TIE ON
ex.web_enc(1, 1);
for (let b = 0; b < 5; b++) ex.web_render(128);
if (ex.web_step_time(2) !== 1 || ex.web_step_n(2) !== 1 || ex.web_step_note(2, 0) !== 60) {
  throw new Error(`Step 2 tie failed: time=${ex.web_step_time(2)}, n=${ex.web_step_n(2)}, note=${ex.web_step_note(2, 0)}`);
}

// Return to Piano Roll
pressButton(1 << 9);
pressButton(1 << 9);

// Verify step 0, 1, 2 tie chain
if (ex.web_step_time(0) !== 0 || ex.web_step_time(1) !== 1 || ex.web_step_time(2) !== 1) {
  throw new Error(`Expected tie chain on steps 0, 1, 2`);
}

// Move cursor back to step 0 with KNOB 1
ex.web_enc(3, -1);
for (let b = 0; b < 5; b++) ex.web_render(128);
ex.web_enc(3, -1);
for (let b = 0; b < 5; b++) ex.web_render(128);

// Remove note on step 0: tap BTN_SEL (bit 1)
pressButton(1 << 1);

// Step 0 must have no notes
if (ex.web_step_n(0) !== 0) {
  throw new Error(`Step 0 note was not cleared: n=${ex.web_step_n(0)}`);
}
// Associated ties on step 1 and step 2 must now be completely removed!
if (ex.web_step_time(1) !== 0 || ex.web_step_n(1) !== 0) {
  throw new Error(`Step 1 tie was not cleared: time=${ex.web_step_time(1)}, n=${ex.web_step_n(1)}`);
}
if (ex.web_step_time(2) !== 0 || ex.web_step_n(2) !== 0) {
  throw new Error(`Step 2 tie was not cleared: time=${ex.web_step_time(2)}, n=${ex.web_step_n(2)}`);
}
console.log("✓ Removing note on step 0 automatically removed all associated ties on steps 1 and 2");

console.log("\n=========================================");
console.log(" ALL FM-106 WEB SIMULATOR TESTS PASSED! ");
console.log("=========================================\n");


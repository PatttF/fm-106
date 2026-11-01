# FM-106

**Authentic 6-Voice Polyphonic Roland Juno-106 Style Synthesizer Firmware for the M-VAVE FM-1**

[![License: GPL v3](https://img.shields.io/badge/License-GPLv3-blue.svg)](LICENSE)
[![Firmware](https://img.shields.io/badge/Firmware-FM--106-red.svg)](firmware/)
[![Web Simulator](https://img.shields.io/badge/Simulator-WebAssembly-cyan.svg)](simulator.html)

FM-106 is inspired by the analog emulation engine of the legendary 1984 **Roland Juno-106** synthesizer to the ultra-portable **M-VAVE FM-1** hardware workstation. Engineered with single-precision floating point effects, an authentic 1980s Roland interface with real-time CRT oscilloscope, 256 presets with on-device naming, a 128-step acid sequencer with piano roll, Apple-compliant Bluetooth MIDI (Host and Peripheral), and a 45-parameter MIDI CC engine featuring live **MIDI Learn**.

> 📖 **Comprehensive User Manual:** For complete operational instructions, diagrams, step-by-step tutorials, and parameter charts, open [manual.html](manual.html) in any web browser.

---

## Key Features

| Feature | Description |
| :--- | :--- |
| **6-Voice Polyphony** | True 6-voice polyphonic Juno-106 synthesis with dynamic voice allocation and unison modes. |
| **PolyBLEP DCO** | Bandlimited PolyBLEP Sawtooth, Pulse width modulation (LFO / Manual), Sub-oscillator (-1 oct), and White Noise. |
| **IR3109 VCF** | 4-pole 24 dB/oct resonant low-pass filter with self-oscillation and non-linear OTA saturation curves. Sub-sampled at 11 kHz for peak CPU efficiency. |
| **4-Position HPF** | Authentic discrete resistor network: `0: BST` (+9.4 dB shelf @ 103 Hz), `1: FLT` (Flat), `2: 236` (236 Hz), `3: 754` (754 Hz). |
| **14-Bit ADSR** | Authentic NEC &mu;PD7811G voice processor envelope with log decay/release and linear attack. |
| **MN3009 BBD Chorus** | Dual bucket-brigade delay line emulation: `OFF`, `Chorus I` (0.39 Hz), `Chorus II` (0.80 Hz), and `Chorus I+II` (8.0 Hz vibrato). |
| **Tape & Shimmer** | FPU-accelerated vintage tape echo (saturation, wow/flutter) and octave pitch-shifted shimmer reverb. |
| **128-Step Sequencer** | Acid sequencer with visual piano roll, parameter locks (P-Locks), ratchet (1..4), tie, accent, slide, probability, swing, and live recording. |
| **256 Presets** | 128 Factory presets (Bank A & B 11..88) + 128 User writable presets (Bank UA & UB 11..88) with on-screen alphanumeric name editing and flash persistence. |
| **MIDI Learn Engine** | 45 mappable sound, effects, and sequencer parameters with dynamic on-device MIDI Learn and flash memory storage. |
| **Bluetooth LE MIDI** | Wireless MIDI in both **Device Mode** (connects to Mac, iOS, Windows, Android) and **Host Mode** (pairs directly with wireless MIDI controllers). |
| **Class-Compliant USB** | 16-bit 44.1 kHz stereo digital audio streaming directly into your DAW and bidirectional USB-MIDI with SysEx librarian support. |
| **CRT Oscilloscope** | Real-time waveform display with zero-crossing trigger stabilization and Roland oscilloscope graticule. |

---

## Hardware Controls & Navigation

```
                       [ M-VAVE FM-1 PANEL ]
 ┌─────────────────────────────────────────────────────────────┐
 │  [ 240x240 LCD DISPLAY ]     [SELECT]   [ALGO]    (MASTER)  │
 │  • Header & Voice LEDs        Encoder    Encoder    Volume   │
 │  • 4 Live Parameter Faders                                  │
 │  • Real-time Oscilloscope    [PRESETS]  [1] [2] [3] [4]     │
 │                               Encoder     Macro Knobs       │
 │                                                             │
 │  [OCT-] [OCT+] [FX]   [SEL]  [ENV]  [LFO]  [EDIT]  [GLO]    │
 │  [HOME] [SAVE] [ARP]  [SEQ]  [PLAY] [REC]                   │
 │                                                             │
 │  [ ||| | ||| | ||| | ||| | ||| | ||| | ||| ] 27-Key Touch   │
 └─────────────────────────────────────────────────────────────┘
```

### Quick Button Shortcuts

- **`ENC_SELECT` (Turn)**: Browse through parameter edit pages.
- **`ENC_ALGORITHM` (Turn)**: Cycle preset banks: **Fact A** &rarr; **Fact B** &rarr; **User A** &rarr; **User B**.
- **`ENC_PRESETS` (Turn)**: Browse 64 patches within the active bank (`11` to `88`).
- **`ENC_KNOB1..4`**: Direct hands-on control of the 4 sliders on the active edit page.
- **`BTN_EDIT`**: Step to next parameter page (Hold 500ms in Sequencer for global options).
- **`BTN_HOME`**: Instant return to primary **VCF** page; cancels menus.
- **`BTN_GLO`**: Open **Global Options Menu** (MIDI Setup, Bluetooth, System Specs).
- **`BTN_SAVE`**: Open **Preset Save & Name Editor** to store custom patches into user banks.
- **`BTN_FX`**: Cycle through effects processors: **Chorus** &rarr; **Tape** &rarr; **Reverb**.
- **`BTN_ENV` / `BTN_LFO` / `BTN_ARP`**: One-touch page jumps.
- **`BTN_PLAY` / `BTN_REC`**: Transport start/stop and real-time live recording with 4-beat count-in.

---

## Parameter Edit Pages

Turn the **`SELECT`** encoder or tap **`EDIT`** to move through the 10 sound design pages:

| Page | Fader 1 (Knob 1) | Fader 2 (Knob 2) | Fader 3 (Knob 3) | Fader 4 (Knob 4) |
| :--- | :--- | :--- | :--- | :--- |
| **1. VCF** | `FREQ`: Cutoff (0..127) | `RES`: Resonance (0..127) | `ENV`: Env Depth (0..127) | `LFO`: LFO Depth (0..127) |
| **2. VCF 2** | `KBD`: Key Track (0..127) | `INV`: Polarity (NORM/INV) | `B.DCO`: Bend&rarr;DCO (0..127)| `B.VCF`: Bend&rarr;VCF (0..127)|
| **3. DCO** | `RNG`: Octave (16', 8', 4')| `PULSE`: Pulse (OFF/ON) | `SAW`: Sawtooth (OFF/ON) | `SUBSW`: Sub-Osc (OFF/ON) |
| **4. DCO 2** | `SUB`: Sub Level (0..127) | `NOISE`: Noise (0..127) | `PWM`: Width/Depth (0..127)| `DCOLFO`: Pitch LFO (0..127) |
| **5. ENVELOPE** | `ATK`: Attack (0..127) | `DEC`: Decay (0..127) | `SUS`: Sustain (0..127) | `REL`: Release (0..127) |
| **6. LFO** | `RATE`: Frequency (0..127)| `DELAY`: Onset (0..127) | `PWM MD`: LFO / MAN | *(Unassigned)* |
| **7. ARP** | `ARP`: Arp (OFF/ON) | `MODE`: UP, U&D, DN | `ARAT`: Rate (0..127) | `ARNG`: Octaves (1..3) |
| **8. FX** | `CHO`: Chorus (OFF..I+II) | `HPF`: BST / FLT / 236 / 754| `VCA`: Mode (ENV / GATE) | `LEVEL`: Volume (0..127) |
| **9. TAPE** | `TYPE`: WARM / VINT / LOFI| `MIX`: Wet/Dry (0..127) | `RATE`: Speed/Flutter (0..127)| `CHAR`: Drive & Tone (0..127)|
| **10. REVERB**| `REV`: Shimmer (OFF/ON) | `MIX`: Wet/Dry (0..127) | `DECAY`: Tail Length (0..127)| `SHIM`: Pitch Shimmer (0..127)|

---

## Step Sequencer & Piano Roll

The onboard sequencer stores up to 128 steps per patch with an interactive piano roll view:

- **Step Entry**: Scroll through steps with `SELECT`; play notes on the keyboard or dial pitches with Knob 2 (`NOTE`).
- **Live Recording**: Tap `BTN_REC`, then press `BTN_PLAY`. A 4-beat visual metronome count-in runs before capturing live performance and gate duration.
- **Step Options (Tap `EDIT`)**:
  - `RATCHET (1..4)`: Trigger micro-burst ratchets per step.
  - `TIE`: Connect notes into smooth legato phrases without envelope re-triggers.
  - `ACCENT` / `SLIDE`: Classic acid bassline punch and portamento glide.
  - `PROB (0%..100%, MUTE)`: Generative rhythm probability.
  - `P-LOCKS`: Per-step knob parameter automation.
- **Global Options (Hold `EDIT` 500ms)**: Set length (1..128 steps), tempo (40..240 BPM), clock division, swing percentage, or duplicate active bars (`DUP BARS`).

---

## MIDI CC Configuration & Learn

FM-106 includes an on-device MIDI continuous controller engine controlling 45 parameters. 

### How to Use Live MIDI Learn:
1. Press `BTN_GLO` &rarr; **MIDI SETUP** &rarr; **CC CONFIG**.
2. Scroll to the parameter you want to assign (e.g. `[VCF] CUTOFF`).
3. Press `BTN_SEL`. The screen flashes **`MIDI LEARN: SEND CC`**.
4. Wiggle any knob, slider, or pedal on your external MIDI controller.
5. The CC is instantly mapped, saved to flash, and ready to use!
6. Tap `BTN_EDIT` to reset the selected parameter to its factory default CC.

*(For the complete 45-parameter default CC chart, refer to [manual.html](manual.html#midi-cc-reference)).*

---

## Web Simulator & Patch Librarian

- **[simulator.html](simulator.html)**: Interactive WebAssembly simulator running the exact FM-106 audio DSP engine and UI directly in your web browser.
- **[Librarian.html](Librarian.html)**: Visual patch librarian supporting Web MIDI. Connect the FM-106 over USB to audition, back up, and load standard Roland Juno-106 SysEx files (`.syx`), Jarvik7 (`.106`), Web JSON, vintage Roland Juno-106 Tape Cassette audio files (`.wav`), and `.pjunoxl` preset files. Includes the 64-preset LFO Store Future Classic soundset and the Lixor soundset.

---

## Building & Installation

### Prerequisites
- macOS or Linux with Docker (for cross-compiling via JieLi toolchain / AC79 SDK).
- Python 3 with `pyserial` and `mido` for USB installation.

### 1. Build Firmware
```sh
# Single-core build (ultra-stable, FPU accelerated):
./build.sh

# Dual-core build (CPU0 + CPU1 parallel audio):
DINGHY_DUAL_CORE=1 ./build.sh
```

### 2. Build WebAssembly Simulator
```sh
./emu/build.sh
node emu/emu_test.mjs
```

### 3. Install to M-VAVE FM-1
Connect your FM-1 via USB-C and flash the package:
```sh
python3 tools/fm1_install.py build/fm106.fwsc --yes
```

*Hardware Recovery:* If needed, hold `BTN_OCT_DOWN` while powering on to enter the hardware bootloader rescue mode.

---

## License

GPL-3.0-only. Built upon the Felucca / SLOOP / Dinghy platform by Leo Kuroshita (@kurogedelic), Hügelton Instruments, and isod89. DSP models adapted from Ultramaster KR-106 by Kayrock Screenprinting and Melodee by Kerem Kilic.

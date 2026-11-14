# FM-106

**Authentic 6-Voice Polyphonic Roland Juno-106 Synthesizer Firmware for the M-VAVE FM-1**

[![License: GPL v3](https://img.shields.io/badge/License-GPLv3-blue.svg)](LICENSE)
[![Firmware](https://img.shields.io/badge/Firmware-FM--106-red.svg)](firmware/)
[![Web Installer](https://img.shields.io/badge/Web%20Installer-Online-brightgreen.svg)](https://patttf.github.io/fm-106/)
[![User Manual](https://img.shields.io/badge/User%20Manual-Online-blueviolet.svg)](https://patttf.github.io/fm-106/manual.html)
[![Patch Librarian](https://img.shields.io/badge/Librarian-Web%20MIDI-amber.svg)](https://patttf.github.io/fm-106/Librarian.html)
[![Web Simulator](https://img.shields.io/badge/Simulator-WebAssembly-cyan.svg)](https://patttf.github.io/fm-106/simulator.html)

FM-106 transforms the ultra-portable **M-VAVE FM-1** hardware workstation into an authentic analog emulation of the legendary 1984 **Roland Juno-106** synthesizer. Engineered with circuit-level modeling, single-precision floating point effects, an authentic 1980s Roland interface with a real-time CRT oscilloscope, 256 presets with on-device naming, a 128-step acid sequencer with piano roll, wireless Bluetooth MIDI (Host and Device), and a 45-parameter MIDI CC engine with live **MIDI Learn**.

> ### 🌐 Online Web Tools (No Software Installation Required)
> - ⚡ **[Web Firmware Installer](https://patttf.github.io/fm-106/)**: 1-click browser flashing via WebUSB / WebSerial in Chrome, Edge, or Opera.
> - 📖 **[Interactive User Manual](https://patttf.github.io/fm-106/manual.html)**: Comprehensive operation manual, voice allocation diagrams, circuit references, and parameter tables.
> - 🎛️ **[Visual Patch Librarian](https://patttf.github.io/fm-106/Librarian.html)**: Web MIDI librarian to audition, import, organize, and back up presets (SysEx `.syx`, `.pjunoxl`, `.106`, tape `.wav`, and JSON).
> - 🕹️ **[Web Simulator](https://patttf.github.io/fm-106/simulator.html)**: Run the exact FM-106 audio DSP engine and CRT oscilloscope directly in any web browser.

---

## Key Features

| Feature | Description |
| :--- | :--- |
| **Authentic Voice Modes** | True 6-voice polyphonic Juno-106 synthesis with **Mono** (6-voice stacked unison with chord memory), **Poly I** (round-robin voice cycling), and **Poly II** (authentic Roland release-choking voice allocation with voice 0 priority). |
| **PolyBLEP DCO** | Bandlimited PolyBLEP Sawtooth, Pulse Width Modulation (LFO / Manual depth), Sub-oscillator (-1 oct square), and White Noise generator. |
| **IR3109 VCF & Warmth** | 4-pole 24 dB/oct resonant low-pass filter with self-oscillation, non-linear OTA saturation curves, and analog filter circuit modeling. Sub-sampled at 11 kHz for peak CPU efficiency. |
| **4-Position HPF** | Authentic discrete resistor network: `0: BST` (+9.4 dB bass boost shelf @ 103 Hz), `1: FLT` (Flat), `2: 236` (236 Hz cutoff), and `3: 754` (754 Hz cutoff). |
| **14-Bit ADSR** | Authentic NEC &mu;PD7811G voice processor envelope with exponential decay/release and linear attack curves. |
| **MN3009 BBD Chorus** | Dual bucket-brigade delay line emulation: `OFF`, `Chorus I` (0.51 Hz antiphase), `Chorus II` (0.86 Hz antiphase), and `Chorus I+II` (9.75 Hz in-phase rotary vibrato). |
| **Tape & Shimmer** | FPU-accelerated vintage tape echo (saturation, wow/flutter, tone) and 1980s octave pitch-shifted shimmer reverb. |
| **128-Step Acid Sequencer** | Acid sequencer with visual piano roll, ratchets (1..8), ties with automatic associated tie cleanup, accents, slides, trigger probability, swing, parameter locks (P-Locks), and live recording overdub with 4-beat count-in. |
| **256 Presets** | 128 Factory presets (Bank A & B 11..88) + 128 User writable presets (Bank UA & UB 11..88) with on-device alphanumeric naming and flash persistence. |
| **MIDI Learn Engine** | 45 mappable sound, effects, and sequencer parameters with dynamic on-device MIDI Learn and non-volatile flash storage. |
| **Bluetooth LE MIDI** | Wireless MIDI in both **Device Mode** (connects to Mac, iOS, Windows, Android) and **Host Mode** (pairs directly with wireless MIDI controllers). |
| **Class-Compliant USB** | 16-bit 44.1 kHz stereo digital audio streaming directly into your DAW and bidirectional USB-MIDI with SysEx librarian support. |
| **CRT Oscilloscope** | Real-time waveform display with zero-crossing trigger stabilization, amplified dynamic range, and Roland graticule. |
| **Universal Hardware Support** | Automatic detection and support for both standard and newer SPI flash revisions (including UC25HQ80). |

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

### Button & Encoder Functions

- **`ENC_SELECT` (Turn)**: Browse through parameter edit pages / scroll rows in menus.
- **`ENC_ALGORITHM` (Turn)**: Cycle preset banks (**Fact A** &rarr; **Fact B** &rarr; **User A** &rarr; **User B**) in performance mode; adjust values in menus.
- **`ENC_PRESETS` (Turn)**: Browse 64 patches within the active bank (`11` to `88`).
- **`ENC_KNOB1..4`**: Direct hands-on control of the 4 sliders on the active edit page. On the Sequencer Piano Roll:
  - `Knob 1`: `STEP` cursor (1..128)
  - `Knob 2`: `NOTE` pitch row (0..127)
  - `Knob 3`: `GATE` duration (1..255)
  - `Knob 4`: `VEL` velocity (dialing down to 0 removes the note and clears its ties)
- **`BTN_EDIT`**: Immediate page cycler: steps forward through all 11 parameter pages, and cleanly loops from Sequencer (Page 11) back to VCF (Page 1).
- **`BTN_SEQ`**: Sequencer navigation cycle:
  - *From any synth page:* Jumps directly to the **Sequencer Piano Roll**.
  - *While on Sequencer:* Cycles **Piano Roll** &rarr; **Step Note Options** &rarr; **Global Sequencer Options** &rarr; **Piano Roll**.
- **`BTN_HOME`**: Instant return to primary **VCF** page; closes all menus and options.
- **`BTN_GLO`**: Open **Global Options Menu** (MIDI Setup, Bluetooth LE, Device Specs).
- **`BTN_SAVE`**: Open **Preset Save & Name Editor** to store custom patches into user banks.
- **`BTN_FX`**: Cycle through effects processors: **Chorus** &rarr; **Tape** &rarr; **Reverb**.
- **`BTN_ENV` / `BTN_LFO` / `BTN_ARP`**: One-touch parameter page jumps.
- **`BTN_PLAY` / `BTN_REC`**: Transport start/stop and real-time live recording with 4-beat visual metronome count-in.
- **`BTN_SEL`**:
  - *On Piano Roll:* Tap to remove note at cursor (and automatically removes associated ties); hold 500ms to clear all steps.
  - *In Menus:* Navigate selection upward.

---

## Parameter Edit Pages

Turn the **`SELECT`** encoder or tap **`EDIT`** to move through the 11 sound design and sequencer pages:

| Page | Fader 1 (Knob 1) | Fader 2 (Knob 2) | Fader 3 (Knob 3) | Fader 4 (Knob 4) |
| :--- | :--- | :--- | :--- | :--- |
| **1. VCF** | `FREQ`: Cutoff (0..127) | `RES`: Resonance (0..127) | `ENV`: Env Depth (0..127) | `LFO`: LFO Depth (0..127) |
| **2. VCF 2** | `KBD`: Key Track (0..127) | `INV`: Polarity (NORM/INV) | `B.DCO`: Bend&rarr;DCO (0..127)| `B.VCF`: Bend&rarr;VCF (0..127)|
| **3. DCO** | `RNG`: Octave (16', 8', 4')| `PULSE`: Pulse (OFF/ON) | `SAW`: Sawtooth (OFF/ON) | `SUBSW`: Sub-Osc (OFF/ON) |
| **4. DCO 2** | `SUB`: Sub Level (0..127) | `NOISE`: Noise (0..127) | `PWM`: Width/Depth (0..127)| `DCOLFO`: Pitch LFO (0..127) |
| **5. ENVELOPE** | `ATK`: Attack (0..127) | `DEC`: Decay (0..127) | `SUS`: Sustain (0..127) | `REL`: Release (0..127) |
| **6. LFO** | `RATE`: Frequency (0..127)| `DELAY`: Onset (0..127) | `PWM MD`: LFO / MAN | `ASSIGN`: MONO, POLY I, POLY II |
| **7. ARP** | `ARP`: Arp (OFF/ON) | `MODE`: UP, U&D, DN | `ARAT`: Rate (0..127) | `ARNG`: Octaves (1..3) |
| **8. FX** | `CHO`: Chorus (OFF..I+II) | `HPF`: BST / FLT / 236 / 754| `VCA`: Mode (ENV / GATE) | `LEVEL`: Volume (0..127) |
| **9. TAPE** | `TYPE`: WARM / VINT / LOFI| `MIX`: Wet/Dry (0..127) | `RATE`: Speed/Flutter (0..127)| `CHAR`: Drive & Tone (0..127)|
| **10. REVERB**| `REV`: Shimmer (OFF/ON) | `MIX`: Wet/Dry (0..127) | `DECAY`: Tail Length (0..127)| `SHIM`: Pitch Shimmer (0..127)|
| **11. SEQ** | `STEP`: Cursor (1..128) | `NOTE`: Pitch Row (0..127) | `GATE`: Length (1..255) | `VEL`: Velocity (0..127) |

---

## Step Sequencer & Piano Roll

The onboard acid sequencer stores up to 128 steps per patch with an interactive piano roll view:

- **Step Entry**: Scroll through steps with Knob 1 or `SELECT`; play notes on the keyboard or dial pitches with Knob 2 (`NOTE`).
- **Live Recording**: Tap `BTN_REC`, then press `BTN_PLAY`. A 4-beat visual metronome count-in runs before capturing live performance and gate duration in real time.
- **Sequential Menus (`BTN_SEQ`)**:
  - **Step Note Options (1st press of `BTN_SEQ`)**:
    - `RATCHET (1..8)`: Trigger micro-burst ratchets per step.
    - `TIE (ON/OFF)`: Connect notes into sustained legato phrases without envelope re-triggers.
    - `ACCENT (ON/OFF)`: TB-303 style velocity and filter envelope punch.
    - `SLIDE (ON/OFF)`: Classic portamento glide into next pitch.
    - `PROB (0%..100%, MUTE)`: Generative rhythm probability.
    - `P-LOCKS`: Count of automated parameter locks on this step.
  - **Global Sequencer Options (2nd press of `BTN_SEQ`)**:
    - `STEPS (1..128)`: Pattern loop length.
    - `BPM (40..240)`: Playback tempo (syncable to external MIDI clock).
    - `DIV`: Clock division (1/4, 1/8, 1/16, 1/32, triplets).
    - `GATE (8..128)`: Default gate duration percentage.
    - `SWING (0..100%)`: MPC-style groove swing.
    - `REC (ON/OFF)`: Arm live recording overdub.
    - `DUP BARS`: Automatically duplicate active pattern to double length.
    - `CLR ALL`: Clear all steps and parameter locks.
  - **Return to Piano Roll (3rd press of `BTN_SEQ`)**:
    - Returns cleanly to the interactive visual piano roll.
- **Automatic Associated Tie Cleanup**:
  - Removing a note (via `BTN_SEL`, dialing velocity to 0 with Knob 4, toggling TIE to OFF, or overwriting) automatically clears all subsequent contiguous tied steps linked to that note.

---

## MIDI CC Configuration & Learn

FM-106 includes an on-device MIDI Continuous Controller engine controlling 45 parameters over USB and Bluetooth MIDI.

### How to Use Live MIDI Learn:
1. Press `BTN_GLO` &rarr; **MIDI SETUP** &rarr; **CC CONFIG**.
2. Scroll to the parameter you want to assign (e.g. `[VCF] CUTOFF`).
3. Press `BTN_SEL`. The screen flashes **`MIDI LEARN: SEND CC`**.
4. Wiggle any knob, slider, or pedal on your external MIDI controller.
5. The CC is instantly mapped, saved to non-volatile flash, and ready to use!
6. Tap `BTN_EDIT` to reset the selected parameter to its factory default CC.

*(For the complete 45-parameter default CC chart, refer to the [Online User Manual](https://patttf.github.io/fm-106/manual.html#midi-cc-reference)).*

---

## Web Tools & Patch Librarian

Connect your M-VAVE FM-1 via USB-C to take advantage of the browser-based web suite:

- ⚡ **[Web Firmware Installer](https://patttf.github.io/fm-106/)**: Flash the latest firmware directly from Chrome, Edge, or Opera using WebUSB/WebSerial.
- 🎛️ **[Visual Patch Librarian](https://patttf.github.io/fm-106/Librarian.html)**: Audition patches live, manage user banks, and drag-and-drop preset files:
  - Standard Roland Juno-106 SysEx files (`.syx`)
  - Jarvik7 preset files (`.106`)
  - TAL U-No-LX preset files (`.pjunoxl`)
  - Vintage Roland Juno-106 Tape Cassette audio files (`.wav`)
  - Full backup JSON library files
  - Includes curated soundsets: **Future Classic** and **Lixor**
- 📖 **[Interactive User Manual](https://patttf.github.io/fm-106/manual.html)**: Full documentation with circuit diagrams, voice allocation explanations, and tutorials.
- 🕹️ **[Web Simulator](https://patttf.github.io/fm-106/simulator.html)**: Full WebAssembly simulator running the FM-106 DSP audio engine and CRT display.

---

## Installation & Setup

### Method 1: Web Browser 1-Click Installer (Recommended)
1. Open **[https://patttf.github.io/fm-106/](https://patttf.github.io/fm-106/)** in Google Chrome, Microsoft Edge, or Opera.
2. Connect your M-VAVE FM-1 to your computer via USB-C and power it on.
3. Click **Install Firmware** and select the FM-1 USB serial device from the prompt.
4. The web installer automatically flashes and verifies the firmware.

### Method 2: Command Line (Python)
If you prefer flashing via the command line with Python:
```sh
# Install serial dependencies
pip install pyserial mido

# Flash the prebuilt firmware package
python3 tools/fm1_install.py fm106.fwsc --yes
```

### Method 3: Building from Source
Prerequisites: macOS or Linux with Docker (for cross-compiling via JieLi toolchain / AC79 SDK).
```sh
# Build firmware package (build/fm106.fwsc)
./build.sh

# Build WebAssembly simulator
./emu/build.sh
node emu_test.mjs
```

*Hardware Bootloader Recovery:* If needed, hold `BTN_OCT_DOWN` while powering on to enter the hardware bootloader rescue mode.

---

## License

GPL-3.0-only. Built upon the Felucca / SLOOP / Dinghy platform by Leo Kuroshita (@kurogedelic), Hügelton Instruments, and isod89. DSP models adapted from Ultramaster KR-106 by Kayrock Screenprinting and Melodee by Kerem Kilic.

# FM-106 Architecture & Implementation Plan

## 1. Overview & Objectives

**FM-106** transforms the M-VAVE FM-1 into a dedicated 6-voice polyphonic Juno-106 analog synthesizer emulator:
- **Port the core DSP** from [ultramaster_kr106](https://github.com/kayrockscreenprinting/ultramaster_kr106) into fixed-point integer arithmetic adhering to the AC791N hardware rules (no floating point, no 64-bit division, strict cycle budget).
- **Dual-Core Audio Acceleration** inspired by [melodee](https://github.com/keremimo/melodee) (`next` branch, commit `f12b310`): CPU0 and CPU1 concurrently render 3 voices each in the audio ISR, then join for HPF, VCA, and stereo BBD chorus processing.
- **Juno-106 1980s Retro UI**: Dark chassis panel with classic Roland color accents (Roland red/orange, cyan stripes, white labels), 7-segment style patch number display, 6 voice allocation indicators, graphic faders with orange caps, chorus status buttons, and keyboard monitor on the 240x240 LCD.
- **Complete Rebranding & Identity**: Rename project, files, USB descriptors, MIDI, USB audio input ("FM-106"), and firmware package (`fm106.fwsc`).

---

## 2. Technical Specifications & Architecture

### A. Synthesis Engine (`FM-106` DSP)
- **Polyphony**: 6 voices (Voice 0..5), with lowest/oldest voice stealing and unison mode option.
- **DCO**:
  - Waveforms: Sawtooth, Variable-width Pulse / PWM, Sub-oscillator (-1 / -2 oct), and Noise.
  - Sub-oscillator: Square wave 1 or 2 octaves below fundamental.
  - PWM: Manual or LFO-driven modulation.
  - PolyBLEP fixed-point bandlimited oscillator synthesis.
- **HPF**:
  - 4-position high-pass filter replicating the Juno-106 hardware circuit:
    - Mode 0: Low shelf bass boost (+9.4 dB @ 103 Hz)
    - Mode 1: Flat (DC blocking only)
    - Mode 2: 236 Hz cutoff
    - Mode 3: 754 Hz cutoff
- **VCF**:
  - 4-pole 24 dB/octave IR3109 / BA662 OTA low-pass filter model in fixed point.
  - Resonance with self-oscillation capability.
  - Cutoff modulations: Envelope (positive or inverted), LFO modulation, Keyboard pitch tracking (0%, 50%, 100%).
- **VCA**:
  - Switchable Envelope control (ADSR) or Gate mode.
  - Master level trim.
- **ADSR Envelope**:
  - Authentic Juno-106 D7811G microprocessor algorithm: 14-bit integer envelope ($0000..$3FFF), linear attack ramp, exponential decay/release using the Roland piecewise multiplier curve table.
- **LFO**:
  - Triangle/Sine LFO with Rate and Delay onset envelope.
- **MN3009 BBD Stereo Chorus**:
  - Modes: OFF, Chorus I (0.4 Hz, gentle), Chorus II (0.8 Hz, deep), Chorus I+II (8 Hz vibrato).
  - Fractional interpolated stereo delay line with antiphase modulation and wet/dry summing.
- **Preset System**:
  - Factory bank of 128 authentic Juno-106 patches from the KR-106 ROM / SysEx library.

### B. Dual-Core Processing Architecture
```
                        [ Audio ISR (256 stereo frames) ]
                                      │
              ┌───────────────────────┴───────────────────────┐
              ▼                                               ▼
       [ Core 0 (Main) ]                              [ Core 1 (Worker) ]
   • Prepare modulation & block params            • Worker job submitted
   • Render Voices 0, 1, 2                        • Render Voices 3, 4, 5 into
     into core0_sum[256]                            worker_sum[256]
              │                                               │
              └───────────────────────┬───────────────────────┘
                                      ▼
                             [ audio_worker_join() ]
                                      │
                        • Sum: mix[t] = core0[t] + worker[t]
                        • Apply HPF (4-position)
                        • Apply VCA & Smoothing
                        • Apply MN3009 Stereo Chorus
                        • Stream to I2S DAC & USB Audio
```

### C. 1980s Juno-106 UI Visual Design (240x240 LCD)
- **Top Bar (0-24 px)**: Classic Roland style header: "ROLAND JUNO-106" badge, Bank/Patch number (`A11`), Patch Name.
- **LED Voice Display (24-40 px)**: 6 retro LEDs numbered `1 2 3 4 5 6` showing active voice triggers and envelope activity.
- **Parameter / Fader Bay (40-140 px)**:
  - 4 retro vertical faders with orange knurled fader caps matching the current page's encoders (`ENC_KNOB1..4`).
  - Section headers with color-coded stripes (Red for DCO, Yellow/Amber for HPF/VCF, Blue for VCA/ENV).
- **Chorus & Mode Buttons (140-170 px)**:
  - Retro rocker / push buttons for `CHORUS: [OFF] [ I ] [ II ] [I+II]`, `VCA: [ENV/GATE]`, `HPF: [0 1 2 3]`.
- **Keyboard & Status (170-240 px)**:
  - 27-note visual keyboard displaying sounding notes in real-time.
  - Octave transpose and USB status indicator.

---

## 3. Implementation Steps

1. **Dual-Core & Memory Alignment**:
   - Update `app.ld` to support `.dsp_text` RAM execution for CPU1 worker code.
   - Update `build.py` verification to check CPU1 cleanly.
   - Confirm dual-core build passes `./build.sh` and host test suite.
2. **DSP Module Implementation (`firmware/src/fm106_*.h`)**:
   - `fm106_adsr.h`: Fixed-point Juno-106 D7811G envelope generator.
   - `fm106_osc.h`: Fixed-point PolyBLEP DCO, Pulse/PWM, Sub, Noise.
   - `fm106_vcf.h`: Fixed-point 4-pole IR3109 resonant ladder filter + HPF.
   - `fm106_chorus.h`: Stereo BBD chorus delay line with mode dispatch.
   - `fm106_presets.h`: 128 factory presets compiled into flash.
   - `fm106_engine.h`: Top-level 6-voice polyphonic orchestrator with dual-core job division.
3. **Application & UI Integration (`firmware/src/fm106.c`)**:
   - Panel encoder and button handlers mapped to Juno-106 controls.
   - GFX routines rendering the 1980s Roland styling.
   - MIDI IN/OUT and preset switching.
4. **USB & Branding**:
   - Update USB string descriptors to "FM-106" in `firmware/src/usb.c`.
   - Update `APP_NAME` to "FM-106", package output to `fm106.fwsc`.
   - Update build scripts and host test harness.
5. **Testing & Verification**:
   - Run host tests and generate audio WAV & screen PNGs.
   - Build target firmware package with dual-core enabled.

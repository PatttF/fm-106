

# FM-106

**6-Voice Polyphonic Roland Juno-106 Synthesizer Firmware for the M-VAVE FM-1 with Dual-Core DSP Acceleration**

FM-106 ports the Roland Juno-106 analog emulation engine from [ultramaster_kr106](https://github.com/kayrockscreenprinting/ultramaster_kr106) to the M-VAVE FM-1, featuring dual-core DSP acceleration adapted from [Melodee](https://github.com/keremimo/melodee), authentic 1980s Roland styling, 128 factory presets, and Roland MN3009 BBD Stereo Chorus.

```
firmware/src/fm106.c          <- Main synth application, panel controls, and 80s Juno-106 UI
firmware/src/fm106_engine.h   <- 6-voice polyphonic engine, dual-core job distribution
firmware/src/fm106_osc.h      <- Bandlimited PolyBLEP DCO: Saw, Pulse/PWM, Sub-Osc, Noise
firmware/src/fm106_vcf.h      <- 4-Pole IR3109 Resonant OTA Ladder Filter & 4-Position HPF
firmware/src/fm106_adsr.h     <- Authentic Juno-106 D7811G 14-bit integer ADSR envelope
firmware/src/fm106_chorus.h   <- MN3009 BBD Stereo Chorus (OFF, I, II, I+II)
firmware/src/fm106_presets.h  <- 128 Factory Juno-106 presets from original SysEx
firmware/src/platform.c       <- Plumbing (boot, dual-core init, USB, audio ISR, settings)
```

## Features

| | |
| --- | --- |
| **6-Voice Polyphony** | True 6-voice polyphonic Roland Juno-106 synthesis with dynamic voice stealing and unison mode. |
| **Dual-Core DSP** | CPU0 and CPU1 concurrently render voices in parallel during the audio ISR before stereo effects mixdown. |
| **1980s Juno-106 UI** | Signature Roland color scheme: dark chassis, Roland red badge, Juno cyan stripe, orange slider caps, 6 voice activity LEDs, and live 27-key keyboard. |
| **DCO** | Bandlimited PolyBLEP Sawtooth, Variable-width Pulse with manual/LFO PWM, Sub-oscillator (-1 or -2 oct), and Noise. |
| **VCF** | 4-pole 24 dB/oct IR3109 / BA662 resonant OTA ladder filter with self-oscillation and OTA saturation. |
| **HPF** | Authentic 4-position high-pass filter: 0: Bass boost (+9.4 dB shelf @ 103 Hz), 1: Flat, 2: 236 Hz, 3: 754 Hz. |
| **ADSR** | Authentic NEC uPD7811G voice microprocessor 14-bit integer envelope algorithm. |
| **BBD Chorus** | MN3009 analog delay line emulation with antiphase LFO modulation: OFF, Chorus I (0.39 Hz), Chorus II (0.80 Hz), and Chorus I+II (8.0 Hz vibrato). |
| **128 Presets** | 128 factory Juno-106 presets decoded from hardware SysEx (Groups A & B, Banks 1-8, Patches 1-8). |
| **USB Audio & MIDI** | Class-compliant 16-bit 44.1/48 kHz stereo USB audio input and USB-MIDI in/out identifying as **FM-106**. |

## Panel Controls

- **Keys (27 keys)**: Play 6 polyphonic voices; played notes illuminate on-screen in Roland red.
- **`ENC_PRESETS`**: Turn to browse and instantly load any of the 128 factory Juno-106 patches.
- **`ENC_KNOB1..4`**: Direct hands-on control of the 4 sliders on the current page with live visual fader response.
- **`BTN_HOME` / `BTN_EDIT`**: Cycle through parameter edit pages:
  - **VCF / DCO**: Cutoff Frequency, Resonance, Envelope Mod, Sub-Osc Level.
  - **ENVELOPE**: Attack, Decay, Sustain, Release (ADSR).
  - **DCO / PWM**: LFO Mod Depth, PWM Depth, Noise Level, HPF Mode.
  - **LFO / ARP**: LFO Rate, LFO Delay, Arp Rate, Arp Range.
- **`BTN_FX`**: Cycle Chorus mode: **OFF** &rarr; **I** &rarr; **II** &rarr; **I+II**.
- **`BTN_ENV`**: Quick jump to Envelope edit page.
- **`BTN_LFO`**: Quick jump to LFO/Arp edit page.
- **`BTN_SEL`**: Toggle VCA mode (**ENV** &harr; **GATE**).
- **`BTN_OCT_DOWN` / `BTN_OCT_UP`**: Octave shift (-2 to +2 octaves).
- **`BTN_ARP`**: Toggle Arpeggiator.

## Quick Start

### Build & Test on Host

Host mode runs FM-106 natively on macOS or Linux without needing hardware:

```sh
tests/run_host.sh
```
Outputs:
- `build/host/fm106.wav`: High-quality 44.1 kHz 16-bit stereo audio render.
- `build/host/fm106-1000.png` & `build/host/fm106-3000.png`: Exact 240x240 LCD screen captures.

### Build Target Firmware

Prerequisites: Docker (running on macOS) and the JieLi toolchain / AC79 SDK:

```sh
# Single-core build:
./build.sh                                  # -> build/fm106.fwsc

# Dual-core build (CPU0 + CPU1 parallel audio):
DINGHY_DUAL_CORE=1 ./build.sh              # -> build/fm106.fwsc
```

### Install to M-VAVE FM-1

Install over USB from the command line:

```sh
python3 tools/fm1_install.py build/fm106.fwsc
```

Or via WebUSB at the [FM-1 Firmware Hub](https://fm1.designburgapps.com).

## License

GPL-3.0-only. Built upon Felucca / SLOOP / Dinghy platform by Leo Kuroshita (@kurogedelic), Hügelton Instruments, and isod89. DSP models ported and adapted from Ultramaster KR-106 by Kayrock Screenprinting and Melodee by Kerem Kilic.

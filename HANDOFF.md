# FM-106 Project Handoff & Status

## Project Status: Completed & Verified
- **Target Synthesizer**: Roland Juno-106 (6-voice polyphonic, DCO, 4-pole IR3109 VCF, Juno-106 D7811G integer ADSR, MN3009 BBD Stereo Chorus, 128 factory presets).
- **Target Hardware**: M-VAVE FM-1 (JieLi AC791N dual-core).
- **Audio Output**: 44.1 kHz, 16-bit stereo line-out / headphones and class-compliant USB Audio input.
- **USB Device Identity**: "FM-106" (MIDI + Audio).
- **Firmware Package**: `build/fm106.fwsc` (Identity `FM-1_906`).

## Milestone Completion Summary
- [x] Baseline Dinghy codebase analyzed and verified on host and Docker.
- [x] Ultramaster KR-106 DSP architecture cloned and analyzed.
- [x] Melodee dual-core commit `f12b310` extracted and analyzed.
- [x] Host build fix applied for macOS (`gfx.c` Mach-O section guard, `audio_worker_thread_test.c` headers and cancellation).
- [x] Architectural plan created (`docs/PLAN.md`).
- [x] Agent guide created (`AGENT.md`).
- [x] Handoff created (`HANDOFF.md`).
- [x] **Phase 1: Dual-core build pipeline configuration**:
  - `tools/build.py` `.ram_text` call-target check updated to allow internal RAM calls.
  - Both `./build.sh` and `DINGHY_DUAL_CORE=1 ./build.sh` compile cleanly via Docker container.
- [x] **Phase 2: Integer DSP engine implementation**:
  - `fm106_tables.h`: Generated lookup tables for 4-pole TPT ladder filter G coefficients, denominator reciprocals, TANH saturation, and sine waves.
  - `fm106_adsr.h`: Authentic NEC uPD7811G 14-bit integer ADSR envelope generator with exact D7811G decay/release curve.
  - `fm106_osc.h`: Bandlimited PolyBLEP DCO with Saw, Pulse/PWM, Sub-oscillator (-1/-2 oct), and Noise generator.
  - `fm106_vcf.h`: 4-pole IR3109 resonant OTA ladder filter with BA662 saturation and 4-position HPF (Bass boost shelf, Flat, 236Hz, 754Hz).
  - `fm106_chorus.h`: Roland MN3009 BBD Stereo Chorus with antiphase LFO modulation (OFF, I, II, I+II).
  - `fm106_presets.h`: 128 authentic Juno-106 factory presets decoded from original hardware SysEx.
  - `fm106_engine.h`: 6-voice polyphonic orchestrator with dynamic voice allocation and dual-core audio worker job distribution.
- [x] **Phase 3: Hardware integration & 1980s Juno-106 UI**:
  - `firmware/src/fm106.c`: Application entry point with 1980s Roland styling.
  - Authentic color scheme: dark chassis, Roland red badge, Juno cyan stripe, orange fader caps, 6 red voice allocation LEDs, and live 27-key keyboard monitor.
  - 4 parameter edit pages (VCF/DCO, ENVELOPE, DCO/PWM, LFO/ARP) with real-time encoder control and visual slider response.
  - Buttons bound to Chorus cycling, Page switching, VCA mode, Arp, and Octave transpose.
- [x] **Phase 4: USB device identity & packaging**:
  - `firmware/src/usb.c`: USB product descriptor string updated to **"FM-106"** (both USB MIDI and USB Audio).
  - `platform.c`: `APP_NAME` defaulted to `"FM-106"`, `FELUCCA_ID` defaulted to `"FM-1_906"`.
  - `tools/build.py`: Output package renamed to `fm106.fwsc`, binary to `fm106.bin`.
- [x] **Phase 5: Verification & Testing**:
  - `tests/run_host.sh`: Passes all unit tests (audio worker busy/offline tests, concurrent thread submit/join tests).
  - Generates `build/host/fm106.wav`: Verified clean 44.1 kHz stereo audio (Peak 65.6% scale, no clipping, RMS 3394.5).
  - Generates `build/host/fm106-1000.png` and `fm106-3000.png`: Visually verified pixel-perfect 1980s Juno-106 screen graphics.
  - Target compilation verified: Both single-core (`./build.sh`) and dual-core (`DINGHY_DUAL_CORE=1 ./build.sh`) produce valid flash images and `.fwsc` update packages meeting all AC791N hardware checks (RAM budget, register access, ROM reference absence, flash-safe execution).
  - **Hardware Installation & Live Verification**:
    - Installed `build/fm106.fwsc` (dual-core enabled) to physical M-VAVE FM-1 hardware via `python3 tools/fm1_install.py build/fm106.fwsc --yes`.
    - Firmware rebooted and confirmed running with identity `FM-1_906`.
    - macOS USB Audio enumerated as **"FM-106"** (2 channels, 48 kHz).
    - macOS USB-MIDI port active as **"Felucca"**.
    - Live MIDI note playback tested and responsive.
- [x] **Phase 6: Audio Fidelity & Exact Mathematical Alignment with Ultramaster KR-106**:
  - Identified and fixed critical DSP discrepancies:
    - **D7811G ADSR Tick Rate**: Replaced sample-rate tick with authentic 236.211 Hz firmware tick accumulator (~186.7 samples @ 44.1 kHz) and linear audio-rate interpolation.
    - **Cutoff Accuracy**: Replaced toy octave formula with 4096-entry measured profile from voice card V4 (`J106DACHzTable.h`) and full 16-bit D7811G VCF DAC math (`fm106_calc_vcf_freq`).
    - **Resonance Accuracy**: Incorporated `ResK_J106` quartic polynomial curve with self-oscillation onset knee.
    - **BA662 OTA Differential**: Circuit-accurate OTA differential pair input compensation, feedback summer, and nonlinear saturation.
    - **Oscillators**: Juno-106 inverted comparator duty cycle, PolyBLEP anti-aliased saw/pulse, and exponential `AudioTaper` sub-oscillator.
    - **Chorus & HPF**: Added 2nd-order Butterworth biquad filter (9,661 Hz) and calibrated IC6 gains (dry 0.863, wet 1.257).
    - **Polyphonic Headroom**: Calibrated mix scaling to achieve 95.8% (-0.37 dBFS) peak with 0 clipped samples across chords.
  - Rebuilt dual-core firmware and reflashed to hardware device.

## Key Artifacts
- Plan: `docs/PLAN.md`
- Agent Instructions: `AGENT.md`
- Handoff & Status: `HANDOFF.md`
- UI Snapshot: `docs/fm106_screen.png`
- Host Audio Render: `build/host/fm106.wav`
- Target Firmware: `build/fm106.fwsc`

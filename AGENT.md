# FM-106 Autonomous Engineering Agent Guide

## Mission
Develop, port, optimize, and maintain **FM-106** — a full 6-voice Juno-106 emulator running on the M-VAVE FM-1 (JieLi AC791N dual-core processor), ported from `ultramaster_kr106` with dual-core DSP acceleration from `melodee` and authentic 1980s Roland Juno-106 visual styling.

## Target Platform & Hardware Constraints
- **MCU**: JieLi AC791N dual-core RISC (pi32v2 architecture, 160-240 MHz).
- **Core 0**: Main application loop, USB audio/MIDI, display SPI driver, panel input polling, audio ISR (3 voices + effects).
- **Core 1**: Dedicated audio worker kernel (3 voices concurrently).
- **Audio Spec**: 44.1 kHz, 16-bit stereo class-compliant USB Audio & I2S DAC. Buffer: 256 stereo frames (5.8 ms per half-buffer).
- **Display**: 240 x 240 ST7789 SPI LCD.
- **Rules of the Road**:
  - **NO FLOAT / NO DOUBLE**: There is no hardware FPU or software emulation library. Use fixed point (Q15 for audio signals, Q14 for envelopes, Q12 for filter gains, 32-bit phase accumulators).
  - **NO 64-BIT DIVISION**: Shifts only, or 32-bit divides.
  - **NO ROM CALLS**: Addresses `0xFFC00000..0xFFCFFFFF` must never be touched.
  - **NO REGISTER ACCESS OUTSIDE `hal/`**: Use existing HAL headers.
  - **FLASH-SAFE CODE**: Worker loop on CPU1 must run out of RAM (`.ram_text` or `.dsp_text`), never execute from XIP during flash erase/programming.

## Key Directories and Files
- `firmware/src/fm106.c`: Main application entry point, UI state machine, control mapping.
- `firmware/src/fm106_engine.h`: Top-level polyphonic DSP engine & voice allocator.
- `firmware/src/fm106_voice.h`: Single voice signal chain (DCO + VCF + VCA).
- `firmware/src/fm106_osc.h`: Bandlimited PolyBLEP DCO (Saw, Pulse/PWM, Sub, Noise).
- `fm106_vcf.h`: 4-pole IR3109 resonant filter & 4-position HPF.
- `fm106_adsr.h`: Juno-106 D7811G 14-bit integer envelope generator.
- `fm106_chorus.h`: MN3009 BBD stereo chorus emulation (Off, I, II, I+II).
- `fm106_presets.h`: 128 factory Juno-106 presets.
- `firmware/src/platform.c`: FM-1 hardware platform layer.
- `firmware/src/usb.c`: USB MIDI and USB Audio descriptors (branded "FM-106").
- `firmware/hal/fm1_multicore.h`: CPU1 lifecycle and audio worker primitives.
- `firmware/src/audio_worker.h`: Lock-free submit/join worker queue.
- `tools/build.py`: Packaging and binary validation script.
- `tests/run_host.sh`: Host compilation, unit tests, WAV generator, PNG screen grabber.

## Autonomous Workflow & Continuous Quality Verification
1. Every DSP change must pass host compilation and execution (`tests/run_host.sh`).
2. Every DSP change must pass JieLi pi32v2 compiler checks (`./build.sh` and `DINGHY_DUAL_CORE=1 ./build.sh`).
3. Disassembly and symbol table must be checked for forbidden ROM addresses, floating point symbols, or unaligned loads.
4. Listen to or inspect generated `build/host/fm106.wav` and inspect `build/host/fm106-*.png` screen snapshots.

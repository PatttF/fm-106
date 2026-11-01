#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# FM-106 Web Simulator Build Script
set -e
cd "$(dirname "$0")/.."
command -v emcc >/dev/null 2>&1 || { echo "emu: no emcc (Emscripten)"; exit 1; }
emcc ${EMU_OPT:--O2} -ffp-contract=off -std=gnu99 -w ${EMU_CFLAGS:-} \
    -DFM106_WEB=1 -Ifirmware/src -Ifirmware/gen \
    --no-entry -sSTANDALONE_WASM -sSTACK_SIZE=1048576 -sINITIAL_MEMORY=33554432 -sFILESYSTEM=0 \
    -o emu/fm106.wasm emu/fm106_web.c
cp emu/fm106.wasm fm106.wasm
echo "emu: emu/fm106.wasm ($(wc -c < emu/fm106.wasm | tr -d ' ') B wasm)"


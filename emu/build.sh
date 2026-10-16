#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# FM-106 Web Simulator Build Script
set -e
cd "$(dirname "$0")/../.."
command -v emcc >/dev/null 2>&1 || { echo "emu: no emcc (Emscripten)"; exit 1; }
mkdir -p build/emu/fonts
emcc ${EMU_OPT:--O2} -ffp-contract=off -std=gnu99 -w ${EMU_CFLAGS:-} \
    -DFM106_WEB=1 -Ifirmware/src -Ifirmware/gen \
    --no-entry -sSTANDALONE_WASM -sSTACK_SIZE=1048576 -sINITIAL_MEMORY=33554432 -sFILESYSTEM=0 \
    -o build/emu/fm106.wasm web/emu/fm106_web.c
cp web/emu/worklet.js build/emu/
cp web/emu/fonts/DotGothic16-subset.woff web/emu/fonts/OFL.txt build/emu/fonts/
cp web/emu/index.html build/emu/index.html
echo "emu: build/emu ($(wc -c < build/emu/fm106.wasm | tr -d ' ') B wasm)"

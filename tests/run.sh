#!/bin/sh
# Host test of the DSP: renders the 8 factory presets to build/render-out/*.wav and fuzzes every parameter.
set -e
cd "$(dirname "$0")/.."
mkdir -p build/render-out
${CC:-cc} -O1 -g -fsanitize=undefined -fno-sanitize-recover -Wall -Isrc -Iplatform \
  tests/render.c src/dsp.c src/presets.c -lm -o build/render
build/render build/render-out

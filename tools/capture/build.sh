#!/bin/sh
# Build the probe ROM. Requires RGBDS (https://rgbds.gbdev.io) on PATH.
set -e
cd "$(dirname "$0")"
rgbasm  -o probe.o chipboy_probe.asm
rgblink -o chipboy_probe.gb -n chipboy_probe.sym probe.o
rgbfix  -v -p 0xFF -t "CHIPBOYPROBE" chipboy_probe.gb
rm -f probe.o
echo "built chipboy_probe.gb ($(wc -c < chipboy_probe.gb) bytes)"

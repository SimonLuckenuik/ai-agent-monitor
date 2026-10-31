# 009 Compressed updates and releases from main

Status: Done in 1.4.0 and 1.5.0 (written after the fact, 2026-10-30)

## Problem

The firmware outgrew the plain-update limit (0x7E000 = 516,096 B): with 4m3m the first
1 MB holds the running image and the staged one. New features need room.

## Requirements

1. Every build writes `firmware.bin.gz` (gzip with an "AM" header field: unpacked size,
   file size, CRC); the Firmware update card takes it; the bootloader unpacks it. A
   damaged or foreign file is refused before anything is written.
2. Budget: the image plus its `.gz` within 1 MB (about 610-640 KB); the build prints the
   limit and fails above it.
3. A stepping stone for clocks that take plain images only: `SDPro_AIAgentMonitor_v1.4.0_nomqtt.bin`
   (no MQTT, no mDNS), the last plain image; later releases ship only the `.bin.gz`.
4. File names: only an image the stock page can install starts with `SDP`.
5. Releases: a push to `main` whose `FW_VERSION` has no release yet builds, tests, tags
   and publishes the `.bin.gz`, `SHA256SUMS.txt` and the CHANGELOG section.

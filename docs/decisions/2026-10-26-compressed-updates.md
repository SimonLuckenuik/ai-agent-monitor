# Compressed firmware updates

Date: 2026-10-26 (releases: 2026-10-28 and 2026-10-29)

Spec: [009](../../specs/009-compressed-updates-and-releases/SPEC.md)

## Evidence

With 4m3m the first 1 MB holds the running image and the staged update: a plain image
must stay under 0x7E000 (516,096 B), which left about 3 KB. A gzip image is about 70% of
the plain one.

## Decisions

- The updater takes `firmware.bin.gz` with an "AM" gzip header field (unpacked size,
  file size, CRC) and refuses damaged or foreign files before writing; the bootloader
  unpacks it. The budget becomes the image plus its `.gz` within 1 MB.
- 1.4.0 ships a plain no-MQTT, no-mDNS image (`SDPro_AIAgentMonitor_v1.4.0_nomqtt.bin`)
  as the stepping stone from plain-only updaters (stock, SmartClock-OSS before 1.4.0).
- From 1.5.0 releases ship only the `.bin.gz`; the firmware is no longer held to the
  plain size. mDNS returns to the full image.
- Only images the stock page can install carry the `SDP` prefix.
- A push to `main` whose `FW_VERSION` has no release publishes one (CHANGELOG section as
  notes); versions with `-` never publish.

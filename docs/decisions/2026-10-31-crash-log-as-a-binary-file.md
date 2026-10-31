# The crash log is a binary file the API serves as it is

Date: 2026-10-31

Status: Accepted

Spec: [021](../../specs/021-crash-report/SPEC.md)

## Context

The first version of the crash log kept JSON in storage and built JSON for the API with
ArduinoJson: 7.7 KB more firmware (5.8 KB compressed), with the update room at 95%. Kept
as binary records with the API's JSON written by hand, it still cost 3.7 KB. The owner
suggested keeping the crashes in a file and having the API return the file.

## Decision

- `/crashes.bin`: a 20-byte header and up to 8 entries of 160 bytes, written at boot
  through a second file, an entry at a time (no RAM buffer).
- `GET /api/crashes` streams the file; the settings page, the emulator page and
  `scripts/decode_crash.py` read the layout (documented in the API reference, checked by
  `static_assert` in `src/crash.cpp`).
- `/status` carries a short summary (`crashes`, `crashLoop`, `safeMode`, `lastCrash`).

## Consequences

- About 3 KB of firmware in all for records, log, protection, API and the settings card.
- The layout is an interface: changing it means a new header magic and readers that
  know both.

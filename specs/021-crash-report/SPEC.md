# 021 Crash records and crash-loop protection

Status: In progress (approved 2026-10-31; scope widened the same day at the owner's
request: a crash log, crash-loop protection, safe mode)

## Problem

Custom faces restarted the owner's clock, and with the face still selected it crashed
again on every boot ([issue](../../docs/issues/2026-10-31-animated-faces-restart-the-clock.md)).
The clock's USB port is power only, so the core's crash dump on the serial port reaches
nobody: `/status` showed only "Software/System restart", and the faces had to be deleted
through the API in the seconds after a boot.

## Goals

1. **A crash record with enough to find the cause.** On any crash (exception, watchdog,
   panic, `abort()`, stack overflow), keep: the reset reason, `exccause`, `epc1`-`epc3`,
   `excvaddr`, `depc`, up to 16 words from the stack that look like code addresses, the
   firmware version and build, uptime, the face shown (and the custom face's name), free
   heap and free stack. Written in the crash callback to RTC memory, copied at the next
   boot to a log in storage.
2. **A crash log.** The last 8 records, kept in storage (they survive power cuts and
   updates). `GET /api/crashes` (signed in) returns them; `/status` shows `crashes` (the
   count since the log was cleared) and `lastCrash` (reason, when, face).
3. **Decoding.** Each release also publishes its `firmware.elf`; `scripts/decode_crash.py`
   reads a clock's log (or a saved one), fetches the matching ELF by version, and prints
   each address as function and file:line (the toolchain's `addr2line`).
4. **Crash-loop protection.** A crash within 10 minutes of boot counts toward a loop; 10
   minutes without one clears the count.
   - After 3 such crashes in a row: custom faces are off (the built-in faces still run),
     and the screen and settings page say why.
   - After 2 more: safe mode. The next boots draw no face: only the startup screen (name,
     address, version) with "Safe mode", while the settings page and every API work.
   - **Resume** (settings page, or `POST /api/crashes/resume`) clears the protection; the
     log stays until **Clear**.
5. **The emulator does the same**: its panic calls the firmware's crash callback, so the
   record, the log and the protection run there too (with the emulator's own trace for
   decoding, since host addresses are not the chip's).

## Non-goals

- Sending crash logs anywhere (no network reporting).
- Fixing the faces' crash: [spec 023](../023-renderer-stack/SPEC.md).

## Open questions

- The thresholds (3, then 2 more, 10 minutes) are first choices; the owner may change them.

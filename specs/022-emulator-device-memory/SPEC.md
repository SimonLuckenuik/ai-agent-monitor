# 022 The emulator runs out of memory where the clock does

Status: In progress (approved 2026-10-31)

## Problem

Three custom faces restart the real clock
([issue](../../docs/issues/2026-10-31-animated-faces-restart-the-clock.md)) and run fine in
the emulator. The emulator uses the host's memory: the core's host mock answers
`getFreeHeap()` 30,000, `getMaxFreeBlockSize()` 20,000 and `getFreeContStack()` 4,000
whatever happens, `malloc` never fails, and `loop()` runs on the host's 8 MB stack. A face
that needs more memory than the clock has passes every test.

## Goals

1. **Heap like the clock.** Firmware allocations come from a fixed arena run by the
   ESP8266 core's own allocator (`umm_malloc`, built for the host), sized so the free heap
   at rest matches the clock's: about 15.7 KB with a built-in face shown (1.8.0,
   2026-10-31). Fragmentation then behaves as on the chip.
2. **Failures like the clock.** `malloc` returns null when the arena is full; a failed
   `new` aborts. A crash prints a host backtrace (function and line) to the log and
   restarts the firmware with the reset reason the chip would report.
3. **Stack like the clock.** `setup()` and `loop()` run on a limited stack with a guard
   page, sized from the chip's 4 KB scaled for the host's larger frames (calibrated, see
   PLAN), so a deep draw overflows in the emulator too.
4. **Honest numbers.** `/status` heap, `heapLow`, the largest free block and free stack
   come from the arena and the stack, not constants.
5. **Live statistics** (added 2026-10-31 at the owner's request): the screen page shows
   the device's CPU load, heap (free, lowest, largest block) and stack as they change,
   pushed on the screen WebSocket each second and served as `GET :8080/stats`; after a
   crash, the reason and the call chain. The firmware's own `/status` gains `stackFree`
   (on the clock too), so the emulator's stack can be calibrated against the chip's.
6. **A test.** An integration case shows each example face for a while under the
   clock's memory and fails on a restart. The three faces that crash the clock must fail it
   before the fix.

## Non-goals

- Fixing the faces or the renderer: that is the issue's fix, done once the emulator
  reproduces the crash.
- Cycle-exact timing (already modelled separately, `emulator/calibrate.py`).

## Open questions

- 32-bit build (`FORCE32=1`, the core's default for host tests) instead of 64-bit: objects
  (String, JsonDocument slots, pointers) would have the chip's sizes, making the arena
  and stack sizes much closer. Needs 32-bit libraries in the image.

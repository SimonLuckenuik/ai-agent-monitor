# The emulator models the clock's heap and stack

Date: 2026-10-31

Status: Accepted

## Context

Three custom faces restarted the clock and ran fine in the emulator, whose heap and stack
were the PC's ([issue](../issues/2026-10-31-animated-faces-restart-the-clock.md),
[spec 022](../../specs/022-emulator-device-memory/SPEC.md)).

## Decision

- The emulator is a 32-bit program, so objects have the chip's sizes.
- The firmware's allocations (during `setup()` and `loop()`) come from a 25.6 KB arena
  with umm_malloc's block size, header and best fit; blocks of 64 KB or more (the host
  mock's own) stay on the host heap.
- The stack is measured on the firmware's own frames only (`-finstrument-functions`), not
  with a real 4 KB stack: the host mock and glibc under the firmware (getifaddrs,
  getaddrinfo, printf) need far more stack than their chip counterparts and overflowed a
  real 4 KB stack at boot.
- Out of memory in `new` and a stack overflow panic as on the chip, with the call chain in
  the log, `GET :8080/stats` and the screen page.

## Consequences

- Crashes caused by the clock's memory now reproduce in the emulator and come with a call
  chain.
- The host's x86 frames are not the chip's Xtensa frames: `EMU_STACK` is calibrated
  against `stackFree` from the clock's `/status`, not exact.
- glibc functions called by the firmware (`snprintf`, `strtod`) use the host's stack
  without being counted; their chip counterparts' stack is not modelled.

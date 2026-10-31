# Animated custom faces restart the clock

Status: Fixed in 1.9.0, confirmed on the clock (2026-10-31)

Date: 2026-10-31

## Summary

On the owner's clock (1.8.0, build bed88cb, 192.168.66.229), showing `retro-runner`
restarts the clock, and so does `flip-clock-wide` after its flip animation has played.
`night-lake` does too: with the faces in the rotation, the clock restarted again after
every reboot. At 08:03 the four faces were deleted through the API as soon as the clock
answered; it then ran the built-in faces without restarting (heap 15.3-16.4 KB).
All three move things: retro-runner and flip-clock-wide every frame (`fps`,
`{time.ms}`), night-lake its sun and moon over the day. All three draw paths and repaint
the moved parts in 5 KB off-screen strips. None crashes in the emulator, whose heap is fixed at 30,000 B and
whose stack is not limited.

## What is known

- `/status` after the restart: `resetReason` "Software/System restart", heap 15.6-16.2 KB
  free, `heapLow` 14.6-15.4 KB. The firmware calls `ESP.restart()` only from the settings,
  WiFi, format, update and `/restart` handlers, none of which ran. On the ESP8266 core a
  panic, `abort()` (a failed `new` without nothrow) or a stack overflow (4 KB stack) is also
  reported as "Software/System restart", so this is very likely a crash.
- Face size is not the cause: retro-runner is 3,963 B minified but flip-clock-wide is
  2,628 B. Night-lake is 3,689 B. Aquarium (1,808 B) was not seen crashing, but was
  deleted with the others.
- No log reaches anyone: the clock's USB port is power only, and `/status` keeps only the
  reason string.

## Cause (found in the emulator, 2026-10-31)

With the clock's heap and 4 KB stack ([spec 022](../../specs/022-emulator-device-memory/SPEC.md)),
all three faces overflow the stack while drawing: `renderItems` -> (`repaintBox` ->
`drawItem` ->) `itemBox` -> `num`/`evalNum` -> the expression parser, which recurses about
eight frames per level of parentheses (`(({it.index}-{it.index}%8)/8)*15`). On the chip a
stack overflow is a panic, reported as "Software/System restart". The free heap at the
time was 9-11 KB, so memory was not the cause.

## Next

[Spec 021](../../specs/021-crash-report/SPEC.md) records the crash (cause, address, stack)
in RTC memory and shows it in `/status`, so the address can be decoded against the build.
[Spec 022](../../specs/022-emulator-device-memory/SPEC.md) gives the emulator the clock's
heap and stack, so the crash can be reproduced and traced there.

## Fix

[Spec 023](../../specs/023-renderer-stack/SPEC.md): the expression parser no longer
recurses (its stack use is the same at any depth), `renderItems` keeps its per-item arrays
off the stack and `repaintBox` is no longer inlined into it. On the chip's frames the worst
paths now take about 1.8-2.3 KB of the 4 KB. All 39 example faces run in the emulator with
the clock's memory (integration case `example_faces_memory`). [Spec 021](../../specs/021-crash-report/SPEC.md)
adds crash records and crash-loop protection, so a face that crashes again turns itself
off instead of restarting the clock forever.

## On the clock with 1.9.0

2026-10-31, after the owner installed 1.9.0 (build 9b10951): retro-runner, flip-clock-wide
and night-lake each shown for 90 s (the owner watched flip-clock-wide's animation), no
restart, crash log empty. The least free stack since boot was 1,040 B: about 3,050 B of
the 4 KB used, startup and the core's frames included (the emulator counts 2,752 B of
firmware frames alone for the deepest example face). Sending a face after another big one
had been shown failed for lack of heap, a separate defect:
[issue](2026-10-31-face-upload-needs-the-loaded-face-memory.md).

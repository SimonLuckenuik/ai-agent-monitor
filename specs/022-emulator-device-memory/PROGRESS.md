# Progress

- 2026-10-31: proposed after three faces restarted the clock and none failed in the emulator.
- 2026-10-31: approved by the owner.
- 2026-10-31: 32-bit build (`EMU_BITS`), the arena allocator and the stack depth hook
  (`emulator/memory.cpp`), the mock's heap, stack, reset reason and `restart()` replaced
  (`build.sh`). The three faces that restart the clock reproduce: stack overflow in the
  expression parser under the off-screen repaint (RESEARCH).
- 2026-10-31: the owner asked for live device statistics on the screen page, over the
  WebSocket, in `/status`, and the crash reason: added as goal 5 (`GET :8080/stats`, the
  Device panel, `last-crash.json`, `stackFree` in the firmware's `/status`, +39 B gz).
- Next: the integration case; calibrate `EMU_STACK` once the clock reports `stackFree`.
- 2026-10-31: the integration case runs; 16 example faces overflow at 4,096 B (RESEARCH).
  Waiting for the clock's `stackFree` (next release) to calibrate before the known list
  is final. `/status` on the screen page opens in a window (a frame: the firmware sends
  no CORS header, as on the clock).
- 2026-10-31: with spec 023's fix the integration case passes with an empty known list.
  Also: the emulator calls the firmware's crash callback on a panic, `POST :8080/panic`
  (the page's Crash button), the firmware's crash log on the page, line-buffered output
  for `docker logs`, the Settings button opens one separate tab.

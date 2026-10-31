# Research

- Core host mock (`/esp8266/tests/host/common/MockEsp.cpp`): `getFreeHeap()` returns
  30000, `getMaxFreeBlockSize()` 20000, `getFreeContStack()` 4000. `ArduinoMain.cpp` calls
  `setup()` then `loop()` on the main thread. The Makefile defaults to `FORCE32=1`;
  `emulator/build.sh` passes `FORCE32=0`.
- The core ships `umm_malloc` (`cores/esp8266/umm_malloc/`), the chip's allocator: 8-byte
  blocks addressed by 15-bit indices, so it can run over a host array.
- The clock (1.8.0, 2026-10-31): free heap 15.3-16.4 KB, `heapLow` 14.4-15.4 KB while the
  built-in faces rotate; the chip's stack is 4 KB (`cont`).
- 2026-10-31, the 32-bit build works with `g++-multilib` (build) and `libc6-i386`,
  `lib32stdc++6`, `lib32gcc-s1` (run). glibc exports `__libc_malloc` and friends, so
  `malloc` can be replaced in the executable and still reach the host's.
- The host mock's UDP socket allocates 128 KB in `WiFiUDP::begin()` (DNS server): blocks of
  64 KB or more stay on the host heap.
- A real 4 KB stack for `loop()` overflowed at once in the mock's `wifi_get_ip_info()`
  (glibc's `getifaddrs`). Counting only the firmware's frames (`-finstrument-functions`,
  excluding `/emulator/`, `/esp8266/` and `/usr/`) gives 2.5 KB at most for boot and the
  built-in faces.
- At rest the emulator's firmware uses about 9.8 KB of the arena; with 25,600 B the free
  heap matches the clock's 15.7 KB.
- Under that heap and a 4 KB stack, retro-runner, flip-clock-wide and night-lake (the three
  that restart the clock) overflow the stack: 4,128-4,160 B, in `renderItems` ->
  (`repaintBox` -> `drawItem` ->) `itemBox` -> `num`/`evalNum` -> the expression parser,
  which recurses about 8 frames per parenthesis level. Aquarium, not seen crashing on the
  clock, overflows here too (4,160 B): the host's frames may be somewhat larger than the
  chip's, or aquarium also crashes there. `stackFree` from the clock settles it.
- What a custom face takes while it repaints: a 5 KB strip sprite (`BAND_PX` 2560 px), 3.2 KB
  for a path's points (`MAX_PTS` 400), the parsed face (`customDoc`), and the night tint.
- Integration case `example_faces_memory` (2026-10-31, 4,096 B counted): 16 of 39 example
  faces overflow: the 4 known (aquarium, flip-clock-wide, night-lake, retro-runner) and
  bauhaus, binary-clock, block-world, calendar, countdown, flip-clock, golden-hour, orbit,
  pomodoro, sky-window, sun-arc, what-to-wear. Only three were seen restarting the clock
  (the others were never loaded there), so either the renderer is that close to the
  limit, or the host's frames are larger than the chip's. `stackFree` from the clock
  decides `EMU_STACK`.

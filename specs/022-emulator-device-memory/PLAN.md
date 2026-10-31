# Plan (draft)

1. Try the 32-bit build first (`FORCE32=1`, 32-bit libs in `emulator/Dockerfile`); keep
   64-bit if it cannot work, and note why in RESEARCH.
2. Heap: build the core's `umm_malloc` for the host over a static arena; route `malloc`,
   `calloc`, `realloc`, `free`, `new` and `delete` of the firmware to it (link wrappers in
   `emulator/`). Allocations made by the host mock itself (sockets, files) stay on the
   host heap so they do not count against the clock's.
3. Patch the mock's `getFreeHeap`, `getMaxFreeBlockSize`, `getHeapFragmentation` and
   `getFreeContStack` in `build.sh` (as `mockWrite` already is) to read the arena and stack.
4. Stack: run `setup()`/`loop()` on their own stack (`ucontext`) and count only the
   firmware's frames (`-finstrument-functions`; the host mock and glibc under it are far
   deeper than on the chip, decision 2026-10-31); over `EMU_STACK`, or in the guard page
   of the 1 MB host stack, log a backtrace and panic.
5. Calibrate: with the clock's built-in face 7 and the same settings, the emulator's idle
   heap should be within 1 KB of the clock's `/status`; the stack size from a face that
   runs close to the limit on both.
6. `GET :8080/stats`, a text message on `/ws` each second, the screen page's Device
   panel, `/data/last-crash.json` (written by the panic, or by `run.sh` for any other
   crash); `stackFree` in the firmware's `/status`.
7. Integration case: show every example face for 20 s, fail on a restart. Expect
   retro-runner, flip-clock-wide and night-lake to fail.
8. Docs: emulator README, CLAUDE.md (emulator numbers are now real), STATUS.

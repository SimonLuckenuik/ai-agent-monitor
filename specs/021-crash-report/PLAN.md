# Plan

1. `src/crash.cpp`/`.h`: the RTC record (magic, version, fields of goal 1), written by
   `custom_crash_callback(rst_info*, stack, stack_end)`; the stack scan keeps words in
   0x40100000-0x40110000 (IRAM) and 0x40201000-0x40300000 (flash). RTC user memory: clear
   of the boot marker (slot 32) and the emulator's use.
2. At boot, after storage mounts: a new record goes to `/crash.log` (JSON lines, last 8),
   the loop count and level update (kept in the RTC record, and in the log for power cuts),
   then the record is marked copied.
3. Protection: level 1 skips custom faces wherever a face is chosen (rotation, the
   selected face, the night face, `show`); level 2 makes `loop()` skip `faceDraw` and show
   the startup screen with "Safe mode". 10 minutes up clears the count.
4. API: `/status` `crashes`, `lastCrash`, `safeMode`; `GET /api/crashes`,
   `POST /api/crashes/resume`, `POST /api/crashes/clear` (signed in). Settings page,
   System: a Crashes card (the log, Resume, Clear, "Copy for decoding").
5. Releases: `release.yml` publishes `AIAgentMonitor_v<version>.elf`;
   `scripts/decode_crash.py`.
6. Emulator: `chipPanic` calls `custom_crash_callback` with a matching `rst_info`.
7. Tests: unit (record, log rotation, levels), safety (callback in IRAM-safe code, no
   allocation in it), integration (a face that panics three times turns custom faces off;
   two more give safe mode; Resume).
8. Docs: integrations API, settings, updates and recovery, STATUS.

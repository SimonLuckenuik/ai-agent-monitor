# Progress

- 2026-10-31: proposed after retro-runner and flip-clock-wide restarted the clock.
- 2026-10-31: approved by the owner, with a crash log, crash-loop protection (custom faces
  off, then safe mode) and decoding against each release's ELF added to the scope.
- 2026-10-31: built. `src/crash.cpp` (record in RTC slot 40, `/crashes.bin`, levels),
  `/status` fields, `/api/crashes` (the file as it is, the owner's suggestion; decision
  2026-10-31), Resume and Clear, the safe screen, System > Crashes on the settings page,
  `scripts/decode_crash.py`, releases attach the `.elf` (`-g` in platformio.ini: debug
  info in the ELF only). Checked in the emulator: 3 panics turn custom faces off, 5 give
  safe mode, Resume. Integration case `crash_loop_protection`. +3 KB compressed (97%).
  Not yet run on the clock.
- 2026-10-31: on the clock with 1.9.0: `/api/crashes` answers 204 with the key, no crash
  so far; the record path has not run on the chip yet.
- 2026-11-03: closed. The record path has now run on the chip: the clock logged a panic (no face
  drawing) before 2.1.1, shown in `/status` `lastCrash`; the owner clears it from the
  settings page. Crash-loop protection was seen in the emulator only (no loop on the clock).

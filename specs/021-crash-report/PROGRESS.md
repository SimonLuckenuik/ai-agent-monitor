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

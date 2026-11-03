# 036 Progress

- 2026-11-03: written after the flash analysis; the owner agreed to goals 2 and 3 and asked
  whether zopfli is backward compatible: it is (uzlib, as the bootloader runs it, unpacks
  the zopfli image of 2.1.0 to the same bytes).
- 2026-11-03: built, all three goals. Room 8,192 B -> 28,672 B (`firmware.bin` 597,104 ->
  589,344 B, `.bin.gz` 433,260 -> 418,987 B).
  - Goals 1 and 3 together: 8,192 -> 20,480 B. `scripts/minify_page.py` (terser 5.36.0
    through npx, or from PATH in the emulator's image; CSS and markup trimmed) and zopfli
    0.4.3 (15 iterations, about 4 s) for both the page and the image; the page is 23,879 B
    gzipped instead of 27,531 B. `tests/eboot_inflate.c` in the safety tests unpacks each
    build's `.bin.gz` as eboot does and compares; it fails on a corrupted file.
    `tests/web/minified_page.test.js`: the script parses and the 24 handlers the markup
    calls are still defined.
  - Goal 2 changed shape: newlib's `localtime_r` calls `tzset` on every call, so a parser
    for `configTime` alone would not unlink `sscanf`. `src/tz_rule.h` parses the rule
    and converts UTC itself (`gmtime_r` never calls `tzset`); the firmware's four local
    time conversions use it (`clockLocal`), and `configTime(0, 0, ntp)` only starts SNTP.
    `_tzset_unlocked_r` and `vfscanf` are gone from the ELF: 20,480 -> 28,672 B.
    `tests/tz/tz_rule_tests.cpp` matches glibc for all 413 zones the page gives a rule
    and 13 hand-written rules (angle-bracket names, J and day-number dates, negative and
    past-24 h times), every 15 minutes of 2026-2027; glibc's default for a rule without
    dates differs from newlib's (its posixrules file), so those are compared with the
    US rule newlib assumes. The `tz` setting now refuses a rule it cannot read.
  - The emulator: its image installs terser and zopfli; `fw_main.cpp` keeps the host's TZ
    in step with the rule the firmware parses, for its own time controls.
- 2026-11-03: released in 2.1.1; the owner installed it (the zopfli image, through the
  update page) and it works, the time right in EST from our own rule code.

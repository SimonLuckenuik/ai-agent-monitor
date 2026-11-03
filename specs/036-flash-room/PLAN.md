# 036 Plan

1. Page minification (goal 3): `scripts/minify_page.py` turns `web/index.html` into the
   embedded page: terser 5.36.0 through `npx` on the script (compress, mangle local
   names), comments and indentation out of CSS and markup. Used by
   `scripts/build_options.py` and the emulator's `gen_headers.py`. Fails the build when
   Node is missing. CI and the release install Node (they already have it for the page
   tests).
2. Zopfli (goal 1): `zopfli` (pinned) in `build_options.py` for the `.bin.gz` and the
   page; `tests/eboot_inflate.c` (uzlib from the core, eboot's flags) in the safety
   tests, unpacking the built `.bin.gz` and comparing.
3. The TZ parser (goal 2): `src/tz_rule.h`, a header-only parser into newlib's
   `__tzinfo_type` and `_timezone`, `_daylight`, `_tzname`; `configTime` with an offset
   then our rule; unit tests on the host (Zig) against newlib's `tzset` for every rule
   `tz_rules.test.js` produces plus edge cases (`<+0545>-5:45`, `/h` times, `Jn`, `n`).
4. Measure after each step; docs (footprint, testing, CLAUDE.md's setup, CHANGELOG).

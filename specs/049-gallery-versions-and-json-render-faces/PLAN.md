# 049 Plan

Status: Draft, waiting for approval.

1. **The version table.** Read the CHANGELOG from 2.0.0 to 2.7.0 for every face-format
   addition (item types, fields, fonts, placeholder groups and names); write the table
   into `gen_marketplace.py` with the version and the CHANGELOG section it comes from.
   Check: RESEARCH.md lists what was found.
2. **Generator.** `"firmware"` computed per face (R1-R3); the `"specs"` list (R8); the
   `--check` rules for specs (6 KB, the shape `clock.py check` accepts, a preview, a
   description). Check: `--check` passes; the computed versions match a hand count for a
   few faces (umbrella, kpi, flip-clock); CI's `--check` step still runs.
3. **Settings page** (`web/index.html`): the clock's version from `/status`, the
   comparison, "Needs <v>", the `specs` list under its category (R4, R8). Check: a web
   test for the comparison (`-dev` included); the page in the emulator with the gallery
   served from the branch; screenshots at an old and a new version.
4. **`clock.py`** `gallery` and `install` (R5).
5. **The faces** (R7, R7a, R9), the first set first: written against the catalog, checked in the emulator one
   by one (`clock.py push`, screenshot), values in `samples.json`. Check: no warnings,
   under 6 KB, looked at on screen; the owner looks at them on the clock.
6. **Integration and previews** (R10): the `json_render` case and `scripts/screenshots.py`.
7. **Docs** (R6, R11): the API reference (gallery format), the skill's SKILL.md and
   api.md, `face-format.md` regenerated, the OpenClaw copy,
   `examples/json-render/README.md`, CHANGELOG, plugin version bump.
8. Unit, web, safety and the full integration suite; the settings page on the clock.

Release: open question 1 (with 2.7.0, or after it).

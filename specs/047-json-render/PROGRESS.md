# 047 Progress

## 2026-11-07

- Asked to scope json-render support on the clock (from the session that runs the
  OpenClaw work). Spec 046 is taken by branch `ap-timeout`, so this is 047.
- Read json-render's README, the core and image package READMEs, and npm metadata
  (0.22.0, Apache-2.0, depends only on zod). RESEARCH.md has the findings.
- Prototype: `prototype/translate.mjs` (no dependencies) compiled
  `prototype/sample-build.json` (1,882 B) into `prototype/face-build.json` (1,275 B,
  11 items). `clock.py check`: OK. Pushed to the Docker emulator (2.3.1) as `jr-demo`
  with variables, and captured `build-running.png` and `build-fail.png`. The live
  update (border, dot, bar hidden) worked with no new upload. Then the face and its
  variables were deleted from the emulator.
- Native parsing (option c) quantified: 8-11 KB of code (14-19 KB of room) against
  10.7 KB raw and 4 KB in whole sectors at 2.3.1, and 9-12 KB of heap against about
  14 KB free. Ruled out.
- Draft written: SPEC.md (recommendation: option b, host only, and only if there is a
  json-render producer; otherwise record and do not build), PLAN.md, RESEARCH.md.

Not verified:

- json-render's own `validateSpec()` and `catalog.prompt()` against a clock catalog.
  Installing `@json-render/core` was refused by this session's sandbox. Needs a
  session where npm installs are allowed.
- Text widths in the prototype are estimates. Nothing ran on a real clock (no flash
  and no sign-in, as required).

Left: the owner's decision and answers to the open questions.

## 2026-11-07 (later): direction changed by the owner

- The owner, in the chat: "Willing to ignore the available space for now and to drop
  features if it means supporting a standard rendering. Could even be a fork or two
  different builds. Full catalog exposed (non interactive, just display considering
  the device as no input) + support as much rendering as possible."
- Decision record: docs/decisions/2026-11-07-json-render-native.md.
- SPEC.md rewritten for native support: `GET /api/catalog`, json-render specs accepted
  by `POST /api/faces`, compiled on save into our face items (the renderer is
  unchanged), and a separate build that can leave out OpenClaw, MQTT and mDNS. The host
  compiler of the first draft became the prototype of the on-device compiler. PLAN.md
  is now phases 0-3.
- Read the two standard catalogs (`@json-render/image`: 9 display-only components;
  `@json-render/shadcn`: 36, of which about 15 are display ones) into RESEARCH.md, and
  sized phase 1 at about 12-16 KB of code plus 3-4 KB of gzipped catalog.

Not verified: every size is an estimate until phase 0 measures the builds and the heap.
The `repeat` shape comes from third-party mirrors of the docs.

Left: the owner's answers to the open questions (separate build or not, keep our format,
phase 2, the shadcn scope, `/vars/` paths, which features leave), then approval.

## 2026-11-07 (evening): emulator prototype and the A/B test

The owner: "can we start by implementing with the emulator and adjust the space
accordingly in the emulator just to see it in action and then you do the test you
propose". Scope narrowed on the way (SPEC.md "Scope decided on the way"): an Ink-based
catalog, no Image, SVG `Icon` with several paths, an OpenClaw `clock.catalog` later.

Built (emulator only; `SMARTCLOCK_JSONRENDER=1` in `emulator/build.sh`, 0 elsewhere):

- `src/jsonrender.cpp`/`.h`: spec → face compiler on the clock. Flex layout (direction,
  gap, padding, margin, grow, shrink, px/%/cells, align, justify, wrap, absolute),
  `$state`/`$template`/`$cond`/`visible`, CSS and terminal colours, 22 Ink display
  components, 4 clock components, image and shadcn display components accepted.
  `state.vars` seeds variables that are not set yet. The reply lists approximations.
- `GET /api/catalog` and `/api/catalog/prompt`: from `integrations/json-render/catalog.json`
  through `scripts/gen_jr_catalog.py` (`include/jr_catalog.h`, `prompt.md`).
- Renderer item cap 64 in this build (32 elsewhere); compiled faces up to 8 KB.
- Compiled on the core's stack thunk (6.2 KB from the heap): the first spec overflowed
  the chip's 4 KB stack in the emulator. `emuStackThunk()` in `emulator/memory.cpp`
  models it.

Measured (emulator, which models the clock's heap and stack):

| | |
|---|---|
| Device build (`pio run`) | 602,384 B and 427,336 B: unchanged from 2.3.1 (the feature is compiled out) |
| Stack thunk used | 2,880-3,296 B of 6,200 for specs nesting 3-4 levels |
| Main stack free after compiling | 1,232 B (it was 160 B before the thunk) |
| Heap to save a 3.9 KB, 29-element spec | about 10.8 KB at once (thunk 6.2 KB, the parsed spec, the body) |
| On the clock's heap (25.6 KB arena) | that spec fails with 507. The demo ran with EMU_HEAP = 33,792 (8 KB more), standing for a build without OpenClaw and MQTT: **not measured yet** |
| Spec sizes vs faces (A/B) | json-render 2.2-3.9 KB, face format 1.3-2.5 KB (1.5-2x) |

A/B test (`ab/`): the same three briefs (`ab/BRIEFS.md`), one fresh agent per arm, one
draft each, no screenshots. A wrote our face format from the Claude Code skill. B wrote
json-render from `prompt.md` only. Rendered by `ab/render.py` with the same time,
weather and variables: `ab/sheet.png`.

- The first sheet showed compiler bugs in B: Lucide-style `fill: "none"` icons not
  drawn, no flex-shrink (clipping), the large fonts' `pad` clearing both sides (it hid
  the weather icon), Ink heights scaled as columns, absolute boxes with top and bottom
  not stretched. All fixed. The second sheet shows B as its agent wrote it.
- What it shows (the owner judges): both arms produced correct, readable screens on the
  first try. A's are more composed: centred hero rings, tiles, whole-screen palette
  tints. B's are tidy, aligned rows, with more empty space and less hierarchy (a
  dashboard look), as expected in this spec's earlier discussion. A's agent reported
  hand-computed positions and hidden rules. B's agent reported not knowing how much
  space things take, and no multi-way choice.

Not verified: anything on a real clock (no device build has the feature); the heap a
build without OpenClaw/MQTT really frees; json-render's own `validateSpec()` against the
catalog (npm installs refused here). The OpenClaw `clock.catalog` command is not done.

## 2026-11-07 (late): the owner's review of the pictures

- The owner: "Keep going". Also a follow-up idea, recorded on the roadmap: "AI Agent
  Monitor Studio", a GitHub Pages site for building faces from the catalog with a live
  preview, linked from the settings page and able to send the face to the clock.
- Defect found by the owner: text drawn over a ring wiped part of it, in both arms.
  Issue record: docs/issues/2026-11-07-text-bg-wipes-items-under-it.md. The compiler no
  longer gives text a `bg`. The renderer's off-screen repaint keeps live updates clean,
  checked in the emulator (no stale pixels; repaint 104 ms, full draw 276 ms).
- The owner: "the 12 and min should have been on the same bottom line". The compiler
  centred every text vertically in its line box. Now, in a row with `alignItems:
  flex-end`, texts share a baseline at the bottom of their boxes; with `flex-start`, a
  top line. The A/B sheet was rendered again.

## 2026-11-07 (evening): room with every feature kept; OpenClaw

- The owner: "keep going, also investigate how to reduce size without dropping
  features", then "dont build the json-render device build variant, try to keep
  everything". One firmware; decision record
  docs/decisions/2026-11-07-json-render-one-build.md.
- The device build with `SMARTCLOCK_JSONRENDER=1` did not compile: `jsonrender.h` was
  included inside faces.cpp's anonymous namespace, and the stack thunk's target needs C
  linkage (the thunk's assembly calls it by name; `used` so LTO keeps it). Fixed.
- Measured (RESEARCH.md has the table): +62.8 KB of image, 92 KB over the update limit.
  The catalog and prompt are now stored gzipped (`scripts/gen_jr_catalog.py`, 19 KB to
  7.7 KB) and sent with `Content-Encoding: gzip`; a client without `Accept-Encoding:
  gzip` gets 406 and a hint (checked in the emulator with curl, with and without
  `--compressed`, and with the bridge's client): 80 KB over. LTO of our sources
  (`scripts/lto_options.py`, not used by the build): 2.3.1 gains 20 KB of room;
  json-render is then 60 KB over. A `noinline` lookup helper for props saved nothing
  (reverted).
- The rest (the settings page, catalog and icons as LittleFS files with the update; a
  leaner compiler) is estimated, not built: RESEARCH.md.
- OpenClaw: the bridge's `clock.catalog` (bridge only) and `clock.face.save` for specs
  up to 12 KB, with a test against the fake clock (now serving the prompt gzipped);
  the node's `clock.help` adds a `catalog` link in json-render builds. The OpenClaw
  skill's table lists `clock.catalog`.
- Checks: the standard build unchanged (602,384 B); unit tests 434 PASS, safety tests
  PASS, time zone rules PASS, bridge tests 17 PASS; a spec pushed to the emulator after
  the changes draws as before.

Not verified: LTO on a clock; anything json-render on a clock (it does not fit yet); the
heap problem (unchanged); `validateSpec()`.

## 2026-11-08: on 2.5.0 and spec 048 phase 2 (branch feat/047-json-render)

- The owner: "spec 048 phase 2 + 047". The branch's five 047 commits were cherry-picked
  onto 2.5.0 plus phase 2 (the 048 draft commits it also carried are in dev in their
  final form). Conflicts: the index pages (dev's kept; 047's entries to rewrite), the
  emulator's source list (json-render added, on in the emulator's firmware build), the
  route table (2.5.0's heap one, with 047's Accept-Encoding header).
- The catalog and its prompt are now files that come with the update in builds with
  json-render (/sys/catalog.json.gz, /sys/prompt.md.gz; `gen_jr_catalog.catalog_files()`
  through scripts/sys_files.py); include/jr_catalog.h is gone. /api/catalog streams them
  (503 without them).
- Flash, `pio run` with SMARTCLOCK_JSONRENDER=1: 628,720 B and 442,614 B (36 KB over the
  update limit) with the catalog in flash; with it as files 621,152 B and 434,805 B,
  **20 KB over** (it was 80 KB over on 2.4.0). Without json-render the same base has 56 KB
  of room. LTO measured about 20 KB of room earlier: with it, about at the limit.
- Heap, in the emulator with the clock's arena (25.6 KB; 18.8 KB free at rest with
  json-render): specs of 1.3-3.0 KB save; b-meeting (2.8 KB) and b-ci (4.8 KB) fail
  with 507. The compile holds the request body, the parsed spec (1.5-2 times its text),
  the core's stack thunk (6.2 KB, 2.9-3.7 KB used) and the compiled face at once.
- Open, for the owner: LTO (about 20 KB of room for every build; a run on a clock), a
  size-minded compiler (15-20 KB, estimated), and memory: parse the spec from a file
  instead of the request body, a smaller stack of our own instead of the core's 6.2 KB
  thunk, a filtered parse.

## 2026-11-08: the memory work (the owner: "Memory work first")

- **The emulator under-reports the clock's free heap.** The same 2.5.0 code, Big weather
  face, weather fetched: the owner's clock 24,872 B free (its /status), the emulator 18,248 B
  with the default arena (25,600 B, calibrated on 1.8.0). An arena of about 32 KB matches
  the clock now; the measurements below use both. Recalibrating the default is a separate
  decision (it moves every memory test).
- **A save no longer holds the request body while compiling.** The HTTP handler moves the
  web server's copy of the body into storeFace() (no second copy); a spec is written to
  `/specs/<name>.json` (its source, kept), the String freed, and the compiler parses the
  file.
- **The face is built in place.** The compiler wrote its items, then copied them into the
  whole face (twice the face at once); it now writes the face's head first and the items
  after it, and the String becomes the face.
- **A stack of our own, sized to what layout needs.** Measured (emulator): about 2.1 KB
  plus 384 B per nesting level (depth 2: 2,848 B; 4: 3,616; 6: 4,384; 8: 5,152). The core's
  6.2 KB thunk (with a depth limit of 24) would have overflowed past depth 10. Now: depth
  limit 8 (deeper elements left out, with a warning), a 5.5 KB stack (the same switch as
  make_stack_thunk(), our own buffer, painted: /status jrStack has the deepest use).
- **A bug on the way:** place() called measure() without its depth, so measuring restarted
  at 0 at each level: the stack grew with the whole tree, past any limit. Fixed.
- **Result, on the default arena (25.6 KB):** every sample saves, including b-meeting (2.8 KB)
  and b-ci (4.8 KB), which failed before; a 95-element, 9.9 KB stress spec too; nesting 2-12
  never overflows (at most 5,152 B of 5,632). On the clock-sized arena (32.2 KB) the same.
- Flash: unchanged by this (the json-render build is still 20 KB over the update limit).
- Not verified: the stack on the chip (Xtensa frames are not the emulator's x86 ones; the
  clock's /status jrStack will tell).

## 2026-11-08: link-time optimisation and RAM (the owner: "try lto and leaner compiler after")

- **LTO, measured on this branch** (`scripts/lto_options.py`, still not in `platformio.ini`):

  | Build | Image | `.bin.gz` | Update limit |
  |---|---|---|---|
  | Standard, no LTO | 576,768 | 404,515 | 56 KB of room |
  | Standard, LTO on src/ | 566,608 | 397,843 | 68 KB of room |
  | Standard, LTO on src/ and libraries | 563,344 | 396,109 | **78 KB of room** |
  | json-render, no LTO | 621,712 | 435,613 | 20 KB over |
  | json-render, LTO on src/ | 609,840 | 427,906 | fits, 0.5 KB to spare |
  | json-render, LTO on src/ and libraries | **606,592** | **426,109** | **fits, 3.7 KB to spare** |

- LTO dropped the symbols only json-render's stack-switch asm uses: they are now marked
  `used, externally_visible` (92ec3fa). LTO over the libraries needs `gcc-ar` for their
  archives and leaves ESP8266WiFi out (it lost `SigningVerifier_verify`, used only by the
  core's Updater; forcing it in added 27 KB of BearSSL).
- Checked on the LTO images: IRAM unchanged (28,831 B), RAM +60-280 B, the safety tests'
  update, unpack and layout cases pass, `addr2line` names functions in a crash address
  (with a DWARF warning). **The RAM-string check fails** under LTO: it counts 5,015 B of our
  literals in RAM for the standard build (4,709 B without LTO; limit 4,864), because LTO
  places some strings differently; the json-render build counts 6,269 B (its JSON keys:
  `p["flexDirection"]`...). Adopting LTO means moving about 150 B of literals to flash
  first, or raising the limit.
- **json-render's RAM:** its name tables (components, CSS and ANSI colours, clock groups)
  were pointer arrays and strings in RAM, and its warnings, Clock formats and glyphs were
  RAM literals: 2.5 KB of RAM, all moved to flash (packed PROGMEM names, `F()`). Faces and
  warnings are byte-identical for every sample spec and for a spec made to trigger each
  warning, colour name and Clock format (emulator, old binary against new).
- **Tried and reverted:** a `noinline` member lookup for the 259 `p["key"]` reads: +352 B
  under LTO (GCC already shares them).
- Not verified: an LTO image on a clock (timing, a day of use); the standard build's
  integration tests on an LTO image (the emulator is a host build, no LTO).

- **Adopted** (the owner: "Yes, wire it in"): `platformio.ini` runs `scripts/lto_options.py`
  for the firmware (not the starter); decision record
  [2026-11-08 link-time optimisation](../../docs/decisions/2026-11-08-link-time-optimisation.md).
  The RAM-string check's 5,015 B was one literal: the default palettes (386 B), a C++17
  `inline` PROGMEM variable in config.h, which LTO put in RAM; `static` now, and the check
  counts 4,629 B (4,709 B without LTO). Safety, unit and web tests pass; the starter is
  unchanged (381,600 B). The json-render build still counts more literals than the limit
  (its JSON keys): to fix before json-render ships.

## 2026-11-09: json-render's literals under the RAM limit (the owner: "continue fixing")

- The json-render build now passes the safety tests' RAM-string check: **4,837 B** of our
  literals in RAM (limit 4,864; it was 5,883 B with LTO), so about 1 KB more heap.
- How: the 127 JSON keys json-render reads are one PROGMEM copy each (`K_<key>`, looked up
  with `FPSTR`; the `num`/`px`/`str`/`is` helpers take a flash key); `is()` compares with a
  flash value; String comparisons go through `eq(s, PSTR(...))` (`strcmp_P`), not
  `String == F()`, which builds a String each time (+560 B of flash for the same saving);
  default colours, the face head, the `#%06x` format and some fragments are `F()`.
- Flash: +368 B (606,960 B, 3.3 KB under the update limit).
- Tried and reverted: `== F("...")` on JSON values too (-199 B of RAM for +976 B of flash).
- Checked: faces and warnings byte-identical for every sample spec and the spec that
  triggers each warning (emulator, old binary against new); every sample saves on the
  25.6 KB arena; safety tests pass on both images, unit tests pass.

## 2026-11-09/10: approved, in the firmware build (the owner: "lets do the json-render")

- Branch rebuilt on 2.6.0: the json-render commits cherry-picked onto `dev` (the earlier
  branch, based on phase 2 before release, is `feat/047-json-render-old`); its LTO and
  docs-restore commits were already in 2.6.0.
- `-D SMARTCLOCK_JSONRENDER=1` in the firmware's build flags (`-U` in the starter's);
  `build_options.py` adds the catalog files only for the firmware. 607,168 B and
  426,501 B: 3.1 KB of update room. The starter is unchanged (381,600 B).
- `examples/json-render/`: six specs (ci, meeting, weather, build, metrics, status) and
  `samples.json`. New integration case `json_render`: the catalog routes (406 without gzip,
  the JSON and the prompt), every example pushed and captured, the warnings in the reply,
  the depth warning, a malformed spec, the size limit.
- **Defect found by it:** bodies of 8 KB and more arrived empty (the web server holds a
  body twice), the clock answered OK and deleted the face of that name. Fixed: 507 when a
  body was sent but did not arrive; the spec limit is 6 KB
  (docs/issues/2026-11-10-large-face-body-arrives-empty.md). The 12 KB limit set in the
  prototype was never reached through HTTP; the memory work's 9.9 KB stress spec was sent
  on a fresher heap.
- `/status` `jrStack` is measured on the chip only (the emulator runs the compile on its
  own stack model and logs its use): not checked in the integration case.
- Docs: the API reference's json-render section (the skill's face-format.md from it), the
  skill (SKILL.md, api.md; `clock.py push` takes a spec, `check` checks its shape,
  `catalog [--prompt]`; plugin 0.8.0), the OpenClaw skill and bridge (6 KB), CHANGELOG,
  CLAUDE.md. Version 2.7.0-dev.

## 2026-11-10: on a clock (agentmonitor-ade1f0, 2.7.0-dev, build 846fdd2+)

The owner installed `AIAgentMonitor_v2.7.0-dev-jr.update`; the specs were pushed with the
API key (`clock.py`).

- After the update: files `ok`, heap 24,192 B (low 23,208), stackFree 984 B, the catalog
  and prompt served (3,743 B and 3,939 B gzipped), draw times as 2.6.0.
- The six examples (1.3 to 4.8 KB) all saved (`OK`, no warnings). `jrStack` 4,084 B of
  5,632 after them; heap 23,928 B at rest.
- Shown one after the other: the custom face's slowest draw 230 ms (the full redraw), the
  others about 100 ms; heap 21.0 KB with a spec face on screen, low 19.2 KB; stackFree
  592 B (the core's stack, lowest since boot; 984 B before any custom face was shown).
- Limits as in the emulator: a 13 KB body 507, the face of that name kept; 6.3 KB 400
  ("larger than 6 KB"); 5.9 KB saved; 13 levels of Box saved with the depth warning,
  `jrStack` 5,172 B (460 B to spare: the 8-level cap is what keeps it there).
- No crash records, no restart.
- Not checked: the screens by eye (the clock has no screenshot; jr-ci was left showing), a
  day's run.

- The owner, looking at the clock: the specs look as in the emulator, but Home lab
  flickered while the limit tests ran. Cause: every face save or delete redrew the face on
  screen (since 1.2.0); now only a save of that face does
  (docs/issues/2026-11-10-face-save-redrew-shown-face.md). +48 B (607,200 B).

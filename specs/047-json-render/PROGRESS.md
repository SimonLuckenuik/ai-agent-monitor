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

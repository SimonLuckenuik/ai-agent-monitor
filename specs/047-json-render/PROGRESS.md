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

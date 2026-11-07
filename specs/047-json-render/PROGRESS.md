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

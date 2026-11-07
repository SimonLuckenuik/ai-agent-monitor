# 047 json-render documents on the clock

Status: Draft

## Problem

[json-render](https://github.com/vercel-labs/json-render) (Vercel Labs, Apache-2.0,
`@json-render/core` 0.22.0) is a "generative UI" framework. A developer declares a
**catalog** of components with zod prop schemas. `catalog.prompt()` turns the catalog
into a system prompt. The model then writes a JSON **spec**: a flat map of elements,
each with a `type`, `props` and `children`. `validateSpec()` checks the spec, and a
renderer (React, Vue, React Native, Ink, Satori images and others) draws it from
registered components only.

The question is whether agents should be able to send the clock a json-render document
in place of, or as well as, our own custom-face JSON. If so, what would it cost, and
where should the work live (firmware, plugin, OpenClaw bridge)?

What was looked at and measured is in [RESEARCH.md](RESEARCH.md). In short:

- json-render's **structure** (a tree of typed components, data binding, visibility)
  maps onto our face format. Its React-side concerns do not: actions, `emit`,
  `$bindState`, `setState`, `watch`, input validation, streaming into a live UI,
  `$computed` functions. They have no meaning on a 240×240 screen with no input.
- The real difference is **layout**. A json-render document says Row, Column, gap and
  padding, and the renderer places things. Our faces give every item pixel
  coordinates. The renderer's flex layout is the thing an agent would gain, and it is
  also what the ESP8266 can least afford.
- A [prototype](prototype/) compiler (one file, no dependencies) translates a
  json-render spec built from a small clock catalog into a face. It turned a 1,882 B
  document into a 1,275 B face that `clock.py check` accepts. The emulator draws that
  face correctly and updates it live from variables
  ([build-running.png](prototype/build-running.png),
  [build-fail.png](prototype/build-fail.png)).

## Options

| | What | Firmware | Fit | Cost |
|---|---|---|---|---|
| **a** | A host-side **compiler**, json-render spec → face JSON, with a fixed clock vocabulary | none | Good for anything that is a face; layout done on the host | Small: ~400-600 lines of JS plus tests |
| **b** | **a**, plus a real **clock catalog** built with `defineCatalog` and zod, so agents use json-render's own `catalog.prompt()` and `validateSpec()` | none | Best: the json-render tooling works unchanged, and the compiler only receives valid specs | **a** plus a catalog file and a dependency on `@json-render/core` and `zod` (Node) |
| **c** | **Native** parsing on the ESP8266: element map, flex layout, `$state`/`$cond` | +8-11 KB of code, so 14-19 KB of update room | Poor | Does not fit: 2.3.1 has ~10.7 KB of raw room (4 KB in whole sectors), and a document that size needs ~10-12 KB of heap, with ~14 KB free at rest |
| **d** | **Render to an image** on the host (`@json-render/image`: Satori, then resvg, then JPEG) and upload it as a photo | none | Narrow: static only (no live time or weather, no variables); every update rewrites flash; full redraw | Satori, resvg and a JPEG encoder on the host |

Option **c** is out; the numbers are in RESEARCH.md. Option **d** is a fallback for a
rich, one-off picture, not a face. It is listed so it is not proposed again.

## Recommendation

**b, in two steps, at no firmware cost, and only if the owner has a use for it.**

What an agent gains over writing our face format directly, honestly:

1. **Layout without pixel arithmetic.** Rows, columns, gap, padding, `justify:
   between` and `grow`. This is where hand-written faces most often go wrong:
   overlapping text, things off screen, forgetting `bg`/`pad` so text smears. The
   compiler also fills `bg` and `pad` for live text from the enclosing container,
   which agents often forget.
2. **The json-render toolchain.** An agent or app that already produces json-render
   for a web UI can target the clock with the same prompt machinery, validation and
   auto-fix (`autoFixSpec`). One spec can be drawn on a dashboard and on the clock.
3. **Validation before upload** with precise issues, from zod.

What it does not give:

- **Anything the clock cannot already draw.** Every component compiles to an item
  that exists today. The face format stays the source of truth and stays the only
  thing the firmware reads.
- **Smaller uploads.** A json-render spec is about 1.5 times the size of the face it
  becomes (ids, the element map, `$state` objects).
- **Runtime reflow.** Layout is fixed when the spec is compiled. A text whose value
  grows at run time (`{repo}`) stays in the box it was given. The catalog's
  `maxChars` prop sizes that box.
- **The expressive parts of our format:** expressions in coordinates (animation),
  paths, sprites, gradients, palettes chosen by expression, `repeat` over numbers.
  These need an escape hatch (a `Face` or `Item` component that carries raw items), or
  the agent writes our format directly for such faces.

Step 1 (small): the catalog, the compiler and a CLI (`compile doc.json > face.json`,
`--push NAME`), with unit tests and an integration case that compiles a sample
document and captures it in the emulator. Step 2 (optional): a `clock.face.render`
command on the OpenClaw bridge (Node, the natural home: branch `openclaw-node`,
`integrations/openclaw/`), and a section in the Claude Code and OpenClaw skills.

If the owner has no json-render producer in mind, the recommendation is to **record
this and not build it**. The gain then is layout alone. That can also be had by
teaching the skill a few layout recipes, or by adding a small `layout` helper to
`clock.py`, without a second face language to keep in step with the firmware.

## Goals (to agree)

1. An agent can produce a json-render spec from a published **clock catalog**,
   validate it with json-render's own tools, and have it shown on the clock as a
   custom face.
2. No firmware change: no flash, no heap, and the face format remains the only
   format the clock reads.
3. Live values keep working: json-render `$state`, `$template`, `$cond` and `visible`
   become our placeholders, expressions and `show`, so the face is saved once and
   updated with `/api/faces/vars`.
4. Errors are clear before anything reaches the clock: an unknown component, an
   unsupported dynamic value, more than 32 items, more than 4 KB, or a layout that
   leaves the screen.

## Non-goals

- Parsing json-render on the ESP8266 (option c).
- Actions, events, input, `$bindState`, `setState`, `watch`, `$computed` functions,
  and the shadcn/React component sets. The clock has no input.
- Streaming onto the screen. SpecStream patches can be applied on the host, but only a
  finished spec is compiled and sent: the clock cannot draw a half-built face, and
  each save writes flash.
- Arbitrary state arrays for `repeat`. Our variables are scalars. Only the clock's own
  lists (forecast days, messages) and counts can repeat.
- Replacing our face format, its schema or its skill reference.

## Requirements (draft, for step 1)

1. The catalog: `Screen` (root: background, padding, gap), `Column`, `Row`, `Card`
   (background, border, radius, padding, gap, align, justify, grow, width, height),
   `Spacer`; and leaves that are one item each: `Text` (`size` sm/md/lg/xl → font 2,
   font 4, big 36, big 60; `font` for the digit fonts; `maxChars`, align, color),
   `Clock` (a `time.`/`date.` format), `Icon` (weather icon name), `Glyph` (SVG path
   data), `Dot`, `Progress` (bar), `Ring`, `Sparkline` (a `$history` variable),
   `Image` (a photo's file name), `Divider`. One escape hatch, `Item`, carries a raw
   face item at the position the layout gives it.
2. State paths: `/name` is a variable (one level only); `/time/hm`, `/now/temp`,
   `/day0/high`, … are the clock's own placeholders (the group names in
   integrations-api.md). The compiler rejects unknown clock names, using the same
   list as `clock.py check`.
3. Dynamic values: `$state` (with `fallback` → `{name|text}`), `$template` (`${/path}`
   and `${/path|fallback}`), `$cond` with `eq`, `neq`, `gt`, `lt`, `not`, `$and`,
   `$or` → `cond?a:b`. Element `visible` → item `show`. Anything else is an error that
   names the element.
4. Face-level fields with no json-render counterpart (`when`, `duration`, `palettes`,
   `maps`, `fps`, `invert`) are given as props on `Screen`.
5. Text measurement uses the clock's real glyph widths (fonts 2 and 4 from TFT_eSPI's
   tables, the big font's 16.9-unit cells, the digit fonts' advances), not estimates
   as in the prototype.
6. Output passes `clock.py check`; the compiler fails above 32 items or 4,096 B.
7. Tests: unit tests for each component and dynamic value, a golden test (spec → face),
   and an integration case that pushes a compiled sample and captures it.

## Behaviour

- `node integrations/json-render/compile.mjs doc.json > face.json`, or `--push NAME
  [--show]` with `CLOCK_URL` and `CLOCK_API_KEY` as `clock.py` uses them.
- `catalog.mjs` exports the catalog. `node integrations/json-render/prompt.mjs` prints
  `catalog.prompt()` for agents that have no skill.
- Step 2: the OpenClaw bridge gains `clock.face.render {name, spec, show}`.

## Budget

- Flash and RAM: none (host only).
- Host: Node 20 or later, `@json-render/core` (Apache-2.0, depends only on `zod`).
  The package is 0.x: 36 versions since January 2026, 0.22.0 on 2026-11-04. Pin the
  version, and expect to follow API changes.
- Upkeep: a new face item type or placeholder group means a catalog change. Add the
  catalog to "Things to keep in step" in CLAUDE.md.

## Open questions

1. **Is there a json-render producer?** An app, a dashboard or an agent framework
   that already emits json-render. Without one, the gain is layout only, and the
   recommendation is to record this spec and not build it.
2. Where should the compiler live: `integrations/json-render/` (Node, beside the
   OpenClaw bridge, which can import it), or a Python port in the plugin's `clock.py`
   (no Node, but no zod catalog either, so option a only)?
3. Should the catalog take json-render's standard names (`Heading`, `Text`, `Box`,
   `Row`, `Column`, `Divider`, `Spacer`, as in `@json-render/image`), so a spec written
   for the image renderer also compiles for the clock, or clock-specific names
   (`Clock`, `Ring`, `Sparkline`) only where there is no standard one?
4. The escape hatch (`Item`): allow it, so animation and paths stay possible, or keep
   the catalog pure and send such faces in our own format?

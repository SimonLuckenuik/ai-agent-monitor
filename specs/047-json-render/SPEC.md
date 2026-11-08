# 047 json-render on the clock

Status: In progress: an emulator prototype (the owner, 2026-11-07: "can we start by
implementing with the emulator and adjust the space accordingly in the emulator just to
see it in action and then you do the test you propose"). The spec as a whole is not
approved; the device build is unchanged.

## Scope decided on the way (2026-11-07, the owner in the chat)

These supersede the goals and requirements below where they differ.

- **The catalog follows `@json-render/ink`'s display components** ("maybe something
  similar to ink without the input components"): Box, Text, Heading, Newline, Spacer,
  Divider, Badge, Spinner, ProgressBar, Sparkline, BarChart, Table, List, ListItem, Card,
  KeyValue, Link, StatusLine, Metric, Callout, Timeline, Markdown. Lengths are character
  cells: a cell is 6 px across (40 columns), a row 16 px. Left out: TextInput, Select,
  MultiSelect, ConfirmInput, Tabs.
- **`@json-render/image` is dropped as the target, and so is `Image`** ("willing to drop
  image and support a path a la svg"). Image-style specs (Frame, Row, Column, lengths in
  px) and shadcn's display components still draw, but the published catalog leaves them
  out.
- **Clock components:** `Clock`, `WeatherIcon`, `Ring`, and `Icon`. `Icon` takes SVG path
  data, one path or `paths` with their own fill and stroke, so agents can draw icons
  (Lucide's work as they are).
- **OpenClaw** ("we'll need to expose that to the openclaw node tools as well"): specs
  are saved through the bridge's `clock.face.save` or HTTP, because the native node has
  no heap for a face in a frame (spec 041). Done as below.
- **One firmware, every feature kept** (the owner: "dont build the json-render device
  build variant, try to keep everything", and "investigate how to reduce size without
  dropping features"). The separate build in Design below is set aside
  (docs/decisions/2026-11-07-json-render-one-build.md). Measured: the prototype is 92 KB
  over the update limit; with the catalog gzipped (done) and LTO (measured) 60 KB over.
  Read-only data as LittleFS files that come with the update (a spec of its own) and a
  leaner compiler would make it fit: RESEARCH.md, "Room for json-render with every
  feature kept".
- **OpenClaw, done in the prototype:** the bridge has `clock.catalog` (bridge only: the
  prompt, fetched gzipped from the clock) and `clock.face.save` takes a spec up to
  12 KB. The node's `clock.help` adds a `catalog` link (the release's `prompt.md` on
  GitHub) in json-render builds; no new node command, so no flash for one.
- **Compiled on a stack of its own.** Layout recurses as deep as the spec nests. The
  emulator caught a stack overflow on the chip's 4 KB stack on the first spec. Specs are
  now compiled on the core's stack thunk: 6.2 KB taken from the heap while compiling, as
  BearSSL uses it. The emulator models the thunk (`emuStackThunk` in
  `emulator/memory.cpp`).

## Problem

[json-render](https://github.com/vercel-labs/json-render) (Vercel Labs, Apache-2.0,
`@json-render/core` 0.22.0) is a "generative UI" standard. A **catalog** declares
components with zod prop schemas. `catalog.prompt()` turns it into a system prompt, the
model writes a JSON **spec** (a flat map of elements: `type`, `props`, `children`), and a
renderer draws it using registered components only. There are renderers for React, Vue,
React Native, Ink, PDF, email and images. One of them, `@json-render/image`, is
display-only by design: a fixed-size frame, flex boxes, text and images, with no input.

Today an agent that wants a custom screen on the clock has to learn our own face format
(25 KB of reference, pixel coordinates). The owner wants the clock to speak this
standard instead:

- the clock **publishes its catalog**: every component it can draw, for display only,
  since it has no input;
- the clock **accepts and renders a json-render spec** as a face, with as much of the
  standard as it can draw;
- **flash is not the constraint** for this spec. The owner will drop features, or ship a
  separate build or a fork, to get there.

The first draft of this spec (same date) recommended compiling on the host because of
flash. The owner set that aside. That draft's findings are kept in
[RESEARCH.md](RESEARCH.md), and its prototype compiler in [prototype/](prototype/). The
prototype shows that the mapping works: a spec drawn in the emulator and updated live
from variables.

## Goals (to agree)

1. `GET /api/catalog` on the clock returns its full catalog: every component with a
   description and its props as JSON Schema, the state paths it can read, the functions
   it offers, and the prompt text. An agent can generate against it with no other
   documentation. A matching `catalog.mjs` (zod) is published for json-render's own
   tools (`catalog.prompt()`, `validateSpec()`, `autoFixSpec()`).
2. `POST /api/faces?name=…` takes a json-render spec as well as our face JSON. It is
   told apart by `root` and `elements`. The face then behaves like any custom face:
   rotation, `show`, `when`, the night face, backup, gallery.
3. **Standard first.** `@json-render/image`'s components are drawn with their own names
   and props (`Frame`, `Box`, `Row`, `Column`, `Heading`, `Text`, `Image`, `Divider`,
   `Spacer`). A spec written for the image renderer at 240×240 shows on the clock with
   no change. Then the display parts of `@json-render/shadcn` (`Card`, `Stack`, `Grid`,
   `Separator`, `Badge`, `Alert`, `Progress`, `Table`, `Avatar`, `Spinner`, `Skeleton`),
   and the interactive ones drawn in their resting state. Then clock components the
   standard lacks: `Clock`, `WeatherIcon`, `Icon` (SVG path), `Ring`, `Sparkline`,
   `Sprite`.
4. **Live data the json-render way.** `$state` paths read the clock (`/time/hm`,
   `/now/temp`, `/day0/high`, `/msg/title`, …) and pushed variables (`/vars/<name>`).
   `$template`, `$cond`, `visible` and `repeat` (over `/days`, `/msgs`) work. A spec's
   `state` seeds variables that are not set yet. Values change without a new upload, as
   today, with no flicker for anything that has a fixed box.
5. **Fidelity is written down.** Everything in the catalog is either drawn, or drawn
   approximately with the approximation stated in its description. Nothing in the
   standard is silently dropped: an unknown component draws as a labelled box and is
   reported in the save's reply.
6. Our own face format keeps working, so the 40+ example faces, animation by
   expression, paths and gradients are not lost.

## Non-goals

- Input of any kind: actions, `on`/`emit`, `setState`, `watch`, `$bindState`,
  `$bindItem`, form validation (`checks`). These are accepted and ignored (reported as
  warnings), so a spec made for a web page still shows.
- Arbitrary frame sizes drawn 1:1. A `Frame` other than 240×240 is scaled to fit, and
  text picks the nearest size the clock can draw.
- Fetching images from URLs (no TLS budget, no RAM for remote JPEGs). `Image.src` names
  a photo on the clock; a URL draws a placeholder box with the `alt` text.
- SpecStream on the clock (phase 3 at the earliest). Live values go through variables,
  which already update faces without a new save.
- Replacing our face format.

## Design

**Compile on save, draw with the existing renderer.** On `POST /api/faces`, the clock:

1. parses the spec (ArduinoJson; the flat element map needs the whole document);
2. builds a compact node array (type, parent, first child, layout props: about 24 B
   per element) and checks it (unknown types, cycles, dangling children);
3. lays it out: a flex subset (direction, gap, padding and margin per side, `flex`
   grow, width and height in px or %, `alignItems`, `justifyContent` including
   `space-between`/`space-around`, `wrap`, `position: absolute` with
   top/left/right/bottom). It measures text with the real fonts (`textWidth`), and live
   text by its current value or a `maxChars` hint;
4. emits the face as items in our format (rects for backgrounds and borders, text with
   `bg`/`pad` from the enclosing box, bars, rings, paths …) and saves both the compiled
   face and the source (`GET /api/faces/get` returns the source; backup keeps the
   source and recompiles on restore).

The renderer, the flicker-free redraws, placeholders, expressions, maps and the night
tint stay exactly as they are: no second drawing engine. The heap peak is at save time
only.

**Phase 2: run-time relayout.** In json-render, an element that turns invisible takes
no space, and text that grows pushes its neighbours. Compile-on-save gives fixed boxes:
a hidden element leaves a gap. Phase 2 keeps the node array (not the JSON) for the face
on screen. When a `visible` condition or a measured value changes, it lays out again and
repaints the boxes that moved (`repaintBox`). It costs heap on every face (about 24 B
per element, plus items) and is the most expensive part. It is worth doing only if
phase 1 shows the gaps matter.

**A separate build.** `env:esp12e_jr` (name to agree) with `SMARTCLOCK_JSONRENDER=1`,
and options to leave out features, so the standard build is not held to json-render's
size and json-render is not held to 4 KB of room:

| Can leave the json-render build | Flash (footprint.md) |
|---|---|
| OpenClaw node (spec 041) | 22.7 KB |
| MQTT and Home Assistant discovery | about 7 KB |
| mDNS | about 4.5 KB |
| geekmagic-hacs compatibility | to measure |
| Our face format (only if the owner wants a json-render-only clock) | parts of the 32 KB engine: the loader and checks, not the drawing |

Both builds share settings, storage layout and the update path. Each release would
publish two `.bin.gz` files. The firmware reports its build in `/status` and the
catalog, so agents and the skill can tell which one they reach.

## Requirements (draft)

1. `GET /api/catalog` (no sign-in, like `/status`): gzipped JSON in flash, generated at
   build time from `integrations/json-render/catalog.mjs` (zod 4's `z.toJSONSchema`),
   with `version`, `build`, `screen: {width: 240, height: 240, input: false}`,
   `components`, `state` (paths and examples), `functions`, `prompt`.
   `GET /api/catalog/prompt` returns the prompt as text.
2. `POST /api/faces?name=<n>` with a json-render spec: 200 with
   `{"ok": true, "warnings": [...]}`; 400 with the element id and the reason for a spec
   that cannot be drawn at all; 507 when short of memory. Limits (json-render build):
   source up to 12 KB (to measure), up to 96 elements, compiled face up to 64 items.
3. Colours: `#rgb`, `#rrggbb`, `rgb()`, `rgba()` (alpha blended at compile time against
   the known background), CSS named colours (the 16 basic ones and the common greys at
   least), `transparent`, and our `@role` names.
4. Text: fonts 2 and 4 for `fontSize` up to about 28; the line font (`"big"`, any size)
   or the digit fonts above that. `fontWeight: bold` → stroke weight 2 on the line font.
   `textDecoration` underline/line-through as lines. Wrapping inside the box (`w`).
   Accents folded to ASCII (`asciiFold`). Italic and `letterSpacing` ignored (warned).
5. Each catalog entry's description states what the clock does with it
   (approximations included), so the prompt is honest.
6. `$state` paths and clock fields come from one table (`fieldValue()`'s groups plus
   `/vars/*`). `catalog.json`, `clock.py`'s `FIELDS` and the unit tests read the same
   list.
7. Tests: host unit tests for the layout (golden spec → items), the integration suite
   runs every catalog example in the emulator, the face preview renders them, and a
   check that spec → image renderer PNG and spec → clock look alike (manual at first).

## Budget

Flash is not a constraint (owner, 2026-11-07); it is still measured and recorded per
phase. RESEARCH.md has the estimates: phase 1 (image catalog, layout, colours, catalog
endpoint) about 12-16 KB of code plus 3-4 KB of gzipped catalog; the shadcn display set
about 6-8 KB more; phase 2 about 3-4 KB more. Heap is the real limit. A save needs
the source in an ArduinoJson tree (1.5-2 times its text) plus the node array. The
compiled face then lives in `customDoc` as faces do today, larger than 4 KB. That
needs measuring in the emulator (it models the clock's heap) before limits are set.
The heap freed by leaving out OpenClaw and MQTT is what makes room.

## Open questions

1. ~~Separate build or replace?~~ Answered: one build, every feature kept.
2. Keep our face format in the json-render build (recommended: the examples, animation
   and the skill depend on it), or json-render only?
3. Phase 2 (run-time relayout): plan for it now, or decide after phase 1 on the clock?
4. The shadcn set: display components only, or also interactive ones drawn in their
   resting state (Button as a label pill, Switch as a toggle, Slider as a bar, Tabs
   showing the default tab)?
5. State path for variables: `/vars/<name>` (clear, recommended) or `/<name>` (shorter,
   but it can clash with clock groups)?
6. ~~Which features leave the json-render build?~~ None (one build).
7. Adopt LTO for the firmware (about 20 KB of room for everyone; needs a run on a
   clock)? And a spec for read-only data as LittleFS files that come with the update?

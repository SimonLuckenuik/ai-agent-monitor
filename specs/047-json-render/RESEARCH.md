# 047 Research: json-render and the clock

Looked at on 2026-11-07: the repository's README, the `@json-render/core` and
`@json-render/image` READMEs, and npm metadata. Installing the packages to try
`validateSpec` against the prototype catalog was refused in this session's sandbox
(external code), so **nothing here was run against json-render itself**. The prototype
compiler reads the spec format as the READMEs document it.

## json-render in brief

- **Licence:** Apache-2.0 (compatible with use from GPL-3.0 tooling; nothing is linked
  into the firmware in any option except c).
- **Packages:** `@json-render/core` 0.22.0 (2026-11-04; 36 versions since 0.0.1 on
  2026-01-14; depends only on `zod` ^4). Renderers: React, Vue, Svelte, Solid, shadcn
  (36 components), React Native, Ink, Next.js, Remotion, React PDF, React Email,
  `@json-render/image` (Satori: SVG, and PNG with `@resvg/resvg-js`), Chat SDK cards,
  React Three Fiber. Also a YAML wire format, devtools, MCP Apps, state adapters.
- **Spec:** flat. `root` names an element id, and `elements` maps ids to
  `{type, props, children: [ids], visible?, watch?}`. A spec may carry its own `state`.

  ```json
  {"root": "card-1", "elements": {
    "card-1": {"type": "Card", "props": {"title": "Hello"}, "children": ["button-1"]},
    "button-1": {"type": "Button", "props": {"label": "Click me"}, "children": []}}}
  ```
- **Catalog:** `defineCatalog(schema, {components: {Name: {props: z.object(...),
  description}}, actions: {name: {description}}})`. `catalog.prompt({system,
  customRules})` writes the system prompt. `defineRegistry(catalog, {components})` binds
  implementations, and `<Renderer spec registry/>` draws.
- **Dynamic props:** `{"$state": "/a/b"}`; `{"$template": "Hi ${/user/name}"}`;
  `{"$cond": <condition>, "$then": x, "$else": y}`; `{"$computed": "fn", "args": {}}`
  (functions registered at run time); `{"$bindState": "/form/x"}` (two-way, for
  inputs). In a repeat: `{"$item": "field"}`, `{"$index": true}`.
- **Conditions** (`visible`, `$cond`): `{"$state": "/p"}` is truthy; `"not": true`;
  `eq`, `neq`, `gt` (and similar); `$and`, `$or`; an array is an implicit AND.
- **Actions:** components call `emit`. `setState` is built in (`statePath`, `value`).
  `watch` runs actions when a state path changes.
- **Streaming:** SpecStream is RFC 6902 JSON Patch, one operation per line
  (`{"op":"add","path":"/elements/btn-1","value":{...}}`).
  `createSpecStreamCompiler().push(chunk)` gives partial specs as tokens arrive.
- **Validation:** `validateSpec(spec)` gives issues (structure beyond the zod props:
  `invalid_visible`, `repeat_without_children`, …). `autoFixSpec(spec)` repairs what it
  can and marks each fix lossy or lossless. `formatSpecIssues` formats them for a
  model to read.

## What maps, what does not

| json-render | Clock face | Notes |
|---|---|---|
| element tree, `type`, `props` | items, drawn in order | Containers vanish: they become positions, plus a `rect` when they have a background or border |
| Row / Column / Box: gap, padding, align, justify, grow | absolute `x`, `y`, `w`, `h`, `align` | **Computed on the host.** The clock has no layout engine |
| Text, Heading | `text` with font 2 / 4 / big / digit fonts | Sizes snap to the clock's fonts; ASCII only (the clock folds the rest) |
| Image | `image` (a photo already on the clock) | URLs: no (the clock does not fetch images) |
| Divider, Spacer | `line`, nothing | |
| (clock-specific) Clock, Icon, Ring, Progress, Sparkline, Dot, Glyph | `text {time.*}`, `icon`, `ring`, `bar`, `spark`, `circle`, `path` | One item each |
| `$state` | `{name}`, `{group.field}` | Variables are one level (`/build`); clock values by group (`/now/temp`) |
| `$template` | text with placeholders | `${/x\|fb}` becomes `{x\|fb}` |
| `$cond`, `visible` | expressions `c?a:b`, item `show` | `eq neq gt lt not $and $or` all have operators |
| `repeat` + `statePath` array, `$item`, `$index` | `repeat` `of: days / msgs / N`, `{it.*}`, `{it.index}` | Only the clock's own lists: variables are scalars |
| spec `state` (initial values) | `POST /api/faces/vars` after the save | The compiler can send them as the first push |
| `$computed` | expressions, partly | Registered JS functions: no |
| actions, `emit`, `setState`, `watch`, `$bindState` | none | No input on the clock. Reject |
| SpecStream | none | Compile the finished spec. A partial face is not worth a flash write |
| styling: arbitrary px font sizes, weights, borders, shadows, opacity | fonts 2/4/6 and sized big/digit fonts; `rect` border; no alpha | Snap or reject |
| (no counterpart) `when`, `duration`, `palettes`, `maps`, `fps`, `invert`, gradients, sprites, animation by expression | face fields and items | `Screen` props, or the `Item` escape hatch |

## The prototype

`prototype/translate.mjs`, about 130 lines with no dependencies, handles `Screen`,
`Row`, `Column`, `Card`, `Spacer`, `Text`, `Clock`, `Icon`, `Dot`, `Progress`, `Ring`
and `Sparkline`. It does a two-pass flex layout: measure, then place, with `gap`,
`padding`, `align`, `justify` (start, center, end, between) and `grow`. It also
translates `$state`, `$template`, `$cond` and `visible`.

`prototype/sample-build.json` is a CI status card with the time, the weather, a card
whose border and dot follow `/build`, a progress bar shown only while running,
pass/fail counts and a load sparkline.

| | Bytes (minified) |
|---|---|
| json-render spec | 1,882 |
| compiled face | 1,275 (11 items) |

The face passed `clock.py check`. Pushed to the Docker emulator (2.3.1 firmware), it
drew as intended, and changing `build` from `running` to `fail` recoloured the border
and dot and hid the bar with no new upload (the two PNGs). Text widths in the
prototype are estimates (7 px a character for font 2, 14 px for font 4). The real
compiler needs the fonts' width tables (requirement 5). After the screenshots, the
demo face and its variables were deleted from the emulator.

Observations from writing it:

- Live text needs a fixed box. The prototype's `maxChars` sizes it, and the box's
  background becomes the text's `bg`. `pad` is then derived from the box. This makes
  updates flicker-free by construction.
- json-render has no concept for `maps`. The prototype passes them through, which is
  better done as `Screen` props.
- The flat element map is easy for a host to walk, and costly for the clock (below).

## Option c: native on the ESP8266, quantified

**Room.** 2.3.1 is `firmware.bin` 602,384 B plus `firmware.bin.gz` 427,337 B =
1,029,721 B, against 1,040,384 B (1 MB less two sectors). That leaves **10,663 B raw**,
and **4,096 B** once both files are rounded up to whole 4 KB sectors (docs/STATUS.md).
New code costs about **1.7 times** its size: it is in the image, and about 0.7 times in
the compressed file.

**Code needed** (estimates from the sizes of similar parts in
docs/specifications/footprint.md: the face loader 3.2 KB, expressions 4.6 KB, `resolve`
3.9 KB):

| Part | Code |
|---|---|
| Element map walk by id, recursion, checks | 1.5 KB |
| Flex layout (measure and place, gap, padding, align, justify, grow) | 2.5-3 KB |
| Dynamic props: `$state`, `$template`, `$cond`, `visible` → existing placeholders and expressions | 1.5-2 KB |
| Component dispatch and props → item fields, defaults | 2-3 KB |
| JSON keys and error messages | 0.5-1 KB |
| **Total** | **8-11 KB, so 14-19 KB of room** |

That is 1.3 to 1.8 times all the raw room left, and 3.4 to 4.6 times the room in whole
sectors. It would need features taken out (for example the OpenClaw node at 22.7 KB, or
mDNS), for a capability the host already provides at no cost.

**Heap.** Free heap at rest is about 14 KB (footprint.md). A face today is read as a
4 KB document. The same face as json-render is about 1.5 times larger (above), so 6 KB
for a full 32-item face. A flat id map cannot be walked as a stream: the whole
document has to be in an ArduinoJson tree at once, about 1.5-2 times its text, so
**9-12 KB**, plus the layout's own state. That is most of the free heap, during an
HTTP request that already holds its buffers. A 507 ("short of memory") would be the
normal answer.

**Draw time.** Layout would run on every load and on every value change that resizes a
box, on a chip where a full face redraw is already the visible cost.

Conclusion (first draft): no, as long as flash is the limit. On 2026-11-07 the owner set
flash aside and chose native support (separate build allowed). The sections below the
prototype's work size that direction.

## Option d: render to an image on the host

`@json-render/image` renders a spec to SVG with Satori, and to PNG with resvg. A
240×240 image converted to JPEG (a few KB) could go to `POST /photo/upload` and be
shown by an `image` item or the Photo face. This gives arbitrary styling, real fonts
and accents, which the clock's fonts lack.

It loses everything live: the time, the weather, variables, `when`. Every change
rewrites a file in flash (wear: the HACS integration is already asked to stay at 30 s
or slower). An image redraws the whole face, so it flickers. The host also needs
Satori, resvg and a JPEG encoder. It is useful for a rich one-off card (a chart, a
summary), not for a face. If the owner wants it, it is a separate, small spec.

## Options weighed for where the compiler lives

- **Node, `integrations/json-render/`:** uses `@json-render/core` and zod directly, so
  the catalog is the real thing (`catalog.prompt()`, `validateSpec()`). The OpenClaw
  bridge (`integrations/openclaw/`, Node) can import it. Recommended.
- **Python, in `clock.py`:** no Node needed, but no zod catalog, so json-render's
  tooling cannot validate against it. It would have to re-implement validation. The
  plugin's scripts are standard-library only by design.
- **Firmware:** option c.

## The standard catalogs (for native support)

json-render has no single standard catalog. Each renderer package ships its own. Two
matter here.

### `@json-render/image` (display-only, the base)

From `packages/image/src/catalog.ts` (2026-11-07). Every prop is nullable.

| Component | Props | On the clock |
|---|---|---|
| `Frame` (root) | `width`, `height`, `backgroundColor`, `padding`, `display`, `flexDirection`, `alignItems`, `justifyContent` | Face `bg`. A size other than 240×240 is laid out at its own size, then scaled |
| `Box` | `padding`, `paddingTop/Bottom/Left/Right`, `margin`, `backgroundColor`, `borderWidth`, `borderColor`, `borderRadius`, `flex`, `width`/`height` (number or string, e.g. `"50%"`), `alignItems`, `justifyContent`, `flexDirection`, `position` (`relative`/`absolute`), `top/left/right/bottom`, `overflow` | `rect` (fill, border, radius); layout. `overflow: hidden` cannot clip (warned) |
| `Row` | `gap`, `alignItems`, `justifyContent` (incl. `space-between`, `space-around`), `padding`, `flex`, `wrap` | layout |
| `Column` | `gap`, `alignItems`, `justifyContent`, `padding`, `flex` | layout |
| `Heading` | `text`, `level` h1-h4, `color`, `align`, `letterSpacing`, `lineHeight` | text: h1/h2 line font, h3/h4 font 4 |
| `Text` | `text`, `fontSize`, `color`, `align`, `fontWeight`, `fontStyle`, `lineHeight`, `letterSpacing`, `textDecoration` | text: nearest font; bold on the line font; underline as a line; italic and spacing ignored |
| `Image` | `src`, `width`, `height`, `borderRadius`, `objectFit` | `image` for a photo on the clock (JPEG scale 1/2/4/8); a URL draws a placeholder |
| `Divider` | `color`, `thickness`, `marginTop`, `marginBottom` | `line` |
| `Spacer` | `height` | layout |

All of it can be drawn with today's items. The new work is the layout (margins, %,
wrap, absolute), CSS colours, and mapping font sizes.

### `@json-render/shadcn` (36 components, web)

From `packages/shadcn/src/catalog.ts`:

- **Display, drawable:** `Card` (title, description), `Stack` (direction, gap sm-xl,
  align, justify), `Grid` (columns, gap), `Separator`, `Heading`, `Text` (variant
  body/caption/muted/lead/code), `Image`, `Avatar` (initials in a circle), `Badge`
  (variant), `Alert` (type info/success/warning/error: icon, title, message), `Progress`
  (value, max, label), `Skeleton`, `Spinner` (an animated ring), `Table` (columns, rows:
  font 2, ruled), `Carousel` (the first item).
- **Hidden by default, drawn as nothing:** `Dialog`, `Drawer` (open by state),
  `Tooltip`, `Popover` (their trigger text only).
- **Interactive, can be drawn in their resting state:** `Button`, `Link`, `Toggle`,
  `ToggleGroup`, `ButtonGroup`, `Input`, `Textarea`, `Select`, `Checkbox`, `Radio`,
  `Switch`, `Slider`, `Tabs`, `Accordion`, `Collapsible`, `DropdownMenu`,
  `Pagination`. They show a value, a label or a choice, and that has meaning on a
  display.

Names overlap between the two sets (`Text`, `Heading`, `Image`, with different props).
The clock's catalog would be their union: `Text` accepts `fontSize` and `variant`, so
specs written for either renderer draw.

### What core does not give

No JSON or JSON Schema export of a catalog is documented. `catalog.prompt()` is the only
output. Zod 4 has `z.toJSONSchema()`, so a build script can turn our `catalog.mjs` into
the JSON the clock serves. Going the other way (JSON Schema back to zod, so an agent's
json-render tools can use the clock's served catalog) has to be checked: zod 4's
`fromJSONSchema`, or the published `catalog.mjs`. The shape of `repeat` is only partly
documented: `{"repeat": {"statePath": "/list", "key": "id"}}`, with `$item` and
`$index` inside, from third-party mirrors of the docs. Check it against the source
before building.

## Native support, sized (owner's direction, 2026-11-07)

Estimates from comparable parts in footprint.md: the face loader 3.2 KB, expressions
4.6 KB, items and text 11.2 KB. Measure in phase 1.

| Part | Code | Notes |
|---|---|---|
| Parse, node array, checks, warnings | 2-2.5 KB | flat map → nodes (about 24 B each), cycles and dangling ids |
| Flex layout: direction, gap, padding/margin per side, grow, px and %, align, justify (5 values), wrap, absolute | 4-5 KB | integer pixels; two passes (measure, place) |
| Emit items (rect, text with bg/pad, line, image, bar, ring, path) | 2-3 KB | into the face format the renderer already reads |
| CSS colours (`rgb()`, `rgba()` blended, names) | 1-1.5 KB | name table about 0.5 KB for 30 names |
| Font mapping, wrapping hints, `asciiFold` | 1 KB | `textWidth` is already linked |
| `$state` / `$template` / `$cond` / `visible` / `repeat` → placeholders, expressions, `show`, repeat | 1.5-2 KB | the target syntax exists |
| `/api/catalog`, source kept beside the compiled face, backup | 1 KB code + 3-4 KB gzipped data | data does not compress again, so it counts twice in room |
| **Phase 1 total** | **about 12-16 KB code, 3-4 KB data** | **about 25-35 KB of update room** |
| shadcn display set (+ resting interactive) | 6-8 KB | mostly composites of existing items |
| Phase 2: run-time relayout | 3-4 KB | plus heap per face on screen |

Room comes from a build without OpenClaw (22.7 KB), MQTT (7 KB) and mDNS (4.5 KB): about
34 KB of code, roughly 58 KB of room with the 1.7 factor, plus today's 10.7 KB raw.
Phase 1 and the shadcn set fit in that. Heap: OpenClaw and MQTT both hold buffers when
active, and leaving them out returns heap. The amount is to be measured in the
emulator before setting the limits (12 KB source, 96 elements, 64 compiled items are
placeholders).

## Room for json-render with every feature kept (measured, 2026-11-07)

The owner, after the A/B pictures: "dont build the json-render device build variant, try
to keep everything", and "investigate how to reduce size without dropping features". So
the separate build (and leaving OpenClaw, MQTT or mDNS out) is off the table: one
firmware, everything in it, plus json-render. These are measurements of the prototype
built for the clock (`SMARTCLOCK_JSONRENDER=1` with `pio run`; nothing flashed).

**What the budget is.** An update must fit the image and its `.bin.gz` together in the
1 MB sketch area, two sectors spare, each rounded up to whole 4 KB sectors. "Over" below
is how many bytes, in sectors, the pair overshoots. A byte of code costs about 1.7 bytes
of that room (it is in the image, and in the `.bin.gz` at about 0.7). A byte of data
that is already compressed (the settings page, the gzipped catalog) costs 2: zopfli
cannot shrink it again.

| Build | Image | `.bin.gz` | Room or over |
|---|---|---|---|
| 2.3.1 as it is | 602,384 | 427,330 | 4 KB of room |
| + json-render, catalog and prompt as text (first try) | 665,216 | 463,935 | **92 KB over** |
| + json-render, catalog and prompt gzipped | 654,272 | 466,193 | 80 KB over |
| 2.3.1 with LTO | 592,384 | 420,537 | 24 KB of room (+20 KB) |
| + json-render, gzipped catalog, LTO | 642,336 | 457,900 | **60 KB over** |

What json-render adds (symbol sizes, without LTO): the catalog and prompt text 19 KB (in
7.7 KB gzipped), `leaf()` 8.4 KB and `leafInk()` 6.0 KB (one case per component, String
heavy), `place()` 4.0 KB, `measure()` 2.8 KB, `jrCompileHere()` 2.8 KB, `value()` 1.9 KB,
the rest small: about 35 KB of code. The earlier estimate (12-16 KB) was for a careful
implementation; the prototype was written for speed.

### Levers, measured

| Lever | Saves | State |
|---|---|---|
| **Catalog and prompt stored gzipped**, sent with `Content-Encoding: gzip`; a client that does not send `Accept-Encoding: gzip` gets 406 and a one-line hint (`curl --compressed`). Browsers, Node's `fetch`, `requests` take it | image -11 KB, `.bin.gz` +2.3 KB: **12 KB of room** | done (`scripts/gen_jr_catalog.py`) |
| **Link-time optimisation of our own sources** (`-flto` on `src/` only; the core and SDK stay as they are, or their link order breaks). The core's linker script places code in flash by file name (`*.cpp.o`); LTO's objects are `*.ltrans.o`, so the script needs that pattern too, or the code lands in IRAM and does not fit | 2.3.1: image -10.0 KB, `.bin.gz` -6.8 KB (**20 KB of room**); with json-render -11.9 KB and -8.3 KB | measured; `scripts/lto_options.py`, not used by the build. Needs a run on a clock (crash decoding, IRAM, timing) before any release uses it |
| `noinline` lookup helper for the ~200 `p["key"]` reads in jsonrender.cpp | +16 B: GCC already calls them | tried, reverted |

### Levers, estimated (not built)

| Lever | Would save | Cost and risk |
|---|---|---|
| **The settings page in LittleFS** (24.4 KB gzipped today, the largest single item): the image keeps a small fallback page (sign in, WiFi, firmware update, about 4 KB) for the setup AP and recovery; the full page is a file. It must come with the update: the `.bin.gz` followed by the files, the update handler writing the files to LittleFS and the image to the staging area. A clock updated by an older firmware lacks the files until the next update, and shows the fallback page | about 20 KB from each: **about 40 KB of room**, for every build, not only json-render | Its own spec: update format, recovery path, the stock-update first install, tests in the emulator |
| The json-render catalog and prompt as LittleFS files the same way | 7.7 KB from each: **about 15 KB** | same mechanism |
| Weather icon data (`WI_DATA` 7.5 KB) and the Message face's JSON (3.9 KB) as files | **about 15-20 KB** | same mechanism; they are drawn often, so read speed to check |
| A size-minded rewrite of the compiler: components as a table (defaults, colours as `uint32_t`, fonts) instead of one String-heavy case each; colours as numbers until written out | 30-40% of 35 KB: **about 15-20 KB of room** (estimate) | the prototype's behaviour stays; the A/B specs and the emulator screenshots are the regression check |
| The prompt generated on the clock from the catalog, not stored | 3.9 KB gzipped, less about 1.5 KB of code: **about 6 KB** | small |

**Where that leaves it.** LTO and the gzipped catalog bring json-render to 60 KB over.
The settings page as a file (40 KB) and a leaner compiler (15-20 KB) bring it to about
zero; the catalog and icons as files give 30-35 KB of margin. So json-render fits with
every feature kept, but only with an "assets in the filesystem" update (a spec of its
own, useful to every future feature) and LTO, both of which change the standard
firmware and need the real clock to check. Without them it does not fit, whatever the
compiler does: even a 15 KB compiler with no catalog would use the 4 KB of room twice
over.

Heap was not changed by any of this. A 3.9 KB spec needs about 10.8 KB at once (6.2 KB
of it the stack thunk) and fails with 507 on the clock's 25.6 KB arena; leaving
OpenClaw and MQTT out was the plan to make room, and is now off the table. Options to
measure: compile when the face is saved without the current face loaded (frees
`customDoc`, up to 8 KB), parse with an ArduinoJson filter, or a smaller thunk now that
layout keeps its arrays on the heap (3.3 KB used of 6.2 KB).

## Links

- https://github.com/vercel-labs/json-render (README, packages/core, packages/image)
- https://www.npmjs.com/package/@json-render/core
- docs/specifications/integrations-api.md (the face format),
  docs/specifications/footprint.md (flash and heap)

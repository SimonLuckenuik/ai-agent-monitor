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

Conclusion: no.

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

## Links

- https://github.com/vercel-labs/json-render (README, packages/core, packages/image)
- https://www.npmjs.com/package/@json-render/core
- docs/specifications/integrations-api.md (the face format),
  docs/specifications/footprint.md (flash and heap)

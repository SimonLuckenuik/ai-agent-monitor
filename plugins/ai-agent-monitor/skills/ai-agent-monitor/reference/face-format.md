# Face format

A face is a JSON object: a background, up to 32 items drawn in order, and optional
`fps`. Copied from `docs/specifications/integrations-api.md` in the firmware
repository (SimonLuckenuik/ai-agent-monitor), which is the source of truth.

## Face JSON

```json
{
  "bg": "#101820",
  "items": [
    {"type": "rect",   "x": 10, "y": 10, "w": 220, "h": 220, "r": 18, "color": "#1c2733"},
    {"type": "text",   "x": 120, "y": 40, "text": "Solar now", "font": 4, "color": "#9aa0a6", "align": "mc"},
    {"type": "text",   "x": 120, "y": 100, "text": "{solar}", "font": 6, "color": "#ffd60a", "align": "mc", "bg": "#1c2733", "pad": 200},
    {"type": "bar",    "x": 30, "y": 170, "w": 180, "h": 14, "value": "{pct}", "max": 100, "color": "#30d158", "track": "#2c3a48"},
    {"type": "text",   "x": 120, "y": 215, "text": "{time}", "font": 2, "color": "#ffffff", "align": "mc", "bg": "#1c2733", "pad": 200}
  ]
}
```

Coordinates are pixels from the top left of the 240×240 screen. Colours are
`"#rrggbb"`, `"#rgb"`, an RGB565 number or, where noted, a [gradient](#gradients).

| `type` | Fields (defaults) |
|---|---|
| `text` | `x`, `y`, `text`, `font` (4), `color`, `align` (`tl`), `bg` (none = transparent), `pad` (0); `w` wraps to that width, `lines` (8) and `lh` (2, extra line spacing) |
| `rect` | `x`, `y`, `w`, `h`, `r` corner radius (0), `color` (or gradient), `fill` (true) |
| `circle` | `x`, `y` (centre), `r` (10), `color` (or gradient), `fill` (true) |
| `line` | `x`, `y`, `x2`, `y2`, `w` width (2), `color` |
| `ring` | `x`, `y` (centre), `r` (50), `w` thickness (8), `value`, `max` (100), `color`, `track`: a progress ring from 12 o'clock |
| `bar` | `x`, `y`, `w` (100), `h` (8), `value`, `max` (100), `color`, `track` |
| `path` | SVG path data drawn into a box: see [Paths](#paths) |
| `icon` | `x`, `y` (centre), `size` (48), `icon` (`"{icon}"`): weather icon code such as `"10d"`, `bg` |
| `image` | `x`, `y`, `src`: a JPEG from the Photos card, by file name |

Every item also takes `show`: `false`, or a condition such as `"{state}==warn"`. A
hidden item is not drawn.

Face-level fields: `bg` (colour or gradient), `items`, and `fps` (4): how often the
face is re-evaluated, 1 to 20. Raise it only for animation.

- **Fonts.** `2` is small text (16 px). `4` is medium text (26 px). `6` (48 px) is
  large **digits only** (`0-9 : . -`). `8` (75 px digits) left the build to save
  flash: it now draws as `6`. Fonts have no degree sign; write `"{temp} {unit}"`, or
  draw a small circle. For larger digits, draw them as paths.
- **`align`** sets which point of the item `x`,`y` refers to: `tl`, `tc`, `tr`, `ml`,
  `mc`, `mr`, `bl`, `bc`, `br` (top/middle/bottom, left/centre/right). For wrapped
  text it places the whole block.
- **Smooth updates.** Only items whose placeholders resolve to something new are
  redrawn. Text with a `bg` colour, rings, bars, and icons with a `bg` are redrawn on
  their own when their content (text, value, colour) changes. `pad` (pixels) clears
  the area around text whose width varies. Anything else that changes, including the
  position, size or visibility of any item, redraws the whole face, which you will see
  as a flicker. There is no RAM for a full-screen buffer.

## Placeholders and expressions

`{name}` in any string field is replaced on the clock. That means `{time}` and the
weather stay current without the face being sent again.

| Placeholder | Example | | Placeholder | Example |
|---|---|---|---|---|
| `{time}` | `9:54` (12/24 h as set) | | `{temp}` `{feels}` | `21` (°C/°F as set) |
| `{hh}` `{mm}` `{ss}` | `9` `54` `26` | | `{hi}` `{lo}` | `24` `15` |
| `{ms}` | `0`–`999`, for animation | | `{unit}` | `C` or `F` |
| `{ampm}` | `PM` (12 h only) | | `{hum}` | `64` |
| `{dow}` `{dow3}` | `Monday` `MON` | | `{wind}` `{wunit}` | `15` `km/h` |
| `{day}` `{month}` `{mon3}` `{year}` | `5` `October` `Oct` `2026` | | `{desc}` `{main}` | `light rain` `Rain` |
| `{date}` | `5 October` | | `{city}` `{icon}` | `Sydney` `10d` |
| `{sunrise}` `{sunset}` | `05:41` `18:12` | | | |

Any other name is a **variable** set with `/api/faces/vars`, or over MQTT. An unknown
variable is empty; `{name|fallback}` shows the fallback instead, for example
`{msg|Waiting for an agent}` until `msg` is pushed (variables are lost on restart).
Weather placeholders show `--` until the first weather update.

**Number and colour fields** (`x`, `y`, `w`, `r`, `value`, `rotate`, `color`, …) may
be a string that, after the placeholders, is an **expression**:

| | |
|---|---|
| Arithmetic | `+ - * / %` and parentheses: `"120+{dx}*2"` |
| Comparison | `== != < > <= >=`; `==`/`!=` compare words as text: `"{state}==warn"` |
| Logic | `&& || !` and `condition ? a : b`: `"{pct}>80?#ff453a:#30d158"` |
| Functions | `sin(deg)`, `cos(deg)`, `min(a,b)`, `max(a,b)`, `abs(a)`, `round(a)` |

A word that is not a number (such as `warn` or `#ff9f0a`) is text. False is `0`, an
empty value, `false` or `off`. Text fields only take placeholders, not expressions.

```json
{"type": "circle", "x": "120+90*sin({ss}*6)", "y": "120-90*cos({ss}*6)", "r": 8,
 "color": "{ss}%2==0?#ffd60a:#ff9f0a"}
```

## Paths

`path` draws [SVG path data](https://www.w3.org/TR/SVG2/paths.html#PathData) (`M L H V C
S Q T A Z`, absolute and relative) from a viewBox into a box on the screen, so icons
from SVG sets such as [Lucide](https://lucide.dev) or
[Material Symbols](https://fonts.google.com/icons) can be used as they are.

| Field | Meaning (default) |
|---|---|
| `d` | The path data, or an array of them drawn as one shape |
| `vb` | The viewBox, `"minX minY width height"` (`"0 0 24 24"`) |
| `x`, `y`, `align` | Where the box goes; `align` as for text (`tl`) |
| `size`, or `w` and `h` | The box size in pixels (48) |
| `rotate` | Degrees clockwise around the box centre (0) |
| `fill` | Colour or gradient; `false` or `"none"` for no fill. Without `fill` and `stroke`, the shape is filled with `color` (white) |
| `stroke` | Outline colour or gradient; with a stroke and no `fill`, the shape is only outlined |
| `sw` | Stroke width in viewBox units (2, as in Lucide) |
| `rule` | `"evenodd"`; the default is SVG's non-zero rule |

Edges are anti-aliased against the face background (the panel cannot be read back,
so a path over another item blends with `bg`, not with that item). Strokes have round
caps and joins. `<circle>`, `<rect>`, `<line>` and `<polyline>` elements are written as
path data: a circle of radius 10 at (12, 12) is `"M2 12a10 10 0 1 0 20 0a10 10 0 1 0 -20 0"`.

```json
{"type": "path", "x": 120, "y": 90, "size": 96, "align": "mc", "stroke": "#ffd60a",
 "d": ["M2 12a10 10 0 1 0 20 0a10 10 0 1 0 -20 0", "M12 8v4", "M12 16h.01"]}
```

A path holds at most 400 points after curves are flattened; a path that needs more
is cut short.

## Gradients

Where a colour may be a gradient (`bg`, `rect` and `circle` `color`, path `fill` and
`stroke`), give an object:

```json
{"stops": ["#ff5f6d", "#ffc371"], "angle": 135}
```

`stops` holds 2 to 4 colours, spread evenly; `{"from": "#…", "to": "#…"}` is the same
as two stops. `angle` is in degrees as in CSS: `180` (the default) runs top to bottom,
`90` left to right. The gradient spans the item's own box (the whole screen for `bg`).
Text drawn over a gradient background needs its own `bg`, or is drawn transparent.
Gradients cost more to draw than solid colours: keep them off items that change
several times a second.

# Integrations API

[Agent messages](#agent-messages) are always on. Three optional integrations are
each **off by default**; turn them on in the settings page under **Integrations**:

| Integration | What it is for | Who may call it |
|---|---|---|
| [Agent messages](#agent-messages) | A title, a message and an icon on the built-in Message face | **Anyone on the network** |
| [Custom faces](#custom-faces) | Your own faces as JSON, with live values | Signed-in page, or the API key |
| [Home Assistant dashboards](#home-assistant-dashboards-geekmagic-hacs) | The `geekmagic-hacs` integration pushes rendered dashboards | **Anyone on the network** (it cannot sign in) |
| [MQTT](#mqtt) | Home Assistant discovery; faces and variables over MQTT | Your MQTT broker |

The clock is at `http://<clock>/`, where `<clock>` is its IP address (or
`agentmonitor-XXXXXX.local` on a build with mDNS).

## Agent messages

Always on, and open to **anyone on the network** (no sign-in, no API key): the one
thing any agent or script may do is put a message on the clock. A message is drawn
by the built-in **Message** face: an icon, a title, wrapped text, an optional progress
bar and the time, in the icon's colour.

```bash
curl -X POST http://<clock>/api/message -d '{"id":"tooling","title":"tooling","text":"Waiting for your approval to deploy.","icon":"waiting","show":true}'
```

| Field | Meaning (default) |
|---|---|
| `id` | Who the message is from: a new message with the same `id` replaces the old one (`agent`, at most 15 characters) |
| `title` | One line in the icon's colour (at most 40 characters) |
| `text` | Up to 4 wrapped lines, 3 with a progress bar (at most 160 characters) |
| `icon` | `info`, `success`, `warning`, `error`, `waiting`, `running`, `bell` or `done` (`info`) |
| `color` | `"#rrggbb"` or `"#rgb"`, instead of the icon's colour |
| `pct` | 0–100: draws a progress bar |
| `ttl` | Seconds until the message goes away (600; 5 to 86400) |
| `show` | `true`: show it now. Otherwise it waits for its turn in the rotation |

| Method and path | Does |
|---|---|
| `POST /api/message` | Stores a message (the JSON above) |
| `GET /api/message` | `{"messages":[{"id","title","text","icon","color","pct","ttl","showing"}]}`, `ttl` being the seconds left |
| `DELETE /api/message?id=<id>` | Removes that message; without `id`, all of them |

- Up to **4 messages** at a time, kept in RAM: they are lost on restart. A fifth
  replaces the one that would expire first.
- Each active message is **a turn of its own in the face rotation**, after the built-in
  and custom faces. With rotation off, a message is only seen when sent with `show`.
- While a `warning` or `error` message is on screen, the backlight is at **100%**, even
  at night.
- The fonts are ASCII: accented letters lose their accents (`é` becomes `e`),
  typographic quotes and dashes become plain ones, and other characters become `?`.
  Variables (`/api/faces/vars`) are folded the same way.

| Icon | Colour | | Icon | Colour |
|---|---|---|---|---|
| `info` | blue `#0a84ff` | | `waiting` | yellow `#ffd60a` |
| `success` | green `#30d158` | | `running` | light blue `#64d2ff` |
| `warning` | amber `#ff9f0a` | | `bell` | amber `#ff9f0a` |
| `error` | red `#ff453a` | | `done` | green `#30d158` |

## Custom faces

A face is a JSON object: a background, up to 32 **items** drawn in order on the
240×240 screen, and optional fields for palettes, maps and its turn in the rotation.
Faces are stored on the clock (at most 40, 4 KB each) and join the face rotation after
the built-in faces. Values that change (a status, a percentage, a title) are
**variables**, pushed separately: a face is saved once and then updated with values
only. A JSON Schema for faces, `face.schema.json`, comes with the Claude Code skill
(`plugins/ai-agent-monitor/skills/ai-agent-monitor/reference/`).

### Authentication

Use a signed-in browser session, or the **API key** from Integrations → Custom faces.
Send the key as the header `X-Api-Key: <key>`, or as `?key=<key>`.

### Endpoints

| Method and path | Body | Does |
|---|---|---|
| `POST /api/faces?name=<name>` | face JSON | Saves the face; `&show=1`, or `"show": true` in the JSON, shows it now. An empty body deletes it |
| `GET /api/faces` | | `{"faces":[...], "showing":"<name>", "vars":{...}}` |
| `GET /api/faces/get?name=<name>` | | The face's JSON |
| `POST /api/faces/show?name=<name>` | | Shows the face now |
| `POST /api/faces/delete?name=<name>` | | Deletes the face |
| `POST /api/faces/vars` | `{"name": value, ..., "$option": ...}` | Sets variables (merged with existing ones) |

Face names use 1–20 characters from `a-z 0-9 _ -`.

```bash
curl -X POST "http://<clock>/api/faces?name=power&show=1" -H "X-Api-Key: <key>" -H "Content-Type: application/json" -d @power.json
```

### Variables

```bash
curl -X POST "http://<clock>/api/faces/vars" -H "X-Api-Key: <key>" -d '{"solar": 4.1, "pct": 62}'
```

A variable name is 1–15 characters from `A-Z a-z 0-9 _` (no dots: dotted names are
the clock's own, below). Values are text or numbers, up to 160 characters; `null`
deletes a variable. Up to 40 variables exist at once. Options, for every variable in
the same request:

| Option | Does |
|---|---|
| `"$ttl": <seconds>` | The variables expire after that long, and read as empty again (`{status\|idle}` then shows `idle`). Use it for anything that goes stale when its sender stops |
| `"$persist": true` | Kept across restarts (written to flash at most once a minute). Without it, variables live in RAM and are lost on restart |
| `"$history": true` | Numeric values are also appended to the variable's history (the last 32 values; 4 variables at most), drawn by `spark` items |
| `"$add": true` | Whole numbers are added to the current value instead of replacing it: a counter several agents can share |

`{age.<name>}` is the number of seconds since `<name>` was last pushed (empty if it
never was, or the clock has no time yet).

```bash
curl -X POST "http://<clock>/api/faces/vars" -H "X-Api-Key: <key>" -d '{"build": "running", "$ttl": 900}'
curl -X POST "http://<clock>/api/faces/vars" -H "X-Api-Key: <key>" -d '{"load": 0.72, "$history": true}'
curl -X POST "http://<clock>/api/faces/vars" -H "X-Api-Key: <key>" -d '{"done": 1, "$add": true, "$persist": true}'
```

### Face JSON

```json
{
  "bg": "@bg",
  "palette": "{sun.up}?day:night",
  "palettes": {
    "day":   {"bg": "#0b1d33", "fg": "#ffffff", "accent": "#ffd60a"},
    "night": {"bg": "#000000", "fg": "#8e8e93", "accent": "#5e5ce6"}
  },
  "maps": {"state": {"ok": "#30d158", "fail": "#ff453a", "*": "#8e8e93"}},
  "items": [
    {"type": "text", "x": 120, "y": 30, "text": "{time.hm}", "font": 4, "align": "mc", "color": "@fg", "bg": "@bg", "pad": 20},
    {"type": "repeat", "of": "days", "x": 10, "y": 60, "dx": 56, "items": [
      {"type": "rect", "x": 0, "y": 0, "w": 52, "h": 70, "r": 8, "color": "#1c2733", "border": "{it.index}==0?@accent:#2c3a48", "bw": 2},
      {"type": "text", "x": 26, "y": 8, "text": "{it.wday3}", "font": 2, "align": "tc", "color": "@fg"},
      {"type": "text", "x": 26, "y": 40, "text": "{it.high}", "font": 4, "align": "tc", "color": "@accent"}]},
    {"type": "spark", "x": 12, "y": 150, "w": 216, "h": 40, "var": "load", "color": "#64d2ff", "fill": "#16324a", "bg": "@bg"},
    {"type": "circle", "x": 24, "y": 212, "r": 9, "color": "{build:state}", "border": "#ffffff", "bw": 2},
    {"type": "text", "x": 42, "y": 212, "text": "Build {{build}==fail?FAILED:OK}", "font": 2, "align": "ml", "color": "@fg", "bg": "@bg", "pad": 40}
  ]
}
```

Coordinates are pixels from the top left of the 240×240 screen. Colours are
`"#rrggbb"`, `"#rgb"`, an RGB565 number, a [palette](#palettes) name such as `"@fg"`
or, where noted, a [gradient](#gradients).

**Face fields**

| Field | Meaning (default) |
|---|---|
| `items` | The items, drawn in order (required; at most 32 at the top level) |
| `bg` | Background colour or gradient (black) |
| `fps` | How often the face is re-evaluated, 1 to 20 (4). Raise it only for animation |
| `palette` | Named colours for `@name`: an object, or the name of one of `palettes` (placeholders and an expression allowed, e.g. `"{sun.up}?day:night"`) |
| `palettes` | `{"<name>": {"<colour name>": "<colour>", ...}, ...}` |
| `maps` | Lookup tables for the `:<map>` modifier: `{"<map>": {"<value>": "<result>", "*": "<otherwise>"}}` |
| `when` | The face takes its turn in the rotation only while this is true (`"{build}==fail"`, `"{day0.rain}>60"`). Absent: always |
| `duration` | Seconds on screen per turn (1–3600), instead of the rotation interval |
| `show` | `true`: show the face when it is saved |

**Items**

| `type` | Fields (defaults) |
|---|---|
| `text` | `x`, `y`, `text`, `font` (4), `color`, `align` (`tl`), `bg` (none = transparent), `pad` (0); `w` wraps to that width, `lines` (8) and `lh` (2, extra line spacing) |
| `rect` | `x`, `y`, `w`, `h`, `r` corner radius (0), `color` (or gradient), `fill` (true), `border` (colour), `bw` border width (2) |
| `circle` | `x`, `y` (centre), `r` (10), `color` (or gradient), `fill` (true), `border`, `bw` (2) |
| `line` | `x`, `y`, `x2`, `y2`, `w` width (2), `color` |
| `ring` | `x`, `y` (centre), `r` (50), `w` thickness (8), `value`, `max` (100), `color`, `track`: a progress ring from 12 o'clock |
| `bar` | `x`, `y`, `w` (100), `h` (8), `value`, `max` (100), `color`, `track` |
| `path` | SVG path data drawn into a box: see [Paths](#paths) |
| `icon` | `x`, `y` (centre), `size` (48), `icon` (`"{now.icon}"`): a [Weather Icons](https://erikflowers.github.io/weather-icons/) name such as `"wi-day-rain"`, `color` (accent 1), `bg` |
| `image` | `x`, `y`, `src`: a JPEG from the Photos card, by file name |
| `sprite` | Pixel art: see [Sprites](#sprites) |
| `spark` | A line chart of a variable's history: see [Sparklines](#sparklines) |
| `repeat` | Its own `items`, once per entry: see [Repeat](#repeat) |

Every item also takes `show`: `false`, or a condition such as `"{state}==warn"`. A
hidden item is not drawn: two items with opposite conditions (`"{sun.up}"` and
`"!{sun.up}"`) swap a sun for a moon.

- **Fonts.** `2` is small text (16 px). `4` is medium text (26 px). `6` (48 px) is
  large **digits only** (`0-9 : . -`). `8` (75 px digits) left the build to save
  flash: it now draws as `6`. Text is ASCII (accents are dropped) and there is no
  degree sign: write `"{now.temp} {units.temp}"`, or draw a small circle. For larger
  digits, draw them as paths.
- **`align`** sets which point of the item `x`,`y` refers to: `tl`, `tc`, `tr`, `ml`,
  `mc`, `mr`, `bl`, `bc`, `br` (top/middle/bottom, left/centre/right). For wrapped
  text it places the whole block.
- **Smooth updates.** Only items whose placeholders resolve to something new are
  redrawn. Text with a `bg` colour, rings, bars, and icons, sprites and sparklines
  with a `bg` are redrawn on their own when their content (text, value, colour,
  frame) changes. `pad` (pixels) clears the area around text whose width varies.
  Anything else that changes, including the position, size or visibility of any
  item, a repeat's entries, the palette or the face's `bg`, redraws the whole face,
  which you will see as a flicker. There is no RAM for a full-screen buffer.

### Placeholders

`{name}` in any string field is replaced on the clock, so the time, the weather and
variables stay current without the face being sent again. Names with a dot are the
clock's own; a name without a dot is a [variable](#variables).

| Group | Placeholders (example) |
|---|---|
| `time.` | `hm` (`9:54`, 12/24 h as set), `hour` (`9`; `09` in 24 h), `min` (`05`), `sec` (`07`), `ms` (`0`–`999`, for animation), `ampm` (`PM`, 12 h only), `epoch` (Unix time), `daymin` (minutes since midnight, `0`–`1439`), `zone` (`EDT`) |
| `date.` | `text` (`5 October`), `day` (`5`), `wday` (`Monday`), `wday3` (`MON`), `month` (`October`), `month3` (`Oct`), `monthnum` (`10`), `year` (`2026`), `yday` (`278`, day of the year), `week` (`41`, ISO week), `wdaynum` (`1` Monday to `7` Sunday), `mdays` (`31`, days in the month) |
| `now.` | The current weather: `temp`, `feels` (`21`, °C/°F as set), `hum` (`64`, %), `wind` (`15`, km/h or mph as set), `press` (`1013`, hPa), `clouds` (`75`, %), `code` (`61`, WMO weather code), `icon` (`wi-day-rain`), `text` (`light rain`), `kind` (`Rain`), `age` (minutes since the last update), `err` (the last error, or empty) |
| `day0.` to `day3.` | Today and the next three days: `wday`, `wday3`, `low`, `high`, `feels` (feels-like high), `rain` (chance, %), `wind` (highest), `hum` (mean), `rise`, `set` (sunrise, sunset), `icon` |
| `sun.` | `up` (`1` between sunrise and sunset, else `0`), `next` (`rise` or `set`), `rise`, `set` (`7:21`), `until_rise`, `until_set` (minutes until the next one), `daylen` (minutes of daylight) |
| `moon.` | `phase` (`0` new, `2` first quarter, `4` full, `6` last quarter; `0`–`7`), `lit` (% lit), `age` (days since the new moon) |
| `units.` | `temp` (`C` or `F`), `wind` (`km/h` or `mph`) |
| `loc.` | `city` (`Montreal`) |
| `theme.` | `color1`, `color2`, `color3` (the user's accent colours, `#ffa826`), `night` (`1` during night hours) |
| `msg.` | The agent message on screen (the Message face): `title`, `text`, `icon` (path data), `color`, `bg` (its colour at a quarter strength), `pct`, `ttl`, `id`; and `count` (active messages) |
| `net.` | `ip`, `host` (`agentmonitor-1a2b3c`), `ssid`, `rssi` (dBm), `mqtt` (`1` when connected) |
| `sys.` | `version`, `uptime` (seconds), `heap` (free bytes), `lang` (`en` or `fr`) |
| `age.` | `age.<variable>`: seconds since it was pushed |
| `it.` | Inside a [repeat](#repeat): the entry's fields, `index` and `count` |

Weather placeholders show `--` until the first weather update (and for days the
forecast does not have). Words follow the clock's language (English or French).

**Modifiers** follow the name after `:`, and can be chained (`{date.wday:u:3}`):

| Modifier | Does | Example |
|---|---|---|
| `u`, `l` | Upper, lower case | `{date.month:u}` → `OCTOBER` |
| `+` | A sign on positive numbers | `{now.temp:+}` → `+21` |
| `0N` | Zero-padded to N characters | `{n:03}` → `007` |
| `N` | Cut to N characters | `{title:12}` |
| `.N` | N decimals (0–3) | `{load:.1}` → `0.7` |
| any other word | Looks the value up in that [map](#maps) | `{build:state}` |

**Fallback.** `{name|text}` shows `text` while the value is empty: a variable not
pushed yet, expired, or lost on restart (`{msg|Waiting for an agent}`).

### Expressions

**Number and colour fields** (`x`, `y`, `w`, `r`, `value`, `rotate`, `color`, `frame`,
…) may be a string that, after the placeholders, is an **expression**. In **text**,
an expression goes in braces, with its own placeholders inside:
`"Build {{build}==fail?FAILED:OK}"`, `"{{done}*100/{total}}%"`. Braces around plain
words with no operator are left as they are.

| | |
|---|---|
| Arithmetic | `+ - * / %` and parentheses: `"120+{dx}*2"` |
| Comparison | `== != < > <= >=`; `==`/`!=` compare words as text: `"{state}==warn"` |
| Logic | `&& || !` and `condition ? a : b`: `"{pct}>80?#ff453a:#30d158"` |
| Functions | `sin(deg)`, `cos(deg)`, `min(a,b)`, `max(a,b)`, `abs(a)`, `round(a)`, `rand(n)` (a whole number from 0 to n−1) |

A word that is not a number (such as `warn` or `#ff9f0a`) is text. False is `0`, an
empty value, `false` or `off`. A text result keeps its spaces (`"1?light rain:clear"`),
but cannot contain operator characters such as `:` or `-`.

```json
{"type": "circle", "x": "120+90*sin({time.sec}*6)", "y": "120-90*cos({time.sec}*6)", "r": 8,
 "color": "{time.sec}%2==0?#ffd60a:#ff9f0a"}
```

### Maps

`maps` turns a value into another one: a colour, a label, a number. `"*"` is the
answer for any other value; without it, the value is kept.

```json
"maps": {
  "state": {"ok": "#30d158", "warn": "#ff9f0a", "fail": "#ff453a", "*": "#8e8e93"},
  "label": {"ok": "All good", "warn": "Look soon", "fail": "Broken", "*": "Unknown"}
},
"items": [
  {"type": "circle", "x": 30, "y": 30, "r": 10, "color": "{build:state}"},
  {"type": "text", "x": 50, "y": 30, "text": "{build:label}", "font": 2, "align": "ml"}
]
```

### Palettes

`"@name"` is a colour from the face's palette, anywhere a colour goes (also inside an
expression: `"{pct}>80?@alert:@fg"`). `palette` is either the colours themselves, or
the name of one of `palettes`, chosen by an expression: a whole face changes colour at
once, for day and night, or for a state.

```json
"palette": "{build}==fail?alarm:calm",
"palettes": {
  "calm":  {"bg": "#0b1d33", "fg": "#ffffff", "accent": "#64d2ff"},
  "alarm": {"bg": "#3a0000", "fg": "#ffffff", "accent": "#ff453a"}
}
```

A palette entry may use placeholders (`"accent": "{theme.color1}"`) but not another
`@name`. A colour missing from the palette falls back to the field's default.

### Repeat

`repeat` draws its own `items` once per entry, moving them by `dx`, `dy` each time.
The items' coordinates are relative to the repeat's `x`, `y`.

| `of` | Entries | `{it.*}` |
|---|---|---|
| `"days"` | The forecast days, today first (up to 4) | as `day0.*`: `wday`, `wday3`, `low`, `high`, `feels`, `rain`, `wind`, `hum`, `rise`, `set`, `icon` |
| `"msgs"` | The active agent messages (up to 4) | as `msg.*`: `title`, `text`, `icon`, `color`, `bg`, `pct`, `ttl`, `id` |
| a number, or an expression | That many (0–60) | only `index` and `count` |

`{it.index}` counts from 0 and `{it.count}` is the number of entries. A repeat cannot
contain another repeat. A change in the entries redraws the whole face.

```json
{"type": "repeat", "of": 12, "items": [
  {"type": "line", "x": "120+100*sin({it.index}*30)", "y": "120-100*cos({it.index}*30)",
   "x2": "120+88*sin({it.index}*30)", "y2": "120-88*cos({it.index}*30)", "w": 3, "color": "#8e8e93"}]}
```

### Sprites

`sprite` draws pixel art: one character per pixel, each scaled to a square of `scale`
pixels. Characters not in `palette` (`.` or a space, say) are transparent.

| Field | Meaning (default) |
|---|---|
| `rows` | The rows, top to bottom (at most 64 × 64) |
| `palette` | `{"<character>": "<colour>"}`, up to 16 (colours as anywhere, `@name` included) |
| `frames`, `frame` | Instead of `rows`: a list of frames (each a list of rows), and an expression choosing one (`"{time.sec}%2"`), for animation |
| `x`, `y`, `align` | Where it goes (`tl`) |
| `scale` | Pixels per character, 1–16 (1) |
| `bg` | Clears its box first, so a changing frame redraws alone instead of the whole face |

```json
{"type": "sprite", "x": 120, "y": 120, "align": "mc", "scale": 8, "bg": "#000000",
 "palette": {"r": "#ff453a", "w": "#ffffff"}, "frame": "{time.sec}%2",
 "frames": [[".rr.rr.", "rrrrrrr", "rrwrrrr", ".rrrrr.", "..rrr..", "...r..."],
            ["......", ".r..r.", ".rrrr.", "..rr..", "......", "......"]]}
```

### Sparklines

`spark` draws the values pushed to a variable with `"$history": true`, oldest on the
left.

| Field | Meaning (default) |
|---|---|
| `var` | The variable's name |
| `x`, `y`, `w`, `h` | The box (100 × 30) |
| `min`, `max` | The vertical range (the values' own) |
| `color` | The line (white) |
| `fill` | A colour under the line (none) |
| `bg` | Clears the box first, so new values redraw it alone |

### Rotation: `when` and `duration`

Custom faces take turns in the rotation after the built-in faces. A face with `when`
is skipped while its condition is false, so it appears only when it has something to
say: `"when": "{build}==fail"`, `"when": "{day0.rain}>=60"`, or
`"when": "{age.heartbeat}>600"` (a sender that went quiet). `duration` sets its own
time on screen. Showing a face explicitly (`/api/faces/show`, `"show": true`) ignores
`when`.

### Paths

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

### Gradients

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

## Home Assistant dashboards (geekmagic-hacs)

[geekmagic-hacs](https://github.com/adrienbrault/geekmagic-hacs) renders dashboards
(gauges, charts, weather, cameras, media and more) in Home Assistant and uploads
them as 240×240 JPEGs. Turning this integration on adds the stock SD Pro firmware's
endpoints, which that integration detects as "SD_PRO firmware":

| Path | Does |
|---|---|
| `GET /theme/list` | `{"themes":[{"id":0,"name":"Classic","enabled":true},...],"interval":10}` |
| `GET /theme/toggle?id=<0-9>&state=<0/1>` | Face in or out of rotation |
| `GET /theme/interval?val=<s>` | Rotation interval |
| `GET /photo/list` | `{"files":[{"name","size","enabled"}],"total","used","interval"}` |
| `POST /photo/upload` | Multipart JPEG upload (any field name), up to 200 KB |
| `GET /photo/toggle?name=<file>&state=<0/1>` | Photo in or out of the slideshow |
| `GET /photo/delete?name=<file>` | Delete a photo |
| `GET /photo/interval?val=<s>` | Seconds per photo |
| `GET /api/set?key=theme&value=2` | Show the Photo face (also `lcd_brightness`) |

**These work without signing in** while the integration is on, because
geekmagic-hacs cannot send credentials. They only reach photos, the face choice and
brightness. WiFi, the password, settings and firmware still need a sign-in. Prefer a
refresh interval of 30 s or longer: each update rewrites a photo in flash. To stop
the time covering part of the dashboard, turn off **Show the time** on the Photos
card (or `/api/set?key=photoclock&value=0` while signed in).

## MQTT

Set the broker, port, optional username and password, and a topic base (default:
the clock's hostname, `agentmonitor-XXXXXX`). The clock connects when it is on WiFi,
retries every 30 s, and announces itself to Home Assistant through **MQTT
discovery** (prefix `homeassistant/`):

| Entity | Topics |
|---|---|
| Light "Backlight" (JSON schema, brightness 0–100) | `<base>/backlight/set`, state `<base>/backlight` |
| Select "Face" (built-in and custom faces) | `<base>/face/set`, state `<base>/face` (`message` while one is shown) |
| Sensors: Temperature, Humidity, Weather, WiFi signal | `<base>/state` (JSON) |
| Button "Update weather" | `<base>/sync` |

Availability is on `<base>/status` (`online`/`offline`, retained). State is
published on every change and every 60 s.

Agent messages go through MQTT too:

- `<base>/message`: a message JSON, as for `POST /api/message`.
- `<base>/message/clear`: an `id` to remove that message, or an empty payload for all.

With **Custom faces** also on, the clock subscribes to:

- `<base>/custom/<name>`: a face JSON (an empty payload deletes it), as for `POST /api/faces`.
  MQTT payloads are limited to about 1.5 KB.
- `<base>/vars`: `{"name": value, ...}` with the same `$` options, as for `POST /api/faces/vars`.

A Home Assistant automation can then push values, for example every time a sensor
changes:

```yaml
action: mqtt.publish
data:
  topic: agentmonitor-XXXXXX/vars
  payload: '{"solar": "{{ states(''sensor.solar_power'') }}", "pct": {{ states(''sensor.battery'') | int }}}'
```

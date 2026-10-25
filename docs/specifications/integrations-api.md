# Integrations API

Three optional integrations, each **off by default**. Turn them on in the settings page
under **Integrations**:

| Integration | What it is for | Who may call it |
|---|---|---|
| [Custom faces](#custom-faces) | Your own faces as JSON, with live values | Signed-in page, or the API key |
| [Home Assistant dashboards](#home-assistant-dashboards-geekmagic-hacs) | The `geekmagic-hacs` integration pushes rendered dashboards | **Anyone on the network** (it cannot sign in) |
| [MQTT](#mqtt) | Home Assistant discovery; faces and variables over MQTT | Your MQTT broker |

The clock is at `http://<clock>/`, where `<clock>` is its IP address or
`smartclock-XXXXXX.local`.

## Custom faces

A face is a JSON object with a background colour and up to 32 **items**, drawn in
order on the 240×240 screen. Faces are stored on the clock (at most 12, 4 KB each)
and join the face rotation after the built-in faces.

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
| `POST /api/faces/vars` | `{"key":"value",...}` | Sets variables (merged with existing ones); values may be numbers |

Names use 1–20 characters from `a-z 0-9 _ -`. Variables are kept in memory (up to
24, keys up to 15 characters, values up to 160) and are lost on restart. Push them
again on a timer or whenever they change.

```bash
curl -X POST "http://<clock>/api/faces?name=power&show=1" -H "X-Api-Key: <key>" -H "Content-Type: application/json" -d @power.json
```

```bash
curl -X POST "http://<clock>/api/faces/vars" -H "X-Api-Key: <key>" -d '{"solar":"4.1","pct":62}'
```

### Face JSON

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

- **Fonts.** `2` is small text (16 px). `4` is medium text (26 px). `6` (48 px) and
  `8` (75 px) are large **digits only** (`0-9 : . -`). Fonts have no degree sign;
  write `"{temp} {unit}"`, or draw a small circle.
- **`align`** sets which point of the item `x`,`y` refers to: `tl`, `tc`, `tr`, `ml`,
  `mc`, `mr`, `bl`, `bc`, `br` (top/middle/bottom, left/centre/right). For wrapped
  text it places the whole block.
- **Smooth updates.** Only items whose placeholders resolve to something new are
  redrawn. Text with a `bg` colour, rings, bars, and icons with a `bg` are redrawn on
  their own when their content (text, value, colour) changes. `pad` (pixels) clears
  the area around text whose width varies. Anything else that changes, including the
  position, size or visibility of any item, redraws the whole face, which you will see
  as a flicker. There is no RAM for a full-screen buffer.

### Placeholders and expressions

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
variable is empty. Weather placeholders show `--` until the first weather update.

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
| `GET /theme/toggle?id=<0-6>&state=<0/1>` | Face in or out of rotation |
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
the clock's hostname, `smartclock-XXXXXX`). The clock connects when it is on WiFi,
retries every 30 s, and announces itself to Home Assistant through **MQTT
discovery** (prefix `homeassistant/`):

| Entity | Topics |
|---|---|
| Light "Backlight" (JSON schema, brightness 0–100) | `<base>/backlight/set`, state `<base>/backlight` |
| Select "Face" (built-in and custom faces) | `<base>/face/set`, state `<base>/face` |
| Sensors: Temperature, Humidity, Weather, WiFi signal | `<base>/state` (JSON) |
| Button "Update weather" | `<base>/sync` |

Availability is on `<base>/status` (`online`/`offline`, retained). State is
published on every change and every 60 s.

With **Custom faces** also on, the clock subscribes to:

- `<base>/custom/<name>`: a face JSON (an empty payload deletes it), as for `POST /api/faces`.
  MQTT payloads are limited to about 1.5 KB.
- `<base>/vars`: `{"key":"value",...}`, as for `POST /api/faces/vars`.

A Home Assistant automation can then push values, for example every time a sensor
changes:

```yaml
action: mqtt.publish
data:
  topic: smartclock-XXXXXX/vars
  payload: '{"solar": "{{ states(''sensor.solar_power'') }}", "pct": {{ states(''sensor.battery'') | int }}}'
```

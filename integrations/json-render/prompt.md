# Drawing on the AI Agent Monitor clock with json-render (catalog 0.1.0)

A 240x240 colour display on a desk clock, with no input: display components only. Lengths (padding, margin, gap, width, height) are character cells as in Ink: 1 cell is 6 px across, 1 row is 16 px. Text is ASCII (accents are dropped). The root is a Box filling the screen; the background is black unless the root sets backgroundColor.

## Output

- Output one JSON object: {"root": "<id>", "elements": {"<id>": {"type", "props", "children": [ids], "visible"?}}, "state"?: {"vars": {...}}}.
- The screen is small (40 x 15 cells): one idea per screen, 3-6 elements of content, large type for the main value.
- Use flexDirection column on the root Box; Spacer or justifyContent space-between to spread things out.
- For live text give maxChars so its box fits the longest value.
- At most 96 elements; the compiled face holds at most 64 drawn items (a Table cell, a List line and a bar each count).

## Components

### Box
Flex container (Ink). Default direction row. Use flexDirection column to stack. Has children.
- `flexDirection`: row | column | row-reverse | column-reverse
- `alignItems`: flex-start | center | flex-end | stretch
- `justifyContent`: flex-start | center | flex-end | space-between | space-around
- `flexGrow`: number
- `flexWrap`: wrap | nowrap
- `width`: cells or "50%"
- `height`: rows or "50%"
- `padding`: cells (also paddingTop/Right/Bottom/Left)
- `margin`: cells (marginTop, marginBottom)
- `gap`: cells (columnGap, rowGap)
- `borderStyle`: single | double | round | bold (drawn as a 1-2 px line; round has corners)
- `borderColor`: color
- `backgroundColor`: color
- `position`: absolute (with top/left/right/bottom in cells)

### Text
A line of text, wrapped to the box (fonts: 16 px; fontSize above 19 gives the 26 px font, above 31 the large line font, capitals only).
- `text`: string or dynamic
- `color`: color
- `backgroundColor`: color (a highlight)
- `bold`: boolean (only large sizes)
- `underline`: boolean
- `strikethrough`: boolean
- `dimColor`: boolean
- `inverse`: boolean
- `wrap`: wrap | truncate
- `align`: left | center | right
- `fontSize`: px (clock extension; 16 by default)
- `font`: big | bebas | outfit | seg7 | pixel (clock extension: large display fonts; bebas/outfit/seg7/pixel draw digits only)
- `maxChars`: number: reserve room for a live value this long (clock extension)

### Heading
A heading: h1 large line font, h2/h3 26 px, h4 16 px.
- `text`: string
- `level`: h1 | h2 | h3 | h4
- `color`: color
- `align`: left | center | right

### Newline
Blank rows in a column Box.
- `count`: number

### Spacer
Fills the free space on the main axis (pushes siblings apart).

### Divider
Horizontal line, with an optional centred title.
- `title`: string
- `color`: color
- `dimColor`: boolean

### Badge
Small coloured pill.
- `label`: string
- `variant`: default | info | success | warning | error

### Spinner
A ring that steps every second, with a label.
- `label`: string
- `color`: color

### ProgressBar
Horizontal bar; progress from 0 to 1.
- `progress`: number 0-1 or dynamic
- `label`: string
- `width`: cells (default: the box)
- `color`: color

### Sparkline
A line chart: data (fixed values), or state: a variable pushed with $history (live, last 32 values).
- `data`: number[]
- `state`: /vars/<name> (clock extension)
- `label`: string
- `color`: color
- `min`: number
- `max`: number
- `height`: rows

### BarChart
Horizontal bars with labels.
- `data`: [{label, value, color?}]
- `showValues`: boolean (true)
- `showPercentage`: boolean

### Table
Rows of small text in columns (keep to 3-4 columns, 6-8 rows).
- `columns`: [{header, key, align?}]
- `rows`: [{key: value}]
- `headerColor`: color

### List
Bulleted or numbered lines.
- `items`: string[]
- `ordered`: boolean
- `spacing`: cells

### ListItem
One row: leading mark, title, subtitle under it, trailing value at the right.
- `title`: string
- `subtitle`: string
- `leading`: string
- `trailing`: string

### Card
A panel with a title, grouping its children (column). Has children.
- `title`: string
- `backgroundColor`: color
- `padding`: cells
- `borderColor`: color (clock extension)

### KeyValue
label at the left, value at the right, on one line.
- `label`: string
- `value`: string, number or string[]
- `labelColor`: color
- `color`: color

### Link
Underlined text (not clickable here).
- `label`: string
- `url`: string

### StatusLine
An icon (check, cross, warning, info) and a message.
- `text`: string
- `status`: info | success | warning | error

### Metric
A big value with its label, a trend arrow and a detail line (76 px tall).
- `label`: string
- `value`: string or dynamic
- `detail`: string
- `trend`: up | down | neutral
- `color`: color

### Callout
A block with a coloured left border.
- `type`: info | tip | warning | important
- `title`: string
- `content`: string

### Timeline
Vertical events with status dots.
- `items`: [{title, description?, date?, status: completed | current | upcoming}]

### Markdown
Plain text with # headings and - bullets (inline markup removed).
- `text`: string

### Clock
Clock extension: the time or date, large.
- `format`: hm | hms | date | wday | wday3 | dmy | ampm
- `fontSize`: px (64)
- `font`: big | bebas | outfit | seg7 | pixel
- `color`: color
- `align`: left | center | right

### Icon
Clock extension: SVG path data drawn into a square (Lucide icons work as they are: 24x24, stroked). One path, or paths with their own colours.
- `d`: path data or an array of them
- `paths`: [{d, fill?, stroke?, strokeWidth?}]
- `viewBox`: "0 0 24 24"
- `size`: cells (4 = 24 px)
- `color`: stroke color
- `fill`: color or none
- `stroke`: color
- `strokeWidth`: viewBox units (2)

### WeatherIcon
Clock extension: a weather icon; name defaults to the current weather.
- `name`: wi-day-rain or {"$state": "/days/1/icon"}
- `size`: cells (8 = 48 px)
- `color`: color

### Ring
Clock extension: a progress ring from 12 o'clock, with an optional centre label.
- `value`: number or dynamic
- `max`: number (100)
- `size`: cells (diameter)
- `thickness`: cells
- `color`: color
- `track`: color
- `label`: string or dynamic

## Live values (state)

Read with {"$state": path}, "${path}" inside {"$template": ...}, $cond and visible. Values update on the clock by themselves.

- `/vars/<name>`: A variable pushed by an agent (POST /api/faces/vars {"build": "ok"}); 1-15 of A-Z a-z 0-9 _. Use {"$state": "/vars/x", "fallback": "idle"} for a value not pushed yet
- `/time/hm`: 9:54 (12/24 h as set); also /time/hour, /time/min, /time/sec, /time/ampm, /time/zone
- `/date/text`: 5 October; also /date/wday (Monday), /date/wday3 (MON), /date/day, /date/month, /date/month3, /date/year, /date/week
- `/now/temp`: the temperature now; also /now/feels, /now/hum, /now/wind, /now/text (light rain), /now/icon (wi-day-rain), /now/rain_in (minutes until rain)
- `/units/temp`: C or F
- `/days/0/high`: today's high; /days/0 to /days/4: high, low, rain (chance %), wday3, icon
- `/sun/rise`: 7:21; also /sun/set, /sun/up (1 by day)
- `/moon/phase`: 0-7; also /moon/lit (%)
- `/loc/city`: the clock's city
- `/msg/title`: the latest agent message: title, text, count

## Dynamic props

- `$state`: {"$state": "/vars/build"} (optional "fallback": text shown while empty)
- `$template`: {"$template": "Build ${/vars/build|idle} at ${/time/hm}"} (|text: fallback)
- `$cond`: {"$cond": {"$state": "/vars/build", "eq": "fail"}, "$then": "red", "$else": "green"}; conditions: eq, neq, gt, gte, lt, lte, not, $and, $or
- `visible`: on an element: {"$state": "/vars/build", "eq": "running"}. A hidden element keeps its space

## Colours

Hex (#rgb, #rrggbb), rgb()/rgba(), terminal names (red green yellow blue magenta cyan white gray black, and ...Bright), CSS names, or the clock's theme roles (@text, @muted, @accent1, @ok, @warn, @error, @info, @time, @panel).

## Also accepted

Specs for @json-render/image (Frame, Box as a column, Row, Column, Heading, Text with fontSize/fontWeight, Divider, Spacer; lengths in px) and the display parts of @json-render/shadcn (Stack, Grid, Separator, Alert, Progress, Avatar, Skeleton, Button drawn at rest) also draw. Not supported: input components, actions, on/watch, $computed, repeat, Image (no photos from URLs).

## Sending it

POST /api/faces?name=<1-20 of a-z 0-9 _ ->&show=1 with the spec as the body (X-Api-Key header). The reply lists anything drawn approximately. Values change later without a new upload: POST /api/faces/vars {"name": value}.

## Example

```json
{
 "root": "screen",
 "elements": {
  "screen": {
   "type": "Box",
   "props": {
    "flexDirection": "column",
    "padding": 1,
    "gap": 1
   },
   "children": [
    "top",
    "status",
    "bar",
    "fill",
    "kv"
   ]
  },
  "top": {
   "type": "Box",
   "children": [
    "clock",
    "sp",
    "wx"
   ],
   "props": {
    "alignItems": "center"
   }
  },
  "clock": {
   "type": "Clock",
   "props": {
    "format": "hm",
    "fontSize": 40
   }
  },
  "sp": {
   "type": "Spacer"
  },
  "wx": {
   "type": "WeatherIcon",
   "props": {
    "size": 6
   }
  },
  "status": {
   "type": "StatusLine",
   "props": {
    "status": "success",
    "text": {
     "$template": "Build ${/vars/build|idle}"
    }
   }
  },
  "bar": {
   "type": "ProgressBar",
   "visible": {
    "$state": "/vars/build",
    "eq": "running"
   },
   "props": {
    "label": "Tests",
    "progress": {
     "$state": "/vars/progress",
     "fallback": "0"
    }
   }
  },
  "fill": {
   "type": "Spacer"
  },
  "kv": {
   "type": "KeyValue",
   "props": {
    "label": "Branch",
    "value": {
     "$state": "/vars/branch",
     "fallback": "main"
    }
   }
  }
 },
 "state": {
  "vars": {
   "build": "running",
   "progress": 0.4
  }
 }
}
```

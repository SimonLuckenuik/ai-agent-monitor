---
name: ai-agent-monitor
description: Show messages, status, progress and custom graphics on the user's AI Agent Monitor desk clock (AI Agent Monitor firmware, 240x240 screen) over HTTP, or on its Docker emulator. Use when the user wants to be alerted or kept informed on the clock (needs input, done, failed, progress), asks for a screen or icon to be designed or pushed to it, or mentions the agent monitor, the smart clock or its emulator.
---

# AI Agent Monitor

The clock is an ESP8266 with a 240×240 colour screen running the AI Agent Monitor
firmware (a fork of SmartClock-OSS). There are two ways to put something on it:

1. **Messages** (start here): a title, a short text and one of 8 built-in icons, drawn
   by the clock's built-in Message face. One call, no key, nothing to design.
2. **Custom faces**: your own JSON layouts (text, shapes, SVG icons, gradients, live
   values) when a message is not enough. These need the clock's API key.

`scripts/clock.py` (Python standard library only) wraps the HTTP API; run it from
this skill's directory, or call the API directly (`reference/api.md`).

## Setup

| Variable | Value |
|---|---|
| `CLOCK_URL` | The clock, e.g. `http://192.168.1.50`, or `http://agentmonitor-xxxxxx.local` from the same network (mDNS, full firmware 1.4.0 on). Default `http://127.0.0.1:8000`: the emulator |
| `CLOCK_API_KEY` | Only for custom faces: settings page → **Integrations** → turn on **Custom faces** → the key is shown there |

Never ask for, or store, the device password of a real clock.

## Messages

```bash
python scripts/clock.py message "my-project" "Waiting for your approval to deploy." --icon waiting --id my-project --show
```

| Icon | Use it for | Colour |
|---|---|---|
| `waiting` | The user must answer or approve | yellow |
| `warning` | Something needs attention soon (lights the screen fully) | amber |
| `error` | Something failed (lights the screen fully) | red |
| `success` | A step or check passed | green |
| `done` | The whole task is finished | green |
| `running` | Work in progress; add `--pct` for a progress bar | light blue |
| `info` | Anything else | blue |
| `bell` | A reminder | amber |

- **One `--id` per task or session** (the project name works well): a new message with
  the same id replaces the old one, so update in place rather than piling up. At most
  4 messages exist at once.
- `--show` puts it on screen now; without it, it waits for its turn in the clock's face
  rotation. Use `--show` when the user should look now (`waiting`, `warning`, `error`,
  `done`).
- `--ttl` (seconds, default 600) is how long it stays. Clear it when it no longer
  applies (`python scripts/clock.py clear my-project`), for example once the user
  has answered.
- Keep the title to a couple of words (one line of large text) and the text to one
  sentence (up to 4 short lines). Accents are dropped: `é` shows as `e`.
- Posting a message needs no key, and messages are lost when the clock restarts.
- Every active message also appears on the clock's **Agents** face: one row per id,
  with its icon, title, text and minutes left. Distinct ids are what make that board
  useful when several agents or projects run at once.

## Custom faces

When a message is not enough, a custom face is a JSON description of what to draw,
saved on the clock. The model:

- **Items** (up to 32, drawn in order): `text`, `rect`, `circle`, `line`, `ring`,
  `bar`, `path` (SVG icons), `icon` (weather), `image`, `sprite` (pixel art, animated
  by frames), `spark` (a chart of a variable's history), and `repeat` (items once per
  forecast day, agent message, or number). Shapes take a `border`.
- **Placeholders** in any string: the clock's own values are dotted (`{time.hm}`,
  `{date.wday}`, `{now.temp}`, `{day1.high}`, `{sun.up}`, `{moon.phase}`,
  `{msg.title}`, `{net.ip}` …); a name without a dot is a **variable** you push.
  Modifiers after `:` (`{title:u}`, `{n:03}`, `{load:.1}`), a fallback after `|`
  (`{status|idle}`).
- **Expressions** in number and colour fields (`"120+{dx}*2"`,
  `"{pct}>80?@alert:@ok"`) and, in braces, in text (`"Build {{build}==fail?FAILED:OK}"`).
  Every item takes `show`, a condition.
- **Maps** turn a value into a colour or a label (`{build:state}`); **palettes** name
  colours (`"@fg"`) and can switch as a whole (`"palette": "{sun.up}?day:night"`).
- **Flashing** for an alarm: `"invert": "{time.sec}%2"` makes the display invert every
  colour each other second, with no redraw (red turns cyan). For two colours of your
  choice, `"palette": "{time.sec}%2==0?on:off"` with two palettes swaps them (the face
  is redrawn each time). Add `"when": "{alarm}"` so it shows only while the alarm
  variable is set.
- **The clock's colours**: `@time`, `@bg`, `@text`, `@muted`, `@panel`, `@warn`,
  `@error`, `@ok` … are the colours the user chose for the built-in faces (day and
  night). Use them unless the face needs colours of its own: it then matches the
  clock and follows the user's palettes.
- **Big text**: `"font": "big"` with a `size` in pixels draws the Big tiles' line font
  at any size: capitals, digits, punctuation, and `° ↑ ↓ ← → ✓ ✗ • ♥ ⚡`.
- **Rotation**: `when` puts the face in the rotation only while a condition holds
  (`"{build}==fail"`, `"{day0.rain}>60"`); `duration` sets its time on screen. The
  user orders the rotation and turns faces on or off in the settings.

`reference/face-format.md` has every field and placeholder; `reference/face.schema.json`
is the JSON Schema of a face.

1. **Pick or write a face.** `examples/` has `attention`, `progress`, `done` and
   `error`. The firmware repository's `examples/faces/` has 37 more (clocks, weather,
   build status, pixel art, games, charts) showing every feature. Adapt one, or design a new
   face with `reference/face-format.md`.
2. **Check it:** `python scripts/clock.py check face.json` validates it against the
   schema and flags unknown placeholders (the clock ignores what it does not know,
   silently). `push` checks too.
3. **Save it once:** `python scripts/clock.py push status examples/progress.json`
4. **Set values and show it:**
   ```bash
   python scripts/clock.py vars "title=my-project" "pct=40" "step=Running the tests"
   python scripts/clock.py show status
   ```
5. **Update with values only** (they live in RAM: fast, and no flash wear). Re-push a
   face only when its layout changes. Values are lost on restart unless pushed with
   `--persist`; give values that go stale a `--ttl`, and write `{name|fallback}` so a
   face reads sensibly without them. `--history` keeps numbers for a `spark` chart;
   `--add` adds to a counter.
6. **Delete it** when done (`python scripts/clock.py delete status`): saved custom
   faces stay in the face rotation until deleted (or give them a `when`).

If a call answers `Custom faces are turned off`, ask the user to turn on
**Integrations → Custom faces** on the settings page and to give you the API key.

**Limits.** A face is at most 4 KB of JSON (the script minifies and checks) and 32
top-level items; 40 custom faces in all; names are 1–20 of `a-z 0-9 _ -`. At most 40
variables, names 1–15 of `A-Z a-z 0-9 _`, values up to 160 characters.

## Rules of the screen

- **Text is ASCII.** Font `2` (16 px) and `4` (26 px) are text; `6` (48 px) is
  **digits only** (`8` is gone and draws as `6`); they have no degree sign: write
  `{now.temp} {units.temp}`. `"big"` (any `size`) has capitals, digits and a few
  symbols, `°` among them; it is slower to draw, so keep it for a few large items.
- **Redraws.** There is no frame buffer. An item whose content changes in place (text,
  icon, sprite or spark with a `bg`; ring; bar) redraws alone; any other change
  (position, visibility, a repeat's entries, the palette) redraws the whole face,
  which flickers. Keep moving parts few, and give changing text a `bg` and a `pad`.
- **Legibility.** The screen is 1.54″: one idea per face, large text, strong
  contrast, at most about 3 short lines of font 2 under a title in font 4.
- **Attention.** There is no sound and no button. Colour and icon carry the meaning;
  the user acknowledges at the computer.

## Icons in custom faces

`path` items draw SVG path data. **Lucide** (`viewBox 0 0 24 24`, outlines): copy each
`<path d>`, set `"stroke"` and `"sw": 2`. Write `<circle cx cy r>` as
`"M{cx-r} {cy}a{r} {r} 0 1 0 {2r} 0a{r} {r} 0 1 0 -{2r} 0"` and `<line>`/`<polyline>`
as `M`/`L` commands. **Material Symbols** (filled): `"vb": "0 -960 960 960"` and
`"fill"`. Your own icons: keep them on the 24×24 grid, round, with strokes of 2.

## Checking your work

On the **emulator** (`CLOCK_URL` unset), look at the result after designing or
changing anything: `python scripts/clock.py screenshot shot.png`, then view the image.

## References

- `reference/api.md`: messages, custom faces, MQTT and the emulator's extras.
- `reference/face-format.md`: every item type, placeholder, modifier, expression,
  map, palette, repeat, sprite, sparkline, path and gradient, with examples.
- `reference/face.schema.json`: the JSON Schema of a face (`clock.py check` uses it).
- `examples/*.json`: tested custom faces to start from.

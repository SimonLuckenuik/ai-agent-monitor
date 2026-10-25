---
name: ai-agent-monitor
description: Show messages, status, progress and custom graphics on the user's AI Agent Monitor desk clock (SmartClock-OSS firmware, 240x240 screen) over HTTP, or on its Docker emulator. Use when the user wants to be alerted or kept informed on the clock (needs input, done, failed, progress), asks for a screen or icon to be designed or pushed to it, or mentions the agent monitor, the smart clock or its emulator.
---

# AI Agent Monitor

The clock is an ESP8266 with a 240×240 colour screen running SmartClock-OSS. Agents
draw on it with **custom faces**: JSON descriptions of what to draw, stored on the
clock. A face uses **variables** (`{title}`, `{msg}`, …) that are pushed separately, so
a face is saved once and then updated by sending only values.

Everything goes through the clock's HTTP API. `scripts/clock.py` (Python standard
library only) wraps it; run it from this skill's directory, or call the API directly
(see `reference/api.md`).

## Setup

| Variable | Value |
|---|---|
| `CLOCK_URL` | The clock, e.g. `http://192.168.1.50`. Default `http://127.0.0.1:8000`: the emulator |
| `CLOCK_API_KEY` | The clock's API key: settings page → **Integrations** → turn on **Custom faces** → the key is shown there |

If neither is set and the emulator is running, the script signs in to the emulator by
itself. If a call answers `Custom faces are turned off`, ask the user to turn on
**Integrations → Custom faces** on the settings page. Never ask for, or store, the
device password of a real clock.

## Workflow

1. **Pick or write a face.** `examples/` has `attention`, `progress`, `done` and
   `error`, all driven by variables. Adapt one, or design a new face with
   `reference/face-format.md`.
2. **Save it once:** `python scripts/clock.py push attention examples/attention.json`
3. **Set the values and show it:**
   ```bash
   python scripts/clock.py vars "title=my-project" "msg=Waiting for your approval to deploy."
   python scripts/clock.py show attention
   ```
4. **Update with values only** (they live in RAM: fast, and no flash wear). Re-push a
   face only when its layout changes.
5. **Clean up** when the message no longer applies: `python scripts/clock.py delete attention`.
   Saved custom faces stay in the clock's face rotation until deleted.

On the **emulator**, check the result: `python scripts/clock.py screenshot shot.png`,
then look at the image. Do this after designing or changing a face.

## Rules of the clock

- **Limits.** A face is at most 4 KB of JSON (the script minifies and checks) and 32
  items; 12 custom faces in all; names are 1–20 of `a-z 0-9 _ -`. At most 24
  variables, keys up to 15 characters, values up to 160. Variables are lost on restart:
  push them again before showing a face after a reboot.
- **Text is ASCII.** The fonts have no accents or symbols beyond ASCII (`é` will not
  draw: write `e`). Font `2` (16 px) and `4` (26 px) are text; `6` (48 px) and `8`
  (75 px) are **digits only**. There is no degree sign: write `{temp} {unit}`.
- **Redraws.** There is no frame buffer. An item whose content changes in place (text
  with a `bg`, ring, bar) redraws alone; any other change redraws the whole face,
  which flickers. Keep moving parts few and slow, and give changing text a `bg`
  and a `pad`.
- **Legibility.** The screen is 1.54″: one idea per face, large text, strong
  contrast. Up to about 3 short lines of font 2 under a title in font 4.
- **Attention.** There is no sound and no button. A distinct colour (amber for
  waiting, red for failed, green for done) and a clear icon carry the meaning. The
  user acknowledges at the computer; clear or replace the face when they do.

## Icons

`path` items draw SVG path data, so icons from SVG sets can be used directly.
**Lucide** (`viewBox 0 0 24 24`, outlines): copy each `<path d>`, set `"stroke"` and
`"sw": 2`. Write `<circle cx cy r>` as `"M{cx-r} {cy}a{r} {r} 0 1 0 {2r} 0a{r} {r} 0 1 0 -{2r} 0"`,
and `<line>`/`<polyline>` as `M`/`L` commands. **Material Symbols** (filled):
`"vb": "0 -960 960 960"`, `"fill"`. You can also draw your own icons: keep them on
the 24×24 grid, with round shapes and strokes of 2.

## References

- `reference/face-format.md`: every item type, placeholders, expressions, paths,
  gradients, with examples.
- `reference/api.md`: the HTTP endpoints, and MQTT.
- `examples/*.json`: tested faces to start from.

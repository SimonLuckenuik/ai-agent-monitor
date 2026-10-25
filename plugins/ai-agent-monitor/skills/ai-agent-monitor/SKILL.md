---
name: ai-agent-monitor
description: Show messages, status, progress and custom graphics on the user's AI Agent Monitor desk clock (SmartClock-OSS firmware, 240x240 screen) over HTTP, or on its Docker emulator. Use when the user wants to be alerted or kept informed on the clock (needs input, done, failed, progress), asks for a screen or icon to be designed or pushed to it, or mentions the agent monitor, the smart clock or its emulator.
---

# AI Agent Monitor

The clock is an ESP8266 with a 240×240 colour screen running SmartClock-OSS. There
are two ways to put something on it:

1. **Messages** (start here): a title, a short text and one of 8 built-in icons, drawn
   by the clock's built-in Message face. One call, no key, nothing to design.
2. **Custom faces**: your own JSON layouts (text, shapes, SVG icons, gradients, live
   values) when a message is not enough. These need the clock's API key.

`scripts/clock.py` (Python standard library only) wraps the HTTP API; run it from
this skill's directory, or call the API directly (`reference/api.md`).

## Setup

| Variable | Value |
|---|---|
| `CLOCK_URL` | The clock, e.g. `http://192.168.1.50`. Default `http://127.0.0.1:8000`: the emulator |
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

## Custom faces

When a message is not enough, a custom face is a JSON description of what to draw,
saved on the clock. A face uses **variables** (`{title}`, `{pct}`, …) pushed
separately, so it is saved once and then updated with values only.

1. **Pick or write a face.** `examples/` has `attention`, `progress`, `done` and
   `error`. Adapt one, or design a new face with `reference/face-format.md`.
2. **Save it once:** `python scripts/clock.py push status examples/progress.json`
3. **Set values and show it:**
   ```bash
   python scripts/clock.py vars "title=my-project" "pct=40" "step=Running the tests"
   python scripts/clock.py show status
   ```
4. **Update with values only** (they live in RAM: fast, and no flash wear). Re-push a
   face only when its layout changes. Values are lost on restart: write
   `{name|fallback}` so a face still reads sensibly before they are pushed again.
5. **Delete it** when done (`python scripts/clock.py delete status`): saved custom
   faces stay in the face rotation until deleted.

If a call answers `Custom faces are turned off`, ask the user to turn on
**Integrations → Custom faces** on the settings page and to give you the API key.

**Limits.** A face is at most 4 KB of JSON (the script minifies and checks) and 32
items; 12 custom faces in all; names are 1–20 of `a-z 0-9 _ -`. At most 24 variables,
keys up to 15 characters, values up to 160; they are lost on restart.

## Rules of the screen

- **Text is ASCII.** Font `2` (16 px) and `4` (26 px) are text; `6` (48 px) and `8`
  (75 px) are **digits only**. There is no degree sign: write `{temp} {unit}`.
- **Redraws.** There is no frame buffer. An item whose content changes in place (text
  with a `bg`, ring, bar) redraws alone; any other change redraws the whole face,
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
- `reference/face-format.md`: every item type, placeholders, expressions, paths and
  gradients, with examples.
- `examples/*.json`: tested custom faces to start from.

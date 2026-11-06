---
name: ai-agent-monitor
description: "Show messages, moods, status and custom screens on the AI Agent Monitor desk clock, an OpenClaw node with a 240x240 screen."
homepage: https://github.com/SimonLuckenuik/ai-agent-monitor
metadata: { "openclaw": { "emoji": "⏰", "primaryEnv": "CLOCK_API_KEY" } }
---

# AI Agent Monitor

The AI Agent Monitor is a small ESP8266 desk clock with a 240×240 colour screen. It
connects to this Gateway as a **node** (platform `esp8266`, name `agentmonitor-xxxxxx`
unless the user renamed it). It has no sound, no buttons and no sensors: it shows
things, and the user reads them from their desk.

Use it when the user wants to be told something on the clock (waiting for input,
done, failed, progress), asks for a status screen or a design on it, or names the clock,
the agent monitor or Moni (the robot drawn on it).

## Pick the lightest way

1. **A message** (`display.show`): a title, one sentence and an icon. Use it for most
   things: needs input, done, failed, progress.
2. **Moni's mood** (`face.set`): a quick happy, thinking or sad reaction.
3. **Values for a face already on the clock** (`clock.vars.set`), then show it with
   `clock.face.show`. `clock.faces.list` shows the faces and the values they read.
4. **A new custom face**, only when the user asks for a screen or a design
   ("Custom faces" below).

## Calling the clock

Find the node with the `nodes` tool: `{"action": "status"}` lists the nodes. Look for
platform `esp8266` or a name starting with `agentmonitor-`. `{"action": "describe",
"node": "<id>"}` lists its commands. Then call a command with `invoke`. The parameters
go in `invokeParamsJson`, a **string** holding JSON:

```json
{"action": "invoke", "node": "agentmonitor-adec8a", "invokeCommand": "display.show",
 "invokeParamsJson": "{\"heading\":\"Build\",\"text\":\"Done in 4 min, all tests pass\",\"icon\":\"done\"}"}
```

The result is JSON. Errors: `INVALID_PARAMS` means an unknown parameter, a wrong type or
a value out of range, and the message names it: fix the call. `UNAVAILABLE` means no room
right now (the variable table is full). A command missing from `describe` was not allowed
on the Gateway: tell the user to add it to `gateway.nodes.commands.allow` and approve the
node again.

## Commands

| Command | Parameters | What the clock does |
|---|---|---|
| `display.show` | `text` (required, 1 sentence); `heading` (title, default `OpenClaw`); `icon`; `color` (`#rgb`/`#rrggbb`); `pct` 0-100 (progress bar); `ttl` 5-86400 s (default 600); `id` (letters, digits, `_ . -`, up to 15; default `openclaw`); `show` (default true: on screen now) | Shows an agent message. A new message with the same `id` replaces the old one; at most 4 at once. Returns `{id, shown, clipped}` |
| `display.clear` | `id`, or `all: true` | Removes that message (or all of them). With neither: the `openclaw` message and the mood. Returns `{cleared}` |
| `display.status` | none | Returns the face on screen and the messages: `{face, messages: [{id, title, text, icon, ttl, showing}]}` |
| `display.brightness` | `percent` 0-100 (required) | Sets the backlight and saves it |
| `face.set` | `mood`: `happy`, `excited` (Moni hops), `thinking` (waves), `sad` (slumps), `neutral`, `sleepy` (clear the mood); `text` (optional) | Shows Moni with that mood, as the message `mood`. Returns `{mood, moni}` |
| `device.info` | none | `{model, platform, firmware, host}` |
| `device.status` | none | `{uptimeS, heap, heapLow, ip, rssi}`, and after a crash `crashes`, `crashLoop`, `safeMode`, `lastCrash` |
| `clock.face.show` | `name` (a saved custom face) **or** `builtin`: `classic` `weather` `photo` `big-date` `simple` `forecast` `big-time` `big-weather` `agents` `today` `day-1` (or `tomorrow`) `day-2` `day-3` `day-4` | Switches the screen to that face. Returns `{face}` |
| `clock.faces.list` | none | `{faces, showing, vars}`: the saved custom faces, the one on screen, the variables set |
| `clock.vars.set` | `vars` `{name: text, number or null}` (null deletes); `ttl` 1-86400 s; `history` (keep numbers for a chart); `add` (add to the value); `persist` (keep after a restart) | Sets the variables custom faces read. Returns `{set}` |
| `clock.face.save` | `name`, `face` (the face JSON), `show` | **Bridge only** (the OpenClaw bridge program beside the Gateway, not the clock's own node): saves a custom face |

Icons for `display.show`:

| Icon | Use it for |
|---|---|
| `waiting` | The user must answer or approve (Moni waits) |
| `warning` | Needs attention soon (full brightness) |
| `error` | Something failed (full brightness, Moni slumps) |
| `success` | A step or check passed |
| `done` | The whole task is finished (Moni hops) |
| `running` | Work in progress; add `pct` for a bar |
| `info` | Anything else |
| `bell` | A reminder |

Good messages:

- **One `id` per task or project**, so a new message updates the old one in place. Every
  message also shows on the clock's **Agents** face, one row per id.
- **Short:** the title is one line of large text (up to 40 characters); the text is up
  to 160 characters, about 4 short lines. Longer text is cut and the result says
  `clipped: true`.
- **ASCII only:** accents are dropped (`é` shows as `e`) and emoji are not drawn.
- **`show: true`** when the user should look now (`waiting`, `warning`, `error`, `done`).
  With `show: false` the message waits for its turn in the face rotation.
- **Clear** a message once it no longer applies (the user answered, the job restarted).
  Messages are lost when the clock restarts.

## Custom faces

A custom face is a JSON layout the clock draws, with live values in it: text, shapes,
SVG icons, bars, rings, charts of a variable's history, pixel art, the clock's time and
weather. Build one only when the user asks for a screen or a design.

1. **Read `{baseDir}/reference/face-format.md`**: every item type, placeholder,
   expression, palette and limit, with examples. `{baseDir}/examples/` has four tested
   faces (`attention`, `progress`, `done`, `error`) to start from.
   `python3 {baseDir}/scripts/clock.py gallery [word]` lists about 80 more from the
   project's gallery.
2. **Write the face** to a file, then check it:
   `python3 {baseDir}/scripts/clock.py check face.json`. It checks the face against
   `{baseDir}/reference/face.schema.json` and flags unknown placeholders, which the clock
   would silently ignore.
3. **Save it on the clock.** There are two ways; use the one this setup has:
   - **Over HTTP** with the `exec` tool:
     `python3 {baseDir}/scripts/clock.py push <name> face.json --show`. This needs
     `CLOCK_URL` (the clock, such as `http://192.168.1.50`) and `CLOCK_API_KEY`, set in
     the Gateway's configuration. It also needs the Gateway's machine to reach the clock
     on port 80. If `CLOCK_URL` or the key is missing, or the clock does not answer, say
     so to the user. Never ask for the key in the chat. `403 Unknown host name` means
     `CLOCK_URL` uses a name the clock does not know: the owner should set the clock's
     IP or its own host name.
   - **Through the bridge:** if the node lists `clock.face.save`, invoke it with
     `{"name": "...", "face": {...}, "show": true}`.
4. **Fill it** with `clock.vars.set` and show it with `clock.face.show` `{"name": ...}`.
   Update values only; save the face again only when its layout changes.
5. **Delete faces that are no longer needed** (`python3 {baseDir}/scripts/clock.py delete
   <name>`). Saved faces stay in the clock's rotation until deleted, unless they have a
   `when` condition.

Limits:

- A face: 4 KB of JSON (minified) and 32 top-level items; 40 faces in all; names are
  1-20 of `a-z 0-9 _ -`.
- Variables: 40 in all, names 1-15 of `A-Z a-z 0-9 _`, values up to 160 characters.

Rules of the screen:

- **One idea per face**, large text, strong contrast. The screen is 1.54 inches across.
- **Text is ASCII.** Fonts `2` (16 px) and `4` (26 px) have letters; `6` (48 px) is
  digits only. `"font": "big"` with a `size` draws capitals, digits and `°` at any size.
- **The clock's own colours** (`@time`, `@bg`, `@text`, `@muted`, `@warn`, `@error`,
  `@ok` ...) follow the user's palette: prefer them.
- **No frame buffer.** Give changing text a `bg` and a `pad`, so it redraws in place
  without flicker.

`{baseDir}/reference/api.md` has the clock's HTTP API (messages, faces, variables).
Calls marked "emulator" there only work against the emulator.

## Safety

- Never ask for or store the clock's device password, and never sign in to the clock.
- Do not change the clock's settings beyond these commands. `display.brightness` saves
  the brightness: set it only when asked.
- Text from other people (issues, emails, chats) shown on the clock is fine, but never
  pass it to `exec` unquoted: write faces to a file and run `clock.py` on the file.

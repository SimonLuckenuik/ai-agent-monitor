# 041 The clock as an OpenClaw node

Status: Draft (proposed 2026-11-05; output only decided 2026-11-05; goals not yet approved;
a prototype bridge exists on branch `openclaw-node`)

## Problem

[esp-openclaw-node](https://github.com/openclaw/esp-openclaw-node) (Apache-2.0, commit
38c5ab2) runs ESP32 boards as OpenClaw *nodes*: the board keeps a WebSocket to an
OpenClaw Gateway, says which commands it offers, and the gateway's agents call them
(`display.show`, `face.set`, `device.status`...). The owner would like the clocks to be
nodes too, so OpenClaw's agents (in a chat, a scheduled job or a heartbeat) can put
things on them.

It cannot run as is: it is ESP-IDF 5.x for the ESP32-S3/P4 (libsodium, cJSON, FreeRTOS
tasks, PSRAM on every listed board), and the clock is an ESP8266 on the Arduino core
with about 10 KB of heap free at rest and about 25 KB of update room left after 2.2.0
(each KB of code costs about 1.7 KB of room: [footprint](../../docs/specifications/footprint.md)).

## Decision: output only (owner, 2026-11-05)

Only things shown on the screen, plus read-only status where it helps. No input of any
kind: no buttons, no touch, no sensors sent to the gateway, no `node.event`, no files
read from the clock, no settings changed beyond what changes the picture (brightness,
which face).

## What a node is

Details and sources in [RESEARCH.md](RESEARCH.md). In short: an Ed25519 identity; a
WebSocket to the gateway; `connect.challenge` answered by a signed `connect` (protocol
3-4, client `node-host`, role `node`, the commands offered); a device token from
`hello-ok`; then `node.invoke.request` events answered by `node.invoke.result`. The
gateway runs a command only when the node declares it, the owner approved the node's
command surface (`openclaw nodes approve`), and the command is on the gateway's allow
list: commands outside OpenClaw's platform defaults, which includes every clock
command, must be added to `gateway.nodes.commands.allow`.

## Goals (to agree)

1. **A bridge, not firmware** (option B below): `integrations/openclaw/`, a program
   that runs beside the gateway (Node 22+, no packages), pairs each configured clock as
   an OpenClaw node of its own, and turns each command into the clock's HTTP API.
   Works with 2.2.0 as installed, on both clocks.
2. **The commands** in the table below, with OpenClaw's own names and parameters where
   they exist, and errors an agent can act on.
3. **Pairing both ways** OpenClaw offers: the gateway token with `openclaw devices
   approve` + `openclaw nodes approve`, or a setup code (`openclaw qr`). Keys and the
   device token are kept on the bridge's machine, one file per clock; a setup code's
   bootstrap token is used once and dropped; the operator token a setup code hands off
   is never kept.
4. **Tests:** unit and round-trip tests against a fake gateway (it checks the signature
   with OpenClaw's own payload builder) and a fake clock; the same commands against the
   firmware in the emulator.
5. **Docs:** the bridge's README (gateway steps, the allow list, clock settings);
   option A recorded with its measurements, not built.

## Requirements

1. One node per clock, each with its own Ed25519 key, shown under its `displayName` in
   `openclaw nodes status`.
2. Connect as the gateway expects: protocol 3-4, client id `node-host`, mode and role
   `node`, no scopes, v3 signature over the challenge's nonce and `ts`.
3. Save the device token from `hello-ok` and reconnect with it; reconnect with back-off
   (1 s to 30 s); keep retrying a refused or pending pairing every 30 s so an approval is
   picked up without a restart.
4. Never store a bootstrap token after use, nor any operator token.
5. Answer every `node.invoke.request` with `node.invoke.result`, within the request's
   `timeoutMs` (20 s at most); unknown commands and bad parameters are coded errors, not
   silence.
6. Commands validate their parameters (unknown fields refused, so typos surface) and
   respect the clock's limits (title 40, text 160 characters: longer text is shortened and
   reported as `clipped`).
7. No call the clock does not already take; no sign-in with the clock's password.
8. Read-only commands make GET requests only.

## The commands

| Command | Params | Clock API |
|---|---|---|
| `display.show` | `text`; optional `heading` (title), `icon`, `color`, `pct`, `ttl`, `id`, `show` (true) | `POST /api/message` |
| `display.clear` | optional `id`, `all` | `DELETE /api/message` (the bridge's own message and mood unless told otherwise) |
| `display.status` | | `GET /status`, `GET /api/message` |
| `display.brightness` | `percent` 0-100 | `GET /api/set?key=lcd_brightness` (needs *Home Assistant dashboards* on) |
| `face.set` | `mood` (`neutral happy excited thinking sleepy sad`), optional `text` | A message with id `mood`: happy/excited -> `done` (Moni hops), thinking -> `waiting` (waves), sad -> `error` (slumps); neutral/sleepy delete it |
| `device.info` | | `GET /status`: model, firmware, build, host |
| `device.status` | | `GET /status`: uptime, heap, RSSI, IP, crashes, safe mode |
| `clock.face.show` | `name` (custom) or `builtin` (name or id) | `POST /api/faces/show` (key) or `GET /api/set?key=theme` (*Home Assistant dashboards*) |
| `clock.face.save` | `name`, `face`, optional `show` | `POST /api/faces` (key) |
| `clock.faces.list` | | `GET /api/faces` (key) |
| `clock.vars.set` | `vars`, optional `ttl`, `history`, `add`, `persist` | `POST /api/faces/vars` (key) |
| Not offered | input, talk, buzzer, camera, Canvas, GPIO, files, settings, firmware | Output only; no hardware for most |

`display.show`, `display.brightness` and `face.set` take the ESP32 nodes' parameters
(`heading`/`text`, `percent`, `mood`), so agents that know those nodes know these.

## Two ways to build it

### A. In the firmware (a native node): not built

Needs Ed25519 with SHA-512, a WebSocket client, the handshake with a reader that skips
`hello-ok`'s snapshot, dispatch, and a pairing page: about 18-22 KB of code, so 30-37 KB
of update room, **more than the 25 KB left**, and a second long-lived socket in about
10 KB of free heap (`ws://` only: no room for TLS). Recorded, not built, unless about
10 KB of code is won back first ([RESEARCH.md](RESEARCH.md)).

### B. A bridge beside the gateway: recommended, prototyped

- **Cost on the clock:** none. Firmware 2.2.0 as installed.
- **Where:** on the gateway's machine (or any machine that reaches both). If it stops,
  the clocks stop answering OpenClaw, as with the Claude Code plugin.
- **Secrets:** the clock holds none. The bridge holds one Ed25519 seed and device token
  per clock, and the faces API keys, in its config and `state/` folder.

## Clock settings the commands need (no firmware change)

| Commands | Needs |
|---|---|
| Messages, moods, status | Nothing (open on the network, as today) |
| Custom faces, variables | Integrations -> Custom faces on, its API key in the bridge's config |
| Brightness, built-in faces | Integrations -> Home Assistant dashboards on, which also opens photos and the face choice to the network |

## Flash and RAM budget

None: no firmware change. The bridge is about 600 lines of JavaScript on the gateway's
machine.

## Non-goals

- Running their ESP-IDF code on the ESP8266; a native node (option A) for now.
- Any input from the clock; audio, camera, Canvas, touch.
- Publishing the commands as agent-visible plugin tools (`node.pluginTools.update`) or a
  node skill (`node.skills.update`): possible later, after the plain node works with a
  real gateway.

## Open questions

- Do you run an OpenClaw Gateway today, and where (this PC, a server, the NAS)? The
  prototype is tested against a fake gateway only; a real one is needed to finish.
- Brightness and built-in faces need *Home Assistant dashboards* on (no API key for
  `/api/set`). Accept that, leave those two commands out, or a small firmware change
  later so `/api/set`'s `theme` and `lcd_brightness` take the faces API key (a few
  hundred bytes)?
- `face.set`'s `neutral` and `sleepy` clear Moni (it has no pose for them). Fine, or
  should one of them show a calm Moni (a firmware change: a new icon)?
- Pair with the gateway token or with setup codes by default (the README offers both)?

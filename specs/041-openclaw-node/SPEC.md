# 041 The clock as an OpenClaw node

Status: Draft (proposed 2026-11-05; output only decided 2026-11-05; native node chosen by
the owner 2026-11-05; goals and the source of the flash room not yet approved)

## Problem

[esp-openclaw-node](https://github.com/openclaw/esp-openclaw-node) (Apache-2.0, commit
38c5ab2) runs ESP32 boards as OpenClaw *nodes*: the board keeps a WebSocket to an
OpenClaw Gateway, says which commands it offers, and the gateway's agents call them
(`display.show`, `face.set`, `device.status`...). The owner wants the clocks to be nodes
too, so OpenClaw's agents (in a chat, a scheduled job or a heartbeat) can put things on
them.

Their code cannot run as is: it is ESP-IDF 5.x for the ESP32-S3/P4 (libsodium, cJSON,
FreeRTOS tasks, PSRAM), and the clock is an ESP8266 on the Arduino core.

## Decisions so far

- **Output only** (owner, 2026-11-05): things shown on the screen, plus read-only status.
  No input of any kind: no buttons, touch or sensors sent to the gateway, no `node.event`,
  no files read from the clock, no settings changed beyond what changes the picture
  (brightness, which face).
- **A native node, in the firmware** (owner, 2026-11-05; [decision
  record](../../docs/decisions/2026-11-05-openclaw-native-node.md)): the clock speaks the
  gateway protocol itself, with no program beside the gateway. The bridge prototyped first
  (`integrations/openclaw/`) stays as a fallback and a test tool.

## What a node is

Details and sources in [RESEARCH.md](RESEARCH.md). An Ed25519 identity; one WebSocket to
the gateway; `connect.challenge` {nonce, ts} answered by a signed `connect` (protocol 3-4,
client `node-host`, role `node`, the commands offered); a device token from `hello-ok`;
then `node.invoke.request` events answered by `node.invoke.result`. The gateway runs a
command only when the node declares it, the owner approved the node's command surface
(`openclaw nodes approve`), and the command is in `gateway.nodes.commands.allow` (none of
the clock's commands are in OpenClaw's platform defaults).

## Goals (to agree)

1. **The firmware is the node:** each clock connects to the configured gateway over
   `ws://`, pairs once, reconnects on its own, and answers the commands below by calling
   its own code (messages, faces, variables, brightness): no HTTP round trip, and no
   *Home Assistant dashboards* needed for brightness or built-in faces.
2. **Pairing from the settings page:** Integrations -> OpenClaw: the gateway's address,
   then a setup code pasted from `openclaw qr --setup-code-only` (or the gateway token);
   the page shows the device id and the state (off, connecting, waiting for approval,
   connected, refused and why).
3. **Fits the flash** with room won back first (open question 1), and **the heap**: it
   waits for enough free heap before connecting, as MQTT does, and holds one socket.
4. **Tests:** host unit tests for signing (RFC 8032 vectors) and the frame and handshake
   code; an integration case where the emulator's firmware pairs with a fake gateway and
   takes every command; screenshots.
5. **Docs:** integrations API reference, settings reference, README, the gateway steps.

## Requirements

1. Identity: an Ed25519 seed made on the clock (`ESP.random()`, the hardware RNG) when
   pairing starts; the public key and device id (SHA-256 of the key, hex) computed once and
   stored with it in `/config.json`. Secrets (seed, device token, gateway token) are never
   exported in backups and never shown again, like the faces API key.
2. Connect as the gateway expects: protocol 3-4, client id `node-host`, mode and role
   `node`, no scopes, platform `esp8266`, device family `AI Agent Monitor`, model `SD PRO`,
   the v3 signature over the challenge's nonce and `ts`.
3. Keep only the node device token from `hello-ok`; never store the setup code's bootstrap
   token after use, nor the operator token a setup code hands off.
4. Read `hello-ok` (which carries a large snapshot) as a stream, keeping only the fields
   used: never the whole frame in memory. Frames over 8 KB other than `hello-ok` are
   skipped.
5. Reconnect with back-off (5 s to 5 min); retry a pending pairing every 30 s so an
   approval is picked up; stop after a refused credential until the settings change.
6. Connect only with at least 12 KB free heap and the largest block at least 6 KB
   (measured and tuned in the emulator and on the clock); signing runs at the top of
   `loop()` with its working memory on the heap (about 3 KB, freed after), never on the
   4 KB stack.
7. Answer every `node.invoke.request` with `node.invoke.result`; unknown commands and bad
   parameters are coded errors (`INVALID_PARAMS`, `UNKNOWN_COMMAND`, `UNAVAILABLE`).
8. A gateway that is gone costs nothing but the reconnect attempts: faces, the web server
   and MQTT go on as before. Off by default.
9. No input: the node declares no capability (`caps` empty) and sends no `node.event`.

## The commands

| Command | Params | On the clock |
|---|---|---|
| `display.show` | `text`; optional `heading`, `icon`, `color`, `pct`, `ttl`, `id`, `show` (true) | An agent message (as `POST /api/message`) |
| `display.clear` | optional `id`, `all` | Removes the node's message and mood, or that one, or all |
| `display.status` | | The face shown, the messages waiting |
| `display.brightness` | `percent` 0-100 | The backlight (the setting, saved) |
| `face.set` | `mood` (`neutral happy excited thinking sleepy sad`), optional `text` | Moni: happy/excited -> `done` (hops), thinking -> `waiting` (waves), sad -> `error` (slumps); neutral/sleepy remove the mood message |
| `device.info` | | Model, firmware, build, host name |
| `device.status` | | Uptime, heap, RSSI, IP, crashes, safe mode |
| `clock.face.show` | `name` (custom) or `builtin` (name or id) | Shows that face |
| `clock.vars.set` | `vars`, optional `ttl`, `history`, `add`, `persist` | Face variables |
| `clock.faces.list` | | Custom face names, the one showing |
| Left out of the node | `clock.face.save` | A 4 KB face as an escaped string inside a frame is about 6 KB of heap at once; faces are saved over HTTP or MQTT (the bridge keeps it). Open question 4 |

Parameters and results are the bridge's (see `integrations/openclaw/README.md`), so the
two are interchangeable for agents.

## Settings

| Key | Meaning |
|---|---|
| `ocon` | OpenClaw node on/off (off) |
| `ocurl` | The gateway, `ws://host:port` (`ws://` only; port 18789 by default) |
| `ocname` | The name shown in `openclaw nodes status` (the host name) |
| `ocpair` (write only) | A setup code, or `token:<gateway token>`: starts pairing |
| `ocforget` (write only) | Forgets the key and token: the node pairs again from scratch |
| Stored, never exported | `ocseed`, `ocpub`, `ocid`, `octoken` |

`/status` gains `openclaw`: `off`, `waiting for memory`, `connecting`, `pairing pending`,
`connected`, `refused: <reason>`; and `openclawHeap`, what connecting took.

## Flash and RAM budget

| | Code (plain) | Notes |
|---|---|---|
| Ed25519 sign + SHA-512 (TweetNaCl, working memory moved to the heap) | 5.9 KB | measured for the lx106 |
| SHA-256 (device id, at pairing) | about 1.2 KB | not linked today |
| WebSocket client (handshake, masked text frames, ping/pong, close) | about 2 KB | `base64::encode` already linked |
| Handshake, `hello-ok` stream filter, invoke dispatch, results, reconnect | about 3.5 KB | the filtered `deserializeJson` on a `WiFiClient` is already linked (weather) |
| Settings, `/status`, pairing | about 1.5 KB | |
| Settings page card | about 0.5 KB gzipped | counts twice |
| **Total** | **about 14-15 KB** | **23-26 KB of update room; 24.6 KB is left** |

It does not fit with any margin: about 10 KB of room must be won back first (open
question 1). RAM: about 200 B of static data; heap while connected about 1.5-2 KB (the
socket and its buffers), about 5 KB during the handshake (the connect frame, about 1 KB,
and the signing workspace, about 3 KB, one after the other).

## Non-goals

- TLS (`wss://`): neither the flash nor the heap allows it. The gateway must be reachable
  over plain `ws://` from the clocks' network.
- Input of any kind; audio, camera, Canvas, touch.
- Agent-visible plugin tools or a node skill (`node.pluginTools.update`,
  `node.skills.update`): possible later.
- Running their ESP-IDF code.

## Open questions

1. **Where the room comes from** (about 10 KB of room; the full inventory is in
   [RESEARCH.md](RESEARCH.md#where-room-could-come-from-inventory-2026-11-05-220)).
   Recommended: first the changes that lose nothing (one ArduinoJson reader type fewer,
   3.5-7 KB of room; the weather fetch without `HTTPClient`, about 5 KB; one try at LTO),
   measured, as their own spec. If that falls short: drop MQTT (about 11 KB; .229 shows
   "cannot connect (-2)", .57 has it off), mDNS (about 9 KB), photos with the Home
   Assistant dashboards (about 16 KB), the weather icons (about 13 KB), or serve the
   settings page from storage (about 42 KB, but two files per update).
2. Do you run an OpenClaw Gateway today, and where? It has to be reachable from the IoT
   VLAN (192.168.66.x) over `ws://` (`gateway.bind lan`, a firewall rule), and a real one is
   needed to finish.
3. Signing takes about 0.4 s once per connection (estimated; measured on the clock once
   built): the screen pauses that long. Acceptable?
4. `clock.face.save` left out of the native node (heap): fine?
5. `face.set`'s `neutral` and `sleepy` clear Moni (no pose for them): fine, or a calm Moni
   (a new icon, about 300 B)?

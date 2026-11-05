# 041 The clock as an OpenClaw node

Status: In progress (approved 2026-11-05: the owner said "start building"; built on branch
`openclaw-native`, tested in the emulator against a stand-in gateway; not released)

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
6. Connect only with at least 12 KB free heap (the largest block is not checked: asking
   for it links 1.6 KB of heap statistics); signing runs at the top of
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

Measured 2026-11-05 on `openclaw-native` against `faces-v2` with spec 042 (579,008 B,
45,056 B of update room):

| | Plain bytes | Notes |
|---|---|---|
| Ed25519 sign, SHA-512, SHA-256 (`src/ed25519.cpp`; constants in flash, SHA-256 sharing SHA-512's) | about 4.6 KB | estimated 7.1 KB |
| The node (`src/openclaw.cpp`: WebSocket, handshake, `hello-ok` filter, invoke, reconnect, pairing, JSON written as text) | about 8 KB | |
| The commands (`openclawCommand` in `integrations.cpp`, its messages) | about 5.2 KB | |
| Settings (`Config`, `applySetting`, `/status`), the page's card, ArduinoJson code the node adds | about 4 KB | |
| The weather's own inlined copy of `Config::save()`, removed (`configSave()`) | -1.5 KB | found on the way |
| **Total** | **+22.7 KB** | `firmware.bin` 601,664 B, `.bin.gz` 426,724 B: **8,192 B of room left** |

The estimate was 14-15 KB: what it missed is ArduinoJson's code at every member read
or write (a `doc[F("key")]` costs 30-60 B where it is written) and the strings. The
first build was +32.6 KB and did not fit; outgoing frames and results written as text
(`jsonAdd`), reads through one path helper (`jsonAt`), dispatch by the command's place
in the declared list, shorter messages and the shared `configSave()` brought it to
+22.7 KB.

RAM: 99 B of static data and 32 B of strings. Heap: the signing workspace is 3.2 KB,
taken and given back around each signature; the connect frame is about 1 KB; `/status`
`openclawHeap` (what the socket and handshake took around it, sampled) was 1,352 B in
the emulator. Stack (`-fstack-usage`): `readFrame` 576 B (the handshake, the reading and
the invoke inlined into it), `openclawCommand` 272 B, the signing chain under it about
700 B at most (`M` 320 B, `pack25519` 272 B); it all runs from `loop()`.

## Non-goals

- TLS (`wss://`): neither the flash nor the heap allows it. The gateway must be reachable
  over plain `ws://` from the clocks' network.
- Input of any kind; audio, camera, Canvas, touch.
- Agent-visible plugin tools or a node skill (`node.pluginTools.update`,
  `node.skills.update`): possible later.
- Running their ESP-IDF code.

## Open questions

Answered when the owner said to start building (2026-11-05), with the answers this spec
recommended; any can be revisited:

1. Where the room comes from: spec 042 (nothing lost), 45,056 B of room.
3. Signing pauses the screen about 0.4 s once per connection: accepted for now; measured
   in the emulator and on the clock, and to raise if it shows.
4. `clock.face.save` stays out of the native node (heap); faces are saved over HTTP or MQTT.
5. `neutral` and `sleepy` remove Moni's mood message (no new pose).

Still open:

2. Is there an OpenClaw Gateway, and where? It has to be reachable from the IoT VLAN
   (192.168.66.x) over `ws://` (`gateway.bind lan`, a firewall rule). Until then the node
   is built and tested against the fake gateway; PLAN step 8 needs a real one.

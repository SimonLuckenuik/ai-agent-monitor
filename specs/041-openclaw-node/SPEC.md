# 041 The clock as an OpenClaw node

Status: Draft (proposed 2026-11-05; not approved)

## Problem

[esp-openclaw-node](https://github.com/openclaw/esp-openclaw-node) (Apache-2.0, commit
38c5ab2) runs ESP32 boards as OpenClaw *nodes*: the board keeps a WebSocket to an
OpenClaw Gateway, says which commands it offers, and the gateway's agents call them
(`display.show`, `face.set`, `device.status`...). The owner would like the clocks to be
nodes too, with the features that make sense on them.

It cannot run as is: it is ESP-IDF 5.x for the ESP32-S3/P4 (libsodium, cJSON, FreeRTOS
tasks, PSRAM on every listed board), and the clock is an ESP8266 on the Arduino core
with about 10 KB of heap free at rest and about 25 KB of update room left after 2.2.0
(each KB of code costs about 1.7 KB of room: [footprint](../../docs/specifications/footprint.md)).

## What a node is (from their source)

1. **Identity:** an Ed25519 key pair from a seed kept on the device; `device_id` from the
   public key.
2. **Connect:** a WebSocket (`ws://` on the LAN; `wss://` optional) to the gateway. The
   gateway sends `connect.challenge` with a nonce; the node answers `connect` with
   protocol 3-4, its client info, role `node`, scopes, `caps`, `commands`, and a
   signature over a v3 auth payload (client, role, scopes, time, token, nonce,
   platform). The first time a short-lived setup code (`openclaw qr --setup-code-only`)
   authorises it; `hello-ok` then gives a device token, saved for reconnects.
3. **Commands:** `node.invoke.request` frames with a command and JSON params; the node
   answers `node.invoke.result`. The gateway runs only commands its owner allowed
   (`gateway.nodes.commands.allow`).

## What makes sense on the clock

| Their command | On the clock |
|---|---|
| `display.show` {heading, text} | A message on the Message face (title, text), as `/api/message` |
| `display.clear` | Clear that message |
| `display.brightness` | The backlight |
| `display.status` | What is on screen: face, message, brightness |
| `face.set` {mood} | Moni's face: neutral, happy, thinking, sleepy, sad (the Message face's robot, spec 039) |
| `device.info`, `device.status`, `wifi.status` | Version, uptime, heap, IP, RSSI (as `/status`) |
| New: `clock.face`, `clock.vars` | Switch faces; push face variables (the ticker, counters) |
| Not: talk, wake word, buzzer, camera, Canvas, GPIO/ADC, touch | No hardware or no room for them |

## Two ways to build it

### A. In the firmware (a native node)

The clock speaks the gateway protocol itself.

- **Needs:** Ed25519 signing (no Ed25519 in the core's BearSSL: a small library, about
  8-12 KB of code with SHA-512), a WebSocket client (about 5 KB), the handshake and
  command dispatch (about 5 KB), a pairing step on the settings page (paste the setup
  code), the device token saved like the other secrets.
- **Cost:** roughly 18-22 KB of code, so 30-37 KB of update room: **more than the
  25 KB left.** It would need room won back first, or features dropped.
- **Heap:** a WebSocket held open beside MQTT and the big faces. Whole frames are
  buffered: `hello-ok` may carry snapshots and catalogs (their SDK allows 2 MiB), which
  the clock cannot hold; it needs a reader that skips what it does not use, and the
  real size measured from a gateway.
- **Signing time:** one Ed25519 signature per connect, likely under a second at 160 MHz.
- **Gain:** no PC needed besides the gateway.

### B. A bridge (a node on the gateway's machine) — recommended

A small program (Python, or Node, which OpenClaw itself uses) runs beside the gateway,
connects as one node per clock, offers the commands above, and turns each into the
clock's HTTP API (`/api/message`, `/vars`, `/face`, `/status`). It could use OpenClaw's
own node library where one exists.

- **Cost on the clock:** none. Works with 2.2.0 as installed, on both clocks.
- **Pairing:** the bridge pairs with a setup code like any node; it keeps the keys and
  tokens on the PC, so the clock holds no new secret.
- **Limits:** the bridge must run where the gateway does (or on any machine that can
  reach both); if it stops, the clocks stop answering agents, as with the Claude Code
  plugin.
- **Fits the project:** the clock already takes messages, faces and variables from
  agents over HTTP and MQTT; this is another client of that API, in
  `integrations/openclaw/`.

## Goals (to agree)

1. Option B: `integrations/openclaw/`, a bridge that pairs each configured clock as an
   OpenClaw node and offers the commands in the table; its own README with the gateway
   steps (`gateway.bind lan`, the allow list, the setup code).
2. Tests against a fake gateway (the challenge, the signature check, an invoke round
   trip) and the emulator.
3. Option A recorded with its measurements, not built, unless room is won back later.

## Non-goals

- Running their ESP-IDF code on the ESP8266; audio, camera, Canvas, touch.

## Open questions

- Do you run an OpenClaw Gateway today, and where (this PC, a server, the NAS)?
- Option B as recommended, or is a clock that needs no PC worth the work and flash of
  option A (and what it would cost: winning back about 10 KB first)?
- Both clocks as two nodes, or one node for both?

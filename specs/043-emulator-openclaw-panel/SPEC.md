# 043 An OpenClaw panel in the emulator

Status: Approved 2026-11-05 (the owner, in the chat: "i want the panel to connect to the
openclaw node and the emulator panel to allow me to talk with the node ? (listing the
tools, etc)")

## Problem

Spec 041 made the firmware an OpenClaw node. Trying it needs an OpenClaw Gateway, which
the owner may not run, and the gateway's own tools show little of what the node sends.
The integration tests have a stand-in gateway (`tests/integration/fake_gateway.py`),
but only scripts can drive it.

## Goals

1. The emulator comes with a stand-in gateway the emulated clock can pair with, in the
   same `docker compose` as the emulator.
2. An **OpenClaw** panel on the emulator's screen page (http://127.0.0.1:8080):
   - pair the emulated clock in one click (a setup code made by the gateway, given to
     the firmware), or approve it when approval is required;
   - the node as the gateway sees it: device id, name, firmware, platform, the credential
     it used, since when it is connected;
   - a checklist of what the gateway checked in its `connect`: protocol range, client id,
     mode and role, the challenge's nonce and time, device id = SHA-256 of the key, the
     Ed25519 signature, no capabilities;
   - its **commands** (the tools it declares), each with example parameters to edit and
     run, and the result (or the error) with the time it took;
   - the frames both ways, newest first;
   - drop the connection, revoke the device token, forget the clock's key, turn it off.
3. The same gateway serves the integration tests (one file, not two).

## Non-goals

- The real OpenClaw Gateway in the emulator (heavy, needs model providers; spec 041
  RESEARCH keeps it as a manual check).
- OpenClaw's own schema validators (`@openclaw/gateway-protocol`): they would need npm
  packages downloaded into the image; possible later.
- Anything in the firmware.

## Behaviour

- `emulator/openclaw_gateway.py` (moved from `tests/integration/fake_gateway.py`):
  WebSocket on 18789 for the clock (as `ws://gateway:18789` inside the compose network),
  its control API on 8790, published on 127.0.0.1:8790 only, with CORS so the screen
  page can call it. New: `POST /setup` (a single-use bootstrap setup code), `POST /config`
  (`requirePairing`), the frames log, the connect checklist, the pending devices, and the
  node's details in `/state`.
- The emulator's screen server gains `GET /openclaw` (the node's settings and state) and
  `POST /openclaw/pair` (a setup code), `/openclaw/off`, `/openclaw/forget`: the firmware's
  own `openclawPair()` and `openclawForget()`, as the settings page would call them.

## Budget

Emulator only: no flash or RAM on the clock.

## Open questions

None.

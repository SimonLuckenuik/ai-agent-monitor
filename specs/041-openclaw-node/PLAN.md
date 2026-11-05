# Plan

A native node in the firmware (the owner's choice, 2026-11-05). Approved 2026-11-05; the
room came from spec 042. Lands in a MINOR release (2.3.0): a new feature, installed as the
usual `.bin.gz`.

State (2026-11-05, branch `openclaw-native`): steps 1-7 done (step 1 is spec 042; step 6's
gateway is `tests/integration/fake_gateway.py`, run as a compose service rather than
inside the emulator); step 8 waits for a real gateway; step 9's docs done, the release
not.

## Steps

1. **Win the room back** as decided (open question 1), alone, in its own commit: measure
   with `pio run` (the room must reach about 35 KB), update the footprint document, the
   settings page and docs for what goes away. *Check:* unit, safety, the integration suite.
2. **Signing** (`src/ed25519.c`, `src/ed25519.h`): TweetNaCl's sign and SHA-512 only
   (public domain), its temporaries moved from the stack to one heap workspace, a `yield()`
   in the 256-step loop; SHA-256 for the device id. *Check:* host unit tests with RFC 8032
   vectors 1-3 and a v3 payload signed here and verified by Node's `crypto` (the bridge's
   tests); `-fstack-usage` for the frame sizes; size with `pio run`.
3. **WebSocket client** (`src/ws.cpp`): connect, `Upgrade` handshake (random key; the
   reply checked for 101), masked text frames out, frames in (text, ping -> pong, close),
   a frame header reader that hands the payload to the caller as the `WiFiClient` stream.
   *Check:* host tests on frame encoding and decoding; the emulator against the fake gateway.
4. **The node** (`src/openclaw.cpp`): state machine (off, waiting for memory, connecting,
   challenge, connect sent, connected, pending, refused), the connect frame (ArduinoJson
   to a `String`), `hello-ok` through the existing filtered `deserializeJson` on the
   stream, the device token saved, `node.invoke.request` parsed with a filter, the command
   table calling the existing message, variable, face and brightness code, results sent,
   reconnect back-off, the heap guard, `/status` fields.
   *Check:* the integration case below.
5. **Settings and page:** `ocon`, `ocurl`, `ocname`, `ocpair`, `ocforget` in
   `applySetting()`, `configJson()`, `Config` (secrets not exported), a card under
   Integrations with the device id, the state and the allow-list block to copy.
   *Check:* `tests/web`, the settings reference, the page screenshot.
6. **Emulator and tests:** a fake gateway for the integration tests (Python, standard
   library, the same checks as `integrations/openclaw/test/fake-gateway.mjs`), a case
   `openclaw_node`: pair with a setup code, reconnect with the device token, every
   command, a refused token, a gateway that disappears; screenshots of Moni's moods via
   `face.set`. New sources in `emulator/build.sh` and the host test copy lists.
7. **Measure:** flash (`pio run`, link map), heap during the handshake and while connected
   (`/status`, the emulator's Device panel), the stack (`-fstack-usage`), the signing time
   in the emulator's log (modelled) and, on the owner's go, on a clock.
8. **With a real gateway** (the owner's): pair both clocks, approve, allow, invoke from
   `openclaw nodes invoke`, an agent and a scheduled job.
9. **Docs and release:** `docs/specifications/integrations-api.md` (an OpenClaw section),
   `settings.md`, README, CHANGELOG, STATUS, roadmap; the Claude Code skill only if its
   API reference mentions the new `/status` fields. Release 2.3.0 by the usual steps; the
   owner installs it.

## How each is checked

| What | How |
|---|---|
| Signing | RFC 8032 vectors in `tests/unit`; a signature checked by Node's Ed25519 |
| Frames and handshake | Host tests; the emulator against the fake gateway |
| Commands | The integration case `openclaw_node`, screenshots |
| Flash, heap, stack | `pio run` and the size guard; `/status`; `-fstack-usage` |
| Real gateway, real clock | Step 8 and the clock check after release, with the owner |

## Kept from the bridge

`integrations/openclaw/` stays on the branch: its fake gateway checks the firmware node's
signatures in step 2, and it remains a fallback if the room cannot be found. Whether it
ships with the release is decided at step 9.

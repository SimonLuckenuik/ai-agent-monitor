# Plan

Option B, a bridge in `integrations/openclaw/`. No firmware change, so no release of the
firmware; the bridge ships with the repository (and the CHANGELOG) once approved.

## Steps

1. **Protocol** (`lib/protocol.mjs`): Ed25519 identity from a stored seed, device id,
   v3 payload and signature, the `connect` request, setup-code decoding, and
   `NodeConnection` (challenge, hello-ok, device token, invoke loop, timeouts,
   reconnect back-off). *Check:* unit tests, including the RFC 8032 vector and OpenClaw's
   own payload builder. **Done (prototype).**
2. **Clock client** (`lib/clock.mjs`): the clock's HTTP API with errors turned into
   `INVALID_PARAMS`, `NOT_CONFIGURED`, `UNAVAILABLE`, `CLOCK_ERROR`. **Done (prototype).**
3. **Commands** (`lib/commands.mjs`): the table in SPEC, parameter checks, clipping, the
   mood map. *Check:* round trips through the fake gateway and fake clock. **Done
   (prototype).**
4. **CLI and state** (`bridge.mjs`): config (`clocks.json`, `env:` secrets), one
   connection per clock, `pair <clock> <code>`, `info` (device ids, the allow-list block),
   state files written atomically with mode 600. **Done (prototype).**
5. **Tests** (`test/`): `bridge.test.mjs` (fake gateway: a small RFC 6455 server that
   checks the signature; fake clock with the firmware's auth rules) and
   `emulator.test.mjs` (the firmware in Docker, screenshots). **Done (prototype).**
6. **With a real gateway** (needs the owner's gateway): pair both clocks, approve the
   surface, add the allow list, invoke each command from `openclaw nodes invoke` and from
   an agent and a scheduled job; note anything the fake gateway got wrong (error codes on
   a pending pairing, `hello-ok` size, tick handling). **Not done.**
7. **Docs on approval:** README (written), `docs/specifications/integrations-api.md` (a
   short "OpenClaw" section pointing at the bridge), CHANGELOG `## Unreleased`, roadmap,
   STATUS, specs/README. The Claude Code skill is not affected (no API change).
8. **Later, if wanted:** agent-visible plugin tools (`node.pluginTools.update`) or a node
   skill describing the clock's face format; a firmware change letting `/api/set` take the
   faces API key for `theme` and `lcd_brightness`.

## How each is checked

| What | How |
|---|---|
| Handshake and signature | `node --test test/bridge.test.mjs` (fake gateway rebuilds the payload with OpenClaw's builder and verifies) |
| Commands | Same file, against the fake clock; `test/emulator.test.mjs` against the firmware |
| Screens | Emulator screenshots after each step (`AAM_SHOTS`) |
| Real gateway | Step 6, with the owner |
| The clocks | Not touched by the tests; on the owner's go, `display.show` to one clock |

## Lands in

The repository only (no firmware version). The bridge's own version is in its
`package.json` (0.1.0 for the prototype).

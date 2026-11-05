# Progress

- 2026-11-05: proposed, after reading esp-openclaw-node (38c5ab2): its node protocol
  (Ed25519 identity, WebSocket, connect.challenge, node.invoke), its commands, and the
  clock's flash and heap. A native node needs more update room than is left; a bridge
  on the gateway's machine needs none.
- 2026-11-05: the owner chose **output only**: things shown on the screen, plus read-only
  status. SPEC updated (decision, goals, requirements, command table).
- 2026-11-05: research (RESEARCH.md): read the gateway side in openclaw/openclaw 92d5d16:
  handshake and node schemas, the command policy (custom commands need
  `gateway.nodes.commands.allow`; device approval and command-surface approval are
  separate), setup codes (they hand off an operator token too), the `nodes` tool and
  scheduled jobs, `@openclaw/gateway-client` on npm (operator-oriented; not used). Nothing
  blocks option B. Option A recorded, not built (30-37 KB of room needed, 25 KB left).
- 2026-11-05: prototype bridge in `integrations/openclaw/` (branch `openclaw-node`), Node
  22+, no packages: `lib/protocol.mjs`, `lib/clock.mjs`, `lib/commands.mjs`,
  `bridge.mjs` (run, `pair <clock> <code>`, `info`), README. 11 commands.
- 2026-11-05: `node --test test/bridge.test.mjs`: 16 pass. Covers the RFC 8032 vector,
  the v3 payload against OpenClaw's own builder (copied into the fake gateway, MIT) and
  the ESP32 node's format, a forged signature refused, pairing with the gateway token,
  pairing to approve (retries until approved), a setup code used once then the device
  token on reconnect, the operator hand-off token never stored, every command against a
  fake clock with the firmware's auth rules, bad parameters, an unknown command, a clock
  that does not answer.
- Found on the way: `/api/set` (brightness, built-in faces) does not take the faces API
  key, only a sign-in or the *Home Assistant dashboards* integration. The bridge uses the
  latter and says so in its error; a firmware change is an open question, not done.
- 2026-11-05: `test/emulator.test.mjs` against the firmware (2.2.0, built from this
  branch) in a dedicated emulator container (ports 28000/28080): pass. Every command
  through the fake gateway and the bridge to the firmware; screens in
  [emulator.png](emulator.png): `display.show` with `waiting` (Moni waves), `face.set`
  happy (hops) and sad (slumps, red), a custom face saved by `clock.face.save` with
  `clock.vars.set` values, `clock.face.show` built-in Big time. The first run was at the
  container's 23:40, in night hours: dimmed, and Moni not drawn (by design at night). The
  test now turns night mode off.

## Not verified

- No real OpenClaw Gateway: whether the owner runs one is open. The fake gateway follows
  the docs and schemas; real error codes for a pending pairing, the first-surface approval
  after a setup code, and tick handling are to check (PLAN step 6).
- Not run against the real clocks.

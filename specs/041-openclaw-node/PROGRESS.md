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

- 2026-11-05: the owner wants **a native node** ("an actual node, not just using a
  bridge"). [Decision record](../../docs/decisions/2026-11-05-openclaw-native-node.md).
  Measured: 24,576 B of update room on 2.2.0; TweetNaCl's Ed25519 + SHA-512 is 5,898 B on
  the lx106 (Monocypher 20,576 B); signing about 0.4 s (estimated from the source); 24-27 KB
  of heap free at rest on both clocks. The whole node comes to about 14-15 KB of code, 23-26
  KB of room: about 10 KB of room must be won back first. SPEC and PLAN rewritten for the
  native node; the bridge stays as a fallback and test tool. Still Draft: waiting for the
  owner's choice of where the room comes from, and approval.
- Not done: compiling or running the downloaded TweetNaCl to measure its stack and time
  (blocked in this session as code from outside); planned in PLAN step 2 once it is
  vendored into `src/`.

- 2026-11-05: the room: spec 042 (branch `flash-room-042`, prototyped) frees 20,480 B
  with nothing lost (one JSON parser per input kind, the weather without HTTPClient): 45,056
  B of room, enough for the node with about 20 KB to spare. LTO tried: +14.9 KB, dropped.
- 2026-11-05: testing without a real OpenClaw (RESEARCH): no ready-made node tester
  exists; OpenClaw's `@openclaw/gateway-protocol` (MIT) has the validators to build one.
  Proposed: a test gateway service and an OpenClaw panel on the emulator page.

- 2026-11-05: **approved** ("start building"), the open questions taken with the
  recommended answers (SPEC). Built on branch `openclaw-native` (from `faces-v2` with spec
  042, `openclaw-node` merged in):
  - `src/ed25519.cpp`: Ed25519 signing (TweetNaCl's arithmetic, written here, workspace on
    the heap), SHA-512, SHA-256. Host tests (`tests/unit/openclaw_tests.cpp`, 21 checks):
    RFC 8032 vectors 1-3, FIPS SHA-256 vectors, base64url, WebSocket headers. All pass.
  - `src/openclaw_proto.cpp`: base64url and WebSocket frame headers.
  - `src/openclaw.cpp`: the node (WebSocket client, challenge, signed connect, `hello-ok`
    read through a filter, invoke, reconnect with back-off, pairing pending every 30 s,
    refusal stops, ping after 20 s of quiet, gone after 45 s), pairing from a setup code or
    `token:`/`password:`, `/status` fields.
  - `openclawCommand()` in `integrations.cpp`: the 10 commands on the clock's own code.
  - Settings `ocon`, `ocurl`, `ocname`, `ocpair`, `ocforget`; secrets `ocseed`, `ocpub`,
    `ocid`, `octoken`, `ocauth` stored, never in `/config` or backups. The settings page's
    card (Integrations -> OpenClaw node).
  - `tests/integration/fake_gateway.py`: a stand-in gateway (standard library; its own
    Ed25519 verifier, checked against RFC 8032), a compose service; the case `openclaw_node`.
- 2026-11-05: flash. The first build was +32.6 KB and did not fit (45,056 B of room).
  ArduinoJson's code at each `doc[F("key")]` and the strings were most of it; writing
  frames and results as text, one path helper for reads, dispatch by index, shorter
  messages, and `configSave()` (the header's `Config::save()` was inlined, 1.5 KB, into
  each file calling it once: the weather had its own copy too) brought it to +22.7 KB:
  `firmware.bin` 601,664 B, `.bin.gz` 426,724 B, 8,192 B of room left (SPEC table).
  [Decision record](../../docs/decisions/2026-11-05-openclaw-node-flash.md).
- 2026-11-05: the integration case passes in the emulator (112 s): pairing pending until
  approved, then connected; the gateway verified the v3 signature; the connect frame
  (client, platform, family, version, no caps or scopes, the 10 commands); the device
  token kept and the credential dropped, the operator hand-off token not stored, no
  secret in `/config`; every command (screens: Moni waving, hopping, slumping; a custom
  face with `clock.vars.set` values; built-in faces 7 and 6), refused parameters
  (`INVALID_PARAMS`), an unknown command (`UNKNOWN_COMMAND`); the gateway dropped: back
  with the device token; *Forget* and a bootstrap setup code: a new identity; the token
  revoked: refused, and no retries. Signing in the emulator: 4-8 ms (the host's speed,
  not modelled); `openclawHeap` 1,352 B.
- Found on the way: the emulator's sockets do not report a peer's close (`connected()`
  stays true), so the clock notices a dropped gateway only by its ping (45 s at most);
  on the chip lwIP reports it at once. The bridge's built-in face names were wrong for
  the Day tiles (`day-4` was 13; it is 6): fixed in the bridge and its test.
- Left: the full integration suite, unit, safety and face preview on the final build
  (below); a real gateway and the real clock (PLAN step 8); the release.

- 2026-11-05: **with the real OpenClaw Gateway** (OpenClaw 2026.11.1, `ghcr.io/openclaw/openclaw`,
  `gateway --dev --allow-unconfigured --bind lan --auth token`, no model provider), the
  emulated clock on the same Docker network:
  - the gateway accepted the clock's signature (`phase=auth_validated`), refused it as
    `not-paired` with a request id; the clock showed "pairing pending" and kept asking;
  - `openclaw devices approve`, then `openclaw nodes approve`: connected, listed as
    "AI Agent Monitor, hw: SD PRO, esp8266";
  - commands refused until `gateway.nodes.commands.allow` lists them ("not in the allowlist
    for platform esp8266"), as the research said; and the first node approval, made before
    the allow list, approved **no** commands ("the node did not declare any supported
    commands", `pending-reapproval` after a reconnect): the allow list has to come first;
  - then all 12 calls through `openclaw nodes invoke` returned what the stand-in had:
    display.show/status/clear, face.set happy/sad/neutral, brightness, device.info/status,
    vars, faces list, face.show; bad parameters came back as `INVALID_PARAMS: ...`;
  - a real setup code (`openclaw qr --setup-code-only`): `{url, bootstrapToken, expiresAtMs}`;
    over `ws://` OpenClaw limits it, so the device still needs approving; pairing from it
    worked with a new identity;
  - **bug found and fixed**: during the gateway's restart it answers `UNAVAILABLE gateway
    starting; retry shortly`, which the node took as a final refusal. Now only codes with
    `AUTH` or `INVALID_REQUEST` stop it; others are retried (`gateway: <code>` in /status).
    The stand-in can now answer that way (`unavailable`), and `openclaw_node` checks it.
  - `emulator/openclaw_real.py` and the compose service `openclaw` (profile `openclaw`)
    repeat this. Image 601,808 B, room unchanged (8,192 B).
- Still not verified: an agent calling the clock (needs a model provider), the real clocks.

## Not verified

- The signing time on the clock (estimated 0.4 s; the emulator does not model it), the
  heap during the handshake there, and the owner's own gateway (where it runs is open). The fake gateway follows
  the docs and schemas; real error codes for a pending pairing, the first-surface approval
  after a setup code, and tick handling are to check (PLAN step 6).
- Not run against the real clocks.

## 2026-11-05: released in 2.3.0

- Merged into `faces-v2` and released as 2.3.0 (the owner: "commit push merge + release").
  Checked before the release: the firmware build and size guard, the unit, settings page
  and safety tests. Not yet on the real clock.

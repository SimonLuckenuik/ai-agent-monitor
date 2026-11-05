# Research

Sources read on 2026-11-05: [openclaw/esp-openclaw-node](https://github.com/openclaw/esp-openclaw-node)
at 38c5ab2 (Apache-2.0), [openclaw/openclaw](https://github.com/openclaw/openclaw) at
92d5d16 (MIT: the gateway, `docs/`, `packages/gateway-client`, `packages/gateway-protocol`),
and this repository's firmware (2.2.0).

## The node protocol (what a node must do)

From `docs/gateway/protocol/handshake.md`, `packages/gateway-protocol/src/schema/nodes.ts`
and `components/esp-openclaw-node/src/esp_openclaw_node_protocol.c`:

1. **Transport:** one WebSocket to the gateway (`ws://` on the LAN or loopback, `wss://`
   otherwise). Text frames of JSON: `{"type":"req","id","method","params"}`,
   `{"type":"res","id","ok","payload"|"error"}`, `{"type":"event","event","payload"}`.
   Default port 18789.
2. **Challenge:** right after the socket opens the gateway sends
   `connect.challenge` `{nonce, ts}`. `ts` (integer ms) is the `signedAt` to sign.
3. **Connect:** `connect` with `minProtocol` 3, `maxProtocol` 4 (nodes are accepted from
   protocol 3, `MIN_NODE_PROTOCOL_VERSION`; 4 is current), `client` {`id`: `node-host`
   (client ids are a fixed list, `GATEWAY_CLIENT_IDS`), `mode`: `node`, `displayName`,
   `version`, `platform`, `deviceFamily`, `modelIdentifier`}, `role`: `node`, `scopes`: [],
   `caps`, `commands` (the names offered), `auth` (one of `bootstrapToken`, `token`,
   `password`, `deviceToken`), `locale`, `userAgent`, and `device` {`id`, `publicKey`,
   `signature`, `signedAt`, `nonce`}.
4. **Identity:** Ed25519. `publicKey` is the raw 32-byte key in base64url without padding;
   `device.id` is the lower-case hex SHA-256 of that raw key.
5. **Signature:** Ed25519 over the UTF-8 string
   `v3|deviceId|clientId|clientMode|role|scopes(csv)|signedAt|token|nonce|platform|deviceFamily`,
   with `platform` and `deviceFamily` trimmed and lower-cased (A-Z only), and `token` the
   auth value (empty for a password). The gateway rebuilds it from the connect params and
   compares byte for byte (`packages/gateway-client/src/device-auth.ts`
   `buildDeviceAuthPayloadV3`; the ESP32 node's `"v3|%s|%s|%s|%s|%s|%" PRId64 "|%s|%s|%s|%s"`).
6. **hello-ok:** `res` ok with `payload.type` `hello-ok`, `protocol`, `server`, `features`,
   `snapshot` (can be large), `policy` (`maxPayload` 25 MiB, `tickIntervalMs` 15000) and
   `auth` {`role`, `scopes`, `deviceToken`}. The device token is the credential for
   reconnects. A setup-code connect also returns `auth.deviceTokens` with an
   **operator** token (full mobile hand-off, or limited with `--limited`/`--voice-node`,
   and limited automatically over plain LAN `ws://`).
7. **Invoke:** the gateway sends the event `node.invoke.request`
   {`id`, `nodeId`, `command`, `paramsJSON`, `timeoutMs`, `idempotencyKey`}; the node
   answers with the request `node.invoke.result` {`id`, `nodeId`, `ok`, `payloadJSON` or
   `payload`, `error` {`code`, `message`}}. `NODE_NOT_READY` is retried by the gateway;
   other errors are not.
8. **Optional, not used yet:** `node.pluginTools.update` publishes agent-visible tool
   descriptors {`pluginId`, `name`, `description`, `parameters`, `command`} for commands
   already approved; `node.skills.update` publishes a node's skills (SKILL.md text, up to
   64 KB). `node.event` sends events to the gateway (input: out of scope).
9. **Setup code:** `openclaw qr --setup-code-only` prints base64url JSON
   {`url`, one of `bootstrapToken` / `token` / `password`}. The bootstrap token is short
   lived and single use.

## The gateway side (how agents reach a node)

From `docs/nodes/command-policy.md`, `docs/nodes/pairing-and-status.md`,
`src/gateway/node-command-policy.ts`:

- **Pairing is two approvals.** Device pairing admits the connection
  (`openclaw devices approve <deviceRequestId>`); the declared command surface is a
  second request (`openclaw nodes pending`, `openclaw nodes approve <nodeRequestId>`).
  Admin-minted bootstrap enrolment can approve the first surface. Pending device requests
  expire 5 minutes after the last retry, so a node should keep retrying (headless hosts cap
  their back-off at 30 s).
- **Allow list.** A command runs only if the node declares it, its surface is approved, and
  the gateway's allowlist has it. The allowlist is per platform; a platform it does not
  know (`esp8266`) gets the "unknown" defaults (camera, location, `system.notify`), so
  every clock command must be added to `gateway.nodes.commands.allow`. `commands.deny`
  always wins.
- **Agents** call node commands through the `nodes` tool (`invoke`, with
  `invokeTimeoutMs`), and operators through `openclaw nodes invoke --node <id|name>
  --command <cmd> --params <json>` or the `node.invoke` RPC.
- **Scheduled work:** heartbeats were migrated to ordinary scheduled jobs
  (`docs/automation/cron-jobs.md`). An agent-turn job uses the owner's tools (`*`) or a
  `--tools` list, so it reaches the clock when `nodes` is in its tools; command payloads
  run as admin automations and can run `openclaw nodes invoke`.
- **Plugin tools** published by a node are trusted by default
  (`gateway.nodes.pluginTools.enabled`), filtered to the approved surface.

## Libraries

- **`@openclaw/gateway-client`** (npm, MIT, 2026.11.1; deps `ws`, `ipaddr.js`,
  `@openclaw/gateway-protocol`): the reference TypeScript client. Built around operator
  sessions (chat, session projection, readiness); it has the device-auth payload builder
  and node protocol helpers, but no ready node runtime with a command table. Usable, but
  pulls three packages to save about 150 lines.
- **Python:** no OpenClaw node library found. A Python bridge would need `cryptography`
  (Ed25519) and `websockets`.
- **The headless node host** (`openclaw node run`) runs on a desktop and offers
  `system.run`, browser proxy and MCP; it has no way to add custom commands short of an
  MCP server, which would expose tools, not node commands.
- **Chosen:** Node 22+ with no packages: `WebSocket` (global since 22), Ed25519 and
  SHA-256 in `node:crypto`, `fetch`. The gateway itself needs Node, so it is there.
  Checked against the RFC 8032 test vector and OpenClaw's own payload builder (copied
  into the test's fake gateway).

## The clock's API for each command

| Command | Clock call | Who may call it |
|---|---|---|
| `display.show`, `face.set`, `display.clear` | `POST /api/message`, `DELETE /api/message?id=` | Anyone on the network |
| `display.status` | `GET /status`, `GET /api/message` | Anyone |
| `device.info`, `device.status` | `GET /status` | Anyone |
| `clock.face.save`, `clock.face.show` (custom), `clock.faces.list`, `clock.vars.set` | `POST /api/faces`, `/api/faces/show`, `GET /api/faces`, `POST /api/faces/vars` | Faces API key (`X-Api-Key`) |
| `display.brightness`, `clock.face.show` (built-in) | `GET /api/set?key=lcd_brightness` / `key=theme` | Signed-in session, or anyone while *Home Assistant dashboards* (`hacompat`) is on. The API key is **not** accepted (`handleApiSet`, `src/main.cpp`) |

Moni (spec 039) is drawn for the icons `waiting` (waves), `done` (hops) and `error`
(slumps, screen flash) only. OpenClaw's moods (`neutral happy excited thinking sleepy sad`,
the M5Stack Dial example) map to those three; `neutral` and `sleepy` have no Moni pose, so
they clear the mood message.

## A native node (option A), measured against the room left

- Code it needs: Ed25519 sign + SHA-512 (no Ed25519 in the core's BearSSL; a compact C
  implementation such as Monocypher's or orlp/ed25519 is about 8-12 KB of code), a
  WebSocket client (about 5 KB), the handshake, a reader that skips `hello-ok`'s
  `snapshot` without holding it, and invoke dispatch (about 5 KB), a pairing step on the
  settings page: about 18-22 KB of code, 30-37 KB of update room at 1.7 KB of room per KB
  of code ([footprint](../../docs/specifications/footprint.md)). About 25 KB of room is left
  after 2.2.0: **it does not fit.**
- Heap: about 10 KB free at rest. `hello-ok` carries the snapshot (the gateway's
  `maxPayload` is 25 MiB; the ESP32 SDK allows 2 MiB); a TLS socket alone needs more than
  is free, so `ws://` only. A second long-lived socket beside MQTT competes with the
  weather fetch and the big faces.
- Not built. It would need about 10 KB of code won back first.

## Options weighed for the bridge

| Option | Why not (or why) |
|---|---|
| Bridge in Node, no packages | **Chosen.** Runs wherever the gateway runs; nothing to install; small enough to audit |
| Bridge on `@openclaw/gateway-client` | Three packages and an operator-oriented API for ~150 lines of protocol |
| Bridge in Python | Needs two packages; Python is not guaranteed on the gateway's machine |
| One node for both clocks | A command would need a `clock` parameter, and the gateway's per-node approval, naming and status would not tell the clocks apart. One node per clock fits `nodes status` and `--node "Desk clock"` |
| MCP server or OpenClaw plugin instead of a node | Possible (the Claude Code plugin already does this for Claude), but not what spec 041 asked: the node model gives per-device pairing, approval and status in OpenClaw's own UI |

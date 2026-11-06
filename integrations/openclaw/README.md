# OpenClaw bridge

Lets [OpenClaw](https://docs.openclaw.ai)'s agents use AI Agent Monitor clocks as
**nodes**: each clock shows up in `openclaw nodes status` and takes commands such as
`display.show` and `face.set`, from a chat, a scheduled job or a heartbeat. The
bridge runs beside the gateway, pairs each clock as a node of its own, and turns
each command into a call to the clock's HTTP API. The clocks need no firmware
change and hold no OpenClaw secret.

**Output only.** The commands show things on the screen and read the clock's status.
Nothing goes the other way: no buttons, no sensors, no files (spec 041).

```
agent -> OpenClaw Gateway --WebSocket (node protocol)--> bridge --HTTP--> clock
```

## Commands

| Command | Params | On the clock |
|---|---|---|
| `display.show` | `text`, optional `heading`, `icon`, `color`, `pct`, `ttl`, `id`, `show` | A message on the Message face (`POST /api/message`). `heading` is the title; `icon` one of `info success warning error waiting running bell done` (`waiting`, `done`, `error`: Moni the robot) |
| `display.clear` | optional `id`, or `all: true` | Takes the bridge's message and mood away (or that message, or all) |
| `display.status` | | The face shown and the messages waiting |
| `display.brightness` | `percent` 0-100 | The backlight (needs *Home Assistant dashboards*, below) |
| `face.set` | `mood`, optional `text` | Moni: `happy`/`excited` hops, `thinking` waves, `sad` slumps; `neutral`/`sleepy` clear it |
| `device.info` | | Model, firmware, build, host name |
| `device.status` | | Uptime, heap, WiFi signal, IP, crashes, safe mode |
| `clock.face.show` | `name` (custom face) or `builtin` | Switches face. Built-in names: `classic weather photo big-date simple forecast big-time big-weather agents today day-1 (tomorrow) day-2 day-3 day-4` (as the firmware node; needs *Home Assistant dashboards*) |
| `clock.face.save` | `name`, `face` (face JSON), optional `show` | Saves a custom face (the [face format](../../docs/specifications/integrations-api.md#face-json)) |
| `clock.faces.list` | | Custom faces, the one showing, the variables |
| `clock.vars.set` | `vars` {name: value}, optional `ttl`, `history`, `add`, `persist` | Face variables (`POST /api/faces/vars`) |
| `clock.help` | | Links to the OpenClaw skill for the clock's firmware, as the firmware node answers (spec 045) |

`display.show`, `display.brightness` and `face.set` take the same parameters as on
OpenClaw's ESP32 nodes (`heading`/`text`, `percent`, `mood`), plus the clock's own.
Text longer than the clock shows (title 40, text 160 characters) is shortened, and the
result says `clipped: true`. Errors come back as `INVALID_PARAMS`, `NOT_CONFIGURED`
(a clock setting or the API key is missing: the message says which), `UNAVAILABLE`
(the clock did not answer) or `UNKNOWN_COMMAND`.

## Teaching the agents

Install the OpenClaw skill on the gateway's machine as well
([skill/README.md](skill/README.md)). It tells the agents what each command takes and how
to design custom faces. With the bridge, the skill saves faces through
`clock.face.save`, so the gateway's machine needs no HTTP access of its own to the clock
beyond the bridge's. Without the skill, an agent can still call `clock.help`: it answers
with links to the skill, and to its `SKILL.md` as plain text.

## What each clock needs

| For | Clock setting |
|---|---|
| Messages, moods, status | Nothing: open on the network |
| Custom faces and variables | Integrations -> Custom faces on; its API key in the bridge's config |
| Brightness, built-in faces | Integrations -> Home Assistant dashboards on. This opens the face choice, brightness and photos to anyone on the network (the clock's `/api/set` takes no API key for them) |

## Run it

Node 22 or later, on the gateway's machine (or any machine that reaches both the
gateway and the clocks). No packages to install.

1. Copy `clocks.example.json` to `clocks.json`, list the clocks (name, URL, faces API
   key), and point `gateway.url` at the gateway (`ws://127.0.0.1:18789` on the same
   machine). Secrets can be `"env:NAME"` to read them from the environment.
2. Allow the commands on the gateway. Commands a node invents are not allowed by
   default; add them to `openclaw.json` (`node bridge.mjs info` prints the block):

   ```json5
   { gateway: { nodes: { commands: { allow: [
     "display.show", "display.clear", "display.status", "display.brightness", "face.set",
     "device.info", "device.status", "clock.face.show", "clock.face.save",
     "clock.faces.list", "clock.vars.set", "clock.help"] } } } }
   ```
3. Pair, one of two ways:
   - **Gateway token** (same machine): set `gateway.token` (or `password`) and run
     `node bridge.mjs`. Each clock asks to pair; approve the device, then its commands:
     ```bash
     openclaw devices list
     openclaw devices approve <deviceRequestId>
     openclaw nodes pending
     openclaw nodes approve <nodeRequestId>
     ```
   - **Setup code**, per clock: `openclaw qr --setup-code-only`, then
     `node bridge.mjs pair desk <code>`. The bootstrap token is used once and never
     stored. A setup code also hands off an *operator* token; the bridge drops it (it
     only needs the node role). Check `openclaw nodes pending` for the command approval.
4. From then on `node bridge.mjs` reconnects with each clock's saved device token
   (in `state/<clock>.json`, with its key: keep that folder private).

Try it:

```bash
openclaw nodes status
openclaw nodes invoke --node "Desk clock" --command display.show --params '{"heading":"OpenClaw","text":"Hello from the gateway.","icon":"done"}'
openclaw nodes invoke --node "Desk clock" --command face.set --params '{"mood":"thinking","text":"Waiting for your OK"}'
```

Agents reach the same commands through OpenClaw's `nodes` tool (`invoke`); scheduled
jobs and heartbeat-style automations do it when their tool policy includes `nodes`.

## Tests

```bash
node --test test/bridge.test.mjs      # protocol, fake gateway + fake clock round trips
```

`test/emulator.test.mjs` runs the same commands against the firmware in the Docker
emulator (instructions at the top of the file).

## How it speaks to the gateway

The node side of OpenClaw's gateway protocol (protocol 3-4), as in
[esp-openclaw-node](https://github.com/openclaw/esp-openclaw-node) and the gateway's
`docs/gateway/protocol/handshake.md`:

- An Ed25519 key per clock (seed in `state/<clock>.json`); the device id is the SHA-256
  of the public key, in hex.
- The gateway sends `connect.challenge` {nonce, ts}; the bridge answers `connect` with
  `client.id` `node-host`, mode and role `node`, no scopes, the commands, and a signature
  over `v3|deviceId|clientId|mode|role|scopes|ts|token|nonce|platform|deviceFamily`.
- `hello-ok` gives the device token, saved for reconnects.
- The gateway sends `node.invoke.request` {id, nodeId, command, paramsJSON}; the bridge
  calls the clock and answers with `node.invoke.result` {id, nodeId, ok, payloadJSON |
  error {code, message}}.

Reconnects back off from 1 s to 30 s; a refused pairing is retried every 30 s, so an
approval is picked up without a restart.

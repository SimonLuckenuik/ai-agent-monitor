# The OpenClaw skill

`ai-agent-monitor/` is an [OpenClaw](https://docs.openclaw.ai) skill. It teaches
OpenClaw's agents how to use an AI Agent Monitor clock (spec 044).

The clock's OpenClaw node tells the Gateway only the names of its commands. An agent
that has this skill also knows:

- what each command takes, its limits, and when to use it;
- how to write messages that read well on a 240×240 screen;
- how to design, check and save **custom faces**. The full face format is included.

Nothing runs on the clock: the skill is a folder of instructions and references,
installed on the Gateway's machine.

```
agent ── reads ──> skill (on the Gateway's machine)
  │
  ├── nodes tool, invoke ──> Gateway ──WebSocket──> clock (messages, moods, values, faces to show)
  └── exec: clock.py push ──────────── HTTP ──────> clock (saving a custom face)
```

## What is in it

| File | What |
|---|---|
| `SKILL.md` | For the agent: when to use the clock; finding its node; the commands with their parameters; good messages; custom faces; safety |
| `reference/face-format.md` | Every item type, placeholder, expression, palette and limit of a face |
| `reference/face.schema.json` | The JSON Schema of a face |
| `reference/api.md` | The clock's HTTP API |
| `scripts/clock.py` | Checks, saves, fills, shows and deletes faces over HTTP (Python 3, standard library) |
| `examples/*.json` | Four tested faces: `attention`, `progress`, `done`, `error` |

Only `SKILL.md` is written for OpenClaw. The other files are copies of the Claude Code
plugin's skill (`plugins/ai-agent-monitor/skills/ai-agent-monitor/`), made by
`python scripts/gen_openclaw_skill.py`. CI fails when a copy is out of date. Edit the
originals, never the copies.

## Before you start

The clock must already be a node of your Gateway: paired, approved, with its commands
allowed. The steps are in the API reference,
[OpenClaw node](../../../docs/specifications/integrations-api.md#openclaw-node). The
skill does not change any of that.

## Install

On the Gateway's machine, from a copy of this repository:

```bash
openclaw skills install integrations/openclaw/skill/ai-agent-monitor --global
openclaw skills info ai-agent-monitor       # should say: eligible
```

- `--global` installs it into the shared managed folder (`~/.openclaw/skills/`), for
  every agent. Without it, the skill goes to the active agent's workspace only (pick one
  with `--agent <id>`).
- To update it later, pull the repository and run the same command with `--force`.
- OpenClaw watches skill files. A chat that is already open sees the skill after `/new`.

## Settings for custom faces

Messages, moods, variables and switching faces go through the node and need no
settings. **Saving a new custom face** goes over HTTP, from the Gateway's machine to the
clock. For that:

1. On the clock's settings page: **Integrations → Custom faces** on. Copy the API key
   shown there.
2. Give the skill the clock's address and key in the Gateway's configuration. They then
   never pass through the chat:

   ```bash
   openclaw config set skills.entries.ai-agent-monitor.env \
     '{"CLOCK_URL": "http://192.168.66.57", "CLOCK_API_KEY": "<the key>"}'
   ```

   Use the clock's **IP address** or its own host name (`agentmonitor-xxxxxx`, or
   `agentmonitor-xxxxxx.local` with mDNS). The clock refuses any other name with
   `403 Unknown host name`, to protect it from DNS rebinding. To allow another name, add
   it in the clock's settings.
3. The Gateway's machine must reach the clock on port 80, and needs `python3`. On a
   separate IoT network (VLAN), that takes a firewall rule from the Gateway's machine to
   the clock's port 80. The node itself only needs the clock to reach the Gateway.

If HTTP to the clock is not possible, use the [bridge](../README.md) instead of the
firmware node: it takes `clock.face.save`, and the skill uses it when the node lists it.

## Using it

Ask in any OpenClaw chat, scheduled job or heartbeat. For example:

- "Tell me on the clock when the backup finishes."
- "Show on the clock that you're waiting for my approval."
- "Make Moni happy, the deploy worked."
- "Show the big time on the clock."
- "Design a clock screen with my three servers' status, and keep it updated every 5
  minutes."

For the first ones, the agent calls the clock's node through its `nodes` tool:

```json
{"action": "invoke", "node": "agentmonitor-adec8a", "invokeCommand": "display.show",
 "invokeParamsJson": "{\"heading\":\"Backup\",\"text\":\"Finished, 42 GB\",\"icon\":\"done\"}"}
```

For the last one, the agent:

1. reads `reference/face-format.md`;
2. writes a face and checks it with `clock.py check`;
3. saves it with `clock.py push` (through its `exec` tool);
4. fills it with `clock.vars.set` and shows it with `clock.face.show`;
5. sends only new values from then on.

To run a command yourself, without an agent:

```bash
openclaw nodes invoke --node <clock id> --command display.show --params '{"text": "Hello"}'
```

## Trying it in the emulator

The emulator can run the real OpenClaw Gateway (no model provider or subscription):

```bash
docker compose -f emulator/docker-compose.yml --profile openclaw up -d
python emulator/openclaw_real.py               # pair the emulated clock with it
python emulator/openclaw_real.py skill --shot face.png
```

The `skill` step:

1. installs the skill on that gateway;
2. turns on custom faces on the emulated clock and gives the skill its address and key;
3. checks that OpenClaw reports the skill eligible;
4. runs the skill's own `clock.py` on the gateway's machine, as an agent's `exec`
   would: check, push, set values, list, then delete a face. `--shot` saves the
   screen in between.

## What was checked, and what was not

Checked on 2026-11-05, with OpenClaw 2026.11.1 in the emulator:

- the skill installs, loads, is eligible and "visible to model";
- the gateway keeps its settings;
- from the gateway's machine, `clock.py` saves, fills, shows and deletes a face on the
  emulated clock;
- every node command in the table works through `openclaw nodes invoke` (spec 041).

Not checked: a model reading the skill and calling the clock. There was no model
provider key. Also not checked: whether OpenClaw passes `skills.entries…env` to the
agent's `exec` the way its documentation says.

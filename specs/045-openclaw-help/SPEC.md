# 045 A help command on the OpenClaw node

Status: Done in 2.3.1 (was: approved 2026-11-06, the owner, in the chat: "go and approved, 2.3.1")

## Problem

OpenClaw shows an agent the names of a node's commands and nothing else (spec 044).
The skill of spec 044 teaches the clock, but only an agent whose gateway has it
installed. An agent on a gateway without the skill sees `display.show`, `face.set`,
`clock.vars.set`... and has to guess the parameters. The guide cannot live on the
clock: `SKILL.md` is 8.9 KB and 2.3.0 has 8 KB of update room.

## Goals

- A command whose name an agent understands without help, `clock.help`, that answers
  where the clock's instructions are: the skill on GitHub and its `SKILL.md` as raw
  text an agent can fetch and read.
- The links name the release the clock runs, so the instructions match its commands.

## Non-goals

- No guide text on the clock (no room), no `node.pluginTools.update`.
- The clock never fetches anything: the answer is built from constants.

## Requirements

1. The firmware node declares an eleventh command, `clock.help`. It takes no
   parameters (any parameter: `INVALID_PARAMS`, like `device.info`).
2. It returns `{"firmware", "skill", "guide", "install"}`:
   - `firmware`: `FW_VERSION`;
   - `skill`: `https://github.com/SimonLuckenuik/ai-agent-monitor/tree/<ref>/integrations/openclaw/skill/ai-agent-monitor`;
   - `guide`: `https://raw.githubusercontent.com/SimonLuckenuik/ai-agent-monitor/<ref>/integrations/openclaw/skill/ai-agent-monitor/SKILL.md`;
   - `install`: one line telling the gateway's owner how to install the skill
     (`openclaw skills install <folder> --global`).
   - `<ref>` is `v<FW_VERSION>`, or `main` for a version with a `-` (never released).
3. The bridge (`integrations/openclaw/`) offers the same command with the same answer,
   built from the clock's reported version.
4. The skill's command table lists it (the safety check compares the table with
   `OPENCLAW_COMMANDS`, now 11), and `SKILL.md` says to call it when unsure.
5. The integration test calls it in the emulator and checks the links; the bridge
   tests check the bridge's.
6. Flash: within the room 2.3.0 left (8,192 B), measured.

## Behaviour

```json
{"nodes": {"action": "invoke", "node": "clock", "invokeCommand": "clock.help", "invokeParamsJson": "{}"}}
→ {"firmware": "2.3.1",
   "skill": "https://github.com/SimonLuckenuik/ai-agent-monitor/tree/v2.3.1/integrations/openclaw/skill/ai-agent-monitor",
   "guide": "https://raw.githubusercontent.com/SimonLuckenuik/ai-agent-monitor/v2.3.1/integrations/openclaw/skill/ai-agent-monitor/SKILL.md",
   "install": "..."}
```

Ships in **2.3.1** (the owner's choice).

## Budget

Measured: `firmware.bin` +576 B (602,384 B), `firmware.bin.gz` +270 B (427,337 B), no RAM
at rest (a 400-byte buffer while answering). The plain image crossed a 4 KB sector
boundary (602,112 B), so the update room went from 8,192 B to **4,096 B**. Getting back
under it would have meant dropping `skill` and `install` (about 200 B) and more; the
full answer was kept (PROGRESS, 2026-11-06).

## Open questions

None.

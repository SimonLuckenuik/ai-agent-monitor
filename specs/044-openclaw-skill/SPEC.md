# 044 An OpenClaw skill for the clock

Status: Draft

## Problem

Spec 041 made the clock an OpenClaw node. On connect it declares ten command names
(`display.show`, `face.set`, `clock.face.show`, ...) and nothing else: no description,
no parameters. An OpenClaw agent sees the names in its `nodes` tool and has to guess the
rest. It does not know the 8 icons, the length limits, the moods, the built-in face
names, or that `clock.vars.set` feeds custom faces. And it cannot make a custom face
at all:

- the face format (items, expressions, paths, palettes) is about 25 KB of text that the
  agent has never seen;
- the firmware node has no `clock.face.save` (a 4 KB face, escaped in a frame, needs
  about 6 KB of heap at once; spec 041), so a face goes to the clock over HTTP
  (`POST /api/faces`, with the API key) or through the bridge's `clock.face.save`.

The Claude Code plugin already teaches all of this to Claude Code
(`plugins/ai-agent-monitor/skills/ai-agent-monitor/`). OpenClaw reads the same kind of
skill (a folder with a `SKILL.md`), installed on the gateway's machine, at no cost to the
clock. The owner chose this over tool descriptions sent by the firmware
(`node.pluginTools.update`, 2-3 KB of the 8 KB of flash room).

## Goals

- An OpenClaw skill, in this repository, that teaches an OpenClaw agent to use the
  clock: the node's commands with their parameters and limits, then custom faces.
- One source for what the two skills share: the face format, its JSON Schema, the HTTP
  API reference and `clock.py` are copied from the Claude Code skill by a script, and
  CI fails when the copy is out of date.
- Installed with one OpenClaw command, documented for a real gateway and tried on the
  emulator's real OpenClaw Gateway.

## Non-goals

- No firmware change, no `node.pluginTools.update` or `node.skills.update` (possible
  later: a separate spec).
- No `clock.face.save` in the firmware node.
- Publishing on ClawHub (needs the owner's ClawHub account: open question 3).
- Proving that a model uses the skill well: that needs a model provider (open
  question 4).

## Requirements

1. **R1 The skill folder** `integrations/openclaw/skill/ai-agent-monitor/` holds:
   - `SKILL.md`, written for OpenClaw;
   - `reference/face-format.md`, `reference/face.schema.json` and `reference/api.md`;
   - `scripts/clock.py`;
   - `examples/` (the four message examples and two small faces).

   The files that are not `SKILL.md` are copies of the Claude Code skill's.
2. **R2 Frontmatter** loads in OpenClaw 2026.11.1:
   - `name: ai-agent-monitor`.
   - A one-line `description` under 160 characters (OpenClaw's limit; the Claude Code
     skill's is longer).
   - `metadata.openclaw` with an emoji and the homepage.
   - No gating. The node commands need nothing on the gateway's machine; only custom
     faces over HTTP need `python3`, and the skill says so where it matters.
3. **R3 The node commands** are documented in `SKILL.md` as a table, one row per command
   the firmware node declares:
   - the parameters with their types and limits;
   - what the clock does;
   - the errors (`INVALID_PARAMS`, `NOT_CONFIGURED`, `UNAVAILABLE`);
   - how to call each command from the agent's `nodes` tool (`action: "invoke"`, the
     node, the command and its parameters as JSON);
   - how to find the clock's node (`nodes` status: name `agentmonitor-xxxxxx`, platform
     `esp8266`).

   The bridge's extra command (`clock.face.save`) is marked "bridge only".
4. **R4 Which way to use**, in this order:
   1. A message (`display.show`) for news, waiting for input, done, failed or progress.
   2. Moni's mood (`face.set`).
   3. Variables for a face already on the clock (`clock.vars.set`).
   4. A new custom face only when asked for a screen or a design.
5. **R5 Custom faces.** `SKILL.md` gives:
   - the steps: read `reference/face-format.md`; write the face; check it
     (`clock.py check`); save it, then show it with `clock.face.show` or
     `clock.vars.set`;
   - the two ways to save: `POST /api/faces` through `clock.py push` (the `exec` tool,
     `CLOCK_URL` and `CLOCK_API_KEY`), or the bridge's `clock.face.save`;
   - the limits: 4 KB, 32 items, ASCII text, 240×240.
6. **R6 Settings.** `CLOCK_URL` and `CLOCK_API_KEY` come from the gateway's config
   (`skills.entries.ai-agent-monitor.env`), never from the chat. The skill never asks
   for, or stores, the clock's device password.
7. **R7 In step.**
   - `scripts/gen_openclaw_skill.py` copies the shared files.
   - With `--check`, it fails when a copy differs.
   - CI runs the check, as it does `gen_face_format.py --check`.
   - A unit or safety check fails when the firmware's `OPENCLAW_COMMANDS` and the
     table in `SKILL.md` list different commands.
8. **R8 Install, documented** in `integrations/openclaw/README.md` and the API
   reference's OpenClaw node section:
   - `openclaw skills install <path>`, with `--global` for every agent;
   - setting the two variables;
   - checking it with `openclaw skills info ai-agent-monitor`.
9. **R9 Tried on the emulator's real gateway.** `emulator/openclaw_real.py skill`
   installs the skill into the `openclaw` service. A check passes when
   `openclaw skills info` reports it loaded and eligible.
10. **R10 Docs and records.**
    - `CHANGELOG.md` (Unreleased), `docs/specifications/testing.md` (the new checks).
    - The repository `CLAUDE.md` "Things to keep in step" rule extended to the
      OpenClaw skill.

## Behaviour

An OpenClaw agent asked "tell me on the clock when the build is done" finds the
ai-agent-monitor skill in its list, reads it, and calls the `nodes` tool like this:

```json
{"action": "invoke", "node": "agentmonitor-adec8a", "invokeCommand": "display.show",
 "invokeParamsJson": "{\"heading\":\"Build\",\"text\":\"Done in 4 min\",\"icon\":\"done\"}"}
```

Asked for "a screen with my three servers' status", it reads `reference/face-format.md`,
writes a face, checks it, and saves it with `clock.py push` through `exec`. Then it
shows the face with `clock.face.show` and updates it later with `clock.vars.set`.

## Budget

- Flash and RAM: none (no firmware change).
- Skill size: `SKILL.md` under 12 KB, because it goes into the agent's context when the
  skill is used. The references are read on demand: face-format.md is about 25 KB and
  the schema about 21 KB, under OpenClaw's 256 KB per file.

## Open questions

1. **Where the shared files live.** Recommended: copies in the OpenClaw folder, made by
   the script and checked in CI, so the folder installs on its own (OpenClaw's
   `install ./path` copies one folder). The alternative was one skill folder for both
   tools, but each tool needs a different description and different instructions.
2. **The gateway reaching the clock over HTTP.** Custom faces over HTTP need the
   gateway's machine to reach the clock (today only the clock dials the gateway).
   Across the IoT VLAN that means a firewall rule from the gateway to the clock's
   port 80. The other way is the bridge for faces. Recommended: document both; nothing
   to build.
3. **ClawHub.** Publishing it (`clawhub skill publish`) would let anyone install it as
   `@owner/ai-agent-monitor`. Needs the owner's account. Recommended: later.
4. **A model to try it with.** Without a provider key, only the loading is tested, not
   an agent using it. If the owner gives a key to the emulator's gateway (an
   environment variable, never committed), the plan adds one agent run per way (a
   message, a face).

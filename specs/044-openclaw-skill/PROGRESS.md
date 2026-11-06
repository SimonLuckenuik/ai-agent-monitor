# 044 Progress

## 2026-11-05

- Asked by the owner whether the node's commands carry documentation an OpenClaw agent
  could use, for custom faces for example: they do not (names only). Of the three ways
  (a gateway-side skill, tool descriptions from the firmware, a node skill), the owner
  chose the gateway-side skill.
- Read OpenClaw 2026.11.1's skill format, install command and `nodes` tool in its image
  (RESEARCH.md). Spec and plan written; waiting for approval.

## 2026-11-05 (later): approved and built

- Approved by the owner: open questions 1 and 2 as recommended (copies made by a script;
  document both ways to save faces), 3 no ClawHub, 4 no model key. "Make sure you
  document all this and how to use it."
- Confirmed the `nodes` tool's invoke parameters in OpenClaw's code (`/app/dist`,
  `NodesToolSchema` and `callNodesToolNodeInvoke`): `action: "invoke"`, `node`,
  `invokeCommand`, `invokeParamsJson` (a JSON string), `invokeTimeoutMs`. `describe`
  lists a node's commands. The tool refuses only commands with a dedicated tool
  (`computer.act`, `browser.proxy`...) or ones the node publishes as plugin tools; the
  clock's commands are neither.
- Built:
  - `scripts/gen_openclaw_skill.py` (write and `--check`). Checked: running it twice
    gives no change; an edited copy fails the check.
  - The skill: `integrations/openclaw/skill/ai-agent-monitor/`, `SKILL.md` 8.9 KB, with a
    description of 123 characters.
  - The guide: `integrations/openclaw/skill/README.md`.
  - A safety check that the skill's command table equals `OPENCLAW_COMMANDS`. It
    passes, and fails with a row renamed.
  - The CI step.
  - `emulator/openclaw_real.py skill`.
- On the real gateway (OpenClaw 2026.11.1, the emulator's `openclaw` service):
  - `openclaw skills install ... --global` installed it into
    `/home/node/.openclaw/skills/ai-agent-monitor`.
  - `skills info`: eligible, nothing missing, source `openclaw-managed`. `skills check`:
    "Ready and visible to model".
  - `skills.entries.ai-agent-monitor.env` is kept.
  - The skill's `clock.py`, run in the gateway's container with that address and key,
    checked, pushed, filled, listed and deleted a face. The screenshot shows the
    progress face with "OpenClaw", 60% and "Pushed by the skill".
- Found on the way: the clock refuses `CLOCK_URL=http://smartclock` (the compose name)
  with `403 Unknown host name`. That is its DNS-rebinding guard, which answers only its
  IP, its own host name and names listed in its settings. The guide, `SKILL.md` and the
  script now say to use the IP or the clock's host name.
- Also corrected: the API reference said the node connects with 12 KB free (it has been
  9 KB since spec 043); STATUS had the sizes from before the last fix.
- Not verified:
  - a model reading the skill and calling the clock (no key);
  - whether OpenClaw hands `skills.entries...env` to the agent's `exec` (documented,
    not observed);
  - the real clocks.

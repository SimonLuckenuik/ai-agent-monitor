# 044 Research

From the OpenClaw 2026.11.1 image (`ghcr.io/openclaw/openclaw:latest`, its `/app/docs`,
`/app/skills` and `/app/dist`), 2026-11-05.

## Skills in OpenClaw

- A skill is a folder with `SKILL.md`: YAML frontmatter (`name`, `description`
  required), markdown body. Same shape as Claude Code's and the Agent Skills format
  (`docs/tools/creating-skills.md`).
- `description`: one line, under 160 characters; shown to the agent and in slash-command
  discovery. The Claude Code skill's description is about 400 characters.
- Optional `metadata: {"openclaw": {...}}`: `emoji`, `requires.bins` / `anyBins` /
  `env` / `config`, `os`, `primaryEnv`, `install`. Bundled examples: `skills/weather`,
  `skills/node-connect`.
- `{baseDir}` in the body is resolved to the skill's own folder (for
  `{baseDir}/scripts/clock.py`).
- Load order, highest first:
  1. `<workspace>/skills`
  2. `<workspace>/.agents/skills`
  3. `~/.agents/skills`
  4. `<state-dir>/skills` (managed; `--global`)
  5. workshop skills
  6. bundled skills
  7. `skills.load.extraDirs` and plugin skills.
- SKILL.md files are watched. A new session picks up changes.
- Install: `openclaw skills install ./path` copies a folder whose root has `SKILL.md`.
  `git:owner/repo[@ref]` also expects `SKILL.md` at the repository root, which ours is
  not, so the local path is the way, or ClawHub (`@owner/slug`) once published.
- Limits: `skills.limits.maxSkillFileBytes` defaults to 256,000 B per file; also
  `maxSkillsPromptChars` and `maxSkillsInPrompt` for the list shown to the agent.
- Per-skill settings: `skills.entries.<name>` with `enabled`, `apiKey`, `env`. The values
  reach the host process for that agent turn only, not a sandbox.
- Node-hosted skills exist (a headless OpenClaw node publishes `~/.openclaw/skills`). They
  are for nodes running OpenClaw itself; a clock is not one (`node.skills.update` is the
  protocol path, up to 64 KB, impossible in our flash).

## How an agent calls a node

- The agent's `nodes` tool, `action: "invoke"`, with `invokeCommand` and
  `invokeParamsJson` (a JSON string; "must be valid JSON") and optional
  `invokeTimeoutMs` (`docs/nodes/device-commands.md`; parameter names from
  `/app/dist`, to be confirmed against the tool schema when writing the skill).
- The same as `openclaw nodes invoke --node <id|name> --command <c> --params '<json>'`,
  which spec 041 used for every command against this gateway.
- Commands outside `gateway.nodes.commands.allow` are not approved (spec 041 findings).
  The skill repeats the allow list for the person setting it up.

## Options weighed

| Option | Flash | Holds the face format | Why not |
|---|---|---|---|
| A gateway-side skill (chosen) | 0 | Yes | Installed by hand on the gateway |
| `node.pluginTools.update` from the firmware | ~2-3 KB (estimate, not measured) | No | Flash; descriptions only; could come later on top |
| `node.skills.update` from the firmware | up to 64 KB | Yes | Does not fit (8 KB of room) |
| One skill folder for Claude Code and OpenClaw | 0 | Yes | Different descriptions (length limit) and instructions (HTTP vs the `nodes` tool) |

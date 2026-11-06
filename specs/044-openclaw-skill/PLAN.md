# 044 Plan

On branch `openclaw-native`, with spec 041; lands in 2.3.0 (no firmware change of its own).

1. **The generator.** Write `scripts/gen_openclaw_skill.py` (write and `--check`).
   - It copies `reference/face-format.md`, `face.schema.json`, `api.md`,
     `scripts/clock.py` and the message examples from the Claude Code skill into
     `integrations/openclaw/skill/ai-agent-monitor/`.
   - Check: run it twice (no diff); edit a copy and `--check` fails.
2. **`SKILL.md`.** Write the frontmatter (R2), when to use the clock and which way
   (R4), finding the node, the command table with `nodes` tool calls (R3), custom
   faces (R5), and settings (R6). The examples are the Claude Code skill's four tested
   faces (copied in step 1).
   - Check: the faces pass `clock.py check`; `SKILL.md` is under 12 KB.
3. **The commands check (R7).** Add a check to `tests/run_safety_tests.py`: the
   command names in `src/integrations.cpp`'s `OPENCLAW_COMMANDS` equal the firmware
   rows of `SKILL.md`'s table. Run the generator's `--check` in `build.yml` next to
   `gen_face_format.py --check`.
   - Check: the test passes; a renamed row fails it.
4. **On the real gateway (R9).** Add `openclaw_real.py skill`: copy the folder into the
   `openclaw` container, `openclaw skills install`, set
   `skills.entries.ai-agent-monitor.env`
   (`CLOCK_URL=http://<the clock's IP>`, the emulator's key), then print
   `openclaw skills info`, and run the skill's `clock.py` in the gateway's container
   (check, push, vars, faces, delete) as an agent's `exec` would.
   - Check: loaded, eligible, description shown; `openclaw skills check` lists it as
     ready; the face shows on the emulator's screen.
   - No model key (the owner): no agent run.
5. **Docs (R8, R10).**
   - A guide, `integrations/openclaw/skill/README.md` (outside the skill folder, so not
     installed): what the skill is, install, settings, use, the emulator, what was checked.
   - `integrations/openclaw/README.md`, the root README and the API reference's OpenClaw
     node section.
   - `emulator/README.md` (OpenClaw section).
   - `docs/specifications/testing.md`, `CHANGELOG.md`.
   - `CLAUDE.md` "Things to keep in step".
   - specs index, roadmap, STATUS.
6. **Commit** (`feat(openclaw): a skill that teaches OpenClaw agents the clock`) and
   log PROGRESS.

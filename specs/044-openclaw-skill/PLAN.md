# 044 Plan

On branch `openclaw-native`, with spec 041; lands in 2.3.0 (no firmware change of its own).

1. **The generator.** Write `scripts/gen_openclaw_skill.py` (write and `--check`).
   - It copies `reference/face-format.md`, `face.schema.json`, `api.md`,
     `scripts/clock.py` and the message examples from the Claude Code skill into
     `integrations/openclaw/skill/ai-agent-monitor/`.
   - Check: run it twice (no diff); edit a copy and `--check` fails.
2. **`SKILL.md`.** Write the frontmatter (R2), when to use the clock and which way
   (R4), finding the node, the command table with `nodes` tool calls (R3), custom
   faces (R5), and settings (R6). Add two example faces in `examples/`.
   - Check: the faces pass `clock.py check`; the face examples render in the emulator
     (`clock.py push`, screenshot); `SKILL.md` is under 12 KB.
3. **The commands check (R7).** Add a check to `tests/run_safety_tests.py`: the
   command names in `src/integrations.cpp`'s `OPENCLAW_COMMANDS` equal the firmware
   rows of `SKILL.md`'s table. Run the generator's `--check` in `build.yml` next to
   `gen_face_format.py --check`.
   - Check: the test passes; a renamed row fails it.
4. **On the real gateway (R9).** Add `openclaw_real.py skill`: copy the folder into the
   `openclaw` container, `openclaw skills install`, set
   `skills.entries.ai-agent-monitor.env`
   (`CLOCK_URL=http://smartclock:80`, the emulator's key), then print
   `openclaw skills info`.
   - Check: loaded, eligible, description shown; `openclaw skills check` has no error.
   - If the owner gives a model key (open question 4): one agent message ("show
     'Build done' on the clock") and one face request, with screenshots.
5. **Docs (R8, R10).**
   - `integrations/openclaw/README.md` and the API reference's OpenClaw node section.
   - `emulator/README.md` (OpenClaw section).
   - `docs/specifications/testing.md`, `CHANGELOG.md`.
   - `CLAUDE.md` "Things to keep in step".
   - specs index, roadmap, STATUS.
6. **Commit** (`feat(openclaw): a skill that teaches OpenClaw agents the clock`) and
   log PROGRESS.

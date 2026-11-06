# 045 Plan

On branch `openclaw-native`; lands in 2.3.1.

1. **Firmware.** `src/integrations.cpp`: `clock.help` at the end of `OPENCLAW_COMMANDS`
   (index 10) and its branch in `openclawCommand()`. Check: `pio run` (size guard,
   room left).
2. **Bridge.** `integrations/openclaw/lib/commands.mjs`: the same command; a bridge test.
3. **Emulator.** The stand-in gateway's command list and examples (`emulator/screen.html`),
   `emulator/openclaw_real.py`'s list.
4. **Tests.** Integration test (`run_integration_tests.py`: the declared list and a call);
   the safety check's count (10 → 11).
5. **Skill and docs.** `SKILL.md` table and "when unsure"; the API reference, the bridge
   README, the skill guide; `CHANGELOG.md`.
6. **Release 2.3.1.** `FW_VERSION`, CHANGELOG section, STATUS, roadmap, specs index.
   Check: unit, safety, settings page, bridge and the OpenClaw integration cases.

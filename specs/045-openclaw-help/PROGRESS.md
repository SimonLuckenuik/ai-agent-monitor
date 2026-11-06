# 045 Progress

## 2026-11-06

- Asked by the owner whether the node could expose a guide command returning the
  skill's URL on GitHub. Proposed `clock.help` linking the release tag; the owner:
  "go and approved, 2.3.1".
- Built: `clock.help` in `src/integrations.cpp` (index 10 of `OPENCLAW_COMMANDS`, one
  `snprintf_P` into a 400-byte heap buffer while answering), the same answer in the
  bridge (`helpLinks()` in `lib/commands.mjs`), the emulator's lists, the allow lists in
  the docs, the skill's table, the API reference, CHANGELOG. `FW_VERSION` 2.3.1.
- Flash: first version (String concatenations) +720 B plain; one `snprintf_P` +576 B
  (602,384 B; `.bin.gz` 427,337 B). The plain image crossed a 4 KB sector boundary at
  602,112 B, so the update room went from 8,192 B to 4,096 B. Dropping `skill` and
  `install` would have saved about 200 B, not enough to get back under it: the full
  answer was kept. To be told to the owner.
- Tests: unit (434, mDNS 123, guard 23, openclaw 21), safety (now 11 commands), settings
  page, `gen_openclaw_skill.py --check`, `gen_face_format.py --check`, bridge 18/18,
  integration `openclaw_node` (declared list and `clock.help`'s links in the emulator
  against the stand-in gateway): all pass.
- Not verified: the real OpenClaw gateway asking again for the new command (expected
  from spec 041's findings, written in the Upgrading note); an agent fetching the guide
  (no model key); the real clocks.

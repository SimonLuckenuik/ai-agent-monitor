# 011 Integration tests and README screenshots

Status: Done in 1.4.0 (written after the fact, 2026-10-30)

## Problem

Changes must be checked end to end, with screens that are the same every run, and the
README's images must follow the firmware.

## Requirements

1. `tests/integration/run_integration_tests.py`: the firmware in its own emulator
   container (ports 18000/18080, blank storage), driven over HTTP with a fixed clock
   (Monday 5 October 2026, 10:09) and weather; every face, language and unit, extremes,
   night mode, messages, the Agents board, custom faces, API limits.
2. Each screen saved to `.pio/integration/` with `sheet.png`; a run lists what changed.
3. `scripts/screenshots.py` regenerates `docs/images/*` (faces, custom faces, agent
   screens, the settings page through Playwright, the emulator page).

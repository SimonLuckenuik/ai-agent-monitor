# The night palette showed empty in the emulator

Status: Closed (data from a dev build)

Date: 2026-10-29

## Cause and fix

The emulator's saved settings held `"night": {}` from a dev build; a saved `palettes`
replaces the defaults. Repaired in the emulator's data
([decision](../decisions/2026-10-29-empty-night-palette-fixed-in-data.md)).

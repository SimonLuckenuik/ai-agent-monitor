# An empty saved night palette is repaired in the data, not the firmware

Date: 2026-10-29

Spec: [013](../../specs/013-palettes-and-roles/SPEC.md)

## Evidence

The emulator showed an empty night palette: its saved settings held `"night": {}` from
a dev build. A firmware fix (fill an empty saved `night` from the defaults on load) cost
1,360 B and crossed a 4 KB sector, lowering the update limit by 4 KB.

## Decision

No firmware change: released firmware never saved an empty `night`. The emulator's data
was repaired through `/api/set?key=palettes`.

# 005 The clock in French

Status: Done in 1.4.0 (written after the fact, 2026-10-30)

## Problem

The owner wants the clock in English or French. The fonts are ASCII only.

## Goals and requirements

1. A `lang` setting (`en`, `fr`) on the Display card and `/api/set?key=lang`.
2. Days, months, weather words and the faces' labels follow it, written without accents.
3. The API, MQTT and the settings page stay English.
4. Accented and typographic characters in messages and variables are folded to ASCII.

## Non-goals

- Uploadable language files; accented glyphs (they would cost flash for every font).

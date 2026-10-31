# 013 Palettes and roles, and the big font in custom faces

Status: Done in 1.6.0 (written after the fact, 2026-10-30)

## Problem

The owner wants a uniform look across faces: palettes custom faces can reuse, a day and
a night palette (more can be added), named colours including the background, and each
visual element of the built-in faces bound to a colour.

## Requirements

1. Palettes with named colours (day, night, up to 8); the built-in night palette has the
   same names, dimmer, plus night colours.
2. 18 roles (time, date, highlights, icons, temperatures, rain, sun, message colours,
   bg, text, labels, cards), each bound to a colour, another role or a rule.
3. Built-in faces draw with roles; custom faces reach them as `@role` and `{theme.role}`.
4. A Colours card on the settings page.
5. `"font": "big"` in custom faces, with punctuation and symbols; lowercase forced to
   capitals (no new font).
6. Numbers zero-padded where useful (scoreboard 128 / 003).

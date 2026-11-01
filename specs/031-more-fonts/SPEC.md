# 031 More fonts: Bebas, Outfit, seven segments, pixel digits, and a stroke weight

Status: Approved (2026-11-01; the owner agreed to the proposal, extras included)

## Problem

Custom faces have three sizes of bitmap text (fonts 2, 4, 6) and the "big" line font:
one monoline style for every large number. smalltv-pomodoro's faces show what a few more
digit styles give (`specs/026-face-gallery/RESEARCH.md`, the comparison sheet of
2026-11-01): a tall condensed time (Bebas Neue), a bold round one (Outfit), seven-segment
LCD digits (DSEG7) and blocky pixel digits. The converted faces lost those looks.

## Goals

1. **`"font": "bebas"` and `"font": "outfit"`:** digits, `:`, `.`, `-`, `%`, `°` and a
   space from Bebas Neue and Outfit SemiBold (SIL OFL 1.1, licence files in
   `licenses/`). Outline glyphs compiled at build time (as spec 030's glyphs are), filled
   and anti-aliased by an integer row renderer, at any `size`, against `under` as the big
   font is. Other characters fall back to the big font at the same height.
2. **`"font": "seg7"`:** seven-segment digits, `:`, `.` and `-`, drawn by code at any
   `size`; `off` (a colour) draws the unlit segments.
3. **`"font": "pixel"`:** 5×7 pixel digits, `:` and `-`, drawn as blocks at any `size`
   (the block is size/7); `off` draws the unlit blocks.
4. **`"weight"` on big-font text:** 0.5 to 3 times the normal stroke (1), so the line font
   can be bold. No new glyphs.
5. **The converted faces use them** where the originals did (the converter maps their
   fonts to these), and our own faces' big numbers move to them broadly (the owner,
   2026-11-01: "everything big maybe"): each proposed on a before/after sheet first.
   Similar information gets similar sizes across the widget faces (bento, modular,
   fitness, pill).
6. **Within the budgets:** all of it under 10 KB of the update budget (the plain image plus
   the compressed one; about 21 KB of code is left); the drawing as fast as the big font
   for the same size, measured as in spec 030.

## Non-goals

- Letters in these fonts (Outfit with letters would be 10.6 KB).
- Righteous and Geo (the comparison showed nothing ours lacks).
- Fonts uploaded at run time.

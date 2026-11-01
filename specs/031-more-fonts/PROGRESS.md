# 031 Progress

- 2026-11-01: written and approved, after the owner saw the fonts side by side with ours
  and agreed to Bebas, Outfit, seven segments, pixel digits and a stroke weight.
- 2026-11-01: built (goals 1-4, and the converted faces of goal 5).
  - Glyphs: `scripts/gen_outline_glyphs.py` takes the digit outlines from
    smalltv-pomodoro's `face_flip.cpp` (Bebas Neue) and `fonts.cpp` (Outfit SemiBold) into
    `src/outline_glyphs.h`, one byte per coordinate: 2,788 B. Licences in `licenses/`.
    It needs their checkout to run again, so CI does not check it.
  - `outlineGlyph()`: non-zero fill, 4 sub-rows, edges in 1/16 px, into `cov[]` and
    `emitRow()` (night tint, band sprites, edge runs). `seg7Char()`, `pixelChar()` from bit
    tables, with `off`. `.` and `-` drawn for the outline fonts; other characters in the
    line font at the same height. `weight` on big text scales its stroke and its box.
  - The preview face `custom:fonts` shows all of them; incremental redraws match. All 32
    integration cases pass (lowest heap 5,112 B).
  - Flash: about 8 KB in all (drawText grew to 4.3 KB with the renderer inlined), above
    the 10 KB of update budget goal 6 set (the plain and compressed images together take
    about 14 KB). The update budget is at 98%: about 12 KB of plain image left.
  - The converter maps their bebas, outfit, dseg and dm to ours. A text mixing a task
    count with other values ("{TEMP}° {FOCOS}/{META}") keeps the rest.

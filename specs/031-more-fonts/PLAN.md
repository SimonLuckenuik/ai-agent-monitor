# 031 Plan

1. **Glyphs.** `scripts/gen_outline_glyphs.py` reads the fonts' TTF files (fontTools) and
   writes `src/outline_glyphs.h`: per glyph, contours flattened to points on a 0-255 grid
   per axis (one byte each), advance widths, and the characters. Checked in CI like
   `mono_glyphs.h`. The TTF files are not shipped in the firmware, only in `fonts/` with
   their licences.
2. **Renderer.** `outlineText()` in `faces.cpp`: per row, the edges that cross it, sorted;
   coverage by the non-zero rule with 4 sub-rows and 1/16 px edges (as smalltv-pomodoro's
   `vecText`), blended with `under`, written as runs.
3. **seg7 and pixel.** Drawn with `fillRect`/`fillRoundRect` from a segment table (7 bits
   per digit) and a 5×7 table (35 bits per digit).
4. **weight.** `MONO_STROKE * weight` in `monoDraw` for custom-face text.
5. **Face format.** `font` takes the new names; `off` and `weight`; the integrations API
   doc and the skill's reference (regenerated), the skill's schema.
6. **Converter.** Their `bebas`, `outfit`, `dseg`, `dm` map to ours; reconvert, redraw
   previews, update the gallery.
7. **Tests.** Unit: glyph data sane, widths. Face preview: a sheet of every font at 3 sizes.
   Integration: the converted faces, and `example_faces_memory`.
8. **Measure.** Flash before and after; draw time of a 90 px time in each font on the
   emulator, then the clock.

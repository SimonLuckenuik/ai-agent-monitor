# 030 Plan

1. **Benchmark.** `tests/face_preview` gets a timing mode. It draws the three cases with
   the current renderer, counting `drawPixel` and `pushColor` calls, pixels computed, and
   host time.
2. **Glyph compiler.** `scripts/gen_mono_glyphs.py` reads the `MG_*` paths (moved to
   `src/mono_glyphs.svg.txt`), flattens the curves, and writes `src/mono_glyphs.h`
   (PROGMEM points and segment lists, and the table). CI checks it is up to date.
3. **Row renderer.** `monoRows()` in `faces.cpp` covers the screen and band-sprite targets,
   the tint, and `under`. `monoDraw` and `monoDiff` call it.
4. **Compare.** The face preview sheet and the integration screens, before and after;
   pixel differences listed per screen.
5. **On the clock.** The owner installs it; `drawMs` for Big time, the flip clock, and a
   converted face.
6. **Optional step (goal 6).** Fonts 4 and 6 through the big font, behind a build flag;
   measure flash and `drawMs`, then decide.
7. **Docs.** `footprint.md`, CHANGELOG, STATUS.

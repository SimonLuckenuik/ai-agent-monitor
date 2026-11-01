# 030 Progress

- 2026-10-31: proposed, after comparing smalltv-pomodoro's outline fonts with ours. The
  owner wants new approaches that save flash or draw faster, not faces closer to the
  originals: so their technique is taken, not their fonts.
- 2026-10-31: corrected before building. The big font does not use `drawWedgeLine`:
  `strokePoly` already renders row by row. The costs are float maths per pixel (no FPU),
  path parsing per draw, and a `drawPixel` per edge pixel. Goal 3 now reads: integers,
  and one block per run of covered pixels (not per row, which would overwrite what lies
  between strokes under transparent text).
- 2026-11-01: built.
  - `src/mono_glyphs.txt` holds the 57 glyphs' paths. `scripts/gen_mono_glyphs.py`
    flattens them (0.03 unit tolerance) into `src/mono_glyphs.h`: 1,008 points in 1/12
    unit, 85 strokes, 2,329 B in flash. Today's path text was 2,849 B, and its 456 B
    table was in RAM. CI checks the header is up to date.
  - `monoStroke()` draws a glyph in integers. Points are in 1/16 px, each segment keeps
    a unit direction in 1/4096, and along a row the projection and the distance change
    by a constant per pixel. Only the round caps take an integer square root. A steep
    segment's reach is narrowed per row. Its 36 B per segment of heap replace `Poly`'s
    3.2 KB.
  - Not done: one block per run of covered pixels. Edge pixels still go through
    `emitRow`'s `drawPixel`, which would need a row buffer (480 B) on a tight stack.
    To decide after the clock's numbers.
  - Look: every face preview screen against the old renderer. Up to 2,801 pixels per
    screen differ, almost all by a shade at the edges; at most 21 by more than a
    quarter of the range (Big date, at the 5's corners). Side by side they look the same.
  - Speed, on the emulator's ESP8266 cycle model (the integer loops charged 25 cycles
    a pixel, 120 a segment and row, 150 a square root, all estimates):

    | Face | Before | After |
    |---|---|---|
    | Big time | 653-724 ms | 122-129 ms |
    | Big date | 543-591 ms | 107-126 ms |
    | Big weather | 411-454 ms | 153-161 ms |
    | giant-time (custom) | 413 ms | 115 ms |

    The clock measured 636-948 ms for Big time before (`drawMs`, 2.0.0); its number
    after decides goal 4.
  - Flash: firmware.bin +192 B; firmware.bin.gz +1.3 KB, since binary tables compress
    less well than path text. Static RAM -500 B.
  - Goal 6 (fonts 4 and 6 through the big font) waits for the clock's numbers.
- 2026-11-01: goals 7 and 8 built.
  - Goal 8: `emitRow` sends each run of covered pixels with an edge as one window
    (`setWindow` + `pushColor`, virtual in TFT_eSPI, so the night tint and band sprites
    still apply; a band sprite's window is offset by the band). Every face preview
    screen is pixel-identical to before; the preview mock gained window writes.
  - Goal 7: `mixValues` hashes positions and sizes (`x y x2 y2 w h r size`) by the pixel
    they land on; `mixItem` hashes a bar's filled width and a ring's degrees, by the same
    integer sums that draw them, instead of their value and max.
  - All 32 integration cases pass (lowest heap 5,112 B). Draw-time gains are to measure
    on the clock with the next release.

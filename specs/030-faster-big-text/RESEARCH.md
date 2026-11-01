# 030 Research

## Ours

- **Glyphs:** `src/faces.cpp`, `MG_*`, 57 SVG path strings in PROGMEM (2,849 B). They
  include digits, capitals, punctuation and symbols (`° ↑ ↓ ← → ✓ ✗ • ♥ ⚡`). The `MONO`
  table pointing at them is 456 B in RAM (`.rodata` at `0x3ffe9794`).
- **Drawing:** `monoDraw` parses each path (`pgmString`, `parsePath` into a `Poly`), then
  `strokePoly` draws every segment with `drawWedgeLine` (`wedge()` in `faces.cpp`, offset
  in band sprites).
- **TFT_eSPI `drawWedgeLine`:** for every pixel of the segment's bounding box it computes
  `wedgeLineDistance` in floats. Solid runs go out with `setWindow` and `pushColor`;
  every edge pixel is a `drawPixel` with a blend. On the screen, each call goes through
  `NightPanel`'s virtual overrides, which tint the colour.
- **Measured on the clock (2.0.0, `drawMs` by face id, last and slowest):** Big time
  (7) 636 / 948 ms; face 3, 814 / 827 ms; built-in faces without the big font draw in
  233-384 ms.
- **Bitmap fonts:** TFT_eSPI fonts 2, 4 and 6, 7.7 KB in all (`footprint.md`).

## smalltv-pomodoro's outline fonts (GPL-3.0, commit 2f6836d, `fonts.cpp`)

- **Glyph data:** outline points (`int16` pairs on a 720-unit cell), the ends of each
  contour, the glyph index and the advance widths. Bebas Neue, Outfit and Righteous
  have digits and `:` only, 737-840 points each, about 3-3.4 KB. Outfit with letters has
  2,653 points, about 10.6 KB.
- **`vecText`:** allocates coverage plus one RGB565 row (3 × width bytes). For each pixel
  row and 4 sub-rows, it finds where the contour edges cross the sub-row, sorts them, and
  fills spans under the non-zero rule. Coverage is in 1/16 px at the edges. Each row is
  blended into a background colour from a 17-entry palette and sent as one `blit`.
- **Strengths:** filled shapes with real weight contrast, and integer maths. Each pixel
  is written once per row, with one block write per row.
- **Limit:** it blends into one solid background colour, so it cannot go over a gradient
  or a picture.

## What carries over

The row-by-row structure carries over to strokes: a pixel's coverage is
`clamp(r + 0.5 - d)`, where `d` is its distance to the nearest segment crossing that row.
Segments are short and few per glyph (a digit has 2-12 after flattening), so per row
only a handful are near. In a band sprite the row is memory; on the screen, one window
and one block write per row replace the per-pixel calls.

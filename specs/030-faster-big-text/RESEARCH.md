# 030 Research

## Ours

- **Glyphs:** `src/faces.cpp`, `MG_*`, 57 SVG path strings in PROGMEM (2,849 B). They
  include digits, capitals, punctuation and symbols (`° ↑ ↓ ← → ✓ ✗ • ♥ ⚡`). The `MONO`
  table pointing at them is 456 B in RAM (`.rodata` at `0x3ffe9794`).
- **Drawing:** `monoDraw` parses each path (`pgmString`, `parsePath` into a `Poly`: two
  400-float arrays, 3.2 KB of heap), then `strokePoly` draws it row by row:
  - for each row, every segment within reach;
  - for each pixel in the segment's reach, the projection onto the segment and the
    squared distance in floats, and `sqrtf` for edge pixels;
  - the coverage kept in `cov[240]`, then `emitRow`: full runs as `span()`, each partial
    pixel a `drawPixel` with a blend against `under`.

  On the screen, each call goes through `NightPanel`'s virtual overrides, which tint the
  colour; in a band sprite, they are memory writes. The emulator charges estimated
  ESP8266 cycles for these loops (`EMU_WORK`), so its draw times follow the chip's.
- **`drawWedgeLine`** (`wedge()`) is used only by `line` items and thin path strokes, not
  by the big font.
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

Not the row structure, which ours already has, but two choices:
- **Integers.** Our pixel coverage is `clamp(r + 0.5 - d)`, where `d` is the distance to
  the nearest segment. In fixed point, each segment's direction and its inverse squared
  length are computed once per draw, and along a row the projection changes by a
  constant per pixel, so the inner loop is a few integer adds and multiplies. A small
  table can replace the square root at the edges.
- **One block per run.** Each run of covered pixels in a row (edge, solid core, edge) is
  built in a buffer, blended with `under` at the edges, and sent with one window and one
  block write, instead of a `drawPixel` for each edge pixel. Not the whole row: the
  pixels between strokes must stay as they are under transparent text (theirs can fill
  the row, because their text always has a solid background).

Segments are short and few per glyph (a digit has 2-12 after flattening), so only a
handful are near any row.

# 030 Faster big text, in less flash

Status: In progress (approved 2026-10-31; built, on the emulator; to measure on the clock)

## Problem

The "big" line font draws most large text: the Big tiles, the flip clock's digits, and
every custom face that uses `"font": "big"` (all 41 converted smalltv-pomodoro faces do).
It is small, 57 glyphs in 2,849 B of path text, but slow. On the clock, a full draw of
Big time takes 636-948 ms (`drawMs`, 2.0.0), and a face that changes its big text often
spends most of its time there.

The renderer already works row by row (`strokePoly`: a coverage buffer per row, solid
runs sent as spans). Each draw still does three slow things:
- it parses the glyph's SVG path text and flattens its curves in floats, into a 3.2 KB
  heap buffer (`Poly`);
- for each pixel near a stroke, it computes the distance to every nearby segment in
  floats, which the ESP8266 does in software (no FPU), with a square root at the edges;
- it sends each anti-aliased edge pixel as its own `drawPixel` (a window and a write per
  pixel on the screen).

(Corrected 2026-10-31: the first version of this spec said `drawWedgeLine`, which only
the `line` item and a path's thin strokes use.)

The bitmap fonts 2, 4 and 6 (7.7 KB) are fast, but come in fixed sizes without smooth
edges.

smalltv-pomodoro fills its large digits row by row too (RESEARCH), but in integers only,
with each row sent as one block. Those two choices are what is worth taking. Its glyph data is not: an outline costs about 300 B
per digit, against about 50 B for one of our strokes.

## Goals

1. **Measure first.** A host benchmark draws Big time, a flip-clock digit, and a line of
   capitals at 40 px with the current renderer and with the new one, and counts pixels
   written and operations. On the clock, `drawMs` per face gives the real numbers.
2. **Glyphs compiled at build time.** A script turns the path text into a compact binary:
   integer points on the glyph grid, curves flattened once. The data is at most the
   2,849 B of today, and the 456 B glyph table moves from RAM to flash.
3. **The row renderer in integers.** For each pixel row, it takes the segments that
   cross the row and computes each pixel's coverage from its distance to the nearest
   segment in fixed point (per-segment constants once per draw, no float and no `sqrt`
   per pixel). It writes each run of covered pixels as one block, to the screen or the band
   sprite. It keeps the anti-aliasing against `under`, and the night
   tint.
4. **At least 3× faster.** A full Big time draw goes from 636-948 ms to under 250 ms on
   the clock, and the flip clock's flip animation runs at its frame rate.
5. **The same look.** The same glyph shapes and stroke width. The face preview and the
   integration screens stay within a small pixel difference at the edges, reviewed on
   the sheet.
6. **Optional, measured, decided after 4:** draw font 6 (48 px digits) and font 4 (26 px)
   with the big font, to free up to 6 KB of flash, if they look right and draw as fast.
   Flash freed this way can go to a new font that fits the product, drawn by the same
   row renderer (an outline digits font, say), each proposed with its flash and look.

7. **Redraw only when pixels change** (added 2026-11-01, approved): each item's change
   check uses what it would draw, not the raw value: a bar's filled width in pixels, a
   ring's arc in steps, a line's end points rounded to pixels. A value that changes but
   moves no pixel redraws nothing (smalltv-pomodoro's per-layer signatures).
8. **Edge runs as one block** (added 2026-11-01, approved): `emitRow` sends each run of
   covered pixels (anti-aliased edges and solid core together) with one window and one
   block write instead of a `drawPixel` per edge pixel, for the big font and every path.
   The row buffer lives on the heap while a glyph or path draws, not on the stack.

## Non-goals

- Importing smalltv-pomodoro's outline fonts (Bebas, Outfit, Righteous): more flash for
  digits only, and they need a solid background behind them.
- Lower case in the big font.
- Compiling a custom face into structures once at load (smalltv-pomodoro does): the
  owner judged CPU not a concern for now.

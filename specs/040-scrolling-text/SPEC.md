# 040 Scrolling text

Status: Draft (proposed 2026-11-04, after smalltv-mod's scrolling faces; not approved)

## Problem

Text longer than its room is cut (`lines`, a modifier such as `{title:12}`) or wrapped,
and wrapped text redraws the whole face. A line that scrolls (a marquee) shows a long
title, a news line or a list of prices in one row. Today it can only be faked: a text
item's `x` from `{time.sec}` and `{time.ms}` at a high `fps`, with the text's width
typed by hand (no placeholder gives it), no box to keep it in (it runs to the screen's
edges, over other items), and a repaint as wide as the text each frame.

## Goals (to agree)

1. `text` items take `"scroll"`: the speed in pixels per second (for example 40). With
   `w`, the text moves right to left inside that width and loops with a gap, measured
   from its real width, so pushed text of any length works. Text that fits stays still.
2. Drawn off screen in the renderer's existing strips (as smooth updates are), only
   inside its box: no flicker, nothing outside the box touched, no new RAM buffer.
3. A short pause at the start of each loop.
4. The Message face scrolls a title that does not fit instead of cutting it.
5. The face's `fps` sets how smooth it is; the item asks for no more than it needs.
6. Within the flash room: a target of 2 KB or less.

## Non-goals

- Vertical scrolling, wrapped text that scrolls, or the panel's hardware scrolling
  (it moves the whole screen).

## Open questions

- Also for `"font": "big"` and the digit fonts, or fonts 2 and 4 only at first?
- The Message face: scroll the title only, or also a one-line text?

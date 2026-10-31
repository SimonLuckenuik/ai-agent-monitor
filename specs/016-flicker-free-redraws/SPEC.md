# 016 Flicker-free redraws

Status: Done in 1.7.0 and 1.8.0 (written after the fact, 2026-10-30)

## Problem

Faces with moving parts (Bauhaus's hands, Binary clock, Block world's sun, Sun arc) blank
and redraw the whole face; big-font numbers redraw every digit when one changes; the
mascot's Z did not animate; `{time.ms}` was not tied to the clock's second.

## Requirements

1. An item that moves, resizes, shows, hides or changes without a `bg` repaints only the
   box around its old and new places, with the items under and over it.
2. That box is drawn off screen in strips and pushed, so nothing else blinks; the night
   tint is applied; without RAM, fall back to clipped drawing on the panel.
3. Covers every item type and repeats, except wrapped text and images.
4. Big-font text redraws only changed characters (Big time, Classic, Simple, custom big
   text with a `bg`).
5. An animated sprite is sized by its largest frame.
6. `{time.ms}` counts from the start of the second and is fixed for a frame.
7. Paths and the big font draw only the rows of the current strip.

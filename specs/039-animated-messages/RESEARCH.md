# Research

## smalltv-mod (giovi321, WTFPL, commit 6e65e03)

Its notification overlays move: a pixel mascot waves for `waiting`, jumps for `done`,
an alert flashes. Only the idea is taken.

## The robot

The owner's references (stock pixel robots: boxy heads, screen faces, antennas, steel
limbs, blue accents) set the genre; the drawing is new. A head that is a screen fits a
monitor, and lets the message's colour be its face. 26 x 31 cells at 3x scale; colours:
outline, shell, shell shade, steel, blue joints, screen, screen glint, accent.

## Cost (2026-11-04)

- One sprite of whole frames would be about 5 KB of template text; five parts, only the
  face and arms with frames, take 2.9 KB, and the template is 3.8 KB (was 1 KB).
- `firmware.bin.gz`: 419,143 B (2.1.1) to 420,306 B, +1,163 B.
- RAM: the template is parsed into the face document while the Message face shows, as a
  custom face of that size is; checked under the clock's heap in the integration run.
- Drawing: the face (opaque, 12 x 5 cells) redraws alone when it changes; an arm's new
  frame and the hop repaint only around the parts, off screen. The flash is the
  panel's INVON, so it draws nothing.

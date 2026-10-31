# Moving items repaint off screen, in TFT_eSprite strips

Date: 2026-10-30

Spec: [016](../../specs/016-flicker-free-redraws/SPEC.md)

## Evidence

1.7.0 repainted only the box around a moved item, clipped on the panel: Bauhaus went
from about 100 ms to 40 ms, but the box still flashed the background. Measured with
frames grabbed at about 40 per second during a minute tick.

## Decision

Draw the box off screen in 5 KB TFT_eSprite strips and push them: no pixel shows
anything but the old or the new picture (0 such pixels on every face tested, with and
without the night tint). Falls back to clipped drawing when the 5 KB is not free.

## Cost

About 10.7 KB of flash (6.5 KB of it TFT_eSprite); update room 618,496 B, 95% used.
288 B of static RAM plus the strip while drawing.

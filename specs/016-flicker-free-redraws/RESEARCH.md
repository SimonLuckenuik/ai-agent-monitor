# Research

- 1.7.0 clipped repaint on the panel: Bauhaus 100 → 40 ms, but the box still flashed.
- TFT_eSprite costs 6.5 KB of flash; the whole change about 10.7 KB; update room 95%
  used (decision record 2026-10-30).
- TFT_eSPI `drawWedgeLine` clips with the viewport datum but draws without it: lines in
  a band went missing (issue record).
- `{time.ms}` was `millis() % 1000`; strips of one repaint saw different values and left
  stripes (issue record).
- Calendar had no minute-tick repaint at all (measured: no screen updates).

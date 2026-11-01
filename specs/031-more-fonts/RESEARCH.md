# 031 Research

- smalltv-pomodoro's `fonts.cpp` (GPL-3.0, commit 2f6836d): Outfit and Righteous digits as
  `int16` outline points on a 720-unit cell (737 and 840 points, about 3-3.4 KB each);
  `vecText` fills them row by row with integer coverage. Bebas lives elsewhere in their
  tree. Their DSEG7 and Geo digits are drawn by code (`dsegDigit`, `geoDigit`), their DM
  digits are 5×7 pixel art (`dmDigit`).
- Fonts: Bebas Neue (Dharma Type), Outfit (The Outfit Project Authors), DSEG7
  (keshikan): all SIL Open Font License 1.1, which allows embedding with the licence.
- One byte per coordinate on a 256-step grid halves their `int16` points; at 120 px a step
  is under half a pixel.
- The update budget (`scripts/build_options.py`): the plain image plus the compressed one
  must fit the update area; at 2.0.x after spec 029, about 37 KB of room, which new code
  uses at about 1.7 times its size.
- The comparison sheet (`fonts_compare.png`, 2026-11-01) shows each of their fonts beside
  our conversion with the big font.

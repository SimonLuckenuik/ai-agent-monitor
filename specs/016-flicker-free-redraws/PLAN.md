# Plan

1. `itemBox()` per type; dirty boxes (up to 4, merged); `repaintBox()` with a 5 KB
   TFT_eSprite band; `wedge()` for lines in bands.
2. `monoDiff()` per glyph cell; `bigShown`/`bigWhere` per item.
3. `spriteCells()`; `frameMs`/`secondAt`.
4. Check: frames grabbed at ~40/s during a minute tick (no pixel that is neither before
   nor after), result equal to a full repaint, with and without the night tint.

# Plan

1. Glyphs as stroke paths (`MG_*`), `monoText()`/`monoDraw()`.
2. The three tiles; Classic and Simple in the line font (font 8 removed).
3. `drawMs` in `/status`; measure on the clock; speed up strokes (only pixels near each
   segment, square roots on edges only, yield every row).
4. CPU at 160 MHz.

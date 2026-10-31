# Research

- On the clock (2026-10-25), the first tiles drew one glyph at a time, visibly; Big date
  tripped the software watchdog. Strokes now yield every row and skip most float maths;
  output is pixel-identical.
- 160 MHz instead of 80: line font and paths roughly twice as fast; more power and heat,
  within the ESP8266's rating (decision record 2026-10-25).
- Pre-drawing glyphs to flash was considered; measuring first showed the faster strokes
  were enough.
- Built-in faces as JSON would not shrink the firmware (about 11 KB of code either way),
  so they stay in code (decision record 2026-10-29).

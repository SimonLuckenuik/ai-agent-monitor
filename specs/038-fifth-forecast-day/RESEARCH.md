# Research

- Open-Meteo's daily forecast takes `forecast_days` up to 16; the reply grows by one value
  per daily field (10 fields).
- Free face ids: 6 (Flip, retired: the order, night face and theme checks skipped it) is
  the only id below `FACE_COUNT` (14); 14 and 15 are the custom face and the message.
  Reusing 6 keeps `FACE_COUNT`, the 14-bit face mask and every saved id.
- `repeat` over `"days"` lays out `forecast-strip` and the Forecast-like example faces in
  four 58-pixel columns (6 + 4 x 58 = 238): a fifth entry would fall off the screen.

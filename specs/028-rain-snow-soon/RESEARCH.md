# 028 Research

- Open-Meteo forecast API: `minutely_15` with `forecast_minutely_15=4` gives four quarter
  hours from the current one. Checked 2026-10-31 for Saint-Hubert: `rain` (mm),
  `snowfall` (cm), `precipitation_probability` (%) and `weather_code` (WMO) all come back.
  Outside the areas with 15-minute models, Open-Meteo interpolates the hourly data.
- smalltv-pomodoro (`main.cpp`, `rainIn`) asks for `precipitation` and
  `precipitation_probability`. It keeps the first quarter with rain as minutes (0-45).
  Its faces show `{RAIN}` ("rain 15 min"), and the tomato opens an umbrella. It has no
  snow.
- Our fetch (`src/weather.cpp`, `httpJson`) is one request with `current`, `daily` and
  `forecast_days=4`, read through a filter document. On the chip it takes 3,080-3,896 B
  of heap. Four quarter hours of four values, and four filter entries, add about
  300-400 B.

# Plan

1. `weather.h` (`FORECAST_DAYS` 5, `FORECAST_STRIP` 4), `weather.cpp` (`forecast_days=5`).
2. `faces.h` (`FACE_DAY4` = 6, `dayOfFace`), `faces.cpp` (the Day 4 tile; the Forecast
   face and `repeat` over `"days"` at `FORECAST_STRIP`), `main.cpp` (id 6 accepted; Day 4
   placed after Day 3), `config.h` (`facesv` 5), `integrations.cpp` (its name), the
   settings page's face list.
3. Face preview (Day 4 drawn, incremental = full), integration tests (`forecastDays` 5,
   the face list), the skill's `clock.py check` accepting `day4.*`.

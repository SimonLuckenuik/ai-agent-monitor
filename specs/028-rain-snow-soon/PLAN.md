# 028 Plan

1. `src/weather.cpp`: the URL and the filter; `wx.rainIn`, `wx.snowIn` (-1 none, else
   minutes), `wx.rainMm`, `wx.snowCm`, from the four quarter hours. Measure `weatherHeap`
   before and after in the emulator.
2. `src/faces.cpp`: `now.rain_in`, `now.snow_in`, `now.rain_mm`, `now.snow_cm`, and the
   Weather today face's line. Unit checks for the placeholders and the counting rule.
3. Emulator: the fixed reply gains `minutely_15`. Integration cases for dry, rain soon,
   snow soon, and rain now.
4. `examples/faces/umbrella.json`, its preview and its README row.
5. Docs: the integrations API, the skill reference (regenerated), CHANGELOG, plugin
   version.
6. On the clock: `weatherHeap` after the owner installs it.

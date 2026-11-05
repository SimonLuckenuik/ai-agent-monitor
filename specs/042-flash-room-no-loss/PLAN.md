# Plan

1. `src/json_parse.h`, `src/json_parse.cpp`: the four `parseJson()` overloads (text or
   stream, filtered or allow-all), the two that parse `noinline`. All `deserializeJson`
   calls in `src/` changed to them. **Prototyped.**
2. `src/weather.cpp`: `httpJson(host, path, ...)` on a `WiFiClient`; `HTTPClient` and its
   include dropped (also the unused include in `main.cpp`). **Prototyped.**
3. Host tests: the mocks gain a `Stream` (the mock `File` derives from it);
   `json_parse.cpp` in `emulator/build.sh` and the unit and face preview runners.
   **Done.**
4. Check: `pio run` sizes; unit, safety, face preview and web tests; the emulator fetching
   the real Open-Meteo (forecast and city search) with no fixed weather; the integration
   suite; `nm` shows only the two parser copies.
5. Docs: footprint document, CHANGELOG (Unreleased, "smaller firmware"), STATUS.
6. On the clock after the release: the weather and a city search, `weatherHeap`.

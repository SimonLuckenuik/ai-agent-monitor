# 033 Progress

- 2026-11-01: written and approved (feature 1 of smalltv-pomodoro's list), the table to be
  loaded only when the owner asks for it.
- 2026-11-01: the owner asked for a source that stays up to date instead of a table in
  the repo: the browser's own time zone database (Intl) and Open-Meteo's geocoding.
- 2026-11-01: built (settings page only).
  - `posixRule(zone)`: offsets at noon for 400 days (Intl `longOffset`), each change
    found to the minute by bisection, then `Mm.w.d/h` rules; names from the en-US, en-GB,
    en-AU and en-IN locales, else `<+05>` forms. Checked every 3 hours for two years.
    417 zones: all but Casablanca, El Aaiun, Jerusalem and one more get a rule; Toronto,
    London, Paris, Sydney and Adelaide match tzdata's own rules exactly.
  - Sampling 366 days failed for North America on 1 November (the day after a change
    leaves one change in the year): 400 days.
  - `tests/web/tz_rules.test.js` (Node, in CI) reads the code from the page.
  - Checked in the emulator's settings page: the list fills only when "More time zones…"
    is chosen (418 zones); Lisbon and the city's zone save their rules, and the clock
    shows WEST, CEST, EDT. The first city lookup dropped ", CA" and found Saint-Hubert in
    Belgium: it now makes the clock's own request (name and `countryCode`), and finds
    America/Toronto.

# 033 Plan

1. Settings page, `posixRule(zone)`: sample the zone's UTC offset at noon each day of the
   next 12 months (`Intl.DateTimeFormat` with `timeZoneName: "longOffset"`); find the two
   changes, then the hour of each by bisection; write `STDoffset[DST[offset],Mm.w.d/h,
   Mm.w.d/h]`, with `<+03>`-style names when the zone has no alphabetic abbreviation
   (`timeZoneName: "short"`). Check it (goal 4) with a small POSIX evaluator in the page.
2. The "More time zones…" option and the "Use the city's time zone" button; the mismatch
   note after a city change.
3. Tests: run `posixRule` in Node for a set of zones (Toronto, Montreal, Lisbon, London,
   Adelaide, Kathmandu, Santiago, Casablanca, Tehran) and compare with known rules; the
   emulator's settings round trip with a long rule.
4. Docs: settings.md, CHANGELOG.

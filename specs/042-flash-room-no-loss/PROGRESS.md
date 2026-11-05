# Progress

- 2026-11-05: spec written after the owner asked to start with the changes that lose
  nothing (041's inventory). LTO tried and dropped (+14.9 KB). Prototyped goal 1 (one
  parser: -9,424 B image, room 24,576 -> 36,864 B) and goal 2 (no HTTPClient: a further
  -4,896 B, room 45,056 B). Unit (434 checks), mDNS, guard, safety, face preview and web
  tests pass.
- 2026-11-05: the emulator (built from this change, no fixed weather) fetched the real
  Open-Meteo: city search "Sherbrooke" -> "Sherbrooke, Quebec, CA", 5 forecast days,
  `weatherHeap` 3,304 B (the clocks report 6,840 B with HTTPClient; the emulator's heap is
  modelled, so the clock's number is to confirm); an unknown city gives "City not found".
  `nm`: the parser is in the ELF for `const char*` and `Stream` only (4,573 B, from
  13,104 B). Integration suite: 34 cases pass.
- 2026-11-05: approved by the owner. Footprint document, CHANGELOG (Unreleased),
  STATUS, roadmap and the specs index updated; tests run again before the commit.
- Left: merging into `faces-v2` (on the owner's go) and the release; on the clock after release,
  the weather, a city search and `weatherHeap`. The unreachable (-1) and no reply (-11)
  paths were not exercised.

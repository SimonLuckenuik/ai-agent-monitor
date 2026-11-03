# Progress

- 2026-10-30: proposed.
- 2026-10-31: the owner installed 1.8.0 (192.168.66.229). Loaded aquarium, night-lake,
  flip-clock-wide and retro-runner; retro-runner and flip-clock-wide restart the clock
  (issue 2026-10-31, spec 021).
- 2026-11-03: the owner installed 2.1.0. Done: the install; heap at rest and at the lowest
  (23.9 and 22.2 KB, and 16.3 KB with every face on 2.0.0); draw times (Big time 106 ms,
  Big date 91, Big weather 99, Agents 55, the Day tiles 98-132; Classic, flip-clock and
  Bauhaus were not drawn since the start); mDNS; MQTT (2.0.0, spec 025; not connecting
  on 2.1.0: the broker at 192.168.66.59 to check); French labels (the clock runs in French); issue
  records (spec 021). Left, for the owner: orientations 90/180/270, gamma curves, screen
  off and wake, and Classic's draw time.
- 2026-11-03: closed. The owner checked the orientations, gamma curves and screen off and wake on
  2.1.1 ("spec 18 works"); the rest was read from `/status` (see the entry above).

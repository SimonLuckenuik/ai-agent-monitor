# 046 Plan

First plan, for the recommended design (option B in RESEARCH.md); to be adjusted to the
owner's answers to the open questions. Nothing is built before approval.

1. **Setting** `apoff` in `src/config.h` (field, load, save), `applySetting()` (0 or
   5-1440), `configJson()`, backup and restore (`docs/specifications/settings.md`).
   Check: unit test of the clamp; `tests/web/backup` round trip.
2. **The switch** in `src/main.cpp`: `apOn()` (the body of today's `startAP()` after the
   mode: `enableAP(true)`, `softAPConfig`, `restartAP()`, `dns.start`) and
   `serviceAp()` in the loop after `serviceSta()` (requirements 2-5). `/status` gets
   `ap`. Recovery's loop never calls it. Check: `pio run` (size), safety tests (a
   source guard: recovery never closes the AP).
3. **Emulator**: the host core's WiFi mock answers `softAPdisconnect` /
   `softAPgetStationNum`; the emulator reports the AP state on its Device panel. An
   integration case with `apoff` at its smallest and the link mocked up: `/status`
   `ap` goes false; a mocked link drop brings it back. (If waiting 5 minutes is too
   slow, a build flag for the test shortens a minute.)
4. **Settings page**: the WiFi card's select and the "off" text; `node tests/web/*.js`;
   screenshot of the card.
5. **Docs**: `docs/specifications/updates-and-recovery.md` ("Recovery access": the AP is
   WPA2 except in recovery, and closes after `apoff`), `settings.md`,
   `integrations-api.md` (`/status` `ap`), `CHANGELOG.md`, the skill's `reference/api.md`
   if it lists `/status` fields.
6. **The clock** (the owner): install, then check (a) the AP disappears an hour after
   boot and the station link survives the switch (no gap in `/status` uptime or
   pings); (b) `/status` heap with the AP on and off; (c) unplugging the router brings
   the AP back and the setup screen; (d) a power cycle brings it back for an hour.

Lands in the next minor release (2.4.0).

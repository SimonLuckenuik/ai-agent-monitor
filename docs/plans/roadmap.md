# Roadmap

Updated: 2026-11-10. The order of work. Each line is a spec in [specs/](../../specs/README.md)
or an idea that becomes one when work on it starts; nothing is built before its spec is
approved.

## Next

0. **json-render on the clock** ([047](../../specs/047-json-render/SPEC.md), approved
   2026-11-09): in the firmware build for 2.7.0; a 2.7.0-dev image on a clock first.
0. **Gallery versions and json-render faces** ([049](../../specs/049-gallery-versions-and-json-render-faces/SPEC.md),
   draft): each entry's minimum firmware, checked by the settings page and `clock.py`;
   json-render faces for the gallery, agent and everyday ones, with demo values.
1. **Check 2.2.0 on the clock**: Moni on waiting, done and error messages, the flashes,
   *Animated messages* off ([039](../../specs/039-animated-messages/SPEC.md)).
2. **Check 2.3.1 on the clock**: the update installs (4 KB of room left); pair the clock
   with the owner's OpenClaw Gateway ([041](../../specs/041-openclaw-node/SPEC.md), done in
   2.3.0 with [042](../../specs/042-flash-room-no-loss/SPEC.md),
   [043](../../specs/043-emulator-openclaw-panel/SPEC.md) and
   [044](../../specs/044-openclaw-skill/SPEC.md)): the signing time on the chip, the
   commands, `clock.help` ([045](../../specs/045-openclaw-help/SPEC.md), 2.3.1), the skill
   with an agent once a model is available. Open: where the gateway runs,
   reachable from the IoT VLAN over `ws://`.
4. [040 Scrolling text](../../specs/040-scrolling-text/SPEC.md): proposed, waiting for
   approval.
5. **Check 2.4.0 on the clock** ([046](../../specs/046-setup-network-timeout/SPEC.md)):
   the setup network closes an hour after joining without dropping the WiFi link, the
   heap with it off, it comes back when the router goes away, recovery's WPA2 network.
6. **Check 2.5.0** ([048](../../specs/048-files-with-the-update/SPEC.md)): Install from
   GitHub on a clock, from the home network and from the clock's own; the Pages job of
   the release run; a phone left on the setup network. 048's phase 2 shipped in 2.6.0
   with link-time optimisation: a day of 2.6.0 on a clock (lowest heap, crashes).

## Waiting

- [037 A Claude usage face](../../specs/037-claude-usage-face/SPEC.md): set aside by the
  owner (2026-11-03).
- [034 Custom faces in the clock's language](../../specs/034-face-languages/SPEC.md):
  deferred by the owner (2026-11-02).

## Ideas (no spec yet)

- **AI Agent Monitor Studio** (the owner, 2026-11-07; a follow-up spec if 047 goes
  ahead): a GitHub Pages site, linked from the clock's settings page, for building custom
  faces from the catalog's components with a live preview (the emulator's renderer in the
  page). Opened from the clock, it would get what it needs to send the face to that clock.
- Hardware scrolling and the panel's idle (8-colour) mode (spec 014 research).
- A way back to stock over WiFi (upstream idea).
- **Visual and animation ideas** (2026-11-10, asked for by the owner). The firmware ones
  compete for about 3 KB of update room once json-render ships, so each needs its own
  room found (more data as files that come with the update, spec 048) or a trade; the
  face-only ones cost nothing on the clock.
  - **Transitions between faces** (firmware): a wipe or slide when rotation changes face,
    the new face drawn into a growing viewport (no frame buffer needed); off at night and
    with a setting.
  - **Animated values** (firmware): an item's value, position or colour eases to its new
    value over a few hundred ms when a variable changes, instead of jumping (bars, rings,
    gauges; `"ease": 400`), drawn with the existing repaint boxes.
  - **Scrolling text** (firmware): a `marquee` text item, clipped to a width, for titles
    longer than the screen (now playing, news, a long agent message).
  - **Split-flap text** (firmware): changed characters flip into place (monoDiff already
    knows which changed); for departure boards, scores, counters.
  - **Weather ambience** (firmware): a few drifting particles over the weather faces from
    the current weather: rain streaks, snowflakes, stars on clear nights. Small dots
    redrawn over a known background, no frame buffer.
  - **Celebrations** (firmware): a message kind, or a `celebrate` call, that bursts
    confetti over the current face for a few seconds: a build fixed, a goal reached, a
    birthday at midnight.
- **Air quality and pollen** (firmware: Open-Meteo's air-quality API, same provider as the
  weather): `air.*` placeholders (AQI, PM2.5, pollen by kind) and a face; costs flash and
  a little heap for one more request. (The other everyday scenarios, energy, music,
  departures, activity, are json-render faces in spec 049.)

## Done

Specs 001-017, released from 1.4.0 to 1.8.0; 021, 023 in 1.9.0; 024 in 1.9.1; 025 in
2.0.0; 026-033 and 035 in 2.1.0; 036 and 038 in 2.1.1; 039 in 2.2.0; 041-044 in 2.3.0; 045 in 2.3.1; 046 in 2.4.0; 018, 022 closed on the clock; 019
in the plugin (0.7.2). See [specs/README.md](../../specs/README.md) and `CHANGELOG.md`.

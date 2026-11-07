# Roadmap

Updated: 2026-11-06. The order of work. Each line is a spec in [specs/](../../specs/README.md)
or an idea that becomes one when work on it starts; nothing is built before its spec is
approved.

## Next

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
- [020 ESP-NOW between clocks](../../specs/020-esp-now/SPEC.md): dropped for now (2026-10-31).

## Done

Specs 001-017, released from 1.4.0 to 1.8.0; 021, 023 in 1.9.0; 024 in 1.9.1; 025 in
2.0.0; 026-033 and 035 in 2.1.0; 036 and 038 in 2.1.1; 039 in 2.2.0; 041-044 in 2.3.0; 045 in 2.3.1; 046 in 2.4.0; 018, 022 closed on the clock; 019
in the plugin (0.7.2). See [specs/README.md](../../specs/README.md) and `CHANGELOG.md`.

# Specs

One folder per piece of work: `NNN-<slug>/` with `SPEC.md`, `PLAN.md`, `RESEARCH.md`
and `PROGRESS.md`. How they are used (approval before code, keeping them true as
decisions change) is in [CLAUDE.md](../CLAUDE.md#how-we-work-spec-driven-development).
Specs 001-017 were written after the fact on 2026-10-30, from the work and the
conversations that produced it; from 018 on, specs come first.

Next free number: **049** (047 is used on branch `json-render`).

| Spec | Status |
|---|---|
| [001 Fork SmartClock-OSS as the AI Agent Monitor](001-fork-and-rename/SPEC.md) | Done in 1.4.0 |
| [002 Emulator](002-emulator/SPEC.md) | Done in 1.4.0 |
| [003 Custom faces v2: dynamic rendering](003-custom-faces-v2/SPEC.md) | Done in 1.4.0 |
| [004 Agent messages and the Agents board](004-agent-messages/SPEC.md) | Done in 1.4.0 |
| [005 The clock in French](005-french/SPEC.md) | Done in 1.4.0 |
| [006 Claude Code plugin and skill](006-claude-code-plugin/SPEC.md) | Done, kept in step (0.5.3) |
| [007 Big tiles and the line font](007-big-tiles-and-line-font/SPEC.md) | Done in 1.4.0 |
| [008 Rotation order, night colour and the night face](008-rotation-and-night/SPEC.md) | Done in 1.4.0 / 1.6.0 |
| [009 Compressed updates and releases from main](009-compressed-updates-and-releases/SPEC.md) | Done in 1.4.0 / 1.5.0 |
| [010 Day tiles and Weather Icons](010-day-tiles-and-weather-icons/SPEC.md) | Done in 1.4.0 |
| [011 Integration tests and README screenshots](011-integration-tests-and-screenshots/SPEC.md) | Done in 1.4.0 |
| [012 A richer custom face model](012-face-model/SPEC.md) | Done in 1.5.0 |
| [013 Palettes and roles, and the big font in custom faces](013-palettes-and-roles/SPEC.md) | Done in 1.6.0 |
| [014 The display's own features](014-display-features/SPEC.md) | Done in 1.6.0 |
| [015 Emulator scenarios](015-emulator-scenarios/SPEC.md) | Done in 1.6.0 |
| [016 Flicker-free redraws](016-flicker-free-redraws/SPEC.md) | Done in 1.7.0 / 1.8.0 |
| [017 Example faces gallery](017-example-faces/SPEC.md) | Done, grows with new faces |
| [018 Check 1.5-1.8 on the real clock](018-first-run-on-the-clock/SPEC.md) | Done |
| [019 Claude Code hooks that drive the clock](019-claude-code-hooks/SPEC.md) | Done (plugin 0.7.0) |
| [020 ESP-NOW between clocks](020-esp-now/SPEC.md) | Dropped |
| [021 Crash records and crash-loop protection](021-crash-report/SPEC.md) | Done in 1.9.0 |
| [022 The emulator runs out of memory where the clock does](022-emulator-device-memory/SPEC.md) | Done |
| [023 Custom faces within the 4 KB stack](023-renderer-stack/SPEC.md) | Done in 1.9.0 |
| [024 Memory headroom: weather and uploads beside big faces](024-memory-headroom/SPEC.md) | Done in 1.9.1 |
| [025 RAM back, and MQTT and mDNS on again](025-ram-mqtt-mdns/SPEC.md) | Done in 2.0.0 |
| [026 The smalltv-pomodoro faces, and a face gallery](026-face-gallery/SPEC.md) | Done in 2.1.0 |
| [027 A small mDNS responder](027-small-mdns/SPEC.md) | Done in 2.1.0 |
| [028 Rain or snow within the hour](028-rain-snow-soon/SPEC.md) | Done in 2.1.0 |
| [029 Network guards: Wi-Fi downgrade, request host, clock time](029-network-guards/SPEC.md) | Done in 2.1.0 |
| [030 Faster big text, in less flash](030-faster-big-text/SPEC.md) | Done in 2.1.0 |
| [031 More fonts: Bebas, Outfit, seven segments, pixel digits, and a stroke weight](031-more-fonts/SPEC.md) | Done in 2.1.0 |
| [032 Backups that keep the custom faces](032-backup-with-faces/SPEC.md) | Done in 2.1.0 |
| [033 Every time zone, and the city's own](033-time-zone-from-city/SPEC.md) | Done in 2.1.0 |
| [034 Custom faces in the clock's language](034-face-languages/SPEC.md) | Deferred |
| [035 One city search, for the weather and the time zone](035-city-search/SPEC.md) | Done in 2.1.0 |
| [036 Flash room: zopfli, our own time zone parser, a minified page](036-flash-room/SPEC.md) | Done in 2.1.1 |
| [037 A Claude usage face, fed by Claude Code](037-claude-usage-face/SPEC.md) | Draft |
| [038 A fifth forecast day, and its tile](038-fifth-forecast-day/SPEC.md) | Done in 2.1.1 |
| [039 Animated waiting and done screens](039-animated-messages/SPEC.md) | Done in 2.2.0 |
| [040 Scrolling text](040-scrolling-text/SPEC.md) | Draft |
| [041 The clock as an OpenClaw node](041-openclaw-node/SPEC.md) | Done in 2.3.0 |
| [042 Flash room without losing a feature](042-flash-room-no-loss/SPEC.md) | Done in 2.3.0 |
| [043 An OpenClaw panel in the emulator](043-emulator-openclaw-panel/SPEC.md) | Done in 2.3.0 |
| [044 An OpenClaw skill for the clock](044-openclaw-skill/SPEC.md) | Done in 2.3.0 |
| [045 A help command on the OpenClaw node](045-openclaw-help/SPEC.md) | Done in 2.3.1 |
| [046 The setup network closes once the clock is on the router](046-setup-network-timeout/SPEC.md) | Done in 2.4.0 |
| [048 Read-only data as files that come with the update](048-files-with-the-update/SPEC.md) | Done in 2.5.0 (phase 2 open) |

## Templates

`SPEC.md`:

```markdown
# NNN Title

Status: Draft

## Problem
## Goals
## Non-goals
## Requirements
1. …
## Behaviour (settings, API, screens)
## Budget (flash, RAM, draw time)
## Open questions
```

`PLAN.md`: steps in order, each with the files it touches and how it is checked (unit
test, face preview, integration case, emulator screenshot, the real clock), and the
release it lands in.

`RESEARCH.md`: findings, measurements, options weighed and why they lost, links.

`PROGRESS.md`: a dated log, newest last: what was done (commits), measured, decided,
what is left and what was not verified.

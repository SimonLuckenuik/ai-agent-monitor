# Specs

One folder per piece of work: `NNN-<slug>/` with `SPEC.md`, `PLAN.md`, `RESEARCH.md`
and `PROGRESS.md`. How they are used (approval before code, keeping them true as
decisions change) is in [CLAUDE.md](../CLAUDE.md#how-we-work-spec-driven-development).
Specs 001-017 were written after the fact on 2026-10-30, from the work and the
conversations that produced it; from 018 on, specs come first.

Next free number: **024**.

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
| [018 Check 1.5-1.8 on the real clock](018-first-run-on-the-clock/SPEC.md) | Draft |
| [019 Claude Code hooks that drive the clock](019-claude-code-hooks/SPEC.md) | Draft |
| [020 ESP-NOW between clocks](020-esp-now/SPEC.md) | Dropped |
| [021 Crash records and crash-loop protection](021-crash-report/SPEC.md) | In progress |
| [022 The emulator runs out of memory where the clock does](022-emulator-device-memory/SPEC.md) | In progress |
| [023 Custom faces within the 4 KB stack](023-renderer-stack/SPEC.md) | In progress |

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

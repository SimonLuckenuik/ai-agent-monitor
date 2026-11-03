# 037 A Claude usage face, fed by Claude Code

Status: Draft (proposed 2026-11-03, after smalltv-mod's usage meter; not approved)

## Problem

How much of the 5-hour and weekly Claude limits is left is only visible inside Claude
Code. smalltv-mod shows it on the desk (big percentages, fill bars, reset countdowns, an
"even pace" marker for the week), from a daemon that reads the owner's OAuth token and
polls an undocumented endpoint. We want the same face without a daemon or a token.

## Approach

Claude Code already hands the numbers to a status line script: `rate_limits.five_hour`
and `rate_limits.seven_day`, each `used_percentage` (0-100) and `resets_at` (Unix time),
for Pro, Max and Ultra subscribers, after the session's first reply. Nothing new is read
or stored: the plugin passes on what Claude Code gives it.

1. **A face**, `examples/faces/claude-usage.json`, in the gallery: the two windows as
   large percentages with bars coloured green → amber → red (one bar per colour, each
   with a `when`), the time to each reset (`{u5h_reset}-{time.epoch}`, as the pomodoro
   face counts down, so it moves between pushes), and the weekly even-pace marker
   ((7 days − time to reset) / 7 days). A spark of the 5-hour figure with `$history`.
   "No data" when `{age.u5h}` passes an hour.
2. **The feed**: `scripts/usage_status.py` in the plugin, set as Claude Code's status line
   command. It pushes `u5h`, `u5h_reset`, `u7d`, `u7d_reset` to `/api/faces/vars` when
   they changed (at most once a minute), never waits more than 2 seconds, and prints a
   short line for the status bar itself (or runs the owner's previous status line command
   and prints its output, so nothing is lost).
3. **The key**: variables need the API key, unlike messages: a second plugin option,
   `hooks_api_key`, marked sensitive (kept in the system's credential store).
4. **The skill** explains it; the face also works fed by anything else (MQTT, curl).

## Open questions

- A plugin cannot set the status line: the owner adds one line to their settings
  (`statusLine.command`), or `clock.py usage-setup` writes it. Which?
- The Stop hook does not get `rate_limits`; is a status line enough (it runs while a
  session is open), or should idle hours show the last values as stale?
- Which session wins when several are open: the newest push (they report the same
  account, so they agree).

## Non-goals

- Reading the OAuth token or calling Anthropic's endpoints from the plugin or the clock.
- Cost in dollars (API keys): `cost.total_cost_usd` is per session, not per account.

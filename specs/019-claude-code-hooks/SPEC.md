# 019 Claude Code hooks that drive the clock

Status: Done (plugin 0.7.0, unreleased; approved 2026-11-03)

## Problem

Agents must remember to message the clock, and often do not. Claude Code hooks can do it
on their own: show when a session waits for the owner, when it finishes, and clear it
when the owner answers.

## Goals

1. Hooks in the plugin, through `POST`/`DELETE /api/message` (no key needed):
   - Notification (a permission prompt, a question, an agent needing input) → `waiting`,
     shown now, kept up to 4 hours.
   - Stop → `done`, shown now, 10 minutes, with the start of Claude's reply as the text.
   - UserPromptSubmit and SessionEnd → the session's message cleared.
2. Off unless the owner gives the clock's address: the plugin's `hooks_clock_url` option
   ("Clock for session status", asked when the plugin is enabled, also in `/config`), or
   `CLOCK_HOOKS_URL`. Without it the hooks send nothing.
3. One message per session: id `cc-` and the session id's first 8 characters, titled with
   the project's folder, so the Agents board has one row per session.
4. Never in the way: every hook exits 0 and prints nothing; a request gives up after
   2 seconds, and after a clock that did not answer the hooks leave it alone for a minute.

## Decisions (the draft's open questions)

- **Titles:** the project folder's name (the hook's `cwd`); several sessions in one
  project differ by id.
- **How long `done` stays:** 10 minutes (the clock's default), or until the owner types.
  An idle prompt (Notification `idle_prompt`, a minute after Stop) does not replace it.
- **The Agents board:** yes, through messages; no face of our own.

## Non-goals

- A firmware change: the clock already has everything (messages, icons, the board).
- Progress while Claude works (PreToolUse/PostToolUse): too many requests for little.
- Subagents' own status.

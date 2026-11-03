# Progress

- 2026-10-30: proposed.
- 2026-11-03: approved and built (plugin 0.7.0).
  - `tests/plugin/session_status_test.py`: 14 checks (off without an address, each
    event's request, Markdown out of the reply, a silent clock costs about 2.7 s on
    Windows and then a minute of quiet, hooks.json's shape).
  - Emulator: the commands from hooks.json put "waiting" (permission prompt) and "done"
    (the reply) on screen under the project's name, and the prompt cleared it.
  - A real session (`claude -p --plugin-dir`, a recording server): UserPromptSubmit
    cleared, Stop posted "done" with the reply, SessionEnd cleared. As a background hook
    Stop's request never arrived, so Stop runs in the foreground.
  - Not seen in a live interactive session yet: the Notification hook (covered by the
    test and the emulator run).
- 2026-11-03: on the real clock (2.1.1): the commands from hooks.json posted "waiting", then "done"
  with the reply, and the prompt cleared it. Stop in the foreground cost a turn about 1.5 s,
  so every hook now runs in the background and the command starts no Python while the
  option is empty (plugin 0.7.2). A `claude -p` run: the prompt and the end cleared; the
  background "done" in between is lost there, as SessionEnd would clear it anyway.

# Plan

1. `plugins/ai-agent-monitor/hooks/session_status.py` (`waiting`, `done`, `clear`, the
   hook's JSON on stdin; standard library only) and `hooks/hooks.json` (Notification,
   Stop in the foreground, UserPromptSubmit, SessionEnd).
2. The `hooks_clock_url` option in plugin.json; plugin 0.7.0.
3. `tests/plugin/session_status_test.py` against a stand-in clock, in CI's plugin job.
4. Tried on the emulator through the commands in hooks.json, and in a real `claude -p`
   session with `--plugin-dir`.
5. The skill, README, CHANGELOG, STATUS and testing docs.

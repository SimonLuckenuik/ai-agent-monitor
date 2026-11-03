# Research

## Claude Code (hooks and plugin manifest documentation, 2026-11-03)

- A plugin's `hooks/hooks.json` holds `{"hooks": {<event>: [{matcher, hooks: [...]}]}}`;
  `${CLAUDE_PLUGIN_ROOT}` is the plugin's directory.
- Every hook gets JSON on stdin with `session_id`, `cwd`, `hook_event_name`, … .
  Notification adds `message` and `notification_type`, which its matcher filters on
  (`permission_prompt`, `idle_prompt`, `elicitation_dialog`, `elicitation_url_dialog`,
  `agent_needs_input`, …). Stop adds `last_assistant_message`.
- Notification ignores the exit code. Stop and UserPromptSubmit block on exit 2 only, so
  exit 0 always is safe. SessionEnd hooks share a budget of 1.5 seconds.
- `"async": true` runs a command hook in the background. Measured with `claude -p`: a
  background Stop hook is lost when the session ends right after it (no request reached
  the server), while UserPromptSubmit and SessionEnd ones arrived. Stop therefore runs
  in the foreground with `"timeout": 5`.
- `userConfig` in plugin.json asks the user for values when the plugin is enabled (and
  shows them in `/config`); hooks get them as `CLAUDE_PLUGIN_OPTION_<KEY>`.

## Python on Windows

`python3` may be the Microsoft Store alias, which exits 49 and swallows the hook's stdin.
The command probes `python3 -c ""` with stdin from /dev/null and falls back to `python`.

## Prior art: smalltv-mod (giovi321, WTFPL, commit 6e65e03)

Firmware for GeekMagic's SmallTV with `/api/notify`, "built for Claude Code sessions,
waving when a session needs an answer and celebrating when a task finishes": types
`waiting` and `done` with their own animations, a priority queue and 429 when full. The
hooks themselves are not in that repository. Our messages already have the icons, the
Agents board and replacement by id, so only the hooks were missing.

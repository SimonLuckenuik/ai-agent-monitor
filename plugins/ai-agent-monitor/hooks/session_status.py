#!/usr/bin/env python3
"""Claude Code hooks that show a session's state on the clock (spec 019).

    session_status.py waiting|done|clear     reads the hook's JSON on stdin

waiting  (Notification: a permission prompt or a question) a "waiting" message, shown now
done     (Stop: Claude finished its turn) a "done" message with the reply's first words
clear    (UserPromptSubmit, SessionEnd) removes the session's message

Off unless the plugin's "Clock for session status" option (CLAUDE_PLUGIN_OPTION_HOOKS_CLOCK_URL)
or CLOCK_HOOKS_URL names the clock, e.g. http://192.168.1.50. One message per session
(id "cc-" and the session id's first 8 characters), titled with the project's folder.
Never fails and never waits more than about 2 seconds; after a clock that did not answer,
the hooks leave it alone for a minute. Every hook runs in the background, so a turn never
waits for the clock, and hooks.json starts no Python at all while the option is empty.
Standard library only.
"""
import json
import os
import re
import sys
import tempfile
import time
import urllib.error
import urllib.parse
import urllib.request

TIMEOUT = 2
WAITING_TTL = 4 * 3600   # until answered: UserPromptSubmit clears it sooner
DONE_TTL = 600
QUIET = 60               # seconds without trying after a clock that did not answer
DOWN = os.path.join(tempfile.gettempdir(), "ai-agent-monitor-hooks.down")


def clock_url():
    url = os.environ.get("CLOCK_HOOKS_URL") or os.environ.get("CLAUDE_PLUGIN_OPTION_HOOKS_CLOCK_URL") or ""
    url = url.strip().rstrip("/")
    if url and "://" not in url:
        url = "http://" + url
    return url


def plain(text, limit):
    """Markdown and line breaks out, at most limit characters."""
    text = re.sub(r"```.*?```", " ", text, flags=re.S)
    text = re.sub(r"[`*_#>|]+", "", text)
    text = re.sub(r"\[([^\]]*)\]\([^)]*\)", r"\1", text)
    text = " ".join(text.split())
    return text if len(text) <= limit else text[:limit - 3].rstrip() + "..."


def message_for(event, data):
    """The request for this event: (method, path, body), or None."""
    sid = re.sub(r"[^A-Za-z0-9]", "", str(data.get("session_id", "")))[:8] or "session"
    mid = "cc-" + sid
    folder = re.split(r"[/\\]", str(data.get("cwd", "")).rstrip("/\\"))[-1]   # either separator
    title = plain(folder, 40) or "Claude Code"
    if event == "waiting":
        text = plain(str(data.get("message") or "Claude needs you"), 160)
        return "POST", "/api/message", {"id": mid, "title": title, "text": text, "icon": "waiting",
                                        "ttl": WAITING_TTL, "show": True}
    if event == "done":
        text = plain(str(data.get("last_assistant_message") or ""), 160) or "Finished, your turn"
        return "POST", "/api/message", {"id": mid, "title": title, "text": text, "icon": "done",
                                        "ttl": DONE_TTL, "show": True}
    if event == "clear":
        return "DELETE", "/api/message?id=" + urllib.parse.quote(mid), None
    return None


def main(argv):
    url = clock_url()
    if not url or len(argv) < 2:
        return
    try:
        data = json.loads(sys.stdin.read() or "{}")
    except ValueError:
        data = {}
    request = message_for(argv[1], data if isinstance(data, dict) else {})
    if not request:
        return
    try:
        with open(DOWN, encoding="utf-8") as f:
            if f.read() == url and time.time() - os.path.getmtime(DOWN) < QUIET:
                return
    except OSError:
        pass
    method, path, body = request
    req = urllib.request.Request(url + path, method=method,
                                 data=json.dumps(body).encode() if body is not None else None,
                                 headers={"Content-Type": "application/json"})
    try:
        urllib.request.urlopen(req, timeout=TIMEOUT).read()
    except urllib.error.HTTPError:
        pass   # it answered
    except Exception:
        with open(DOWN, "w", encoding="utf-8") as f:
            f.write(url)
        return
    if os.path.exists(DOWN):
        os.remove(DOWN)


if __name__ == "__main__":
    try:
        main(sys.argv)
    except Exception:   # a hook must never get in the way
        pass
    sys.exit(0)

"""Spec 019: the plugin's session status hooks, against a stand-in clock.

    python tests/plugin/session_status_test.py

Runs hooks/session_status.py as Claude Code would (the hook's JSON on stdin) and checks
the requests it makes: none while off, the waiting and done messages, the clear, and that
a clock that does not answer costs at most about 2 seconds and never an error.
"""
import http.server
import json
import os
import pathlib
import socket
import subprocess
import sys
import tempfile
import threading
import time

ROOT = pathlib.Path(__file__).resolve().parents[2]
HOOK = ROOT / "plugins" / "ai-agent-monitor" / "hooks" / "session_status.py"
HOOKS_JSON = ROOT / "plugins" / "ai-agent-monitor" / "hooks" / "hooks.json"
seen = []


class Clock(http.server.BaseHTTPRequestHandler):
    def answer(self):
        n = int(self.headers.get("Content-Length") or 0)
        body = json.loads(self.rfile.read(n)) if n else None
        seen.append((self.command, self.path, body))
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.end_headers()
        self.wfile.write(b'{"ok":true}')

    do_POST = do_DELETE = answer

    def log_message(self, *args):
        pass


server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), Clock)
threading.Thread(target=server.serve_forever, daemon=True).start()
URL = f"http://127.0.0.1:{server.server_port}"
TMP = tempfile.mkdtemp()   # where the hook notes a clock that did not answer
failures = 0


def check(ok, what):
    global failures
    print(("PASS " if ok else "FAIL ") + what, flush=True)
    failures += not ok


def run(event, data, url=URL, option=False):
    env = {k: v for k, v in os.environ.items() if k not in ("CLOCK_HOOKS_URL", "CLAUDE_PLUGIN_OPTION_HOOKS_CLOCK_URL")}
    env.update(TMPDIR=TMP, TEMP=TMP, TMP=TMP)
    if url:
        env["CLAUDE_PLUGIN_OPTION_HOOKS_CLOCK_URL" if option else "CLOCK_HOOKS_URL"] = url
    seen.clear()
    start = time.monotonic()
    p = subprocess.run([sys.executable, str(HOOK), event], input=json.dumps(data) if isinstance(data, dict) else data,
                       capture_output=True, text=True, env=env, timeout=20)
    return p, time.monotonic() - start


base = {"session_id": "3f2a9c1e-77aa-4b0e-9d1c-0123456789ab", "cwd": "/home/me/projects/ai-agent-monitor"}

p, _ = run("waiting", dict(base, message="Claude needs your permission to use Bash"), url=None)
check(p.returncode == 0 and not seen and not p.stdout and not p.stderr, "off without an address: no request, no output")

p, _ = run("waiting", dict(base, hook_event_name="Notification", notification_type="permission_prompt",
                           message="Claude needs your permission to use Bash"), option=True)
check(seen == [("POST", "/api/message", {"id": "cc-3f2a9c1e", "title": "ai-agent-monitor",
                                          "text": "Claude needs your permission to use Bash", "icon": "waiting",
                                          "ttl": 14400, "show": True})],
      f"waiting, from the plugin option: {seen}")

reply = "## Done\n\nI fixed **the bug** in `src/main.cpp` ([details](http://x)).\n\n```cpp\nint x;\n```\n" + "More words. " * 30
p, _ = run("done", dict(base, hook_event_name="Stop", last_assistant_message=reply))
body = seen[0][2] if seen else {}
check(seen and seen[0][:2] == ("POST", "/api/message") and body["icon"] == "done" and body["ttl"] == 600
      and body["text"].startswith("Done I fixed the bug in src/main.cpp (details). More words.")
      and len(body["text"]) <= 160 and body["text"].endswith("..."), f"done, the reply in plain text: {body.get('text')!r}")

p, _ = run("done", dict(base, cwd="C:\\dev\\tooling\\"))
check(seen and seen[0][2]["text"] == "Finished, your turn" and seen[0][2]["title"] == "tooling",
      "done without a reply; a Windows path's folder")

p, _ = run("clear", dict(base, hook_event_name="UserPromptSubmit", prompt="yes"))
check(seen == [("DELETE", "/api/message?id=cc-3f2a9c1e", None)], f"clear: {seen}")

p, _ = run("clear", "not json")
check(p.returncode == 0 and seen == [("DELETE", "/api/message?id=cc-session", None)], "unreadable input: still clears, by a fallback id")

p, _ = run("other", base)
check(p.returncode == 0 and not seen, "unknown event: nothing")

p, _ = run("waiting", base, url=f"127.0.0.1:{server.server_port}/")
check(len(seen) == 1, "an address without http:// and with a trailing slash")

silent = socket.socket()   # accepts and never answers
silent.bind(("127.0.0.1", 0))
silent.listen(1)
p, took = run("waiting", base, url=f"http://127.0.0.1:{silent.getsockname()[1]}")
check(p.returncode == 0 and not p.stderr and took < 5, f"a clock that never answers: exit 0 after {took:.1f} s")

p, took = run("done", base, url=f"http://127.0.0.1:{silent.getsockname()[1]}")
check(p.returncode == 0 and took < 1.5, f"the same clock again within a minute: not tried ({took:.1f} s)")

p, _ = run("done", base)
check(len(seen) == 1, "another clock: tried")
p, _ = run("done", base, url=f"http://127.0.0.1:{silent.getsockname()[1]}")
check(p.returncode == 0 and not p.stderr, "the silent clock again: after a success elsewhere, tried")

p, took = run("done", base, url="http://127.0.0.1:9")
check(p.returncode == 0 and not p.stderr, f"nothing at that address: exit 0 after {took:.1f} s")

hooks = json.loads(HOOKS_JSON.read_text(encoding="utf-8"))["hooks"]
check(set(hooks) == {"Notification", "Stop", "UserPromptSubmit", "SessionEnd"}
      and all(bool(h.get("async")) == (event != "Stop") for event, e in hooks.items() for m in e for h in m["hooks"])
      and hooks["Stop"][0]["hooks"][0]["timeout"] <= 5
      and "idle_prompt" not in hooks["Notification"][0]["matcher"],
      "hooks.json: the four events, Stop in the foreground (5 s at most); an idle prompt does not replace done")

print(("FAIL" if failures else "PASS") + f": session status hooks, {failures} failure(s)")
sys.exit(1 if failures else 0)

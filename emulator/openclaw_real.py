"""Pairs the emulated clock with the real OpenClaw Gateway (the compose service "openclaw").

    docker compose -f emulator/docker-compose.yml --profile openclaw up -d
    python emulator/openclaw_real.py                 # configure, pair, approve, then a message
    python emulator/openclaw_real.py invoke display.show '{"text": "Hello"}'
    python emulator/openclaw_real.py skill           # install the OpenClaw skill (spec 044)

The steps are the ones for a real clock and a real gateway (docs/specifications/
integrations-api.md, "OpenClaw node"), in the order that works:
  1. on the gateway: gateway.bind lan, and the clock's commands in
     gateway.nodes.commands.allow (BEFORE approving the node: OpenClaw approves the
     commands the node declared that the allow list let through, so an empty list
     approves an empty surface and needs a second approval later);
  2. on the clock: the gateway's address and token:<gateway token> (or a setup code);
  3. on the gateway: openclaw devices approve, then openclaw nodes approve.
No model provider is needed: commands go through `openclaw nodes invoke`.

`skill` installs integrations/openclaw/skill/ai-agent-monitor/ on the gateway as a real
gateway's owner would (`openclaw skills install <folder> --global`), gives it the clock's
address and API key (skills.entries.ai-agent-monitor.env), checks that OpenClaw loads it,
then saves, fills, shows and deletes a face with the skill's own clock.py run on the
gateway's machine, as an agent's `exec` would.
"""
import os
import json
import subprocess
import sys
import time
import urllib.parse
import urllib.request
import http.cookiejar

API, SCREEN = "http://127.0.0.1:8000", "http://127.0.0.1:8080"
TOKEN = "emulator-token"   # OPENCLAW_GATEWAY_TOKEN in emulator/docker-compose.yml
COMMANDS = ["display.show", "display.clear", "display.status", "display.brightness", "face.set", "device.info",
            "device.status", "clock.face.show", "clock.faces.list", "clock.vars.set",
            "clock.help"]
jar = http.cookiejar.CookieJar()
web = urllib.request.build_opener(urllib.request.HTTPCookieProcessor(jar))


def openclaw(*args):
    """The gateway's own CLI, inside its container; returns the JSON it printed (or None)."""
    p = subprocess.run(["docker", "compose", "-f", "emulator/docker-compose.yml", "--profile", "openclaw", "exec", "-T",
                        "openclaw", "node", "openclaw.mjs", *args], capture_output=True, text=True, encoding="utf-8", errors="replace")
    out = p.stdout
    start = min([i for i in (out.find("{"), out.find("[")) if i >= 0], default=-1)
    try:
        return json.loads(out[start:]) if start >= 0 else None
    except ValueError:
        return {"text": out[-500:] + p.stderr[-300:]}


def clock_set(key, value):
    body = urllib.parse.urlencode({"key": key, "value": value}).encode()
    reply = web.open(API + "/api/set", body).read().decode()
    if reply != "OK":
        raise SystemExit(f"the clock refused {key}: {reply}")


def login():
    password = urllib.request.urlopen(SCREEN + "/password").read().decode().strip()
    web.open(API + "/api/auth/login", urllib.parse.urlencode({"password": password}).encode())


def clock_state():
    return json.loads(urllib.request.urlopen(SCREEN + "/openclaw").read())


def wait_clock(*states, timeout=60):
    end = time.time() + timeout
    while time.time() < end:
        s = clock_state()
        if s["openclaw"].startswith(states):
            return s
        time.sleep(2)
    raise SystemExit(f"the clock says {clock_state()['openclaw']!r}")


def invoke(command, params):
    node = clock_state()["openclawId"]   # by id: the name stays on older identities too
    return openclaw("nodes", "invoke", "--node", node, "--command", command, "--params", json.dumps(params), "--json")


def gateway_sh(script, env=None):
    """A shell command in the gateway's container (its own python3, as an agent's exec)."""
    extra = [a for k, v in (env or {}).items() for a in ("-e", f"{k}={v}")]
    p = subprocess.run(["docker", "compose", "-f", "emulator/docker-compose.yml", "--profile", "openclaw", "exec", "-T",
                        *extra, "openclaw", "sh", "-c", script], capture_output=True, text=True, encoding="utf-8", errors="replace")
    return (p.stdout + p.stderr).strip()


def skill():
    here = os.path.dirname(os.path.abspath(__file__))
    folder = os.path.join(here, "..", "integrations", "openclaw", "skill", "ai-agent-monitor")
    login()
    clock_set("facesapi", "1")   # Integrations > Custom faces, and its API key
    key = json.loads(web.open(API + "/api/integrations").read())["apiKey"]
    if not key:
        key = json.loads(web.open(API + "/api/integrations", b"action=newkey").read())["apiKey"]
    print("gateway: openclaw skills install <folder> --global")
    gateway_sh("rm -rf /tmp/ai-agent-monitor")
    subprocess.run(["docker", "compose", "-f", "emulator/docker-compose.yml", "--profile", "openclaw", "cp", folder,
                    "openclaw:/tmp/ai-agent-monitor"], check=True, capture_output=True)
    print(" ", gateway_sh("node openclaw.mjs skills install /tmp/ai-agent-monitor --global --force 2>&1 | tail -2"))
    # The clock as the gateway reaches it: by its IP (the clock answers only its IP, its own
    # host name or names listed in its settings, so the compose name "smartclock" is refused).
    ip = gateway_sh("getent hosts smartclock").split()[0]
    env = {"CLOCK_URL": f"http://{ip}", "CLOCK_API_KEY": key}
    openclaw("config", "set", "skills.entries.ai-agent-monitor.env", json.dumps(env))
    info = openclaw("skills", "info", "ai-agent-monitor", "--json") or {}
    print("skill:", json.dumps({k: info.get(k) for k in ("name", "description", "source", "eligible", "disabled",
                                                         "blockedByAllowlist", "missing", "baseDir", "filePath")}, indent=1))
    base = info.get("baseDir") or os.path.dirname(info.get("filePath") or "/home/node/.openclaw/skills/ai-agent-monitor/x")
    run = f"cd {base} && python3 scripts/clock.py"
    print("exec: clock.py push / vars / show / delete, from the gateway's machine")
    for step in (f"{run} check examples/progress.json",
                 f"{run} push skill-test examples/progress.json --show",
                 f"{run} vars title=OpenClaw pct=60 'step=Pushed by the skill'",
                 f"{run} faces"):
        print(" ", step.split("clock.py ")[1], "->", gateway_sh(step, env)[-300:])
    time.sleep(2)
    if "--shot" in sys.argv:   # the screen, from this computer: python ... skill --shot out.png
        subprocess.run([sys.executable, os.path.join(folder, "scripts", "clock.py"), "screenshot",
                        sys.argv[sys.argv.index("--shot") + 1]], check=True)
    print("  delete ->", gateway_sh(f"{run} delete skill-test", env)[-200:])


def main():
    if sys.argv[1:2] == ["skill"]:
        skill()
        return
    if sys.argv[1:2] == ["invoke"]:
        print(json.dumps(invoke(sys.argv[2], json.loads(sys.argv[3] if len(sys.argv) > 3 else "{}")), indent=1))
        return
    print("gateway: allow the clock's commands, bind lan")
    for _ in range(20):   # the allow list first: changing gateway.bind restarts the gateway
        openclaw("config", "set", "gateway.nodes.commands.allow", json.dumps(COMMANDS))
        if openclaw("config", "get", "gateway.nodes.commands.allow") == COMMANDS:
            break
        time.sleep(3)
    else:
        raise SystemExit("could not set gateway.nodes.commands.allow")
    if "lan" not in json.dumps(openclaw("config", "get", "gateway.bind", "--json")):
        openclaw("config", "set", "gateway.bind", "lan")
        time.sleep(5)
    for _ in range(40):   # back up after a restart
        if openclaw("health", "--json") is not None:
            break
        time.sleep(3)
    login()
    print("clock: ws://openclaw:18789 with the gateway's token")
    clock_set("ocurl", "ws://openclaw:18789")
    clock_set("ocpair", "token:" + TOKEN)
    s = wait_clock("pairing pending", "connected", timeout=90)
    if s["openclaw"].startswith("pairing"):
        dev = s["openclawId"]
        pending = (openclaw("devices", "list", "--json") or {}).get("pending", [])
        for req in pending:
            if req.get("deviceId") in (dev, None) or len(pending) == 1:
                print("gateway: devices approve", req["requestId"])
                openclaw("devices", "approve", req["requestId"])
        wait_clock("connected", timeout=90)
    for _ in range(10):
        nodes = (openclaw("nodes", "status", "--json") or {}).get("nodes", [])
        me = next((n for n in nodes if n.get("nodeId") == clock_state()["openclawId"]), {})
        if me.get("pendingRequestId"):
            print("gateway: nodes approve", me["pendingRequestId"], "for", len(me.get("pendingDeclaredCommands") or []), "commands")
            openclaw("nodes", "approve", me["pendingRequestId"])
        elif me.get("commands"):
            break
        time.sleep(2)
    print("node:", me.get("approvalState"), len(me.get("commands") or []), "commands")
    print(json.dumps(invoke("display.show", {"heading": "OpenClaw", "text": "Connected to the real gateway", "icon": "success"}), indent=1))


if __name__ == "__main__":
    main()

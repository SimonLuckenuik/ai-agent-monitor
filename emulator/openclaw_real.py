"""Pairs the emulated clock with the real OpenClaw Gateway (the compose service "openclaw").

    docker compose -f emulator/docker-compose.yml --profile openclaw up -d
    python emulator/openclaw_real.py                 # configure, pair, approve, then a message
    python emulator/openclaw_real.py invoke display.show '{"text": "Hello"}'

The steps are the ones for a real clock and a real gateway (docs/specifications/
integrations-api.md, "OpenClaw node"), in the order that works:
  1. on the gateway: gateway.bind lan, and the clock's commands in
     gateway.nodes.commands.allow (BEFORE approving the node: OpenClaw approves the
     commands the node declared that the allow list let through, so an empty list
     approves an empty surface and needs a second approval later);
  2. on the clock: the gateway's address and token:<gateway token> (or a setup code);
  3. on the gateway: openclaw devices approve, then openclaw nodes approve.
No model provider is needed: commands go through `openclaw nodes invoke`.
"""
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
            "device.status", "clock.face.show", "clock.faces.list", "clock.vars.set"]
jar = http.cookiejar.CookieJar()
web = urllib.request.build_opener(urllib.request.HTTPCookieProcessor(jar))


def openclaw(*args):
    """The gateway's own CLI, inside its container; returns the JSON it printed (or None)."""
    p = subprocess.run(["docker", "compose", "-f", "emulator/docker-compose.yml", "--profile", "openclaw", "exec", "-T",
                        "openclaw", "node", "openclaw.mjs", *args], capture_output=True, text=True)
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


def main():
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
    password = urllib.request.urlopen(SCREEN + "/password").read().decode().strip()
    web.open(API + "/api/auth/login", urllib.parse.urlencode({"password": password}).encode())
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

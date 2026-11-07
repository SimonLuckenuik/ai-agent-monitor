"""Spec 047 A/B: push each arm's faces to a json-render emulator and capture them.

    python specs/047-json-render/ab/render.py [API] [SCREEN]

Defaults to the private emulator used for spec 047 (127.0.0.1:28000 and :28080), which
runs a build with SMARTCLOCK_JSONRENDER=1. Writes <arm>-<brief>[-<state>].png beside
this file and sheet.png (A on the left, B on the right).
"""
import http.cookiejar
import json
import os
import sys
import time
import urllib.parse
import urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", "..", ".."))
sys.path.insert(0, os.path.join(ROOT, "tests", "integration"))
from emulator import open_meteo  # noqa: E402

API = sys.argv[1] if len(sys.argv) > 1 else "http://127.0.0.1:28000"
SCREEN = sys.argv[2] if len(sys.argv) > 2 else "http://127.0.0.1:28080"
jar = http.cookiejar.CookieJar()
web = urllib.request.build_opener(urllib.request.HTTPCookieProcessor(jar))


def req(method, url, body=None, ctype="application/json"):
    r = urllib.request.Request(url, data=body, method=method, headers={"Content-Type": ctype})
    try:
        with web.open(r, timeout=20) as f:
            return f.status, f.read()
    except urllib.error.HTTPError as e:
        return e.code, e.read()


def setup():
    pw = urllib.request.urlopen(SCREEN + "/password", timeout=5).read().decode()
    req("POST", API + "/api/auth/login", urllib.parse.urlencode({"password": pw}).encode(), "application/x-www-form-urlencoded")
    req("GET", API + "/api/set?key=facesapi&value=1")
    req("POST", SCREEN + "/time?local=2026-10-05T10:42&speed=0", b"")
    req("POST", SCREEN + "/weather", json.dumps(open_meteo()).encode())


def vars_(**v):
    req("POST", API + "/api/faces/vars", json.dumps(v).encode())


def shot(name):
    time.sleep(3)
    os.system(f'python "{os.path.join(ROOT, "plugins/ai-agent-monitor/skills/ai-agent-monitor/scripts/clock.py")}" screenshot "{name}"')


STATES = {
    "ci": [("fail", dict(repo="ai-agent-monitor", branch="faces-v2", build="fail", pct=0, passed=402, failed=3)),
           ("running", dict(build="running", pct=64, failed=0))],
    "weather": [("", {})],
    "meeting": [("12", dict(meeting="Design review", room="Lab 2", mins=12)), ("4", dict(mins=4))],
}


def main():
    os.environ.setdefault("CLOCK_URL", API)
    os.environ.setdefault("CLOCK_SCREEN_URL", SCREEN)
    setup()
    rows = []
    for brief, states in STATES.items():
        for state, values in states:
            row = []
            for arm in "AB":
                path = os.path.join(HERE, f"{arm}-{brief}.json")
                out = os.path.join(HERE, f"{arm}-{brief}{'-' + state if state else ''}.png")
                if not os.path.exists(path):
                    row.append(None); continue
                if values: vars_(**values)
                code, reply = req("POST", API + f"/api/faces?name={arm.lower()}-{brief}&show=1", open(path, "rb").read())
                print(f"{arm}-{brief} {state}: {code} {reply.decode(errors='replace').strip()}")
                shot(out)
                row.append(out)
            rows.append(row)
    try:
        from PIL import Image, ImageDraw
    except ImportError:
        return
    sheet = Image.new("RGB", (2 * 240 * 2 + 30, len(rows) * (480 + 10) + 30), "#202020")
    d = ImageDraw.Draw(sheet)
    d.text((10 + 240, 8), "A: face format (skill)", fill="white")
    d.text((20 + 480 + 240, 8), "B: json-render (catalog)", fill="white")
    for r, row in enumerate(rows):
        for c, p in enumerate(row):
            if p and os.path.exists(p):
                sheet.paste(Image.open(p).convert("RGB").resize((480, 480), Image.NEAREST), (10 + c * 490, 30 + r * 490))
    sheet.save(os.path.join(HERE, "sheet.png"))
    print("sheet.png")


if __name__ == "__main__":
    main()

#!/usr/bin/env python3
"""Talk to an AI Agent Monitor clock (SmartClock-OSS firmware) or its emulator.

    python clock.py status
    python clock.py faces
    python clock.py push NAME FACE.json [--show]
    python clock.py show NAME
    python clock.py delete NAME
    python clock.py vars key=value [key=value ...]
    python clock.py screenshot OUT.png        (emulator only)

Configuration, from the environment:
    CLOCK_URL         the clock, e.g. http://192.168.1.50  (default http://127.0.0.1:8000,
                      the emulator)
    CLOCK_API_KEY     the API key from the settings page (Integrations > Custom faces)
    CLOCK_SCREEN_URL  the emulator's screen server (default: CLOCK_URL's host, port 8080)
    CLOCK_PASSWORD    the device password: signs in instead of using the API key. Against
                      the emulator it is read from the screen server when unset.

Standard library only.
"""
import http.cookiejar
import json
import os
import struct
import sys
import urllib.error
import urllib.parse
import urllib.request
import zlib

CLOCK = os.environ.get("CLOCK_URL", "http://127.0.0.1:8000").rstrip("/")
KEY = os.environ.get("CLOCK_API_KEY", "")
_u = urllib.parse.urlparse(CLOCK)
SCREEN = os.environ.get("CLOCK_SCREEN_URL", f"{_u.scheme}://{_u.hostname}:8080").rstrip("/")

jar = http.cookiejar.CookieJar()
opener = urllib.request.build_opener(urllib.request.HTTPCookieProcessor(jar))
signed_in = False


def call(method, path, body=None, base=CLOCK, content_type="application/json"):
    headers = {"X-Api-Key": KEY} if KEY and base == CLOCK else {}
    data = None
    if body is not None:
        data = body if isinstance(body, bytes) else body.encode("utf-8")
        headers["Content-Type"] = content_type
    req = urllib.request.Request(base + path, data=data, method=method, headers=headers)
    try:
        with opener.open(req, timeout=10) as r:
            return r.read()
    except urllib.error.HTTPError as e:
        raise SystemExit(f"{method} {path}: HTTP {e.code} {e.read().decode(errors='replace').strip()}")
    except urllib.error.URLError as e:
        raise SystemExit(f"{method} {base}{path}: {e.reason}")


def sign_in():
    """Without an API key, sign in with the device password (the emulator's is public)."""
    global signed_in
    if KEY or signed_in:
        return
    password = os.environ.get("CLOCK_PASSWORD")
    if not password:
        try:
            password = call("GET", "/password", base=SCREEN).decode().strip()
        except SystemExit:
            raise SystemExit("Set CLOCK_API_KEY (settings page > Integrations > Custom faces) or CLOCK_PASSWORD")
    call("POST", "/api/auth/login", urllib.parse.urlencode({"password": password}),
         content_type="application/x-www-form-urlencoded")
    signed_in = True


def face_name(name):
    if not name or len(name) > 20 or any(not (c.isalnum() or c in "-_") for c in name):
        raise SystemExit("Face names are 1-20 characters of a-z 0-9 _ -")
    return name.lower()


def screenshot(out):
    """The emulator's framebuffer (240x240 RGB565) as a PNG."""
    raw = call("GET", "/fb", base=SCREEN)
    if len(raw) != 240 * 240 * 2:
        raise SystemExit(f"Unexpected framebuffer size {len(raw)}")
    rows = bytearray()
    for y in range(240):
        rows.append(0)   # PNG filter: none
        for x in range(240):
            v = raw[2 * (y * 240 + x)] | raw[2 * (y * 240 + x) + 1] << 8
            rows += bytes(((v >> 11) * 255 // 31, ((v >> 5) & 63) * 255 // 63, (v & 31) * 255 // 31))

    def chunk(kind, data):
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data))

    png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", 240, 240, 8, 2, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(bytes(rows), 9)) + chunk(b"IEND", b"")
    with open(out, "wb") as f:
        f.write(png)
    print(out)


def main(argv):
    if not argv or argv[0] in ("-h", "--help"):
        print(__doc__)
        return
    cmd, args = argv[0], argv[1:]
    if cmd == "status":
        print(json.dumps(json.loads(call("GET", "/status")), indent=2))
    elif cmd == "screenshot" and len(args) == 1:
        screenshot(args[0])
    elif cmd == "faces":
        sign_in()
        print(json.dumps(json.loads(call("GET", "/api/faces")), indent=2))
    elif cmd == "push" and len(args) >= 2:
        sign_in()
        with open(args[1], encoding="utf-8") as f:
            face = json.load(f)   # validates the JSON before it goes to the clock
        body = json.dumps(face, separators=(",", ":"))
        if len(body) > 4096:
            raise SystemExit(f"Face is {len(body)} bytes once minified; the clock takes at most 4096")
        show = "&show=1" if "--show" in args[2:] else ""
        print(call("POST", f"/api/faces?name={face_name(args[0])}{show}", body).decode())
    elif cmd in ("show", "delete") and len(args) == 1:
        sign_in()
        print(call("POST", f"/api/faces/{cmd}?name={face_name(args[0])}", "").decode())
    elif cmd == "vars" and args:
        sign_in()
        values = dict(a.split("=", 1) for a in args if "=" in a)
        print(call("POST", "/api/faces/vars", json.dumps(values)).decode())
    else:
        raise SystemExit(__doc__)


if __name__ == "__main__":
    main(sys.argv[1:])

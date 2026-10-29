"""Drive the emulator from Python: start it, fix its time and weather, sign in, change
settings, push faces and messages, and capture the screen.

Used by run_integration_tests.py and scripts/screenshots.py. Needs Docker (to start an
emulator) and Pillow (for the screen captures).
"""
import calendar
import hashlib
import http.cookiejar
import json
import os
import subprocess
import time
import urllib.error
import urllib.parse
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
COMPOSE = Path(__file__).with_name("docker-compose.yml")
PROJECT = "aam-itest"

# The fixed moment every test screen shows: Monday 5 October 2026, 10:09 in Montreal
# (EDT, UTC-4). The zone is set on the clock, so the time below is UTC.
TZ = "EST5EDT,M3.2.0,M11.1.0"
FIXED_TIME = calendar.timegm((2026, 10, 5, 14, 9, 0))


def epoch(y, mo, d, h, mi, utc_offset=-4):
    """Unix time of a local time in the clock's zone (EDT by default)."""
    return calendar.timegm((y, mo, d, h - utc_offset, mi, 0))


def open_meteo(temp=18.4, feels=16.9, humidity=72, wind=5.2, code=2, is_day=1, city="Montreal",
               days=None, pressure=1016.4, clouds=40):
    """An Open-Meteo forecast reply, as the clock asks for it (wind in m/s).

    days: four (code, high, low, feels_high, precip %, wind m/s, humidity %, sunrise,
    sunset) tuples, today first.
    """
    days = days or [
        (2, 21, 11, 20, 10, 6.1, 64, "07:02", "18:31"),
        (61, 15, 9, 13, 80, 9.4, 88, "07:03", "18:29"),
        (0, 19, 6, 18, 0, 3.3, 52, "07:05", "18:27"),
        (71, 4, -3, -1, 60, 7.8, 90, "07:06", "18:25"),
    ]
    dates = ["2026-10-0%d" % (5 + i) for i in range(4)]
    col = lambda i: [d[i] for d in days]
    return {
        "city": city,
        "current": {"temperature_2m": temp, "apparent_temperature": feels,
                    "relative_humidity_2m": humidity, "wind_speed_10m": wind,
                    "weather_code": code, "is_day": is_day, "pressure_msl": pressure,
                    "cloud_cover": clouds},
        "daily": {"time": dates, "weather_code": col(0), "temperature_2m_max": col(1),
                  "temperature_2m_min": col(2), "apparent_temperature_max": col(3),
                  "precipitation_probability_max": col(4), "wind_speed_10m_max": col(5),
                  "relative_humidity_2m_mean": col(6),
                  "sunrise": [f"{t}T{d[7]}" for t, d in zip(dates, days)],
                  "sunset": [f"{t}T{d[8]}" for t, d in zip(dates, days)]},
    }


class HttpError(Exception):
    def __init__(self, code, text):
        super().__init__(f"HTTP {code}: {text}")
        self.code, self.text = code, text


class Emulator:
    """One emulated clock: its API (firmware port 80) and its screen server (8080)."""

    def __init__(self, api="http://127.0.0.1:18000", screen="http://127.0.0.1:18080"):
        self.api, self.screen = api.rstrip("/"), screen.rstrip("/")
        self.jar = http.cookiejar.CookieJar()
        self.opener = urllib.request.build_opener(urllib.request.HTTPCookieProcessor(self.jar))

    # ---- The container
    @staticmethod
    def compose(*args, check=True):
        cmd = ["docker", "compose", "-p", PROJECT, "-f", str(COMPOSE), *args]
        return subprocess.run(cmd, cwd=ROOT, check=check)

    def start(self, build=True, fresh=True):
        """A dedicated emulator on ports 18000/18080, with blank storage when fresh."""
        if fresh:
            self.compose("down", "-v", "--remove-orphans", check=False)
        self.compose("up", "-d", *(["--build"] if build else []))
        self.wait_ready()

    def stop(self):
        self.compose("down", "-v", "--remove-orphans", check=False)

    def wait_ready(self, timeout=120):
        """Until the API answers and the settings are loaded: storage mounts, and the
        settings load, a few seconds into the boot (the API refuses them until then)."""
        end = time.time() + timeout
        while time.time() < end:
            try:
                s = self.status()
                if s["storageReady"] and not s["recovery"] and s["uptime"] >= 8:
                    return
            except (OSError, HttpError, ValueError, KeyError):
                pass
            time.sleep(1)
        raise TimeoutError(f"No emulator at {self.api} after {timeout} s")

    # ---- HTTP
    def request(self, method, url, body=None, content_type="application/json", timeout=20):
        data = None
        headers = {}
        if body is not None:
            data = body if isinstance(body, bytes) else (
                body.encode() if isinstance(body, str) else json.dumps(body).encode())
            headers["Content-Type"] = content_type
        req = urllib.request.Request(url, data=data, method=method, headers=headers)
        try:
            with self.opener.open(req, timeout=timeout) as r:
                return r.status, r.read(), r.headers
        except urllib.error.HTTPError as e:
            raise HttpError(e.code, e.read().decode(errors="replace").strip()) from None

    def get(self, path, **kw):
        return self.request("GET", self.api + path, **kw)[1]

    def post(self, path, body=b"", **kw):
        return self.request("POST", self.api + path, body, **kw)[1]

    def form(self, path, fields):
        return self.post(path, urllib.parse.urlencode(fields), content_type="application/x-www-form-urlencoded")

    def status(self):
        return json.loads(self.get("/status", timeout=5))

    # ---- Sign-in and settings
    def password(self):
        return self.request("GET", self.screen + "/password")[1].decode().strip()

    def sign_in(self):
        self.form("/api/auth/login", {"password": self.password()})

    def set(self, key, value):
        reply = self.form("/api/set", {"key": key, "value": str(value)}).decode()
        if reply != "OK":
            raise HttpError(200, f"set {key}={value}: {reply}")

    def settings(self, **values):
        for k, v in values.items():
            self.set(k, v)

    def config(self):
        return json.loads(self.get("/config"))

    # ---- Emulator-only: time, weather, restarts
    def fix_time(self, when):
        """Stops the clock at a Unix time (0: the real time again)."""
        self.request("POST", self.screen + f"/time?epoch={int(when)}", b"")

    def fix_weather(self, reply):
        """Replaces the Open-Meteo fetch with this reply (None: the network again)."""
        if reply is None:
            self.request("DELETE", self.screen + "/weather")
        else:
            self.request("POST", self.screen + "/weather", reply)

    def restart(self):
        """The firmware's own restart (a software reset): storage and EEPROM stay. Not
        /reset on the screen server: two external resets within 10 s of boot are the
        recovery gesture."""
        before = self.status()["uptime"]
        try:
            self.post("/restart", timeout=5)
        except (OSError, HttpError):
            pass
        end = time.time() + 60   # a new boot (uptime went down) that has settled
        while time.time() < end:
            try:
                up = self.status()["uptime"]
                if up < before and up >= 8:   # settings load a few seconds into the boot
                    break
            except (OSError, HttpError, ValueError):
                pass
            time.sleep(0.5)
        else:
            raise TimeoutError("the emulator did not restart")
        self.jar.clear()

    # ---- Messages and faces
    def message(self, **fields):
        return self.post("/api/message", fields).decode()

    def messages(self):
        return json.loads(self.get("/api/message"))["messages"]

    def clear_messages(self):
        self.request("DELETE", self.api + "/api/message")

    def push_face(self, name, face, show=False):
        body = face if isinstance(face, (str, bytes)) else json.dumps(face, separators=(",", ":"))
        name = urllib.parse.quote(name)
        return self.post(f"/api/faces?name={name}" + ("&show=1" if show else ""), body).decode()

    def show_face(self, name):
        return self.post(f"/api/faces/show?name={name}").decode()

    def delete_face(self, name):
        return self.post(f"/api/faces/delete?name={name}").decode()

    def faces(self):
        return json.loads(self.get("/api/faces"))

    def vars(self, **values):
        return self.post("/api/faces/vars", values).decode()

    def clear_vars(self):
        names = list(self.faces()["vars"])
        if names:
            self.post("/api/faces/vars", {n: None for n in names})

    def apply_sample(self, sample, now=FIXED_TIME):
        """Sets an example face's sample values from examples/faces/samples.json: "vars"
        ("@+N" is the Unix time N seconds after `now`; "@age N value" pushes value, or 1,
        N seconds before it), "history" (each value pushed with $history, oldest first) and
        "messages"; "shift" moves `now` by that many seconds and "weather" holds open_meteo()
        arguments (the default weather otherwise). The clock is left stopped at `now`."""
        now += sample.get("shift", 0)
        self.fix_weather(open_meteo(**sample.get("weather", {})))
        aged = {}
        for k, v in sample.get("vars", {}).items():
            if isinstance(v, str) and v.startswith("@+"):
                self.vars(**{k: now + int(v[2:])})
            elif isinstance(v, str) and v.startswith("@age "):
                age, _, value = v[5:].partition(" ")
                aged[k] = (int(age), value or 1)
            else:
                self.vars(**{k: v})
        for k, (age, value) in aged.items():
            self.fix_time(now - age)
            self.vars(**{k: value})
        self.fix_time(now)
        for k, values in sample.get("history", {}).items():
            for v in values:
                self.post("/api/faces/vars", {k: v, "$history": True})
        for m in sample.get("messages", []):
            self.message(**m)

    # ---- The screen
    def framebuffer(self):
        status, raw, headers = self.request("GET", self.screen + "/fb")
        if len(raw) != 240 * 240 * 2:
            raise ValueError(f"framebuffer is {len(raw)} bytes")
        return raw, int(headers.get("X-Backlight", 100))

    def settle(self, quiet=0.8, timeout=10):
        """Waits until the screen has not changed for `quiet` seconds; returns it."""
        end = time.time() + timeout
        last, since = None, time.time()
        while time.time() < end:
            raw, _ = self.framebuffer()
            if raw != last:
                last, since = raw, time.time()
            elif time.time() - since >= quiet:
                return raw
            time.sleep(0.15)
        return last

    def screenshot(self, path=None, quiet=0.8):
        """The settled screen as a Pillow image (saved as PNG when path is given)."""
        from PIL import Image
        raw = self.settle(quiet)
        px = bytearray()
        for i in range(0, len(raw), 2):
            v = raw[i] | raw[i + 1] << 8
            px += bytes(((v >> 11) * 255 // 31, ((v >> 5) & 63) * 255 // 63, (v & 31) * 255 // 31))
        img = Image.frombytes("RGB", (240, 240), bytes(px))
        if path:
            Path(path).parent.mkdir(parents=True, exist_ok=True)
            img.save(path)
        return img

    def show(self, face_id, settle=True):
        """Shows a built-in face by id, the way the settings page does."""
        self.set("theme", face_id)
        if settle:
            time.sleep(0.4)
            self.settle()


def digest(img):
    return hashlib.sha256(img.tobytes()).hexdigest()[:16]


def sheet(images, columns, path, scale=1, gap=8, labels=None, bg=(32, 32, 36)):
    """A contact sheet of 240x240 screens, optionally with a caption under each."""
    from PIL import Image, ImageDraw, ImageFont
    rows = (len(images) + columns - 1) // columns
    cap = 22 if labels else 0
    font = ImageFont.load_default(size=14)
    w, h = 240 * scale, 240 * scale
    out = Image.new("RGB", (columns * (w + gap) + gap, rows * (h + gap + cap) + gap), bg)
    draw = ImageDraw.Draw(out)
    for i, img in enumerate(images):
        x, y = gap + (i % columns) * (w + gap), gap + (i // columns) * (h + gap + cap)
        out.paste(img.resize((w, h), Image.NEAREST) if scale != 1 else img, (x, y))
        if labels:
            draw.text((x + w // 2, y + h + 4), labels[i], fill=(200, 200, 205), anchor="mt", font=font)
    Path(path).parent.mkdir(parents=True, exist_ok=True)
    out.save(path, optimize=True)
    return out

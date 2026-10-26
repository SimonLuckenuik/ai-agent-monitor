"""Calibrates the emulator's drawing speed (panel.cpp) against the real clock.

    python emulator/calibrate.py

Shows each face in the running emulator and records what its full draw cost there
(GET :8080/timing): the firmware's time on the PC, the path loops' counted work
(EMU_WORK in faces.cpp, estimated ESP8266 cycles) and the SPI time. It then fits
panel.cpp's two constants so that

    clock ms = SPI time + PC time * CPU_FACTOR + counted cycles * WORK_SCALE / F_CPU

matches CLOCK_MS: the slowest draw of each face on the real clock at 160 MHz, from its
/status drawMs (custom faces are id 10). Update CLOCK_MS after measuring on the clock,
run this, and copy the printed constants into panel.cpp. Run it twice: the PC time
varies from run to run. It changes the emulator's face and rotation settings
(themeInterval 0); set them back afterwards. The "nightlake" custom face must exist
in the emulator (the one on the clock: push it with the plugin's clock.py).
"""
import http.cookiejar
import json
import time
import urllib.parse
import urllib.request

FIRMWARE = "http://127.0.0.1:8000"
SCREEN = "http://127.0.0.1:8080"
# The real clock, 160 MHz, firmware 865c672 (2026-10-25): face -> slowest draw, ms.
# Classic (0, 54 ms) and Simple (4, 47 ms) were measured with font 8; their time is
# now the line font, so measure them again before adding them back.
CLOCK_MS = {1: 68, 3: 808, 5: 98, 7: 722, 8: 498, 9: 54, "nightlake": 315}
NAMES = {0: "Classic", 1: "Weather", 3: "Big date", 4: "Simple", 5: "Forecast",
         7: "Big time", 8: "Big weather", 9: "Agents", "nightlake": "nightlake"}

jar = urllib.request.build_opener(urllib.request.HTTPCookieProcessor(http.cookiejar.CookieJar()))


def get(url, data=None):
    return jar.open(url, data=data, timeout=30).read().decode()


def setting(key, value):
    get(f"{FIRMWARE}/api/set?" + urllib.parse.urlencode({"key": key, "value": value}))


def timing(reset=False):
    return json.loads(get(f"{SCREEN}/timing" + ("?reset=1" if reset else "")))


def show(face):
    if isinstance(face, str):
        get(f"{FIRMWARE}/api/faces/show?" + urllib.parse.urlencode({"name": face}), data=b"")
    else:
        setting("theme", face)


def measure(face):
    """The face's full draw: the biggest burst of TFT_eSPI calls after switching to it."""
    show(4 if face == 0 else 0)   # start from another face
    time.sleep(3)
    timing(reset=True)
    show(face)
    for _ in range(100):
        time.sleep(0.3)
        draws = timing()["draws"]
        if draws and max(d["calls"] for d in draws) > 200:
            time.sleep(0.5)
            return max(timing()["draws"], key=lambda d: d["calls"])
    raise SystemExit(f"{NAMES[face]}: no draw seen")


password = urllib.request.urlopen(f"{SCREEN}/password").read().decode().strip()
jar.open(f"{FIRMWARE}/api/auth/login", urllib.parse.urlencode({"password": password}).encode())
setting("themeInterval", "0")
info = timing()
mhz = info["fcpuMHz"]
rows = []
for face in CLOCK_MS:
    d = measure(face)
    rows.append((face, d))
    print(f"{NAMES[face]:<12} PC {d['pcCpuUs']/1000:5.2f} ms  work {d['workCycles']/1e6:6.1f} M cycles  "
          f"SPI {d['spiUs']/1000:5.1f} ms  emulator {d['clockMs']:6.0f} ms  clock {CLOCK_MS[face]:4d} ms")

# Least squares, in us: clock - SPI = cpu * pc + work * cycles / MHz.
a11 = a12 = a22 = b1 = b2 = 0.0
for face, t in rows:
    y = CLOCK_MS[face] * 1000 - t["spiUs"]
    x1, x2 = t["pcCpuUs"], t["workCycles"] / mhz
    a11 += x1 * x1; a12 += x1 * x2; a22 += x2 * x2; b1 += x1 * y; b2 += x2 * y
det = a11 * a22 - a12 * a12
cpu = (b1 * a22 - b2 * a12) / det
work = (a11 * b2 - a12 * b1) / det
print(f"\nCPU_FACTOR_160 = {cpu * mhz / 160:.1f}, WORK_SCALE = {work:.2f}  "
      f"(now {info['cpuFactor'] * mhz / 160:.1f} and {info['workUs'] * mhz:.2f})")
print("Predicted vs clock:")
for face, t in rows:
    p = (t["spiUs"] + cpu * t["pcCpuUs"] + work * t["workCycles"] / mhz) / 1000
    print(f"  {NAMES[face]:<12} {p:7.0f} ms vs {CLOCK_MS[face]:4d} ms ({(p / CLOCK_MS[face] - 1) * 100:+.0f}%)")

"""Build the public release images: no WiFi details, stock-updater-friendly names.

    python scripts/make_release.py

Builds with SMARTCLOCK_RELEASE=1, which makes build_options.py ignore secrets.ini,
then refuses to finish if an image contains the SSID or password from a local
secrets.ini. Writes to release/. The stock update page only accepts names starting
with "SDP", so only the image it can install has that prefix:

- SDPro_AIAgentMonitor_v<version>_nomqtt.bin: everything but MQTT, as a plain image
  small enough for any updater (stock, or this firmware before 1.4.0). The first step
  on a new clock; then update with the .bin.gz.
- AIAgentMonitor_v<version>.bin.gz: the full firmware, compressed. For clocks already
  running 1.4.0 or later (the no-MQTT image included). No "SDP": a stock clock cannot
  unpack it, so its update page refuses the name.
- AIAgentMonitor_v<version>_usb.bin: the full firmware, plain, for flashing over USB
  only. It is too big for the clock's own updater, and its name keeps the stock page
  from taking it.
- SHA256SUMS.txt
"""
from pathlib import Path
import configparser
import hashlib
import os
import re
import shutil
import subprocess
import sys

root = Path(__file__).resolve().parents[1]
version = re.search(r'#define FW_VERSION "([^"]+)"', (root / "src/main.cpp").read_text(encoding="utf-8")).group(1)
if version.endswith("-dev"):
    sys.exit(f"FW_VERSION is {version}; set a release version before building a release")

env = dict(os.environ, SMARTCLOCK_RELEASE="1")
pio = [sys.executable, "-m", "platformio"]
if (root / ".pio-tools" / "platformio").is_dir():
    env["PYTHONPATH"] = str(root / ".pio-tools") + os.pathsep + env.get("PYTHONPATH", "")
secrets = root / "secrets.ini"
local = []
if secrets.is_file():   # belt and braces: the local values must not be in an image
    cfg = configparser.ConfigParser(interpolation=None)
    cfg.read(secrets, encoding="utf-8")
    local = [(k, cfg.get("wifi", k, fallback="").strip()) for k in ("ssid", "password")]


def build(name):
    subprocess.run(pio + ["run", "-e", name, "-t", "clean"], cwd=root, env=env, check=True, stdout=subprocess.DEVNULL)
    subprocess.run(pio + ["run", "-e", name], cwd=root, env=env, check=True)
    out = root / ".pio" / "build" / name
    header = (out / "generated" / "wifi_secrets.h").read_text(encoding="utf-8")
    if "DEFAULT_WIFI" in header:
        sys.exit("Build picked up WiFi defaults; release images must not contain them")
    image = out / "firmware.bin"
    for key, value in local:
        if len(value) >= 3 and value.encode("utf-8") in image.read_bytes():
            sys.exit(f"{name}: image contains the local WiFi {key}; not releasing")
    return image


lite = build("esp12e_nomqtt")
full = build("esp12e")   # last, so .pio/build/esp12e is the release build for the tests
if lite.stat().st_size > 0x7E000:
    sys.exit(f"{lite}: too big for a plain update ({lite.stat().st_size} bytes)")

out = root / "release"
out.mkdir(exist_ok=True)
for old in out.glob("*AIAgentMonitor_v*"):
    old.unlink()   # one release's files at a time
files = ((full.with_suffix(".bin.gz"), f"AIAgentMonitor_v{version}.bin.gz"),
         (lite, f"SDPro_AIAgentMonitor_v{version}_nomqtt.bin"),
         (full, f"AIAgentMonitor_v{version}_usb.bin"))
sums = ""
for src, dst in files:
    shutil.copy(src, out / dst)
    blob = src.read_bytes()
    sums += f"{hashlib.sha256(blob).hexdigest()}  {dst}\n"
    print(f"{out / dst}: {len(blob)} bytes")
(out / "SHA256SUMS.txt").write_bytes(sums.encode())   # LF, or `sha256sum -c` fails
print("No WiFi details in the images.")

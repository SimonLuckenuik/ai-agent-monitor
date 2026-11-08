"""Build the public release image: no WiFi details.

    python scripts/make_release.py

Builds with SMARTCLOCK_RELEASE=1, which makes build_options.py ignore secrets.ini,
then refuses to finish if an image contains the SSID or password from a local
secrets.ini. Writes to release/:

- AIAgentMonitor_v<version>.update: the firmware's .bin.gz with the files it reads from
  storage (the settings page, spec 048), for clocks running 2.5.0 or later, and for the
  starter. The settings page's Install button downloads it.
- AIAgentMonitor_v<version>.bin.gz: the firmware alone, compressed, for clocks running
  1.4.0 to 2.4.x (they take nothing else); its setup page then installs the .update. No
  "SDP" prefix: a stock clock cannot unpack it, and its update page only accepts names
  starting with "SDP".
- SDPro_AIAgentMonitor_starter_v<version>.bin: the starter (spec 048), a plain image the
  stock update page installs; it joins WiFi and installs the latest release from GitHub.
- AIAgentMonitor_v<version>.elf: the firmware with its symbols, for
  scripts/decode_crash.py (a crash's addresses to file:line). Not for the clock.
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


starter = build("starter")
full = build("esp12e")   # .pio/build/esp12e is then the release build, for the tests

out = root / "release"
out.mkdir(exist_ok=True)
for old in list(out.glob("*AIAgentMonitor_v*")) + list(out.glob("*AIAgentMonitor_starter_v*")):
    old.unlink()   # one release's files at a time
files = ((full.with_suffix(".update"), f"AIAgentMonitor_v{version}.update"),
         (full.with_suffix(".bin.gz"), f"AIAgentMonitor_v{version}.bin.gz"),
         (starter, f"SDPro_AIAgentMonitor_starter_v{version}.bin"),
         (full.with_suffix(".elf"), f"AIAgentMonitor_v{version}.elf"))
sums = ""
for src, dst in files:
    shutil.copy(src, out / dst)
    blob = src.read_bytes()
    sums += f"{hashlib.sha256(blob).hexdigest()}  {dst}\n"
    print(f"{out / dst}: {len(blob)} bytes")
(out / "SHA256SUMS.txt").write_bytes(sums.encode())   # LF, or `sha256sum -c` fails
print("No WiFi details in the image.")

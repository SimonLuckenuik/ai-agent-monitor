"""Unit tests for the custom-face engine and text folding, built and run on the host.

    python tests/unit/run_unit_tests.py

Compiles tests/unit/unit_tests.cpp, which includes src/faces.cpp (so its internal
functions are reachable) against the face preview's mocks, plus src/ascii.cpp, with
Zig; then tests/unit/mdns_tests.cpp against src/mdns_packet.cpp. Uses ArduinoJson and TFT_eSPI fonts from .pio/libdeps (run `pio run` once first).
"""
from pathlib import Path
import os
import re
import shutil
import subprocess
import sys

root = Path(__file__).resolve().parents[2]
here = Path(__file__).resolve().parent
mock = root / "tests" / "face_preview" / "mock"
build = root / ".pio" / "unit-tests"
shutil.rmtree(build, ignore_errors=True)
build.mkdir(parents=True)
libs = root / ".pio" / "libdeps" / "esp12e"
if not (libs / "TFT_eSPI").is_dir():
    raise SystemExit("Run `pio run` first so .pio/libdeps holds TFT_eSPI and ArduinoJson")

# As in the face preview: config.h without load()/save(), and the sources copied next
# to it so their #include "config.h" finds this copy.
config = (root / "src" / "config.h").read_text(encoding="utf-8")
config = re.sub(r"\n  bool load\(\) \{.*?\n  \}\n", "\n", config, flags=re.S)
config = re.sub(r"\n  bool save\(\) \{.*?\n  \}\n", "\n", config, flags=re.S)
config = config.replace("#include <ArduinoJson.h>\n", "").replace("#include <LittleFS.h>\n", "")
(build / "config.h").write_text(config, encoding="utf-8")
for name in ("faces.cpp", "faces.h", "mono_glyphs.h", "outline_glyphs.h", "weather.h", "weather_icons.h", "integrations.h", "ascii.h", "ascii.cpp", "json_parse.h", "json_parse.cpp"):
    shutil.copy(root / "src" / name, build / name)
tjpg = libs / "TJpg_Decoder" / "src"
header = (tjpg / "tjpgd.h").read_text(encoding="utf-8").replace("#if defined(_WIN32)", "#if 0")
(build / "tjpgd.h").write_text(header, encoding="utf-8")
shutil.copy(tjpg / "tjpgdcnf.h", build / "tjpgdcnf.h")

zig = shutil.which("zig") or str(root / ".pio-tools" / "ziglang" / "zig.exe")
cache = root / ".pio" / "unit-tests-cache"
env = dict(os.environ, ZIG_LOCAL_CACHE_DIR=str(cache / "local"), ZIG_GLOBAL_CACHE_DIR=str(cache / "global"))
inc = [f"-I{build}", f"-I{mock}", f"-I{libs / 'TFT_eSPI'}", f"-I{tjpg}", f"-I{libs / 'ArduinoJson' / 'src'}"]
obj = build / "tjpgd.o"
subprocess.run([zig, "cc", "-c", "-O1", f"-I{tjpg}", str(tjpg / "tjpgd.c"), "-o", str(obj)], check=True, env=env)
exe = build / "unit_tests.exe"
subprocess.run([zig, "c++", "-std=c++17", "-O1", "-Wall", "-Wno-nullability-completeness", *inc,
                str(here / "unit_tests.cpp"), str(build / "ascii.cpp"), str(build / "json_parse.cpp"), str(mock / "TFT_eSPI.cpp"), str(obj),
                "-o", str(exe)], check=True, env=env)
faces = subprocess.run([str(exe)]).returncode
# The mDNS packets (spec 027): plain C++, no mocks.
mdns = build / "mdns_tests.exe"
subprocess.run([zig, "c++", "-std=c++17", "-O1", "-Wall", str(here / "mdns_tests.cpp"),
                str(root / "src" / "mdns_packet.cpp"), "-o", str(mdns)], check=True, env=env)
# The time guard (spec 029): plain C++ too.
guard = build / "guard_tests.exe"
subprocess.run([zig, "c++", "-std=c++17", "-O1", "-Wall", str(here / "guard_tests.cpp"), "-o", str(guard)], check=True, env=env)
# The OpenClaw node's signing and byte helpers (spec 041): plain C++ too.
openclaw = build / "openclaw_tests.exe"
subprocess.run([zig, "c++", "-std=c++17", "-O1", "-Wall", str(here / "openclaw_tests.cpp"), str(root / "src" / "ed25519.cpp"),
                str(root / "src" / "openclaw_proto.cpp"), "-o", str(openclaw)], check=True, env=env)
# The update file (spec 048): written by scripts/make_update.py, read by update_bundle.h.
sys.path.insert(0, str(root / "scripts"))
import make_update   # noqa: E402
fixture_files = [("index.html.gz", bytes((i * 7) & 0xFF for i in range(3000))), ("empty.txt", b""), ("notes.md", b"hello")]
fixture = build / "fixture.update"
fixture.write_bytes(make_update.make_update(fixture_files, bytes((i * 13) & 0xFF for i in range(5000))))
update = build / "update_tests.exe"
subprocess.run([zig, "c++", "-std=c++17", "-O1", "-Wall", f"-I{root / 'include'}", str(here / "update_tests.cpp"), "-o", str(update)],
               check=True, env=env)
sys.exit(faces or subprocess.run([str(mdns)]).returncode or subprocess.run([str(guard)]).returncode
         or subprocess.run([str(openclaw)]).returncode
         or subprocess.run([str(update), str(fixture), "%08x" % make_update.files_hash(fixture_files)]).returncode)

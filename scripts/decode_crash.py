"""Decode the clock's crash log: each crash's cause, and its addresses as function and
file:line, from the matching release's firmware.elf.

    python scripts/decode_crash.py http://192.168.1.50          # reads /api/crashes
    python scripts/decode_crash.py crashes.bin                  # a downloaded log
    python scripts/decode_crash.py crashes.bin --elf .pio/build/esp12e/firmware.elf

From a clock, it signs in with CLOCK_API_KEY (Integrations > Custom faces) or
CLOCK_PASSWORD. Without --elf it uses .pio/build/esp12e/firmware.elf when that build
has the crash's version, else downloads AIAgentMonitor_v<version>.elf from the GitHub
release (kept in .pio/crash-elf/). addr2line comes with PlatformIO's xtensa toolchain.

The log's layout is src/crash.cpp's (little-endian): a 20-byte header (magic "CRLG",
count, loop, level, n), then n entries of 160 bytes, newest first.
"""
import argparse
import glob
import http.cookiejar
import os
import re
import struct
import subprocess
import sys
import urllib.parse
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
REPO = "SimonLuckenuik/ai-agent-monitor"
REASONS = {1: "hardware watchdog", 2: "exception", 3: "software watchdog", 253: "stack overflow", 254: "panic"}
# Xtensa exception causes the ESP8266 reports most (EXCCAUSE).
CAUSES = {0: "illegal instruction", 2: "instruction fetch error", 3: "load/store error", 6: "division by zero",
          9: "unaligned load or store", 28: "load from an invalid address (null pointer?)",
          29: "store to an invalid address (null pointer?)"}


def fetch(base):
    jar = http.cookiejar.CookieJar()
    opener = urllib.request.build_opener(urllib.request.HTTPCookieProcessor(jar))
    headers = {}
    if os.environ.get("CLOCK_API_KEY"):
        headers["X-Api-Key"] = os.environ["CLOCK_API_KEY"]
    elif os.environ.get("CLOCK_PASSWORD"):
        body = urllib.parse.urlencode({"password": os.environ["CLOCK_PASSWORD"]}).encode()
        opener.open(urllib.request.Request(base + "/api/auth/login", body), timeout=10)
    else:
        sys.exit("Set CLOCK_API_KEY or CLOCK_PASSWORD to read the clock's crash log")
    with opener.open(urllib.request.Request(base + "/api/crashes", headers=headers), timeout=10) as r:
        return r.read()


def parse(blob):
    if len(blob) < 20:
        return None, []
    magic, count, loop, level, n = struct.unpack_from("<5I", blob, 0)
    if magic != 0x474C5243:
        sys.exit("Not a crash log (bad magic)")
    entries = []
    for i in range(n):
        o = 20 + i * 160
        if o + 160 > len(blob):
            break
        f = struct.unpack_from("<12Ii24sI16I", blob, o)
        (_, build, reason, exccause, epc1, epc2, epc3, excvaddr, depc, uptime, heap, stack_free, face, custom,
         n_addrs) = f[:15]
        addrs = list(f[15:15 + min(n_addrs, 16)])
        at, version = struct.unpack_from("<I12s", blob, o + 144)
        entries.append(dict(reason=reason, exccause=exccause, epc1=epc1, epc2=epc2, epc3=epc3, excvaddr=excvaddr,
                            depc=depc, uptime=uptime, heap=heap, stack_free=stack_free, face=face,
                            custom=custom.split(b"\0")[0].decode(errors="replace"), addrs=addrs, at=at,
                            version=version.split(b"\0")[0].decode(errors="replace")))
    return dict(count=count, loop=loop, level=level), entries


def elf_for(version, given):
    if given:
        return Path(given)
    local = ROOT / ".pio" / "build" / "esp12e" / "firmware.elf"
    src = (ROOT / "src" / "main.cpp").read_text(encoding="utf-8")
    if local.exists() and f'#define FW_VERSION "{version}"' in src:
        return local
    cache = ROOT / ".pio" / "crash-elf" / f"AIAgentMonitor_v{version}.elf"
    if not cache.exists():
        url = f"https://github.com/{REPO}/releases/download/v{version}/AIAgentMonitor_v{version}.elf"
        print(f"Downloading {url}")
        cache.parent.mkdir(parents=True, exist_ok=True)
        try:
            urllib.request.urlretrieve(url, cache)
        except OSError as e:
            print(f"  no ELF for {version} ({e}): addresses stay raw")
            return None
    return cache


def addr2line(elf, addrs):
    tools = glob.glob(os.path.expanduser("~/.platformio/packages/toolchain-xtensa/bin/xtensa-lx106-elf-addr2line*"))
    if not elf or not tools or not addrs:
        return {}
    out = subprocess.run([tools[0], "-f", "-C", "-p", "-e", str(elf)] + [hex(a) for a in addrs],
                         capture_output=True, text=True).stdout.splitlines()
    return dict(zip(addrs, out))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("source", help="the clock's address (http://...) or a crashes.bin")
    ap.add_argument("--elf", help="firmware.elf of the build that crashed")
    args = ap.parse_args()
    blob = fetch(args.source.rstrip("/")) if re.match(r"https?://", args.source) else Path(args.source).read_bytes()
    head, entries = parse(blob)
    if not entries:
        print("No crashes recorded.")
        return
    print(f"{head['count']} crashes logged; {head['loop']} in a row now; "
          + ["faces run normally", "custom faces off", "safe mode"][min(head["level"], 2)])
    for i, e in enumerate(entries):
        import datetime
        when = datetime.datetime.fromtimestamp(e["at"]).strftime("%Y-%m-%d %H:%M:%S") if e["at"] else "time unknown"
        face = f", drawing {e['custom'] or 'face ' + str(e['face'])}" if e["face"] >= 0 else ""
        print(f"\n#{i + 1} {REASONS.get(e['reason'], 'crash')} ({e['reason']}), {when}, after {e['uptime']} s up{face}")
        print(f"   v{e['version']}, heap {e['heap']} B free, stack {e['stack_free']} B free")
        if e["reason"] == 2:
            print(f"   exception {e['exccause']}: {CAUSES.get(e['exccause'], 'see the Xtensa EXCCAUSE table')}, "
                  f"address 0x{e['excvaddr']:08x}")
        lines = addr2line(elf_for(e["version"], args.elf), [a for a in [e["epc1"]] + e["addrs"] if a])
        if e["epc1"]:
            print(f"   at   0x{e['epc1']:08x} {lines.get(e['epc1'], '')}")
        for a in e["addrs"]:
            print(f"   from 0x{a:08x} {lines.get(a, '')}")


if __name__ == "__main__":
    main()

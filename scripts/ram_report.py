"""Static RAM of a firmware ELF by category, and the strings it keeps in RAM by source file.

    python scripts/ram_report.py .pio/build/esp12e/firmware.elf [--strings]

RAM here is .data, .rodata and .bss (all in the ESP8266's 80 KB of data RAM). Symbols
are attributed through the debug info (nm -l); string literals, which have no symbol,
are found in .rodata and matched against the sources in src/ and include/."""
import collections, glob, os, re, subprocess, sys

tool = lambda n: glob.glob(os.path.expanduser(f"~/.platformio/packages/toolchain-xtensa/bin/xtensa-lx106-elf-{n}*"))[0]
elf = sys.argv[1]
root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

secs = {}
for line in subprocess.run([tool("objdump"), "-h", elf], capture_output=True, text=True).stdout.splitlines():
    p = line.split()
    if len(p) >= 7 and p[0].isdigit() and p[1] in (".data", ".rodata", ".bss", ".noinit"):
        secs[p[1]] = (int(p[3], 16), int(p[2], 16))


def section(a):
    for n, (vma, size) in secs.items():
        if vma <= a < vma + size:
            return n
    return None


# Categories: by source path first, then by symbol name.
PATH_RULES = [
    (r"/src/faces\.cpp", "Faces (built-in and custom)"),
    (r"/src/integrations\.cpp", "Integrations (faces API, variables, messages, MQTT)"),
    (r"/src/weather\.(cpp|h)", "Weather and forecast"),
    (r"/src/crash\.cpp", "Crash records"),
    (r"/src/(main\.cpp|config\.h)|/include/", "Settings, web API, WiFi, update (main.cpp)"),
    (r"lwip|/glue|netif|etharp|dhcp|dns\.c|tcp|udp|igmp|pbuf|memp", "Network stack (lwIP)"),
    (r"TFT_eSPI|TJpg", "Display and JPEG libraries"),
    (r"ArduinoJson", "ArduinoJson"),
    (r"littlefs|LittleFS|/FS", "File system"),
    (r"ESP8266WebServer|DNSServer|ESP8266HTTPClient|ESP8266WiFi|ESP8266mDNS|PubSubClient", "Arduino libraries (web, WiFi, MQTT, mDNS)"),
    (r"newlib|libgcc|libstdc|gcc-gnu", "C and C++ runtime (newlib, libgcc)"),
    (r"cores/esp8266|umm_malloc", "Arduino core"),
]
NAME_RULES = [
    (r"^(g_ic|pmc|chip6_|gScanStruct|event_TaskQueue|rtc|phy|wDev|ieee80211|sta_|wpa|lmac|pp_|esf_|ets_|user_|system_|SPI|flash|bit_|sleep|hostap|cnx|scan|rcons|sdk|wifi|gpio|int_|_x_|noise|ap_|wps|tx_|rx_|g_|pm_|tmr|timer)",
     "ESP8266 SDK (WiFi, PHY, system)"),
]


def category(name, path):
    for rx, cat in PATH_RULES:
        if path and re.search(rx, path):
            return cat
    for rx, cat in NAME_RULES:
        if re.match(rx, name):
            return cat
    return "ESP8266 SDK (WiFi, PHY, system)" if not path else "Other"


# Which object or library defines each symbol (the build's .o and .a files, the SDK's and
# the toolchain's libraries), then a category from that path.
fw = os.path.expanduser("~/.platformio/packages/framework-arduinoespressif8266")
tc = os.path.expanduser("~/.platformio/packages/toolchain-xtensa")
build = os.path.dirname(elf)
objs = (glob.glob(os.path.join(build, "**", "*.o"), recursive=True) + glob.glob(os.path.join(build, "**", "*.a"), recursive=True)
        + glob.glob(os.path.join(fw, "tools", "sdk", "lib", "*.a")) + glob.glob(os.path.join(fw, "tools", "sdk", "lib", "NONOSDK*", "*.a"))
        + glob.glob(os.path.join(tc, "xtensa-lx106-elf", "lib", "*.a")) + glob.glob(os.path.join(tc, "lib", "gcc", "**", "*.a"), recursive=True))
owner = {}
for o in objs:
    out = subprocess.run([tool("nm"), "-A", "--defined-only", o], capture_output=True, text=True, errors="replace").stdout
    for line in out.splitlines():
        m = re.match(r"(.*?):\s*[0-9a-f]*\s+[bBdDrRcCgGsSvV]\s+(\S+)$", line)
        if m:
            owner.setdefault(m[2], m[1].replace("\\", "/"))

OBJ_RULES = [
    (r"src/faces\.cpp", "Faces (built-in and custom)"),
    (r"src/integrations\.cpp", "Integrations (faces API, variables, messages, MQTT)"),
    (r"src/weather\.cpp", "Weather and forecast"),
    (r"src/crash\.cpp", "Crash records"),
    (r"src/(main|ascii)\.cpp", "Settings, web API, WiFi, update (main.cpp)"),
    (r"liblwip", "Network stack (lwIP)"),
    (r"lib(pp|net80211|phy|main|wpa|wpa2|crypto|hal|airkiss|smartconfig|espnow|wps)\.a", "ESP8266 SDK (WiFi, PHY, system)"),
    (r"TFT_eSPI|TJpg", "Display and JPEG libraries"),
    (r"LittleFS|littlefs", "File system"),
    (r"WebServer|DNSServer|HTTPClient|ESP8266WiFi|mDNS|PubSubClient", "Arduino libraries (web, WiFi, MQTT, mDNS)"),
    (r"libc|libm|libgcc|libstdc", "C and C++ runtime (newlib, libgcc)"),
    (r"FrameworkArduino|/core/|cores", "Arduino core"),
]


def category(name):
    o = owner.get(name, "")
    for rx, cat in OBJ_RULES:
        if re.search(rx, o):
            return cat
    return "Unattributed" if o == "" else "Other: " + os.path.basename(o)


cats = collections.defaultdict(collections.Counter)
named = collections.Counter()
rows = []
for line in subprocess.run([tool("nm"), "-n", "-S", elf], capture_output=True, text=True, errors="replace").stdout.splitlines():
    p = line.split()
    try:
        a = int(p[0], 16)
    except (ValueError, IndexError):
        continue
    sec = section(a)
    if sec and len(p) >= 3:
        size = int(p[1], 16) if len(p) >= 4 else 0
        rows.append((a, size, p[-1], sec))
for (a, size, name, sec), nxt in zip(rows, rows[1:] + [None]):
    cat = category(name)
    cats[cat][sec] += size
    named[sec] += size
    # SDK buffers have symbols without a size: the gap up to the next symbol is theirs.
    if sec in (".bss", ".data") and nxt and nxt[3] == sec:
        gap = nxt[0] - (a + size)
        if gap > 16:
            cats[cat if size == 0 or cat.startswith("ESP8266") else "Padding and unnamed"][sec] += gap
            named[sec] += gap

# String literals (no symbol): printable runs in .rodata, matched to our sources.
raw = subprocess.run([tool("objdump"), "-s", "-j", ".rodata", elf], capture_output=True, text=True).stdout
blob = bytearray()
for l in raw.splitlines():
    p = l.split()
    if len(p) > 1 and re.fullmatch(r"[0-9a-f]{8}", p[0]):
        for w in p[1:5]:
            if re.fullmatch(r"[0-9a-f]+", w):
                blob += bytes.fromhex(w)
srcs = {os.path.relpath(f, root).replace("\\", "/"): open(f, encoding="utf-8", errors="replace").read()
        for f in glob.glob(os.path.join(root, "src", "*.*")) + glob.glob(os.path.join(root, "include", "*.h"))}
strings = collections.defaultdict(list)
for m in re.finditer(rb"[\x20-\x7e\t\n]{3,}\x00", bytes(blob)):
    t = m.group(0)[:-1].decode()
    key = t.replace("\\", "\\\\").replace('"', '\\"').replace("\n", "\\n")[:40]
    owner = next((f for f, txt in srcs.items() if key in txt), "(libraries)")
    strings[owner].append(t)
str_total = sum(len(t) + 1 for v in strings.values() for t in v)
ours = sum(len(t) + 1 for f, v in strings.items() if f != "(libraries)" for t in v)
cats["String literals, ours (src/, include/)"][".rodata"] += ours
cats["String literals, libraries"][".rodata"] += str_total - ours
unnamed = secs[".rodata"][1] - named[".rodata"] - str_total
cats["Other .rodata without a symbol (tables, vtables, literals < 3)"][".rodata"] += max(0, unnamed)
for sname in (".data", ".bss"):
    rest = secs[sname][1] - named[sname]
    if rest > 0:
        cats["Padding and unnamed"][sname] += rest

total = sum(v[1] for v in secs.values())
print(f"Static RAM: {total} B (.data {secs['.data'][1]}, .rodata {secs['.rodata'][1]}, .bss {secs['.bss'][1]})\n")
print(f"{'Category':62} {'.data':>6} {'.rodata':>7} {'.bss':>6} {'total':>7}")
for c, v in sorted(cats.items(), key=lambda kv: -sum(kv[1].values())):
    t = v[".data"] + v[".rodata"] + v[".bss"] + v[".noinit"]
    if t:
        print(f"{c[:62]:62} {v['.data']:6} {v['.rodata']:7} {v['.bss'] + v['.noinit']:6} {t:7}")

if "--strings" in sys.argv:
    for f, v in sorted(strings.items(), key=lambda kv: -sum(len(t) + 1 for t in kv[1])):
        print(f"\n{f}: {len(v)} strings, {sum(len(t) + 1 for t in v)} B")
        for t in sorted(v, key=len, reverse=True):
            print(f"  {len(t) + 1:4}  {t[:100]!r}")

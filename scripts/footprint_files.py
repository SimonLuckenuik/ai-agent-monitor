"""Flash and static RAM of the firmware ELF, grouped by source file (from the debug info).
scripts/footprint_files.py ELF"""
import collections, glob, os, re, subprocess, sys

tool = lambda n: glob.glob(os.path.expanduser(f"~/.platformio/packages/toolchain-xtensa/bin/xtensa-lx106-elf-{n}*"))[0]
elf = sys.argv[1]

# Section of each address range: flash (irom/rodata in flash), iram, dram (data/rodata copied to RAM), bss.
secs = []
for line in subprocess.run([tool("objdump"), "-h", elf], capture_output=True, text=True).stdout.splitlines():
    p = line.split()
    if len(p) >= 7 and p[0].isdigit():
        secs.append((p[1], int(p[3], 16), int(p[2], 16)))


def section(addr):
    for name, vma, size in secs:
        if vma <= addr < vma + size:
            return name
    return "?"


def kind(sec):
    if sec.startswith(".irom0") or sec == ".flash.text":
        return "flash"
    if sec.startswith(".text") or sec.startswith(".iram"):
        return "iram"
    if sec in (".data", ".rodata"):
        return "dram"   # in flash too, copied into RAM at boot
    if sec == ".bss" or sec == ".noinit":
        return "bss"
    return "other"


out = subprocess.run([tool("nm"), "-l", "-S", "-C", elf], capture_output=True, text=True, errors="replace").stdout
by = collections.defaultdict(lambda: collections.Counter())
for line in out.splitlines():
    m = re.match(r"([0-9a-f]{8}) ([0-9a-f]{8}) (\w) (.*?)(?:\t(\S+):\d+)?$", line)
    if not m:
        continue
    addr, size, file = int(m[1], 16), int(m[2], 16), m[5] or "(no file)"
    file = file.replace("\\", "/")
    for key, rx in [("src/", r".*/ai-agent-monitor/(src/.*)"), ("lib", r".*/libdeps/esp12e/([^/]+)/.*"),
                    ("core", r".*/framework-arduinoespressif8266/(cores/esp8266|libraries/[^/]+)/.*"),
                    ("sdk", r".*/(tools/sdk|toolchain).*")]:
        mm = re.match(rx, file)
        if mm:
            file = mm[1] if key != "sdk" else "sdk/toolchain"
            break
    by[file][kind(section(addr))] += size

rows = sorted(by.items(), key=lambda kv: -(kv[1]["flash"] + kv[1]["iram"] + kv[1]["dram"]))
print(f"{'file':55} {'flash':>8} {'iram':>7} {'dram':>7} {'bss':>7}")
tot = collections.Counter()
for f, c in rows:
    tot.update(c)
    if c["flash"] + c["iram"] + c["dram"] + c["bss"] >= 400:
        print(f"{f[:55]:55} {c['flash']:8} {c['iram']:7} {c['dram']:7} {c['bss']:7}")
print(f"{'total':55} {tot['flash']:8} {tot['iram']:7} {tot['dram']:7} {tot['bss']:7}")

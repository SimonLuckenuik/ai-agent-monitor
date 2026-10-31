"""Code bytes per source section (the "// ---- " headers in src/*.cpp), from the DWARF line
table, so inlined code counts where it is written. Data symbols are added through nm -l.
scripts/footprint_sections.py ELF REPO"""
import bisect, collections, glob, os, re, subprocess, sys

tool = lambda n: glob.glob(os.path.expanduser(f"~/.platformio/packages/toolchain-xtensa/bin/xtensa-lx106-elf-{n}*"))[0]
elf, repo = sys.argv[1], sys.argv[2]

# Section headers per source file.
heads = {}
for path in glob.glob(os.path.join(repo, "src", "*.cpp")) + glob.glob(os.path.join(repo, "src", "*.h")):
    name = "src/" + os.path.basename(path)
    hs = [(0, "(top)")]
    for i, line in enumerate(open(path, encoding="utf-8", errors="replace"), 1):
        m = re.match(r"// -{2,} ?(.*?)\s*-*$", line)
        if m and m[1]:
            hs.append((i, m[1][:60]))
    heads[name] = hs


def where(file, line):
    file = file.replace("\\", "/")
    base = "src/" + file.rsplit("/", 1)[-1]
    if base not in heads:
        return "lib: " + file.rsplit("/", 1)[-1]
    hs = heads[base]
    i = bisect.bisect_right([h[0] for h in hs], line) - 1
    return f"{base}: {hs[i][1]}"


# Decoded line table: rows of (address, file, line), per sequence.
out = subprocess.run([tool("objdump"), "--dwarf=decodedline", elf], capture_output=True, text=True, errors="replace").stdout
rows = []
cur = None
for line in out.splitlines():
    m = re.match(r"^(\S+\.(?:cpp|h|c|hpp|ino))\s+(\d+)\s+(0x[0-9a-f]+)", line)
    if m:
        rows.append((int(m[3], 16), m[1], int(m[2])))
    elif line.startswith("CU:") or line.endswith(":"):
        # a file header line: the full path follows in some formats
        p = line[3:].strip() if line.startswith("CU:") else line[:-1]
        cur = p
rows.sort()
code = collections.Counter()
for (a, f, l), (b, _, _) in zip(rows, rows[1:]):
    if 0 < b - a < 4096:
        k = where(f, l)
        if k:
            code[k] += b - a

data = collections.Counter()
nm = subprocess.run([tool("nm"), "-l", "-S", "-C", elf], capture_output=True, text=True, errors="replace").stdout
for line in nm.splitlines():
    m = re.match(r"([0-9a-f]{8}) ([0-9a-f]{8}) ([rRdDbB]) .*?\t(\S+):(\d+)$", line)
    if m:
        k = where(m[4], int(m[5]))
        if k:
            data[(k, "ram" if m[3] in "bBdD" else "flash")] += int(m[2], 16)

keys = set(code) | {k for k, _ in data}
print(f"{'section':78} {'code':>7} {'data':>7} {'ram':>6}")
for k in sorted(keys, key=lambda k: -(code[k] + data[(k, 'flash')])):
    c, d, r = code[k], data[(k, "flash")], data[(k, "ram")]
    if c + d + r >= 300 and (k.startswith("src") or c + d + r >= 1500):
        print(f"{k[:78]:78} {c:7} {d:7} {r:6}")
print("total code", sum(code.values()), "data", sum(v for (k, t), v in data.items() if t == "flash"),
      "ram", sum(v for (k, t), v in data.items() if t == "ram"))

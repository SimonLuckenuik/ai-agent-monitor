"""Compiles the big line font's glyphs (spec 030) from SVG path text into points.

    python scripts/gen_mono_glyphs.py            # writes src/mono_glyphs.h
    python scripts/gen_mono_glyphs.py --check    # fails if src/mono_glyphs.h is out of date
    python scripts/gen_mono_glyphs.py --stats    # points per glyph, sizes, at each tolerance

The glyphs live in src/mono_glyphs.txt: one per line, `<char> <path>`, the char as itself
or as \\xNN (the codes 1-9 monoPrep() gives the symbols). Paths use M L H V C S A Z
(absolute and relative) on a 20-unit-tall grid. Curves and arcs are flattened here, once,
so the clock never parses path text or does curve maths while drawing.
"""
import argparse
import math
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
SRC = ROOT / "src" / "mono_glyphs.txt"
OUT = ROOT / "src" / "mono_glyphs.h"
UNIT = 12            # points are stored in 1/12 of a glyph unit (a glyph is 20 units tall)
TOLERANCE = 0.03     # the most a flattened curve strays from the true one, in glyph units


def tokens(d):
    for m in re.finditer(r"[MLHVCSQTAZmlhvcsqtaz]|-?(?:\d+\.?\d*|\.\d+)(?:e-?\d+)?", d):
        yield m.group(0)


def flatten(d, tol):
    """The path as polylines: a list of lists of (x, y) in glyph units."""
    toks = list(tokens(d))
    i, cmd = 0, None
    x = y = sx = sy = 0.0
    lastc = None   # the previous cubic's second control point, for S
    lines, cur = [], []

    def num():
        nonlocal i
        v = float(toks[i]); i += 1
        return v

    def flag():   # arc flags may be written without separators: "a2 2 0 1 0 4 0"
        nonlocal i
        t = toks[i]
        if len(t) > 1 and t[0] in "01" and not t.startswith(("0.", "-")):
            toks[i] = t[1:]
            return int(t[0])
        i += 1
        return int(float(t))

    def line_to(nx, ny):
        nonlocal x, y
        if not cur:
            cur.append((x, y))
        cur.append((nx, ny))
        x, y = nx, ny

    def steps_for(length):
        # chord error of a circle-ish curve ~ L^2 / (8 R); aim for tol with R ~ length / 2
        return max(2, min(32, math.ceil(math.sqrt(length / (4 * tol)))))

    def cubic(x1, y1, x2, y2, x3, y3):
        length = math.dist((x, y), (x1, y1)) + math.dist((x1, y1), (x2, y2)) + math.dist((x2, y2), (x3, y3))
        n = steps_for(length)
        x0, y0 = x, y
        for k in range(1, n + 1):
            t = k / n
            a, b, c, e = (1 - t) ** 3, 3 * t * (1 - t) ** 2, 3 * t * t * (1 - t), t ** 3
            line_to(a * x0 + b * x1 + c * x2 + e * x3, a * y0 + b * y1 + c * y2 + e * y3)

    def arc(rx, ry, rot, large, sweep, ex, ey):
        x0, y0 = x, y
        if rx == 0 or ry == 0 or (x0 == ex and y0 == ey):
            line_to(ex, ey)
            return
        phi = math.radians(rot)
        cp, sp = math.cos(phi), math.sin(phi)
        dx, dy = (x0 - ex) / 2, (y0 - ey) / 2
        x1p, y1p = cp * dx + sp * dy, -sp * dx + cp * dy
        rx, ry = abs(rx), abs(ry)
        lam = x1p ** 2 / rx ** 2 + y1p ** 2 / ry ** 2
        if lam > 1:
            rx, ry = rx * math.sqrt(lam), ry * math.sqrt(lam)
        num_ = rx * rx * ry * ry - rx * rx * y1p * y1p - ry * ry * x1p * x1p
        den = rx * rx * y1p * y1p + ry * ry * x1p * x1p
        co = math.sqrt(max(0.0, num_ / den)) * (-1 if large == sweep else 1)
        cxp, cyp = co * rx * y1p / ry, -co * ry * x1p / rx
        cx = cp * cxp - sp * cyp + (x0 + ex) / 2
        cy = sp * cxp + cp * cyp + (y0 + ey) / 2

        def ang(ux, uy, vx, vy):
            a = math.atan2(ux * vy - uy * vx, ux * vx + uy * vy)
            return a
        t1 = ang(1, 0, (x1p - cxp) / rx, (y1p - cyp) / ry)
        dt = ang((x1p - cxp) / rx, (y1p - cyp) / ry, (-x1p - cxp) / rx, (-y1p - cyp) / ry)
        if not sweep and dt > 0:
            dt -= 2 * math.pi
        elif sweep and dt < 0:
            dt += 2 * math.pi
        n = steps_for(abs(dt) * max(rx, ry))
        for k in range(1, n + 1):
            t = t1 + dt * k / n
            px, py = rx * math.cos(t), ry * math.sin(t)
            line_to(cp * px - sp * py + cx, sp * px + cp * py + cy)
        x_, y_ = ex, ey
        cur[-1] = (x_, y_)

    while i < len(toks):
        if re.fullmatch(r"[A-Za-z]", toks[i]):
            cmd = toks[i]; i += 1
            if cmd in "Zz":
                if cur:
                    cur.append(cur[0])
                    x, y = cur[0]
                    lines.append(cur); cur = []
                lastc = None
                continue
        rel = cmd.islower()
        c = cmd.upper()
        ox, oy = (x, y) if rel else (0.0, 0.0)
        if c == "M":
            if cur:
                lines.append(cur); cur = []
            x, y = num() + ox, num() + oy
            sx, sy = x, y
            cmd = "l" if rel else "L"   # later pairs are line-tos
            lastc = None
        elif c == "L":
            line_to(num() + ox, num() + oy); lastc = None
        elif c == "H":
            line_to(num() + ox, y); lastc = None
        elif c == "V":
            line_to(x, num() + oy); lastc = None
        elif c == "C":
            x1, y1, x2, y2, x3, y3 = (num() + (ox if k % 2 == 0 else oy) for k in range(6))
            cubic(x1, y1, x2, y2, x3, y3); lastc = (x2, y2)
        elif c == "S":
            x1, y1 = (2 * x - lastc[0], 2 * y - lastc[1]) if lastc else (x, y)
            x2, y2, x3, y3 = (num() + (ox if k % 2 == 0 else oy) for k in range(4))
            cubic(x1, y1, x2, y2, x3, y3); lastc = (x2, y2)
        elif c == "A":
            rx, ry, rot = num(), num(), num()
            large, sweep = flag(), flag()
            ex, ey = num() + ox, num() + oy
            arc(rx, ry, rot, large, sweep, ex, ey); lastc = None
        else:
            raise ValueError(f"path command {cmd} in {d!r}")
    if cur:
        lines.append(cur)
    elif not lines and (x, y) != (0, 0):
        lines.append([(x, y)])
    return lines


def load():
    glyphs = []
    for raw in SRC.read_text(encoding="utf-8").splitlines():
        if raw.startswith("#") or (not raw.strip() and not raw.startswith("  ")):   # "  " is the space
            continue
        ch, path = raw.split(" ", 1) if not raw.startswith("  ") else (" ", raw[2:])
        if ch.startswith("\\x"):
            ch = chr(int(ch[2:], 16))
        glyphs.append((ch, path.strip()))
    return glyphs


def header(glyphs):
    """src/mono_glyphs.h: every point as two bytes (x, y in 1/UNIT of a unit), each stroke
    as its point count, each glyph as its character, first stroke and stroke count."""
    pts, lens, table = [], [], []
    for ch, path in glyphs:
        strokes = flatten(path, TOLERANCE)
        table.append((ch, len(lens), len(strokes)))
        for line in strokes:
            q = []
            for x, y in line:   # drop points that round onto the previous one
                p = (round(x * UNIT), round(y * UNIT))
                if not q or p != q[-1]:
                    q.append(p)
            if len(q) == 1 and len(line) > 1:
                q.append(q[0])   # a dot drawn as a zero-length stroke keeps two points
            if any(not 0 <= v <= 255 for p in q for v in p) or len(q) > 255:
                raise ValueError(f"glyph {ch!r}: out of range")
            lens.append(len(q))
            pts.extend(q)

    def c(ch):
        return f"'\\x{ord(ch):02x}'" if ord(ch) < 32 else ("'\\''" if ch == "'" else ("'\\\\'" if ch == "\\" else f"'{ch}'"))
    rows = []
    for i in range(0, len(pts), 8):
        rows.append("  " + " ".join(f"{x},{y}," for x, y in pts[i:i + 8]))
    lrows = ["  " + " ".join(f"{n}," for n in lens[i:i + 24]) for i in range(0, len(lens), 24)]
    trows = ["  " + " ".join(f"{{{c(ch)}, {first}, {n}}}," for ch, first, n in table[i:i + 6]) for i in range(0, len(table), 6)]
    size = 2 * len(pts) + len(lens) + 4 * len(table)
    return "\n".join([
        "// Generated by scripts/gen_mono_glyphs.py from src/mono_glyphs.txt: do not edit.",
        f"// The big line font (spec 030): {len(table)} glyphs, {len(lens)} strokes, {len(pts)} points, {size} B of flash.",
        "#pragma once",
        "#include <stdint.h>",
        "",
        f"constexpr int MONO_UNIT = {UNIT};   // points are in 1/MONO_UNIT of a glyph unit (20 units tall)",
        "struct MonoGlyphData { char c; uint8_t firstStroke, strokes; };",
        "",
        "const uint8_t MONO_PTS[] PROGMEM = {",
        *rows,
        "};",
        "const uint8_t MONO_STROKE_LEN[] PROGMEM = {",
        *lrows,
        "};",
        "const MonoGlyphData MONO_GLYPHS[] PROGMEM = {",
        *trows,
        "};",
        f"constexpr int MONO_GLYPH_COUNT = {len(table)};",
        "",
    ])


def stats():
    glyphs = load()
    for tol in (0.02, 0.04, 0.08):
        pts = sum(len(l) for _, p in glyphs for l in flatten(p, tol))
        lines = sum(len(flatten(p, tol)) for _, p in glyphs)
        print(f"tolerance {tol}: {pts} points in {lines} strokes -> about {2 * pts + lines + 2 * len(glyphs)} B")
    allp = [pt for _, p in glyphs for l in flatten(p, TOLERANCE) for pt in l]
    print("x", min(p[0] for p in allp), max(p[0] for p in allp), "y", min(p[1] for p in allp), max(p[1] for p in allp))


if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("--stats", action="store_true")
    ap.add_argument("--check", action="store_true")
    a = ap.parse_args()
    if a.stats:
        stats()
        sys.exit(0)
    text = header(load())
    if a.check:
        if not OUT.exists() or OUT.read_text(encoding="utf-8") != text:
            sys.exit("src/mono_glyphs.h is out of date: run python scripts/gen_mono_glyphs.py")
        print("mono_glyphs.h is up to date")
    else:
        OUT.write_text(text, encoding="utf-8", newline="\n")
        print(text.splitlines()[1])

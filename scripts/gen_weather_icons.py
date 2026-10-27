"""Generate src/weather_icons.h: Erik Flowers' Weather Icons as compact polygons.

    python scripts/gen_weather_icons.py

Downloads the SVGs the WMO mapping needs (pinned commit, cached in .pio/weather-icons),
flattens each glyph's path into polygons and stores the points as bytes: one unit is
1/8 of the icons' 30x30 viewBox, 0xFF ends a ring. faces.cpp fills them with the path
filler (non-zero rule, anti-aliased) at any size.

The WMO code -> icon name table follows getWeatherIconName() from the app this firmware
pairs with: "wi-day-<name>" / "wi-night-<name>", or "wi-<name>" for the four icons that
have no day/night variant. The set has no "wi-day-clear"; it is drawn with wi-day-sunny.

Weather Icons by Erik Flowers (https://github.com/erikflowers/weather-icons), SIL OFL 1.1.
"""
import math
import os
import re
import urllib.request

COMMIT = "bb80982bf1f43f2d57f9dd753e7413bf88beb9ed"
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CACHE = os.path.join(ROOT, ".pio", "weather-icons")
OUT = os.path.join(ROOT, "src", "weather_icons.h")
FLAT = 0.05   # curve flattening tolerance, viewBox units
SIMPLIFY = 0.08   # Douglas-Peucker tolerance, viewBox units
Q = 8   # stored units per viewBox unit (30 * 8 = 240 fits a byte)

# WMO code -> icon name.
WMO = {
    0: 'clear', 1: 'clear', 2: 'cloudy', 3: 'cloudy', 4: 'fog', 5: 'fog', 10: 'fog', 11: 'fog', 12: 'lightning',
    18: 'strong-wind', 20: 'fog', 21: 'rain-mix', 22: 'rain-mix', 23: 'rain', 24: 'snow', 25: 'hail', 26: 'thunderstorm',
    27: 'dust', 28: 'dust', 29: 'dust', 30: 'fog', 31: 'fog', 32: 'fog', 33: 'fog', 34: 'fog', 35: 'fog',
    40: 'rain-mix', 41: 'sprinkle', 42: 'rain', 43: 'sprinkle', 44: 'rain', 45: 'hail', 46: 'hail', 47: 'snow', 48: 'snow',
    50: 'sprinkle', 51: 'sprinkle', 52: 'rain', 53: 'rain', 54: 'snowflake-cold', 55: 'snowflake-cold',
    56: 'snowflake-cold', 57: 'sprinkle', 58: 'rain', 60: 'sprinkle', 61: 'sprinkle', 62: 'rain', 63: 'rain',
    64: 'hail', 65: 'hail', 66: 'hail', 67: 'rain-mix', 68: 'rain-mix', 70: 'snow', 71: 'snow', 72: 'snow', 73: 'snow',
    74: 'snowflake-cold', 75: 'snowflake-cold', 76: 'snowflake-cold', 77: 'snow', 78: 'snowflake-cold', 80: 'rain',
    81: 'sprinkle', 82: 'rain', 83: 'rain', 84: 'storm-showers', 85: 'rain-mix', 86: 'rain-mix', 87: 'rain-mix',
    89: 'hail', 90: 'lightning', 91: 'storm-showers', 92: 'thunderstorm', 93: 'thunderstorm', 94: 'lightning',
    95: 'thunderstorm', 96: 'thunderstorm', 99: 'tornado',
}
AGNOSTIC = ['strong-wind', 'dust', 'snowflake-cold', 'tornado']   # one icon, no day/night variant
BASES = sorted(set(WMO.values()) - set(AGNOSTIC)) + AGNOSTIC
FILE = {'day-clear': 'day-sunny'}


def files():
    out = []
    for b in BASES:
        out += [b] if b in AGNOSTIC else ['day-' + b, 'night-' + b]
    return out


def fetch(name):
    path = os.path.join(CACHE, "wi-%s.svg" % name)
    if not os.path.isfile(path):
        os.makedirs(CACHE, exist_ok=True)
        url = "https://raw.githubusercontent.com/erikflowers/weather-icons/%s/svg/wi-%s.svg" % (COMMIT, name)
        with urllib.request.urlopen(url) as r, open(path, "wb") as f:
            f.write(r.read())
    with open(path, encoding="utf-8") as f:
        svg = f.read()
    paths = re.findall(r'\sd="([^"]+)"', svg)
    assert len(paths) == 1 and "transform" not in svg and 'viewBox="0 0 30 30"' in svg, name
    return paths[0]


def cubic(p0, p1, p2, p3, out):
    """Flatten a cubic Bezier by subdivision until its control points are within FLAT."""
    def flat(a, b, c, d):
        ux, uy = 3 * b[0] - 2 * a[0] - d[0], 3 * b[1] - 2 * a[1] - d[1]
        vx, vy = 3 * c[0] - a[0] - 2 * d[0], 3 * c[1] - a[1] - 2 * d[1]
        return max(ux * ux, vx * vx) + max(uy * uy, vy * vy) <= 16 * FLAT * FLAT
    stack = [(p0, p1, p2, p3)]
    while stack:
        a, b, c, d = stack.pop()
        if flat(a, b, c, d):
            out.append(d)
            continue
        ab = mid(a, b); bc = mid(b, c); cd = mid(c, d); abc = mid(ab, bc); bcd = mid(bc, cd); m = mid(abc, bcd)
        stack.append((m, bcd, cd, d))
        stack.append((a, ab, abc, m))


def mid(a, b):
    return ((a[0] + b[0]) / 2, (a[1] + b[1]) / 2)


def rings(d):
    """SVG path data (M L H V C S Z, absolute and relative) -> list of point rings."""
    toks = re.findall(r"[A-Za-z]|-?(?:\d+\.?\d*|\.\d+)(?:e-?\d+)?", d)
    i, cmd, cur, start, last = 0, None, (0.0, 0.0), (0.0, 0.0), None
    out, ring = [], []

    def nums(n):
        nonlocal i
        v = [float(t) for t in toks[i:i + n]]
        i += n
        return v
    while i < len(toks):
        if toks[i].isalpha():
            cmd = toks[i]; i += 1
        c, rel = cmd.lower(), cmd.islower()
        ox, oy = cur if rel else (0.0, 0.0)
        if c == 'm':
            if len(ring) > 2: out.append(ring)
            x, y = nums(2); cur = start = (x + ox, y + oy); ring = [cur]
            cmd = 'l' if rel else 'L'; last = None
        elif c == 'l':
            x, y = nums(2); cur = (x + ox, y + oy); ring.append(cur); last = None
        elif c == 'h':
            cur = (nums(1)[0] + ox, cur[1]); ring.append(cur); last = None
        elif c == 'v':
            cur = (cur[0], nums(1)[0] + oy); ring.append(cur); last = None
        elif c in 'cs':
            if c == 'c':
                x1, y1, x2, y2, x, y = nums(6); p1 = (x1 + ox, y1 + oy)
            else:
                x2, y2, x, y = nums(4)
                p1 = (2 * cur[0] - last[0], 2 * cur[1] - last[1]) if last else cur
            p2, p3 = (x2 + ox, y2 + oy), (x + ox, y + oy)
            cubic(cur, p1, p2, p3, ring); cur, last = p3, p2
        elif c == 'z':
            if len(ring) > 2: out.append(ring)
            ring, cur, last = [start], start, None
        else:
            raise ValueError("unsupported path command " + cmd)
    if len(ring) > 2: out.append(ring)
    return out


def simplify(pts):
    """Douglas-Peucker on a closed ring."""
    if len(pts) < 4: return pts
    def dp(a, b):
        (ax, ay), (bx, by) = pts[a], pts[b]
        L = math.hypot(bx - ax, by - ay) or 1e-9
        best, k = -1, -1
        for j in range(a + 1, b):
            dist = abs((bx - ax) * (ay - pts[j][1]) - (ax - pts[j][0]) * (by - ay)) / L
            if dist > best: best, k = dist, j
        return dp(a, k)[:-1] + dp(k, b) if best > SIMPLIFY else [pts[a], pts[b]]
    far = max(range(len(pts)), key=lambda j: math.hypot(pts[j][0] - pts[0][0], pts[j][1] - pts[0][1]))
    return dp(0, far)[:-1] + dp(far, len(pts) - 1)[:-1]


def encode(d):
    data, npts, nrings = [], 0, 0
    for ring in rings(d):
        q = []
        for x, y in simplify(ring):
            p = (min(254, max(0, round(x * Q))), min(254, max(0, round(y * Q))))
            if not q or q[-1] != p: q.append(p)
        if len(q) > 1 and q[0] == q[-1]: q.pop()
        if len(q) < 3: continue
        for p in q: data += p
        data.append(0xFF); npts += len(q); nrings += 1
    return data, npts, nrings


def main():
    names = files()
    blobs, starts, report = [], [0], []
    for n in names:
        data, npts, nrings = encode(fetch(FILE.get(n, n)))
        blobs += data; starts.append(len(blobs)); report.append((n, npts, nrings))
    first, k = [], 0
    for b in BASES:
        first.append(k); k += 1 if b in AGNOSTIC else 2
    wmo = [BASES.index(WMO.get(c, 'clear')) for c in range(100)]
    L = ["#pragma once",
         "// Generated by scripts/gen_weather_icons.py. Do not edit.",
         "// Weather Icons by Erik Flowers, https://github.com/erikflowers/weather-icons",
         "// (commit %s), licensed under the SIL Open Font License 1.1." % COMMIT[:12],
         "// Glyphs as polygons: x, y bytes in 1/%d of the 30x30 viewBox, 0xFF ends a ring." % Q,
         "#include <Arduino.h>", "",
         "constexpr int WI_BASES = %d, WI_ICONS = %d, WI_AGNOSTIC = %d;   // the last bases have one icon"
         % (len(BASES), len(names), len(AGNOSTIC)),
         "constexpr int WI_UNIT = %d;" % Q,
         "// Base names; \"wi-day-\" / \"wi-night-\" / \"wi-\" + base is the icon name.",
         "const char WI_BASE_NAMES[] PROGMEM = \"%s\";" % "\\0".join(BASES),
         "// The first icon of each base (day; night follows unless the base is agnostic).",
         "const uint8_t WI_FIRST[WI_BASES] PROGMEM = {%s};" % ", ".join(map(str, first)),
         "// WMO code (0..99) -> base; codes the table leaves out are 'clear'.",
         "const uint8_t WI_WMO[100] PROGMEM = {%s};" % ", ".join(map(str, wmo)),
         "const uint16_t WI_START[WI_ICONS + 1] PROGMEM = {%s};" % ", ".join(map(str, starts)),
         "const uint8_t WI_DATA[] PROGMEM = {"]
    for n, a, b in zip(names, starts, starts[1:]):
        row = blobs[a:b]
        L.append("  // wi-%s" % n)
        for j in range(0, len(row), 24):
            L.append("  " + ",".join(map(str, row[j:j + 24])) + ",")
    L.append("};")
    text = "\r\n".join(L) + "\r\n"
    with open(OUT, "w", encoding="utf-8", newline="") as f:
        f.write(text)
    for n, p, r in report: print("  wi-%-22s %4d points %3d rings" % (n, p, r))
    print("%s: %d icons, %d bytes of points, max %d points / %d rings per icon"
          % (os.path.relpath(OUT, ROOT), len(names), len(blobs), max(p for _, p, _ in report), max(r for _, _, r in report)))


if __name__ == "__main__":
    main()

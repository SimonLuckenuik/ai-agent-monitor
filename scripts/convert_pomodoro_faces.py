"""Converts the smalltv-pomodoro faces to this clock's face format (spec 026).

    python scripts/convert_pomodoro_faces.py <their faces/ folder>

smalltv-pomodoro (https://github.com/davicorrea0/smalltv-pomodoro, by davicorrea0,
GPL-3.0 like this project) is firmware for the GeekMagic SmallTV Ultra. Its faces are
layers drawn by its smalltv-pomodoro/facepack.cpp; this script maps each layer to our
items (the table is in specs/026-face-gallery/RESEARCH.md):

  text     their pixel font -> "big" at the same height (fonts 2 and 4 for lower case);
           their digit fonts -> "big", fitted to their width
  rect, circle, line, bar -> the same; arc -> ring; hand -> a line by sin/cos;
  ticks -> a repeat of lines; weather -> icon {now.icon}; sprite -> sprite, animated
  mascot -> their tomato sprite, bouncing; plant -> a small plant sprite
  when     -> show conditions on our placeholders

No pomodoro here: their focus count and daily goal become the variables done and goal
(tasks done today), labelled TASKS. Faces over 4 KB or 32 items are made smaller: a
repeated sprite becomes a repeat, long animations keep every other frame, and a few
scenes are reworked by hand (TWEAKS). Writes examples/faces/smalltv-pomodoro/<name>.json
and its index, faces.json.
"""
import json
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
OUT = ROOT / "examples" / "faces" / "smalltv-pomodoro"
UPSTREAM = "https://github.com/davicorrea0/smalltv-pomodoro"
UPSTREAM_COMMIT = "2f6836d646c59b1288a8bfafe9841317eb34a363"

# Their id -> ours: English, and not one of ours already.
NAMES = {
    "8bit-verde": "8-bit-green", "aneis": "rings", "aquario": "aquarium-reef", "arcade": "arcade",
    "astronauta": "astronaut", "aurora": "aurora", "bauhaus": "bauhaus-blocks", "bebas-gigante": "giant-time",
    "bento": "bento", "brutal": "brutal", "cafe-pixel": "pixel-cafe", "compasso": "compass", "duna": "dune",
    "duo": "duo", "estacao": "station", "fitness": "fitness", "fogueira": "campfire", "gato-soneca": "catnap",
    "gradiente": "gradient", "infografico": "infograph", "inverno": "winter", "janela": "window",
    "jardim": "greenhouse", "koi": "koi", "listras": "stripes", "mare": "tide", "memphis": "memphis",
    "metro": "metro", "modular": "modular", "mono": "mono", "nixie": "nixie", "orbita": "orbit-sun",
    "papel": "paper", "pausa-cafe": "coffee-break", "pilula": "pill", "radar-analogico": "radar",
    "romano": "roman", "synthwave": "synthwave-sun", "terminal": "terminal-green", "tomate-pulando": "jumping-tomato",
    "vinil": "vinyl",
}
# Left out: too close to one of ours or to each other (the owner's review, 2026-11-01).
SKIP = {"aquario", "orbita", "terminal", "bebas-gigante", "duo", "mono", "estacao", "romano"}

# Each in one of our gallery's categories (examples/faces/README.md's sections).
CATEGORY = {
    "Clocks and calendars": ["aurora", "bauhaus-blocks", "bento", "brutal", "compass", "duo", "giant-time",
                             "gradient", "infograph", "memphis", "modular", "mono", "nixie", "orbit-sun",
                             "paper", "pill", "radar", "roman", "station", "stripes", "synthwave-sun",
                             "terminal-green", "vinyl"],
    "Weather, sun and moon": ["dune", "tide", "window", "winter"],
    "Agents and work": ["fitness", "greenhouse", "metro", "rings"],
    "Games and worlds": ["8-bit-green", "aquarium-reef", "arcade", "astronaut", "campfire", "catnap",
                         "coffee-break", "jumping-tomato", "koi", "pixel-cafe"],
}
CATEGORY_OF = {name: cat for cat, names in CATEGORY.items() for name in names}

# English titles and descriptions where theirs are only in Portuguese.
TITLES = {"bebas-gigante": ("Giant Time", "The time huge, with the weather and a task bar"),
          "cafe-pixel": ("Pixel Cafe", "A pixel-art cafe table: a window that follows the day, and the tomato"),
          "radar-analogico": ("Radar", "A night-blue analog dial with a seconds hand"),
          "tomate-pulando": ("Jumping Tomato", "A pixel-art tomato that jumps and blinks beside the time")}

NAMED = {"bg": "#000000", "card": "#16161a", "card2": "#2c2c32", "text": "#f2f2f2", "muted": "#7d7d87",
         "dim": "#1e1e22", "work": "#ff5a47", "short": "#3ecf7a", "long": "#4f8dff", "sun": "#ffc428",
         "cloud": "#d6dce6", "rain": "#5aaaff", "accent": "@time"}
DEFAULT = {"rect": "card", "circle": "card", "bar": "work", "arc": "work"}

RAIN = "(({now.code}>=51&&{now.code}<=67)||({now.code}>=80&&{now.code}<=82)||{now.code}>=95)"
WHEN = {"day": "{sun.up}", "night": "!{sun.up}", "rain": RAIN, "dry": "!" + RAIN,
        "goal": False, "nogoal": None, "streak": False,   # no goals or streaks here: dropped / always
        "weather": None, "always": None, "noweather": False}
TEXT = {"HH": "{time.hour:02}", "H": "{time.hour}", "MM": "{time.min}", "SS": "{time.sec}",
        "WD": "{date.wday3}", "DD": "{date.day:02}", "MON": "{date.month3:u}", "MO": "{date.monthnum:02}",
        "YYYY": "{date.year}", "AMPM": "{time.ampm}", "TEMP": "{now.temp}", "TMIN": "{day0.low}",
        "TMAX": "{day0.high}", "HUM": "{now.hum}", "UNIT": "{units.temp}", "WX": "{now.text:u}",
        "CITY": "{loc.city:u}", "IP": "{net.ip}", "FOCOS": "{done|0}", "META": "{goal|8}",
        "STREAK": "{streak|0}", "BEST": "{best|0}", "RAIN": ""}
VALUE = {"sec": ("{time.sec}", 60), "min": ("{time.min}", 60), "hour": ("{time.hour}", 24),
         "focos": ("{time.daymin}", 1440), "hum": ("{now.hum}", 100)}   # their focus count: the day's progress
# Their digit fonts' advance, per unit of height: (a digit, a colon); dm's cell is 1/7 of the height.
WIDTH = {"bebas": (0.42, 0.2), "outfit": (0.6, 0.26), "righteous": (0.62, 0.26), "geo": (0.75, 0.3),
         "dseg": (0.72, 0.25), "dm": (6 / 7, 3 / 7)}
LABELS = [(r"tomate@smalltv", "agent@monitor"), (r"\bSMALLTV\b", "MONITOR"), (r"\bONE FOCUS\b", "ONE TASK"), (r"\bone focus\b", "one task"), (r"\bSESSIONS\b", "TASKS"), (r"\bsessions\b", "tasks"), (r"\bFOCUS\b", "TASKS"),
          (r"\bFocus\b", "Tasks"), (r"\bfocus\b", "tasks"), (r"\bFOCOS\b", "TASKS"), (r"\bfocos\b", "tasks")]
ANGLE = {"hour": "(({time.hour}%12)*30+{time.min}/2)", "min": "({time.min}*6)", "sec": "({time.sec}*6)"}

# Their tomato (the "mascot"), from tomate-pulando.json: four frames, 16 x 16.
TOMATO = None   # their tomato sprite, read from tomate-pulando.json by main()
PLANT = {"pal": {"g": "#4caf50", "G": "#2e7d32", "p": "#b5651d", "P": "#7a3f12"}, "fps": 2, "frames": [[
    "................", "......gg........", ".....gGGg.......", "....gGGGGg..gg..", "...gGG.GGGggGGg.",
    "...gG...GGGGGg..", "....g....GGg....", ".........G......", "....gg...G......", "...gGGg..G......",
    "....gGGggG......", "......gGGG......", "...pppppppppp...", "....pPPPPPPp....", "....pPPPPPPp....",
    ".....pppppp....."]]}


def colour(v, kind=None):
    if v is None:
        return NAMED[DEFAULT[kind]] if kind in DEFAULT else NAMED["text"]
    if v == "none":
        return None
    return NAMED.get(v, v)


PCT = "{round({time.daymin}*100/1440)}%"   # the day's progress
SLOGANS = {"ONE STEP AT A TIME", "TIME FOR WHAT MATTERS", "BREATHE. ONE FOCUS AT A TIME.", "KEEP MOVING FORWARD",
           "AT YOUR OWN PACE", "THE TIME IS NOW.", "YOU HAVE ARRIVED!", "LEVEL COMPLETE!"}


def keep_text(v):
    """False for a slogan, or a text that counts focus sessions, goals or streaks."""
    t = (v.get("en") or v.get("pt") or "") if isinstance(v, dict) else v
    if t.strip() in SLOGANS:
        return False
    if re.fullmatch(r"(FOCUS|FOCOS|TASKS) \{FOCOS\}/\{META\}", t.strip()):
        return True   # a bar's label: it becomes DAY
    if re.sub(TASK_PART, "", t).strip() and re.search(r"\{(?!FOCOS|META)[A-Z]+\}", t):
        return True   # "{TEMP}° {FOCOS}/{META}": the temperature stays (text_of drops the rest)
    return t.strip() == "{FOCOS}" or not re.search(r"\{(FOCOS|META|STREAK|BEST)\}", t)


TASK_PART = r"\s*\{FOCOS\}/\{META\}(\s*(focos|done|tasks))?"


def text_of(v):
    if isinstance(v, dict):
        v = v.get("en") or v.get("pt") or ""
    if v.strip() == "{FOCOS}":
        return PCT
    if re.fullmatch(r"(FOCUS|FOCOS|TASKS) \{FOCOS\}/\{META\}", v.strip()):
        return "DAY"
    if v.strip() in ("FOCUS", "FOCOS", "TASKS", "SESSIONS", "DAILY GOAL", "META DO DIA"):
        return "DAY"
    v = {"LINE 01 / FOCUS": "LINE 01 / DAY", "START": "00:00", "GOAL": "24:00"}.get(v.strip(), v)
    v = re.sub(TASK_PART, "", v)   # a count of focus sessions beside other values
    v = re.sub(r"\{([A-Z]+)\}", lambda m: TEXT.get(m.group(1), m.group(0)), v)
    for a, b in LABELS:   # no pomodoro here: their focus sessions are tasks done
        v = re.sub(a, b, v)
    return v


def frame_expr(n, fps):
    if n <= 1:
        return None
    return f"(({{time.sec}}*1000+{{time.ms}})*{fps}/1000)%{n}"


def convert(src):
    face = json.loads(src.read_text(encoding="utf-8"))
    sprites = face.get("sprites", {})
    items, fps = [], 1

    def show(l):
        w = WHEN[l.get("when", "always")]
        return w

    for l in face["layers"]:
        t, sz = l["type"], l.get("size")
        it = None
        cond = show(l)
        if cond is False:
            continue
        if t == "rect":
            it = {"type": "rect", "x": l.get("x", 0), "y": l.get("y", 0), "w": l.get("w", 0), "h": l.get("h", 0)}
            if l.get("r"):
                it["r"] = l["r"]
            c, s = colour(l.get("color"), "rect"), l.get("stroke") or l.get("color2")
            if c:
                it["color"] = c
            else:
                it["fill"] = False
            if s and s != "none":
                if c:
                    items.append(dict(it))
                it.update({"fill": False, "color": colour(s), "bw": 1})
        elif t == "circle":
            it = {"type": "circle", "x": l.get("x", 0), "y": l.get("y", 0), "r": l.get("r", 0)}
            c, s = colour(l.get("color"), "circle"), l.get("stroke") or l.get("color2")
            if c:
                it["color"] = c
            if s and s != "none":
                if c:
                    one = dict(it)
                    if cond:
                        one["show"] = cond
                    items.append(one)
                it.update({"fill": False, "color": colour(s), "bw": 1})
            elif not c:
                continue
        elif t == "line":
            it = {"type": "line", "x": l.get("x", 0), "y": l.get("y", 0), "x2": l.get("x2", 0), "y2": l.get("y2", 0),
                  "w": sz or 3, "color": colour(l.get("color"))}
        elif t == "text":
            if not keep_text(l.get("text", "")):
                continue
            font, size = l.get("font", "pixel"), sz or 2
            h = 8 * size if font == "pixel" else 7 * size if font == "dm" else size
            al = {"left": "tl", "center": "tc", "right": "tr"}[l.get("align", "left")]
            txt = text_of(l.get("text", ""))
            it = {"type": "text", "x": l.get("x", 0), "y": l.get("y", 0), "text": txt, "font": "big",
                  "size": h, "align": al, "color": colour(l.get("color"))}
            if font == "pixel" and size in (2, 3) and "°" not in txt:   # fonts 2 and 4 have lower case and symbols
                it["font"] = 2 if size == 2 else 4
                del it["size"]
            elif font in ("bebas", "outfit", "dseg", "dm"):   # the clock has these (spec 031)
                it["font"] = {"bebas": "bebas", "outfit": "outfit", "dseg": "seg7", "dm": "pixel"}[font]
                off = l.get("off") or l.get("color2")
                if font in ("dseg", "dm") and off and off != "none":
                    it["off"] = colour(off)
            elif font != "pixel":   # Geo, Righteous: the line font, as wide as theirs
                sample = re.sub(r"\{[^}]*\}", "00", txt)
                digit, colon = WIDTH[font]
                w = sum(colon if ch in ":." else digit for ch in sample) * (size if font != "dm" else 7 * size)
                x = l.get("x", 0)
                room = 2 * min(x, 240 - x) if al == "tc" else 240 - x if al == "tl" else x
                it["w"] = round(min(room - 6, w * (1.35 if font == "bebas" else 1.1)))
            if l.get("glow"):   # their halo: the same text behind, a little larger, in the glow colour
                g = {**it, "color": colour(l["glow"])}
                if "size" in g:
                    g["size"] = g["size"] + 2 * l.get("spread", 2)
                    g["y"] = it["y"] - l.get("spread", 2)
                    if "w" in g:
                        g["w"] = g["w"] + 2 * l.get("spread", 2)
                if l.get("pulse"):   # their halo breathes: here it comes and goes
                    g["show"] = "({time.sec}*1000+{time.ms})%" + str(l["pulse"]) + "<" + str(l["pulse"] // 2)
                    fps = max(fps, 4)
                items.append(g)
        elif t == "bar":
            v, mx = VALUE[l.get("value", "sec")]
            it = {"type": "bar", "x": l.get("x", 0), "y": l.get("y", 0), "w": l.get("w", 100), "h": l.get("h", 8),
                  "value": v, "max": l.get("max") or mx, "color": colour(l.get("color"), "bar")}
            tr = l.get("track") or l.get("color2")
            if tr and tr != "none":
                it["track"] = colour(tr)
        elif t == "arc":
            v, mx = VALUE[l.get("value", "sec")]
            it = {"type": "ring", "x": l["x"], "y": l["y"], "r": l.get("r", 50), "w": sz or 10, "value": v,
                  "max": l.get("max") or mx, "color": colour(l.get("color"), "arc")}
            tr = l.get("track") or l.get("color2")
            if tr and tr != "none":
                it["track"] = colour(tr)
        elif t == "hand":
            a, ln, tail = ANGLE[l.get("value", "sec")], l.get("len", 60), l.get("tail", 0)
            x, y = l["x"], l["y"]
            it = {"type": "line", "x": f"{x}-{tail}*sin{a}", "y": f"{y}+{tail}*cos{a}",
                  "x2": f"{x}+{ln}*sin{a}", "y2": f"{y}-{ln}*cos{a}", "w": sz or 3, "color": colour(l.get("color"))}
            if l.get("value") == "sec":
                fps = max(fps, 1)
        elif t == "ticks":
            n, r, ln, x, y = l.get("count", 12), l.get("r", 100), l.get("len", 8), l["x"], l["y"]
            a = f"({{it.index}}*{360 / n:g})"
            it = {"type": "repeat", "of": n, "items": [{
                "type": "line", "x": f"{x}+{r - ln}*sin{a}", "y": f"{y}-{r - ln}*cos{a}",
                "x2": f"{x}+{r}*sin{a}", "y2": f"{y}-{r}*cos{a}", "w": sz or 3, "color": colour(l.get("color"))}]}
        elif t == "weather":
            s = (sz or 10) / 10
            it = {"type": "icon", "x": l["x"], "y": l["y"], "size": round(48 * s), "icon": "{now.icon}"}
            if l.get("bg") and l["bg"] != "none":
                it["bg"] = colour(l["bg"])
        elif t in ("sprite", "mascot", "plant"):
            spr = sprites[l["sprite"]] if t == "sprite" else TOMATO if t == "mascot" else PLANT
            frames = spr.get("frames") or [spr["px"]]
            it = {"type": "sprite", "x": l.get("x", 0), "y": l.get("y", 0), "scale": sz or 1,
                  "palette": {k: colour(v) for k, v in spr["pal"].items()}}
            f = frame_expr(len(frames), spr.get("fps", 4))
            if f:
                it["frames"], it["frame"], it["_fps"] = frames, f, spr.get("fps", 4)
                fps = max(fps, min(20, spr.get("fps", 4)))
            else:
                it["rows"] = frames[0]
        else:
            raise ValueError(f"{src.name}: layer type {t}")
        if cond:
            it["show"] = cond
        items.append(it)
    items = TWEAKS.get(src.stem, lambda i: i)(items)
    items = merge_sprites(items)
    if len(items) > 32:
        items = merge_lines(items)
    out = {"bg": colour(face.get("bg", "#000000")) or "#000000", "items": items}
    if fps > 1:
        out["fps"] = fps
    shrink(out)
    return face, out


T = "({time.sec}*1000+{time.ms})"   # milliseconds within the minute


def aquarium(items):
    """The water's eight bands become one gradient; the clownfish swims back and forth."""
    water = {"type": "rect", "x": 0, "y": 0, "w": 240, "h": 200,
             "color": {"stops": [items[0]["color"], items[7]["color"]]}}
    items = [water] + items[8:]
    for it in items:
        if it["type"] == "sprite" and it["x"] == 140 and it["y"] == 100:   # the clownfish
            it["x"] = f"120+20*sin({T}*0.012)"
    return items


def koi(items):
    """One koi circling the pond instead of two still ones."""
    fish = [i for i, it in enumerate(items) if it["type"] == "sprite" and (it["x"], it["y"]) in ((52, 96), (133, 146))]
    first = items[fish[0]]
    first["x"] = f"72+38*cos({T}*0.006)"
    first["y"] = f"110+18*sin({T}*0.006)"
    return [it for i, it in enumerate(items) if i != fish[1]]


def compass(items):
    """A thin seconds hand over the others, under the centre dot."""
    a = ANGLE["sec"]
    hand = {"type": "line", "x": f"120-12*sin{a}", "y": f"119+12*cos{a}", "x2": f"120+66*sin{a}",
            "y2": f"119-66*cos{a}", "w": 1, "color": "#b86b43"}
    dot = next(i for i, it in enumerate(items) if it["type"] == "circle" and it.get("r") == 5)
    return items[:dot] + [hand] + items[dot:]


def gradient(items):
    """Their 11 bands, 15 px apart, as 34 rings 5 px apart in between colours."""
    bands = [it for it in items if it["type"] == "circle" and it.get("r", 0) >= 20]
    cols = [tuple(int(b["color"][k:k + 2], 16) for k in (1, 3, 5)) for b in bands]
    steps = []
    for i in range(34):
        f = i / 33 * (len(cols) - 1)
        a, b, t = cols[int(f)], cols[min(int(f) + 1, len(cols) - 1)], f - int(f)
        steps.append("#%02x%02x%02x" % tuple(round(a[k] + (b[k] - a[k]) * t) for k in range(3)))
    rings = {"type": "repeat", "of": 34, "items": [   # each edge blends into the ring outside it
        {"type": "circle", "x": 120, "y": 120, "r": "170-{it.index}*5", "color": "{it.index:ring}",
         "under": "{it.index:outer}"}]}
    rest = [it for it in items if it not in bands]
    maps = {"ring": {str(i): c for i, c in enumerate(steps)},
            "outer": {str(i): steps[i - 1] if i else steps[0] for i in range(34)}}
    return [{"_maps": maps}, rings] + rest


def fitness(items):
    """Their focus row became a second day row: only their own day row stays."""
    bar = next(i for i, it in enumerate(items) if it["type"] == "bar" and it.get("max") == 1440)
    label = next(i for i, it in enumerate(items) if it["type"] == "text" and it.get("text") == "DAY")
    return [it for i, it in enumerate(items) if i not in (bar, label)]


TWEAKS = {"aquario": aquarium, "koi": koi, "compasso": compass, "gradiente": gradient, "fitness": fitness}
# Descriptions that spoke of focus sessions, goals or streaks.
DESCS = {"aneis": "Three progress rings: the day, the minutes of the hour and the hours of the day",
         "arcade": "A pixel platform level, with a hero, coins and a flag",
         "bento": "Widget-style grid of cards: time, weather and the day's progress",
         "jardim": "A little greenhouse with a growing plant",
         "koi": "A pond from above, with a koi circling between the lily pads",
         "mare": "Pixel lighthouse, ocean bands and the day's progress on the sand",
         "metro": "A transit board whose route fills as the day goes by"}


def merge_sprites(items):
    """The same sprite placed several times becomes one repeat, its places from two maps."""
    pre = items[0]["_maps"] if items and "_maps" in items[0] else {}   # a tweak's own maps
    items = items[1:] if pre else items
    groups = {}
    for i, it in enumerate(items):
        if it["type"] == "sprite":
            key = json.dumps({k: v for k, v in it.items() if k not in ("x", "y")}, sort_keys=True)
            groups.setdefault(key, []).append(i)
    out, maps, done = [], dict(pre), set()
    for i, it in enumerate(items):
        if i in done:
            continue
        key = json.dumps({k: v for k, v in it.items() if k not in ("x", "y")}, sort_keys=True) if it["type"] == "sprite" else None
        same = groups.get(key, [i]) if key else [i]
        if len(same) < 2:
            out.append(it)
            continue
        m = len(maps) // 2
        maps[f"sx{m}"] = {str(k): items[j]["x"] for k, j in enumerate(same)}
        maps[f"sy{m}"] = {str(k): items[j]["y"] for k, j in enumerate(same)}
        one = {**it, "x": f"{{it.index:sx{m}}}", "y": f"{{it.index:sy{m}}}"}
        rep = {"type": "repeat", "of": len(same), "items": [one]}
        if "show" in one:
            rep["show"] = one.pop("show")
        out.append(rep)
        done.update(same)
    out_items = out
    if maps:
        out_items = [{"_maps": maps}] + out
    return out_items


def merge_lines(items):
    """Over the 32 items: plain lines of one colour and width become one repeat each, at the
    first one's place, their ends from maps."""
    maps = items[0]["_maps"] if items and "_maps" in items[0] else {}
    body = items[1:] if maps else items
    groups = {}
    for i, it in enumerate(body):
        if it["type"] == "line" and "show" not in it and all(isinstance(it[k], int) for k in ("x", "y", "x2", "y2")):
            groups.setdefault((it["color"], it["w"]), []).append(i)
    out, done = [], set()
    for i, it in enumerate(body):
        if i in done:
            continue
        g = groups.get((it.get("color"), it.get("w"))) if it["type"] == "line" and i in groups.get((it.get("color"), it.get("w")), []) else None
        if not g or len(g) < 2:
            out.append(it)
            continue
        m = len(maps)
        one = {"type": "line", "w": it["w"], "color": it["color"]}
        for k in ("x", "y", "x2", "y2"):
            maps[f"l{m}{k}"] = {str(n): body[j][k] for n, j in enumerate(g)}
            one[k] = f"{{it.index:l{m}{k}}}"
        out.append({"type": "repeat", "of": len(g), "items": [one]})
        done.update(g)
    return ([{"_maps": maps}] if maps else []) + out


def sprites_of(out):
    for it in out["items"]:
        if it.get("type") == "sprite":
            yield it
        for sub in it.get("items", []):
            if sub.get("type") == "sprite":
                yield sub


def finish(out):
    if out["items"] and "_maps" in out["items"][0]:
        out["maps"] = out["items"].pop(0)["_maps"]
    for it in sprites_of(out):
        it.pop("_fps", None)


def shrink(out):
    """Over 4 KB: every other frame of the biggest animation, at half the rate (the same speed)."""
    while True:
        probe = json.loads(json.dumps(out))
        finish(probe)
        if len(json.dumps(probe, separators=(",", ":"), ensure_ascii=False).encode()) <= 4096:
            break
        anim = [s for s in sprites_of(out) if len(s.get("frames", [])) > 2]
        if not anim:
            two = [s for s in sprites_of(out) if len(s.get("frames", [])) == 2]
            if not two:
                break
            s = max(two, key=lambda s: len(json.dumps(s["frames"])))
            s["rows"] = s.pop("frames")[0]
            s.pop("frame")
            s["y"] = f'{s["y"]}+2*sin(({{time.sec}}*1000+{{time.ms}})*0.36)'   # one slow bob a second
            out["fps"] = max(out.get("fps", 1), 6)
            continue
        s = max(anim, key=lambda s: len(json.dumps(s["frames"])))
        s["frames"] = s["frames"][::2]
        s["_fps"] = max(1, s["_fps"] / 2)
        s["frame"] = frame_expr(len(s["frames"]), f'{s["_fps"]:g}')
    finish(out)




def main():
    global TOMATO
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    src = pathlib.Path(sys.argv[1])
    TOMATO = json.loads((src / "tomate-pulando.json").read_text(encoding="utf-8"))["sprites"]["tomate"]
    OUT.mkdir(parents=True, exist_ok=True)
    index, bad = [], []
    for old in OUT.glob("*.json"):   # faces since left out
        if old.name != "faces.json" and old.stem not in {NAMES[k] for k in NAMES if k not in SKIP}:
            old.unlink()
    for p in sorted(src.glob("*.json")):
        if p.name == "catalog.json" or p.stem in SKIP:
            continue
        face, out = convert(p)
        name = NAMES[p.stem]
        body = json.dumps(out, separators=(",", ":"), ensure_ascii=False)
        size = len(body.encode())
        if size > 4096 or len(out["items"]) > 32:
            bad.append((name, size, len(out["items"])))
        (OUT / f"{name}.json").write_text(body + "\n", encoding="utf-8", newline="\n")
        title, desc = TITLES.get(p.stem) or (face.get("name_en") or face["name"], face.get("desc_en") or face["desc"])
        desc = DESCS.get(p.stem, desc)
        for a, b in LABELS:
            desc = re.sub(a, b, desc)
        index.append({"name": name, "title": title, "description": desc, "category": CATEGORY_OF[name],
                      "author": face.get("author", "davicorrea0"),
                      "upstream": f"{UPSTREAM}/blob/{UPSTREAM_COMMIT}/faces/{p.name}", "license": "GPL-3.0"})
        print(f"{name:16} {size:5} B {len(out['items']):3} items  {title}")
    (OUT / "faces.json").write_text(json.dumps(index, indent=2, ensure_ascii=False) + "\n", encoding="utf-8", newline="\n")
    if bad:
        sys.exit(f"over 4 KB or 32 items: {bad}")
    print(len(index), "faces, all within 4 KB and 32 items")


if __name__ == "__main__":
    main()

"""Integration tests: the firmware in the emulator, driven over its real HTTP API.

    python tests/integration/run_integration_tests.py            # build, start, test, stop
    python tests/integration/run_integration_tests.py --no-build # reuse the last image
    python tests/integration/run_integration_tests.py --keep     # leave it running after
    python tests/integration/run_integration_tests.py -k message # only matching cases

Starts its own emulator (Docker, ports 18000/18080, blank storage), fixes its clock to
Monday 5 October 2026 10:09 EDT and its weather to a known Open-Meteo reply, so every
run draws the same screens. Each case checks the API's answers and saves what the
screen shows to .pio/integration/<case>.png; sheet.png puts them side by side.
screens.json records a digest per screen: a later run lists the screens that changed
(a change is reported, not failed: look at the PNGs and decide).

Needs Docker and Pillow.
"""
import argparse
import json
import sys
import time
import traceback
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from emulator import (FIXED_TIME, ROOT, TZ, Emulator, HttpError, digest, epoch,  # noqa: E402
                      open_meteo, sheet)

OUT = ROOT / ".pio" / "integration"
EXAMPLES = ROOT / "examples" / "faces"
SKILL_EXAMPLES = ROOT / "plugins" / "ai-agent-monitor" / "skills" / "ai-agent-monitor" / "examples"
FACES = {0: "classic", 1: "weather", 2: "photo", 3: "big-date", 4: "simple", 5: "forecast",
         7: "big-time", 8: "big-weather", 9: "agents", 10: "weather-today",
         11: "forecast-day-1", 12: "forecast-day-2", 13: "forecast-day-3"}
ICONS = ["info", "success", "warning", "error", "waiting", "running", "bell", "done"]
DEFAULT_PALETTES = {"day": {"accent1": "#ffa826", "accent2": "#6ab7ff", "accent3": "#4cd9a0", "black": "#000000",
                            "white": "#ffffff", "rain": "#5ab2ff", "green": "#30d158", "orange": "#ff9f0a",
                            "red": "#ff453a", "blue": "#0a84ff"},
                    "night": {"accent1": "#a86a14", "accent2": "#3a6694", "accent3": "#2a7a5c", "black": "#000000",
                              "white": "#8e8e93", "rain": "#2f6496", "green": "#1e7a35", "orange": "#a85f0a",
                              "red": "#a3271f", "blue": "#0a4f99"}}
COLD = open_meteo(temp=-38.4, feels=-51.6, humidity=100, wind=27.8, code=75, is_day=1,
                  city="Iqaluit", days=[
                      (75, -38, -46, -52, 100, 27.8, 100, "10:23", "20:56"),
                      (73, -41, -49, -58, 90, 22.0, 95, "10:25", "20:54"),
                      (3, -36, -44, -50, 10, 16.4, 80, "10:27", "20:52"),
                      (0, -29, -40, -42, 0, 8.0, 70, "10:29", "20:50"),
                  ])

cases = []
screens = {}   # name -> digest


def case(fn):
    cases.append(fn)
    return fn


class Failure(AssertionError):
    pass


def check(cond, what):
    if not cond:
        raise Failure(what)


def expect_error(code, fragment, fn, *args, **kw):
    try:
        fn(*args, **kw)
    except HttpError as e:
        check(e.code == code and fragment in e.text, f"expected HTTP {code} '{fragment}', got {e.code} '{e.text}'")
        return
    raise Failure(f"expected HTTP {code} '{fragment}', got success")


def shot(emu, name, min_lit=400):
    """Saves the settled screen; fails when it is (nearly) blank."""
    img = emu.screenshot(OUT / f"{name}.png")
    lit = sum(1 for p in img.getdata() if max(p) > 24)
    check(lit >= min_lit, f"{name}: screen nearly blank ({lit} lit pixels)")
    screens[name] = digest(img)
    return img


def baseline(emu):
    """The state every case starts from: English, metric, 24 h, no rotation, no night."""
    emu.fix_time(FIXED_TIME)
    emu.fix_weather(open_meteo())
    emu.settings(lang="en", celsius=1, hour12=0, mile=0, nightmode=0, themeInterval=0,
                 brightness=100, palettes=json.dumps(DEFAULT_PALETTES), palette="day", nightpalette="night",
                 roles="{}", order="", customoff="", turns="", orientation=0, gamma="2.2", nightgamma="",
                 nightoff=0, facesapi=1)
    for name in emu.faces()["faces"]:   # no custom faces, variables or messages left from a case
        emu.delete_face(name)
    emu.clear_vars()
    emu.clear_messages()


# ---------------------------------------------------------------- cases
@case
def status_and_fixed_time(emu):
    s = emu.status()
    check(s["version"].startswith("1."), f"version {s['version']}")
    check("emulator" in s["build"], f"build {s['build']}")
    check(s["time"] == "2026-10-05 10:09:00 EDT", f"time {s['time']}")
    check(round(s["weather"]["temp"]) == 18, f"weather {s['weather']}")
    check(s["weather"]["icon"] == "wi-day-cloudy", f"icon {s['weather']['icon']}")
    check(s["weather"]["city"] == "Montreal", f"city {s['weather']['city']}")
    check(s.get("forecastDays") == 4, f"forecastDays {s.get('forecastDays')}")


@case
def settings_round_trip(emu):
    emu.settings(brightness=55, hour12=1, lang="fr")
    c = emu.config()
    check(c["brightness"] == 55 and c["hour12"] in (1, True) and c["lang"] == "fr", f"config {c}")
    check(emu.form("/api/set", {"key": "nosuchkey", "value": "1"}).decode() != "OK",
          "an unknown key is refused")
    emu.settings(brightness=100, hour12=0, lang="en")


@case
def builtin_faces(emu):
    for fid, name in FACES.items():
        emu.show(fid)
        check(emu.status()["face"] == fid, f"face {fid} not showing")
        shot(emu, f"face_{fid:02d}_{name}", min_lit=150 if name in ("photo", "agents") else 400)


@case
def french(emu):
    emu.set("lang", "fr")
    for fid in (0, 1, 3, 10, 11):
        emu.show(fid)
        shot(emu, f"fr_{fid:02d}_{FACES[fid]}")
    emu.set("lang", "en")


@case
def imperial_12h(emu):
    emu.settings(celsius=0, hour12=1, mile=1)
    for fid in (0, 1, 10):
        emu.show(fid)
        shot(emu, f"imperial_{fid:02d}_{FACES[fid]}")
    emu.settings(celsius=1, hour12=0, mile=0)


@case
def extreme_cold(emu):
    """The widest numbers: -NN everywhere, 100 km/h wind, 100% humidity, 4-digit sun times."""
    emu.fix_weather(COLD)
    check(round(emu.status()["weather"]["temp"]) == -38, "cold weather not applied")
    for fid in (1, 5, 8, 10, 11):
        emu.show(fid)
        shot(emu, f"cold_{fid:02d}_{FACES[fid]}")
    emu.fix_weather(open_meteo())


@case
def sun_phases(emu):
    """Weather today shows the next two sun events: before sunrise, by day, after sunset."""
    for label, when, day in (("before_sunrise", epoch(2026, 10, 5, 5, 30), 0),
                             ("afternoon", epoch(2026, 10, 5, 14, 0), 1),
                             ("after_sunset", epoch(2026, 10, 5, 21, 45), 0)):
        emu.fix_time(when)
        emu.fix_weather(open_meteo(is_day=day))
        emu.show(10)
        shot(emu, f"today_{label}")
    emu.fix_time(FIXED_TIME)
    emu.fix_weather(open_meteo())


@case
def night_mode(emu):
    emu.fix_time(epoch(2026, 10, 5, 23, 30))
    emu.settings(nightmode=1, starttime=22, stoptime=7, nightface=7, nightbrightness=10, nighttint=1)
    time.sleep(1.5)
    emu.settle()
    _, backlight = emu.framebuffer()
    check(backlight <= 20, f"backlight {backlight}% at night")
    shot(emu, "night_big_time", min_lit=50)
    emu.set("nightmode", 0)
    emu.fix_time(FIXED_TIME)


@case
def messages_api(emu):
    emu.clear_messages()
    check(emu.message(id="a", title="T" * 60, text="x" * 300, icon="nope", color="red",
                      pct=150, ttl=1) == "OK", "message not stored")
    m = emu.messages()[0]
    check(len(m["title"]) == 40 and len(m["text"]) == 160, "title/text not cut to 40/160")
    check(m["icon"] == "info" and not m.get("color"), f"bad icon/colour kept: {m}")
    check(m["pct"] == 100 and m["ttl"] <= 5, f"pct/ttl not clamped: {m}")
    emu.message(id="a", title="Accents", text="Fete deja prevue: e a c", icon="done")
    check(len(emu.messages()) == 1, "same id should replace")
    expect_error(400, "JSON object", emu.post, "/api/message", "[1,2]")
    emu.request("DELETE", emu.api + "/api/message?id=a")
    check(emu.messages() == [], "delete by id")


@case
def message_layouts(emu):
    texts = {
        "info": ("Heads up", "The nightly build moved to 02:00."),
        "success": ("Tests passed", "179 checks, 0 failures."),
        "warning": ("Disk 91% full", "The build cache needs a clean-up soon."),
        "error": ("Build failed", "3 tests failed in faces.cpp: see the log."),
        "waiting": ("ai-agent-monitor", "Waiting for your approval to push to main."),
        "running": ("Refactor", "Moving the weather code into its own module."),
        "bell": ("Stand-up", "In 5 minutes, room 2."),
        "done": ("Release 1.4.0", "Published with both images and the notes."),
    }
    for icon in ICONS:
        title, text = texts[icon]
        extra = {"pct": 62} if icon == "running" else {}
        emu.message(id=icon, title=title, text=text, icon=icon, ttl=7200, show=True, **extra)
        time.sleep(0.3)
        check(emu.status()["face"] == 15, f"{icon}: the Message face is not showing")
        shot(emu, f"message_{icon}")
    emu.message(id="long", title="A title that is far too long for one line",
                text="A long message wraps to four lines at most and anything past the fourth "
                     "line is cut, so keep it to a sentence; this one goes on and on and on.",
                icon="warning", ttl=7200, show=True)
    shot(emu, "message_long")
    emu.clear_messages()


@case
def agents_board(emu):
    emu.clear_messages()
    emu.show(9)
    shot(emu, "agents_empty", min_lit=100)
    rows = [("api", "api-server", "Waiting for review of PR 42", "waiting", None),
            ("web", "web-app", "Running the end-to-end tests", "running", 45),
            ("fw", "firmware", "Build failed: 2 errors", "error", None),
            ("docs", "docs", "README and screenshots updated", "done", None)]
    for i, (mid, title, text, icon, pct) in enumerate(rows):
        extra = {"pct": pct} if pct is not None else {}
        emu.message(id=mid, title=title, text=text, icon=icon, ttl=7200 + 3600 * i, **extra)
    check(len(emu.messages()) == 4, "four messages")
    emu.show(9)
    shot(emu, "agents_full")
    emu.message(id="fifth", title="fifth", text="pushes out the one ending first", ttl=9000)
    ids = sorted(m["id"] for m in emu.messages())
    check(ids == ["docs", "fifth", "fw", "web"], f"full board: the soonest to end goes ({ids})")
    emu.clear_messages()


@case
def custom_faces_off(emu):
    emu.set("facesapi", 0)
    expect_error(404, "turned off", emu.get, "/api/faces")
    emu.set("facesapi", 1)


@case
def example_faces(emu):
    """Every face in examples/faces, with its sample values (samples.json), one at a time
    (as a user would try them)."""
    emu.set("facesapi", 1)
    samples = json.loads((EXAMPLES / "samples.json").read_text(encoding="utf-8"))
    names = sorted(p.stem for p in EXAMPLES.glob("*.json") if p.name != "samples.json")
    check(len(names) >= 37, f"examples: {names}")
    check(set(samples) <= set(names), f"samples for missing faces: {set(samples) - set(names)}")
    for name in names:
        face = json.loads((EXAMPLES / f"{name}.json").read_text(encoding="utf-8"))
        emu.clear_vars()
        emu.clear_messages()
        emu.apply_sample(samples.get(name, {}))
        check(emu.push_face(name, face, show=True) == "OK", f"push {name}")
        check(emu.faces()["showing"] == name, f"{name} is not on screen")
        shot(emu, f"custom_{name}")
        saved = json.loads(emu.get(f"/api/faces/get?name={name}"))
        check(saved == face, f"{name}: saved face differs from the pushed one")
        emu.delete_face(name)


@case
def face_variables(emu):
    """Variable options: names, null, $add, $ttl, $history, age, and $persist across a restart."""
    expect_error(400, "Bad variable name", emu.post, "/api/faces/vars", {"bad.name": 1})
    emu.post("/api/faces/vars", {"n": 1, "$add": True})
    emu.post("/api/faces/vars", {"n": 2, "$add": True})
    check(emu.faces()["vars"].get("n") == "3", f"$add: {emu.faces()['vars']}")
    emu.post("/api/faces/vars", {"n": None})
    check("n" not in emu.faces()["vars"], "null deletes")
    emu.post("/api/faces/vars", {"brief": "soon gone", "$ttl": 60})
    emu.fix_time(FIXED_TIME + 61)
    check("brief" not in emu.faces()["vars"], "$ttl: still there after 61 s")
    emu.fix_time(FIXED_TIME - 600)
    emu.vars(seen="yes")
    emu.fix_time(FIXED_TIME)
    for v in [3, 7, 4, 9, 12, 8, 15]:
        emu.post("/api/faces/vars", {"load": v, "$history": True})
    face = {"bg": "#000000", "items": [
        {"type": "text", "x": 120, "y": 30, "text": "seen {age.seen}s ago", "font": 2, "align": "mc"},
        {"type": "text", "x": 120, "y": 60, "text": "{{load}>10?HIGH:low} {load:03} {load:.1}", "font": 4, "align": "mc"},
        {"type": "spark", "x": 20, "y": 100, "w": 200, "h": 80, "var": "load", "color": "#30d158", "fill": "#0b3d1a", "min": 0},
        {"type": "text", "x": 120, "y": 210, "text": "{brief|expired}", "font": 2, "align": "mc"}]}
    emu.push_face("vars", face, show=True)
    shot(emu, "custom_variables")
    emu.post("/api/faces/vars", {"kept": "across restarts", "$persist": True})
    emu.vars(lost="in RAM only")
    time.sleep(1.5)   # the first save is immediate, then at most once a minute
    emu.restart()
    emu.sign_in()
    v = emu.faces()["vars"]
    check(v.get("kept") == "across restarts", f"$persist: {v}")
    check("lost" not in v, f"RAM variable survived a restart: {v}")
    emu.post("/api/faces/vars", {"kept": None})


@case
def rotation_when(emu):
    """A face with "when" is in the rotation only while its condition holds."""
    mask = emu.config()["faces"]
    emu.settings(faces=1, themeInterval=1)   # rotate Classic and the custom faces, 1 s each
    emu.push_face("always", {"items": [{"type": "text", "x": 120, "y": 120, "text": "always", "align": "mc"}]})
    emu.push_face("onlygo", {"when": "{go}==1", "duration": 3,
                             "items": [{"type": "text", "x": 120, "y": 120, "text": "go", "align": "mc"}]})

    def seen(seconds):
        names, end = [], time.time() + seconds
        while time.time() < end:
            s = emu.faces().get("showing")
            if s and (not names or names[-1] != s):
                names.append(s)
            time.sleep(0.2)
        return names

    emu.vars(go=0)
    first = seen(7)
    check("always" in first and "onlygo" not in first, f"when false: {first}")
    emu.vars(go=1)
    second = seen(9)
    check("onlygo" in second, f"when true: {second}")
    emu.settings(faces=mask, themeInterval=0)


def showing(emu, seconds):
    """The faces shown in turn over that many seconds: built-in as "#<id>", custom by name."""
    names, end = [], time.time() + seconds
    while time.time() < end:
        s = emu.status()
        e = s.get("customFace") if s["face"] == 14 else "#%d" % s["face"]
        if not names or names[-1] != e:
            names.append(e)
        time.sleep(0.2)
    return names


@case
def rotation_list(emu):
    """One rotation list: custom faces anywhere in it, turned off, with their own time."""
    face = lambda s: {"items": [{"type": "text", "x": 120, "y": 120, "text": s, "align": "mc"}]}
    emu.push_face("alpha", face("alpha"))
    emu.push_face("beta", face("beta"))
    order = emu.config()["order"].split(",")
    check(order[-2:] == ["alpha", "beta"] and order[0] == "#0", f"new faces join at the end: {order}")
    emu.settings(faces=1, order="beta,#0,alpha", themeInterval=1)   # Classic, then the custom faces
    check(emu.config()["order"].startswith("beta,#0,alpha,#1"), emu.config()["order"])
    seen = showing(emu, 6)
    joined = ",".join(seen)
    check("beta,#0,alpha" in joined or "#0,alpha,beta" in joined or "alpha,beta,#0" in joined, f"order: {seen}")
    emu.set("customoff", "alpha")
    seen = showing(emu, 5)
    check("alpha" not in seen[1:] and "beta" in seen, f"alpha off: {seen}")
    emu.settings(customoff="", turns="beta=4")
    seen, end, last, spans = [], time.time() + 12, None, {}
    while time.time() < end:   # how long each face stays
        s = emu.status()
        e = s.get("customFace") if s["face"] == 14 else "#%d" % s["face"]
        if e != last:
            last, start = e, time.time()
        spans[e] = max(spans.get(e, 0), time.time() - start)
        time.sleep(0.2)
    check(spans.get("beta", 0) >= 3, f"beta's own 4 s: {spans}")
    check(spans.get("alpha", 9) < 2.5, f"alpha the rotation's 1 s: {spans}")
    expect_error(400, "Unknown face", emu.set, "nightface", "Bad Name")
    emu.settings(themeInterval=0, faces=0x3fbf, order="", turns="")
    emu.set("customface", "beta")
    check(emu.faces()["showing"] == "beta", "customface shows it")
    expect_error(400, "No such face", emu.set, "customface", "gamma")


@case
def night_custom_face(emu):
    """A custom face all night, its "when" aside."""
    emu.push_face("nightowl", {"when": "{nope}==1", "bg": "#0b1026", "items": [
        {"type": "text", "x": 120, "y": 90, "text": "{time.hm}", "font": "big", "size": 60, "align": "mc", "color": "#c9d3ff"},
        {"type": "text", "x": 120, "y": 170, "text": "OWL", "font": "big", "size": 30, "align": "mc", "color": "#5e5ce6"}]})
    emu.fix_time(epoch(2026, 10, 5, 23, 30))
    emu.settings(nightmode=1, starttime=22, stoptime=7, nightface="nightowl", nighttint=0)
    time.sleep(1.5)
    check(emu.config()["nightface"] == "nightowl", "night face saved")
    shot(emu, "night_custom_face", min_lit=50)
    emu.settings(nightmode=0, nightface="#7", nighttint=1)
    emu.fix_time(FIXED_TIME)


@case
def palettes_and_roles(emu):
    """Palettes of named colours, roles bound to them, the night palette, @role in faces."""
    pals = {"day": dict(DEFAULT_PALETTES["day"], navy="#0b1d33", paper="#f4efe6", ink="#1c1c1e"),
            "night": {"accent1": "#8a5a14", "navy": "#000000"}}
    emu.set("palettes", json.dumps(pals))
    emu.set("roles", json.dumps({"bg": "navy", "time": "@warn", "date": "{now.temp}<0?@blue:@accent3"}))
    c = emu.config()
    check(c["palettes"]["day"]["navy"] == "#0b1d33" and c["roles"]["bg"] == "navy", f"saved: {c['roles']}")
    for fid in (0, 5, 10):
        emu.show(fid)
        shot(emu, f"roles_navy_{fid:02d}_{FACES[fid]}")
    emu.set("roles", json.dumps({"bg": "paper", "text": "ink", "panel": "#e4ddd0"}))
    for fid in (0, 7, 10, 9):
        emu.show(fid)
        shot(emu, f"roles_paper_{fid:02d}_{FACES[fid]}")
    # A custom face that uses the roles follows them.
    emu.push_face("roleface", {"items": [
        {"type": "rect", "x": 20, "y": 20, "w": 200, "h": 200, "r": 16, "color": "@panel"},
        {"type": "text", "x": 120, "y": 100, "text": "{time.hm}", "font": "big", "size": 50, "align": "mc", "color": "@time"},
        {"type": "text", "x": 120, "y": 160, "text": "{theme.palette}", "font": 4, "align": "mc", "color": "@muted"}]}, show=True)
    shot(emu, "roles_custom_face")
    # The night palette at night.
    emu.fix_time(epoch(2026, 10, 5, 23, 30))
    emu.settings(roles=json.dumps({"bg": "navy"}), nightmode=1, starttime=22, stoptime=7, nightface="", nighttint=0)
    emu.show(0)
    shot(emu, "roles_night_palette", min_lit=50)
    emu.settings(nightmode=0, nighttint=1, nightface="#7")
    emu.fix_time(FIXED_TIME)
    # Refused: a palette set without day, a bad colour, an unknown role, an unknown palette.
    expect_error(400, "day palette", emu.set, "palettes", json.dumps({"night": {}}))
    expect_error(400, "Bad colour", emu.set, "palettes", json.dumps({"day": {"x": "red"}}))
    expect_error(400, "Unknown role", emu.set, "roles", json.dumps({"hands": "red"}))
    expect_error(400, "No such palette", emu.set, "palette", "nope")
    # A backup restores the palettes and roles.
    backup = emu.get("/api/settings/export")
    emu.settings(palettes=json.dumps(DEFAULT_PALETTES), roles="{}")
    emu.post("/api/settings/import", backup)
    check(emu.config()["roles"].get("bg") == "navy", "roles restored")
    check("navy" in emu.config()["palettes"]["day"], "palettes restored")


@case
def orientation(emu):
    """The screen turned by the display controller: the same picture, rotated."""
    emu.show(4)   # Simple: nothing on it moves at the fixed time
    upright = emu.screenshot(OUT / "orientation_0.png")
    for deg in (90, 180, 270):
        emu.set("orientation", deg)
        time.sleep(0.6)
        img = shot(emu, f"orientation_{deg}")
        want = upright.rotate(-deg)   # PIL turns counter-clockwise
        diff = sum(1 for a, b in zip(img.getdata(), want.getdata()) if a != b)
        check(diff < 200, f"{deg}: {diff} pixels differ from the upright picture turned")
    expect_error(400, "Orientation", emu.set, "orientation", 45)
    emu.set("orientation", 0)
    check(emu.config()["orientation"] == 0, "back upright")


@case
def display_features(emu):
    """The display's own: a face's "invert", the gamma curve, the panel off at night."""
    emu.push_face("flip", {"invert": "{flip}==1", "bg": "#204060", "items": [
        {"type": "text", "x": 120, "y": 120, "text": "FLIP", "font": 4, "align": "mc", "color": "#ffcc00"}]}, show=True)
    emu.vars(flip=0)
    normal = emu.settle()
    emu.vars(flip=1)
    time.sleep(0.8)
    flipped, _ = emu.framebuffer()
    n = len(normal) // 2
    a = [int.from_bytes(normal[2 * i:2 * i + 2], "little") for i in range(0, n, 97)]
    b = [int.from_bytes(flipped[2 * i:2 * i + 2], "little") for i in range(0, n, 97)]
    check(all(x ^ y == 0xFFFF for x, y in zip(a, b)), "invert: every colour flipped")
    shot(emu, "display_invert")
    emu.vars(flip=0)
    emu.show(0)
    plain = emu.screenshot(OUT / "display_gamma_2.2.png")
    emu.set("gamma", "1.0")
    time.sleep(1.2)
    flat = shot(emu, "display_gamma_1.0")
    check(list(plain.getdata()) != list(flat.getdata()), "gamma 1.0 changes the picture")
    expect_error(400, "Gamma", emu.set, "gamma", "3")
    emu.set("gamma", "2.2")
    # The panel off at night; an error message wakes it.
    emu.fix_time(epoch(2026, 10, 5, 23, 30))
    emu.settings(nightmode=1, starttime=22, stoptime=7, nightface="#7", nightoff=1)
    time.sleep(1.5)
    raw, backlight = emu.framebuffer()
    check(not any(raw) and backlight == 0, f"panel off at night (backlight {backlight}%)")
    emu.message(id="wake", title="Disk full", text="nas", icon="error", show=True)
    time.sleep(2)
    raw, backlight = emu.framebuffer()
    check(any(raw) and backlight == 100, f"an error wakes it (backlight {backlight}%)")
    shot(emu, "display_woken_by_error")
    emu.clear_messages()
    emu.settings(nightmode=0, nightoff=0)
    emu.fix_time(FIXED_TIME)


@case
def scenarios(emu):
    """The emulator's Scenarios panel: a running mocked clock, MQTT-style pushes, presets."""
    c = emu.set_time(local="2026-10-05T07:45", speed=60)
    check(c["local"] == "2026-10-05T07:45:00" and c["mocked"] and c["speed"] == 60, f"set_time {c}")
    time.sleep(2)
    moved = emu.clock()["epoch"] - c["epoch"]
    check(60 <= moved <= 600, f"60x clock moved {moved} s in about 2 s")
    c = emu.set_time(speed=0)   # the pace alone: the time stays
    time.sleep(1.5)
    check(emu.clock()["epoch"] == c["epoch"], "a stopped clock moved")
    check(emu.status()["time"].startswith(c["local"].replace("T", " ")[:16]), f"status time {emu.status()['time']}")

    check(emu.mqtt("vars", {"cpu": 5, "$history": True}) == "OK", "vars over MQTT")
    emu.mqtt("vars", {"cpu": 9, "$history": True})
    v = emu.emu_vars()["vars"].get("cpu", {})
    check(v.get("value") == "9" and v.get("history") == [5, 9], f"cpu {v}")
    emu.mqtt("message", {"id": "mq", "title": "From MQTT", "icon": "success"})
    check(any(m["id"] == "mq" for m in emu.messages()), "message over MQTT")
    emu.mqtt("face/set", "Big time")
    check(emu.emu_vars()["showing"] == "Big time", f"showing {emu.emu_vars()['showing']}")
    emu.request("POST", emu.screen + "/facesapi?on=0", b"")
    expect_error(400, "Custom faces are turned off", emu.mqtt, "vars", {"cpu": 1})
    emu.request("POST", emu.screen + "/facesapi?on=1", b"")

    presets = json.loads(emu.request("GET", emu.screen + "/presets")[1])
    weathers = {w["name"]: w["weather"] for w in presets["weather"]}
    for name, w in weathers.items():
        emu.fix_weather(open_meteo(**w))
    for t in presets["time"]:
        check(emu.set_time(local=t["local"])["local"].startswith(t["local"][:16]) or "forward" in t["name"],
              f"time preset {t['name']}")
    builtin = {"Classic", "Weather", "Big date", "Simple", "Forecast", "Big time", "Big weather", "Agents",
               "Weather today", "Weather forecast - Day 1", "Weather forecast - Day 2", "Weather forecast - Day 3"}
    for s in presets["scenarios"]:
        check(s.get("weather") in weathers, f"scenario {s['name']}: weather {s.get('weather')}")
        check(s.get("face") is None or s["face"] in builtin, f"scenario {s['name']}: face {s.get('face')}")
    index = json.loads(emu.request("GET", emu.screen + "/examples/index")[1])
    check("samples.json" in index and len(index) >= 38, f"examples {len(index)}")
    expect_error(404, "No such example", emu.request, "GET", emu.screen + "/examples/..%2Fpresets.json")

    emu.set_time(local="2027-01-18T07:40")
    emu.fix_weather(open_meteo(**weathers["Blizzard"]))
    emu.mqtt("face/set", "Big weather")
    shot(emu, "scenario_blizzard")
    emu.fix_time(FIXED_TIME)


@case
def agent_faces(emu):
    """The skill's example faces, with the variables an agent would push."""
    values = {"attention": {"title": "ai-agent-monitor", "msg": "Approve the release?"},
              "progress": {"title": "Integration tests", "pct": 64, "step": "Custom faces"},
              "done": {"title": "Release 1.4.0", "msg": "Published"},
              "error": {"title": "CI", "msg": "2 tests failed"}}
    for name, v in values.items():
        face = json.loads((SKILL_EXAMPLES / f"{name}.json").read_text(encoding="utf-8"))
        emu.vars(**v)
        emu.push_face(f"agent-{name}", face, show=True)
        shot(emu, f"agent_{name}")
        emu.delete_face(f"agent-{name}")


@case
def face_validation(emu):
    ok ={"bg": "#000", "items": [{"type": "text", "x": 120, "y": 120, "text": "{nope|fallback}", "align": "mc"}]}
    check(emu.push_face("fallback", ok, show=True) == "OK", "small face")
    shot(emu, "custom_fallback_text", min_lit=50)
    expect_error(400, "At most 32 items", emu.push_face, "big", {"items": [{"type": "circle"}] * 33})
    expect_error(400, "larger than 4 KB", emu.push_face, "huge",
                 {"items": [{"type": "text", "text": "x" * 4200}]})
    expect_error(400, "items", emu.push_face, "nojson", "not json")
    expect_error(400, "a-z 0-9", emu.push_face, "Bad Name!", ok)
    expect_error(404, "No such face", emu.show_face, "missing")
    emu.delete_face("fallback")
    for i in range(40):
        emu.push_face(f"f{i}", ok)
    expect_error(400, "At most 40", emu.push_face, "one-too-many", ok)


@case
def restart_keeps_settings(emu):
    emu.set("facesapi", 1)
    emu.push_face("keep", {"bg": "#123", "items": [{"type": "text", "x": 120, "y": 120, "text": "{time.hm}", "align": "mc", "font": 6}]})
    emu.message(id="ram", title="gone after restart", text="", ttl=600)
    emu.show(5)
    emu.restart()
    emu.sign_in()
    check(not emu.status()["recovery"], "restarted into recovery")
    check(emu.status()["face"] == 5, "saved face after restart")
    check(emu.status()["time"] == "2026-10-05 10:09:00 EDT", "fixed time after restart")
    check(emu.messages() == [], "messages live in RAM")
    check("keep" in json.dumps(emu.faces()), "custom face kept in storage")
    emu.delete_face("keep")


# ---------------------------------------------------------------- runner
def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--no-build", action="store_true", help="reuse the last emulator image")
    ap.add_argument("--keep", action="store_true", help="leave the emulator running")
    ap.add_argument("--reuse", action="store_true", help="test the emulator already on 18000/18080")
    ap.add_argument("-k", default="", help="only cases whose name contains this")
    args = ap.parse_args()

    emu = Emulator()
    OUT.mkdir(parents=True, exist_ok=True)
    for old in OUT.glob("*.png"):
        old.unlink()
    previous = json.loads((OUT / "screens.json").read_text(encoding="utf-8")) if (OUT / "screens.json").exists() else {}
    if not args.reuse:
        emu.start(build=not args.no_build)
    failed = []
    try:
        emu.wait_ready()
        emu.sign_in()
        emu.set("tz", TZ)
        for fn in cases:
            if args.k not in fn.__name__:
                continue
            baseline(emu)
            t0 = time.time()
            try:
                fn(emu)
                print(f"PASS {fn.__name__} ({time.time() - t0:.1f} s)")
            except Exception as e:   # noqa: BLE001 - every failure is reported, the run goes on
                failed.append(fn.__name__)
                print(f"FAIL {fn.__name__}: {e}")
                if not isinstance(e, Failure):
                    traceback.print_exc()
    finally:
        if not args.keep and not args.reuse:
            emu.stop()

    from PIL import Image
    names = sorted(screens)
    if names:
        sheet([Image.open(OUT / f"{n}.png") for n in names], 8, OUT / "sheet.png", labels=names)
    (OUT / "screens.json").write_text(json.dumps(screens, indent=1, sort_keys=True))
    changed = [n for n in names if n in previous and previous[n] != screens[n]]
    print(f"{len(names)} screens -> {OUT}" + (f"; changed since the last run: {', '.join(changed)}" if changed else ""))
    if failed:
        print(f"FAILED: {', '.join(failed)}")
        return 1
    print(f"PASS: {sum(1 for f in cases if args.k in f.__name__)} cases")
    return 0


if __name__ == "__main__":
    sys.exit(main())

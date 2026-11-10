#!/usr/bin/env python3
"""Talk to an AI Agent Monitor clock (AI Agent Monitor firmware) or its emulator.

    python clock.py message TITLE TEXT [--icon NAME] [--id ID] [--ttl S] [--pct N] [--color #hex] [--show]
    python clock.py messages
    python clock.py clear [ID]
    python clock.py status
    python clock.py faces
    python clock.py check FACE.json [FACE.json ...]
    python clock.py push NAME FACE.json [--show] [--force]   (a face, or a json-render spec)
    python clock.py catalog [--prompt]        the clock's json-render catalog (or its prompt)
    python clock.py show NAME
    python clock.py delete NAME
    python clock.py vars name=value [name=value ...] [--ttl S] [--persist] [--history] [--add]
    python clock.py screenshot OUT.png        (emulator only)
    python clock.py gallery [WORD]            the face gallery on GitHub (names matching WORD),
                                              with the firmware each one needs
    python clock.py install NAME [--show] [--force]   a gallery face, onto the clock (refused when
                                              the clock is older than the face's firmware)

check validates faces against reference/face.schema.json (with the jsonschema package
when it is installed, else the main rules) and flags unknown placeholder names. push
checks first; --force pushes anyway. A json-render spec ("root" and "elements") is
compiled by the clock: check only looks at its shape, and the clock's reply lists what it
drew approximately. vars sends numbers as numbers, "null" deletes.

The gallery is examples/faces/marketplace.json in the project's repo: every example face
with its preview and the variables it reads.

Messages need no key: anyone on the network may post one. Icons: info, success,
warning, error, waiting, running, bell, done.

Configuration, from the environment:
    CLOCK_URL         the clock, e.g. http://192.168.1.50  (default http://127.0.0.1:8000,
                      the emulator)
    CLOCK_API_KEY     the API key from the settings page (Integrations > Custom faces)
    CLOCK_SCREEN_URL  the emulator's screen server (default: CLOCK_URL's host, port 8080)
    CLOCK_PASSWORD    the device password: signs in instead of using the API key. Against
                      the emulator it is read from the screen server when unset.

Standard library only.
"""
import gzip
import http.cookiejar
import json
import os
import re
import struct
import sys
import urllib.error
import urllib.parse
import urllib.request
import zlib

CLOCK = os.environ.get("CLOCK_URL", "http://127.0.0.1:8000").rstrip("/")
KEY = os.environ.get("CLOCK_API_KEY", "")
_u = urllib.parse.urlparse(CLOCK)
SCREEN = os.environ.get("CLOCK_SCREEN_URL", f"{_u.scheme}://{_u.hostname}:8080").rstrip("/")

GALLERY = "https://raw.githubusercontent.com/SimonLuckenuik/ai-agent-monitor/main/examples/faces/marketplace.json"

jar = http.cookiejar.CookieJar()
opener = urllib.request.build_opener(urllib.request.HTTPCookieProcessor(jar))
signed_in = False


def call(method, path, body=None, base=CLOCK, content_type="application/json"):
    headers = {"X-Api-Key": KEY} if KEY and base == CLOCK else {}
    data = None
    if body is not None:
        data = body if isinstance(body, bytes) else body.encode("utf-8")
        headers["Content-Type"] = content_type
    req = urllib.request.Request(base + path, data=data, method=method, headers=headers)
    try:
        with opener.open(req, timeout=10) as r:
            return r.read()
    except urllib.error.HTTPError as e:
        raise SystemExit(f"{method} {path}: HTTP {e.code} {e.read().decode(errors='replace').strip()}")
    except urllib.error.URLError as e:
        raise SystemExit(f"{method} {base}{path}: {e.reason}")


def sign_in():
    """Without an API key, sign in with the device password (the emulator's is public)."""
    global signed_in
    if KEY or signed_in:
        return
    password = os.environ.get("CLOCK_PASSWORD")
    if not password:
        try:
            password = call("GET", "/password", base=SCREEN).decode().strip()
        except SystemExit:
            raise SystemExit("Set CLOCK_API_KEY (settings page > Integrations > Custom faces) or CLOCK_PASSWORD")
    call("POST", "/api/auth/login", urllib.parse.urlencode({"password": password}),
         content_type="application/x-www-form-urlencoded")
    signed_in = True


def face_name(name):
    if not name or len(name) > 20 or any(not (c.isalnum() or c in "-_") for c in name):
        raise SystemExit("Face names are 1-20 characters of a-z 0-9 _ -")
    return name.lower()


SCHEMA = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "reference", "face.schema.json")
# The clock's own placeholders, by group ({group.name}); a name without a dot is a variable.
FIELDS = {
    "time": "hm hour min sec ms ampm epoch daymin zone",
    "date": "text day wday wday3 month month3 monthnum year yday week wdaynum mdays",
    "now": "temp feels hum wind press clouds code icon text kind age err rain_in snow_in rain_mm snow_cm",
    "sun": "up next rise set until_rise until_set daylen",
    "moon": "phase lit age",
    "units": "temp wind",
    "loc": "city",
    "theme": "color1 color2 color3 night palette bg text muted panel time seconds date highlight icon "
             "weather high low rain sun ok warn error info",
    "net": "ip host ssid rssi mqtt",
    "sys": "version uptime heap lang",
    "msg": "title text icon color bg pct ttl id count kind shown anim",
}
DAY = "wday wday3 low high feels rain wind hum rise set icon"
for d in range(5):
    FIELDS[f"day{d}"] = DAY
FIELDS = {g: set(f.split()) for g, f in FIELDS.items()}
IT = {"days": set(DAY.split()), "msgs": FIELDS["msg"] - {"count", "kind", "shown", "anim"}}


def placeholders(value):
    """Every {name} key in a string, nested ones included."""
    return re.findall(r"\{([A-Za-z0-9_.]+)(?=[}:|])", value) if isinstance(value, str) else []


def is_spec(doc):
    """A json-render spec (spec 047), which the clock compiles into a face."""
    return isinstance(doc, dict) and "root" in doc and "elements" in doc


def check_spec(spec):
    """The shape of a json-render spec; the clock checks the rest when it compiles it."""
    els = spec.get("elements")
    if not isinstance(els, dict) or not els:
        return ["elements: an object of elements by id"]
    problems = []
    if spec.get("root") not in els:
        problems.append(f"root: {spec.get('root')!r} is not an element")
    if len(els) > 96:
        problems.append(f"{len(els)} elements: at most 96")
    for eid, e in els.items():
        if not isinstance(e, dict) or not isinstance(e.get("type"), str):
            problems.append(f"{eid}: needs a type")
            continue
        for c in e.get("children") or []:
            if c not in els:
                problems.append(f"{eid}: child {c!r} does not exist")
    return problems


def check_face(face):
    if is_spec(face):
        return check_spec(face)
    """Problems in a face (a list of strings; empty when it is fine)."""
    with open(SCHEMA, encoding="utf-8") as f:
        schema = json.load(f)
    problems = []
    size = len(json.dumps(face, separators=(",", ":")))
    if size > 4096:
        problems.append(f"(face): {size} bytes once minified; the clock takes at most 4096")
    try:
        import jsonschema
    except ImportError:
        jsonschema = None
    if jsonschema:
        for e in jsonschema.Draft202012Validator(schema).iter_errors(face):
            problems.append(f"{'/'.join(map(str, e.absolute_path)) or '(face)'}: {e.message}")
    elif not isinstance(face, dict) or not isinstance(face.get("items"), list):
        return problems + ['(face): a face is an object with an "items" array']
    else:
        defs = schema["$defs"]
        types = defs["item"]["properties"]["type"]["enum"]
        problems += [f'(face): unknown field "{k}"' for k in face if k not in schema["properties"]]
        if len(face["items"]) > 32:
            problems.append(f"items: {len(face['items'])} items; at most 32")

        def walk(items, where, nested):
            for i, it in enumerate(items):
                at = f"{where}/{i}"
                kind = it.get("type") if isinstance(it, dict) else None
                if kind not in types:
                    problems.append(f"{at}: unknown type {kind!r} (one of {', '.join(types)})")
                    continue
                if nested and kind == "repeat":
                    problems.append(f"{at}: a repeat cannot contain another repeat")
                    continue
                d = defs[kind]
                problems.extend(f'{at}: "{kind}" has no field "{k}"' for k in it if k not in d["properties"])
                problems.extend(f'{at}: "{kind}" needs "{k}"' for k in d.get("required", []) if k not in it)
                if kind == "sprite":
                    if ("rows" in it) == ("frames" in it):
                        problems.append(f'{at}: a sprite has "rows", or "frames" and "frame"')
                    problems.extend(f'{at}: sprite palette key "{k}" is not one character' for k in it.get("palette", {}) if len(k) != 1)
                if kind == "repeat":
                    walk(it.get("items", []), at + "/items", True)
        walk(face["items"], "items", False)

    # Placeholder names (the schema cannot see inside strings).
    def names(node, where, of):
        if isinstance(node, dict):
            inner = node.get("of") if node.get("type") == "repeat" else of
            for k, v in node.items():
                names(v, f"{where}/{k}", inner if k == "items" else of)
        elif isinstance(node, list):
            for i, v in enumerate(node):
                names(v, f"{where}/{i}", of)
        else:
            for key in placeholders(node):
                group, _, field = key.partition(".")
                if not field:
                    continue   # a variable
                if group == "it":
                    known = {"index", "count"} | IT.get(of, set()) if of is not None else None
                    if known is None:
                        problems.append(f"{where}: {{{key}}} outside a repeat")
                    elif field not in known:
                        problems.append(f"{where}: {{{key}}} is not a field of a repeat over {of!r}")
                elif group == "age":
                    continue
                elif group not in FIELDS:
                    problems.append(f"{where}: unknown placeholder {{{key}}}")
                elif field not in FIELDS[group]:
                    problems.append(f"{where}: unknown placeholder {{{key}}} ({group}. has {', '.join(sorted(FIELDS[group]))})")
    names(face, "", None)
    return problems


def screenshot(out):
    """The emulator's framebuffer (240x240 RGB565) as a PNG."""
    raw = call("GET", "/fb", base=SCREEN)
    if len(raw) != 240 * 240 * 2:
        raise SystemExit(f"Unexpected framebuffer size {len(raw)}")
    rows = bytearray()
    for y in range(240):
        rows.append(0)   # PNG filter: none
        for x in range(240):
            v = raw[2 * (y * 240 + x)] | raw[2 * (y * 240 + x) + 1] << 8
            rows += bytes(((v >> 11) * 255 // 31, ((v >> 5) & 63) * 255 // 63, (v & 31) * 255 // 31))

    def chunk(kind, data):
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data))

    png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", 240, 240, 8, 2, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(bytes(rows), 9)) + chunk(b"IEND", b"")
    with open(out, "wb") as f:
        f.write(png)
    print(out)


def gallery():
    """Every gallery entry: faces, then json-render specs (spec 049)."""
    with urllib.request.urlopen(GALLERY, timeout=20) as r:
        g = json.load(r)
    return g["faces"] + g.get("specs", [])


def version_key(v):
    """2.7.0-dev -> (2, 7, 0): a development build counts as its release."""
    return tuple(int(x) if x.isdigit() else 0 for x in (v or "0").lstrip("v").split("-")[0].split("."))


def main(argv):
    if not argv or argv[0] in ("-h", "--help"):
        print(__doc__)
        return
    cmd, args = argv[0], argv[1:]
    if cmd == "message" and len(args) >= 2:
        msg = {"title": args[0], "text": args[1]}
        rest = args[2:]
        for i, a in enumerate(rest):
            if a == "--show":
                msg["show"] = True
            elif a in ("--icon", "--id", "--color") and i + 1 < len(rest):
                msg[a[2:]] = rest[i + 1]
            elif a in ("--ttl", "--pct") and i + 1 < len(rest):
                msg[a[2:]] = int(rest[i + 1])
        print(call("POST", "/api/message", json.dumps(msg)).decode())
    elif cmd == "messages":
        print(json.dumps(json.loads(call("GET", "/api/message")), indent=2))
    elif cmd == "clear":
        path = "/api/message" + (f"?id={urllib.parse.quote(args[0])}" if args else "")
        print(call("DELETE", path).decode())
    elif cmd == "status":
        print(json.dumps(json.loads(call("GET", "/status")), indent=2))
    elif cmd == "screenshot" and len(args) == 1:
        screenshot(args[0])
    elif cmd == "faces":
        sign_in()
        print(json.dumps(json.loads(call("GET", "/api/faces")), indent=2))
    elif cmd == "check" and args:
        bad = 0
        for path in args:
            with open(path, encoding="utf-8") as f:
                problems = check_face(json.load(f))
            print(f"{path}: " + ("OK" if not problems else f"{len(problems)} problem(s)"))
            for p in problems:
                print("  " + p)
            bad += bool(problems)
        if bad:
            raise SystemExit(1)
    elif cmd == "push" and len(args) >= 2:
        with open(args[1], encoding="utf-8") as f:
            face = json.load(f)   # validates the JSON before it goes to the clock
        problems = check_face(face)
        if problems and "--force" not in args[2:]:
            raise SystemExit("Not pushed (--force to push anyway):\n  " + "\n  ".join(problems))
        sign_in()
        body = json.dumps(face, separators=(",", ":"))
        limit = 6144 if is_spec(face) else 4096
        if len(body) > limit:
            raise SystemExit(f"{'Spec' if is_spec(face) else 'Face'} is {len(body)} bytes once minified; "
                             f"the clock takes at most {limit}")
        show = "&show=1" if "--show" in args[2:] else ""
        print(call("POST", f"/api/faces?name={face_name(args[0])}{show}", body).decode())
    elif cmd == "catalog":
        path = "/api/catalog/prompt" if "--prompt" in args else "/api/catalog"
        req = urllib.request.Request(CLOCK + path, headers={"Accept-Encoding": "gzip"})
        with urllib.request.urlopen(req, timeout=20) as r:
            data = r.read()
            if r.headers.get("Content-Encoding") == "gzip":
                data = gzip.decompress(data)
        print(data.decode())
    elif cmd in ("show", "delete") and len(args) == 1:
        sign_in()
        print(call("POST", f"/api/faces/{cmd}?name={face_name(args[0])}", "").decode())
    elif cmd == "vars" and args:
        sign_in()
        values = {}
        for a in args:
            if "=" not in a:
                continue
            k, v = a.split("=", 1)
            try:
                values[k] = None if v == "null" else json.loads(v) if re.fullmatch(r"-?\d+(\.\d+)?", v) else v
            except ValueError:
                values[k] = v
        for i, a in enumerate(args):
            if a == "--ttl" and i + 1 < len(args):
                values["$ttl"] = int(args[i + 1])
            elif a in ("--persist", "--history", "--add"):
                values["$" + a[2:]] = True
        print(call("POST", "/api/faces/vars", json.dumps(values)).decode())
    elif cmd == "gallery":
        word = args[0].lower() if args else ""
        for f in gallery():
            if word in (f["name"] + " " + f["title"] + " " + f["description"] + " " + f["category"]).lower():
                reads = f"  reads: {', '.join(f['variables'])}" if f["variables"] else ""
                print(f"{f['name']:18} {f.get('firmware', ''):7} {f['category']:24} {f['description']}{reads}")
    elif cmd == "install" and args:
        f = next((f for f in gallery() if f["name"] == args[0]), None)
        if not f:
            raise SystemExit(f"No face {args[0]!r} in the gallery: python clock.py gallery")
        clock = json.loads(call("GET", "/status")).get("version", "0")
        if version_key(f.get("firmware")) > version_key(clock) and "--force" not in args[1:]:
            raise SystemExit(f"{f['name']} needs firmware {f['firmware']} (the clock runs {clock}): "
                             "update the clock first, or --force")
        with urllib.request.urlopen(f["json"], timeout=20) as r:
            face = json.load(r)
        sign_in()
        show = "&show=1" if "--show" in args[1:] else ""
        print(call("POST", f"/api/faces?name={face_name(f['name'])}{show}", json.dumps(face, separators=(",", ":"))).decode())
        if f["variables"]:
            print("It reads: " + ", ".join(f["variables"]) + " (python clock.py vars name=value ...)")
    else:
        raise SystemExit(__doc__)


if __name__ == "__main__":
    main(sys.argv[1:])

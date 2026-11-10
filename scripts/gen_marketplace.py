"""Builds examples/faces/marketplace.json, the face gallery (spec 026).

    python scripts/gen_marketplace.py             # writes it
    python scripts/gen_marketplace.py --check     # fails if it is out of date, a face is over
                                                  # 4 KB or 32 items, or a preview is missing
    python scripts/gen_marketplace.py --previews .pio/integration
                                                  # first copies each face's screen from an
                                                  # integration run (custom_<name>.png,
                                                  # json_render_<name>.png)
    python scripts/gen_marketplace.py --snapshot  # rewrites scripts/gallery_versions.json's
                                                  # known names (after adding a version)

Every face in examples/faces and examples/faces/smalltv-pomodoro, with links to its JSON
and its preview on GitHub, so the settings page, the skill and anyone else can list and
fetch them without cloning the repo. Our faces' descriptions and categories come from the
tables in examples/faces/README.md; the converted ones' from smalltv-pomodoro/faces.json.
json-render specs (spec 049) come from examples/json-render/gallery and its README, listed
under "specs": settings pages before 2.7.0 read only "faces", so they never offer one.

Each entry's "firmware" is the oldest firmware that draws it (spec 049): the floor, raised
by any feature in scripts/gallery_versions.json's "since". --check fails when the face
schema, clock.py's placeholder names or the json-render catalog know a name that
gallery_versions.json does not: give it a version in "since", or run --snapshot when it is
as old as the floor.
"""
import argparse
import importlib.util
import json
import pathlib
import re
import shutil
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
FACES = ROOT / "examples" / "faces"
POMODORO = FACES / "smalltv-pomodoro"
PREVIEWS = ROOT / "docs" / "images" / "faces"
OUT = FACES / "marketplace.json"
REPO = "SimonLuckenuik/ai-agent-monitor"
RAW = f"https://raw.githubusercontent.com/{REPO}/main/"
SPECS = ROOT / "examples" / "json-render" / "gallery"
SKILL = ROOT / "plugins" / "ai-agent-monitor" / "skills" / "ai-agent-monitor"
CATALOG = ROOT / "integrations" / "json-render" / "catalog.json"
VERSIONS = pathlib.Path(__file__).with_name("gallery_versions.json")


def vkey(v):
    return tuple(int(x) for x in v.split("-")[0].split("."))


def clock_module():
    spec = importlib.util.spec_from_file_location("clock", SKILL / "scripts" / "clock.py")
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def known_names():
    """What the face format and the json-render catalog know now, as feature names."""
    face, specs = set(), set()
    def walk(v):
        if isinstance(v, dict):
            for k, x in v.items():
                if k == "properties" and isinstance(x, dict):
                    face.update("prop:" + n for n in x)
                if k == "enum" and isinstance(x, list):
                    face.update("value:" + str(e) for e in x)
                walk(x)
        elif isinstance(v, list):
            for x in v:
                walk(x)
    walk(json.loads((SKILL / "reference" / "face.schema.json").read_text(encoding="utf-8")))
    for group, names in clock_module().FIELDS.items():
        face.update(f"field:{group}.{n}" for n in names)
    cat = json.loads(CATALOG.read_text(encoding="utf-8"))
    for comp, d in cat["components"].items():
        specs.add("component:" + comp)
        specs.update(f"prop:{comp}.{p}" for p in d.get("props", {}))
    return face, specs


def features(doc, spec):
    """The feature names a face or spec uses, in the terms of known_names()."""
    used = set()
    if spec:
        for e in (doc.get("elements") or {}).values():
            if isinstance(e, dict) and isinstance(e.get("type"), str):
                used.add("component:" + e["type"])
                used.update(f"prop:{e['type']}.{p}" for p in (e.get("props") or {}))
        return used
    def walk(v):
        if isinstance(v, dict):
            for k, x in v.items():
                used.add("prop:" + k)
                if isinstance(x, str) and k in ("type", "font"):
                    used.add("value:" + x)
                walk(x)
        elif isinstance(v, list):
            for x in v:
                walk(x)
        elif isinstance(v, str):
            used.update("field:" + m for m in re.findall(r"\{([a-z]+\.[a-z0-9_]+)", v))
    walk(doc)
    return used


def firmware(doc, spec, versions):
    floor = versions["specs" if spec else "faces"]["floor"]
    found = [versions["since"][f] for f in features(doc, spec) if f in versions["since"]]
    return max([floor] + found, key=vkey)


def readme_entries(readme=FACES / "README.md"):
    """name -> (description, category) from the README's tables."""
    out, category = {}, None
    for line in readme.read_text(encoding="utf-8").splitlines():
        if line.startswith("## "):
            category = line[3:].strip()
        m = re.match(r"\|\s*!\[\]\([^)]*\)\s*\|\s*\*\*([a-z0-9-]+)\*\*:\s*([^|]+?)\s*\|", line)
        if m:
            d = m.group(2).rstrip(".")
            out[m.group(1)] = (d[:1].upper() + d[1:], category)
    return out


def spec_variables(spec):
    """The variables a json-render spec reads: /vars/<name> in $state, $template, Sparkline."""
    return sorted(set(re.findall(r"/vars/([A-Za-z0-9_]+)", json.dumps(spec))))


def variables(face):
    """The variables a face reads: {name}, {name|fallback}, {name:modifier}, spark vars."""
    text = json.dumps(face)
    names = set(re.findall(r"\{([A-Za-z_][A-Za-z0-9_]*)(?=[|:}])", text))
    def walk(v):
        if isinstance(v, dict):
            if v.get("type") == "spark" and isinstance(v.get("var"), str):
                names.add(v["var"])
            for x in v.values():
                walk(x)
        elif isinstance(v, list):
            for x in v:
                walk(x)
    walk(face)
    return sorted(names)


def title(name):
    return " ".join(w.capitalize() for w in name.split("-"))


def entries():
    ours = readme_entries()
    faces = []
    order = {name: i for i, name in enumerate(ours)}   # the README's order: grouped by category
    files = [p for p in FACES.glob("*.json") if p.name not in ("samples.json", "marketplace.json")]
    for p in sorted(files, key=lambda p: (order.get(p.stem, len(order)), p.stem)):
        desc, cat = ours.get(p.stem, ("", "Other"))
        faces.append((p, {"title": title(p.stem), "description": desc, "category": cat,
                          "author": "SimonLuckenuik", "source": f"https://github.com/{REPO}", "license": "GPL-3.0"}))
    index = {e["name"]: e for e in json.loads((POMODORO / "faces.json").read_text(encoding="utf-8"))}
    for p in sorted(POMODORO.glob("*.json")):
        if p.name == "faces.json":
            continue
        e = index[p.stem]
        faces.append((p, {"title": e["title"], "description": e["description"], "category": e["category"],
                          "author": e["author"], "source": e["upstream"], "license": e["license"]}))
    # Grouped by category, in the README's order of sections: ours first in each, then the
    # converted ones (sorted() keeps that order within a category).
    cats = list(dict.fromkeys(cat for _, cat in ours.values()))
    return sorted(faces, key=lambda f: cats.index(f[1]["category"]) if f[1]["category"] in cats else len(cats))


def spec_entries():
    ours = readme_entries(SPECS / "README.md")
    files = [p for p in SPECS.glob("*.json") if p.name != "samples.json"]
    order = {name: i for i, name in enumerate(ours)}
    out = []
    for p in sorted(files, key=lambda p: (order.get(p.stem, len(order)), p.stem)):
        desc, cat = ours.get(p.stem, ("", "json-render"))
        out.append((p, {"title": title(p.stem), "description": desc, "category": cat,
                        "author": "SimonLuckenuik", "source": f"https://github.com/{REPO}", "license": "GPL-3.0"}))
    return out


def check_names(versions):
    """Names the face format or the catalog know that gallery_versions.json does not."""
    face, specs = known_names()
    since = set(versions["since"])
    missing = sorted((face - set(versions["faces"]["known"]) - since) | (specs - set(versions["specs"]["known"]) - since))
    return [f"{n}: not in scripts/gallery_versions.json (a version in \"since\", or --snapshot)" for n in missing]


def build():
    versions = json.loads(VERSIONS.read_text(encoding="utf-8"))
    out, specs, problems = [], [], check_names(versions)
    check_spec = clock_module().check_spec
    for path, meta in spec_entries():
        spec = json.loads(path.read_text(encoding="utf-8"))
        size = len(json.dumps(spec, separators=(",", ":"), ensure_ascii=False).encode())
        name = path.stem
        rel = path.relative_to(ROOT).as_posix()
        preview = (PREVIEWS / f"{name}.png").relative_to(ROOT).as_posix()
        if size > 6144:
            problems.append(f"{name}: {size} B, over 6 KB")
        problems += [f"{name}: {p}" for p in check_spec(spec)]
        if not (ROOT / preview).exists():
            problems.append(f"{name}: no preview at {preview}")
        if not meta["description"]:
            problems.append(f"{name}: no description (a row in examples/json-render/gallery/README.md)")
        specs.append({"name": name, **meta, "format": "json-render", "size": size,
                      "variables": spec_variables(spec), "firmware": firmware(spec, True, versions),
                      "json": RAW + rel, "preview": RAW + preview, "path": rel, "previewPath": preview})
    for path, meta in entries():
        face = json.loads(path.read_text(encoding="utf-8"))
        size = len(json.dumps(face, separators=(",", ":"), ensure_ascii=False).encode())
        name = path.stem
        rel = path.relative_to(ROOT).as_posix()
        preview = (PREVIEWS / f"{name}.png").relative_to(ROOT).as_posix()
        if size > 4096:
            problems.append(f"{name}: {size} B, over 4 KB")
        if len(face.get("items", [])) > 32:
            problems.append(f"{name}: {len(face['items'])} items, over 32")
        if not (ROOT / preview).exists():
            problems.append(f"{name}: no preview at {preview}")
        if not meta["description"]:
            problems.append(f"{name}: no description (a row in examples/faces/README.md)")
        out.append({"name": name, **meta, "size": size, "variables": variables(face),
                    "firmware": firmware(face, False, versions),
                    "json": RAW + rel, "preview": RAW + preview, "path": rel, "previewPath": preview})
    doc = {"format": 1, "repo": f"https://github.com/{REPO}",
           "about": "Faces for the AI Agent Monitor clock: POST a face's JSON to /api/faces?name=<name>. "
                    "\"firmware\" is the oldest firmware that draws it; \"specs\" are json-render specs.",
           "faces": out, "specs": specs}
    return json.dumps(doc, indent=2, ensure_ascii=False) + "\n", problems


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--check", action="store_true")
    ap.add_argument("--previews", help="an integration run's output folder, to copy missing previews from")
    ap.add_argument("--snapshot", action="store_true", help="record every name known now as known at the floor")
    a = ap.parse_args()
    if a.snapshot:
        versions = json.loads(VERSIONS.read_text(encoding="utf-8"))
        face, specs = known_names()
        versions["faces"]["known"] = sorted(face - set(versions["since"]))
        versions["specs"]["known"] = sorted(specs - set(versions["since"]))
        VERSIONS.write_text(json.dumps(versions, indent=1) + "\n", encoding="utf-8", newline="\n")
        print(f"gallery_versions.json: {len(face)} face names, {len(specs)} spec names")
        return
    if a.previews:
        src = pathlib.Path(a.previews)
        for path, _ in entries() + spec_entries():
            target = PREVIEWS / f"{path.stem}.png"
            shot = src / (f"json_render_{path.stem}.png" if path.parent == SPECS else f"custom_{path.stem}.png")
            if not target.exists() and shot.exists():
                shutil.copy(shot, target)
                print("preview", target.name)
    text, problems = build()
    if problems:
        sys.exit("\n".join(problems))
    if a.check:
        if not OUT.exists() or OUT.read_text(encoding="utf-8") != text:
            sys.exit("examples/faces/marketplace.json is out of date: run python scripts/gen_marketplace.py")
        print("marketplace.json is up to date")
        return
    OUT.write_text(text, encoding="utf-8", newline="\n")
    doc = json.loads(text)
    print(f"marketplace.json: {len(doc['faces'])} faces, {len(doc['specs'])} specs")


if __name__ == "__main__":
    main()

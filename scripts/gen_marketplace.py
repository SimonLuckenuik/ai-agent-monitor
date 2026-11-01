"""Builds examples/faces/marketplace.json, the face gallery (spec 026).

    python scripts/gen_marketplace.py             # writes it
    python scripts/gen_marketplace.py --check     # fails if it is out of date, a face is over
                                                  # 4 KB or 32 items, or a preview is missing
    python scripts/gen_marketplace.py --previews .pio/integration
                                                  # first copies each face's screen from an
                                                  # integration run (custom_<name>.png)

Every face in examples/faces and examples/faces/smalltv-pomodoro, with links to its JSON
and its preview on GitHub, so the settings page, the skill and anyone else can list and
fetch them without cloning the repo. Our faces' descriptions and categories come from the
tables in examples/faces/README.md; the converted ones' from smalltv-pomodoro/faces.json.
"""
import argparse
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
FIRMWARE = "2.0.0"   # every face here is tested on the emulator of this version or later


def readme_entries():
    """name -> (description, category) from the README's tables."""
    out, category = {}, None
    for line in (FACES / "README.md").read_text(encoding="utf-8").splitlines():
        if line.startswith("## "):
            category = line[3:].strip()
        m = re.match(r"\|\s*!\[\]\([^)]*\)\s*\|\s*\*\*([a-z0-9-]+)\*\*:\s*([^|]+?)\s*\|", line)
        if m:
            d = m.group(2).rstrip(".")
            out[m.group(1)] = (d[:1].upper() + d[1:], category)
    return out


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


def build():
    out, problems = [], []
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
        out.append({"name": name, **meta, "size": size, "variables": variables(face), "firmware": FIRMWARE,
                    "json": RAW + rel, "preview": RAW + preview, "path": rel, "previewPath": preview})
    doc = {"format": 1, "repo": f"https://github.com/{REPO}",
           "about": "Faces for the AI Agent Monitor clock: POST a face's JSON to /api/faces?name=<name>.",
           "faces": out}
    return json.dumps(doc, indent=2, ensure_ascii=False) + "\n", problems


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--check", action="store_true")
    ap.add_argument("--previews", help="an integration run's output folder, to copy missing previews from")
    a = ap.parse_args()
    if a.previews:
        src = pathlib.Path(a.previews)
        for path, _ in entries():
            target = PREVIEWS / f"{path.stem}.png"
            shot = src / f"custom_{path.stem}.png"
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
    print(f"marketplace.json: {text.count(chr(10) + '    {') } faces")


if __name__ == "__main__":
    main()

"""Spec 047: the clock's json-render catalog, from integrations/json-render/catalog.json.

Writes integrations/json-render/prompt.md; catalog_files() gives the clock's copies of the
catalog and the prompt (GET /api/catalog and /api/catalog/prompt, in builds with
SMARTCLOCK_JSONRENDER) and integrations/json-render/prompt.md (the same prompt, for
agents that read files).

    python scripts/gen_jr_catalog.py
"""
import json
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(ROOT, "integrations", "json-render", "catalog.json")


def prompt(cat):
    s = cat["screen"]
    out = [f"# Drawing on the AI Agent Monitor clock with json-render (catalog {cat['version']})", "",
           s["note"], "", "## Output", ""]
    out += [f"- {r}" for r in cat["rules"]]
    out += ["", "## Components", ""]
    for name, c in cat["components"].items():
        kids = " Has children." if c.get("children") else ""
        out.append(f"### {name}")
        out.append(c["description"] + kids)
        for k, v in c["props"].items():
            out.append(f"- `{k}`: {v}")
        out.append("")
    out += ["## Live values (state)", "", cat["state"]["note"], ""]
    out += [f"- `{k}`: {v}" for k, v in cat["state"]["paths"].items()]
    out += ["", "## Dynamic props", ""]
    out += [f"- `{k}`: {v}" for k, v in cat["dynamic"].items()]
    out += ["", "## Colours", "", cat["colors"], "", "## Also accepted", "", cat["also_accepted"], "",
            "## Sending it", "", cat["upload"], "",
            "## Example", "", "```json", json.dumps(EXAMPLE, indent=1), "```", ""]
    return "\n".join(out)


EXAMPLE = {
    "root": "screen",
    "elements": {
        "screen": {"type": "Box", "props": {"flexDirection": "column", "padding": 1, "gap": 1}, "children": ["top", "status", "bar", "fill", "kv"]},
        "top": {"type": "Box", "children": ["clock", "sp", "wx"], "props": {"alignItems": "center"}},
        "clock": {"type": "Clock", "props": {"format": "hm", "fontSize": 40}},
        "sp": {"type": "Spacer"},
        "wx": {"type": "WeatherIcon", "props": {"size": 6}},
        "status": {"type": "StatusLine", "props": {"status": "success", "text": {"$template": "Build ${/vars/build|idle}"}}},
        "bar": {"type": "ProgressBar", "visible": {"$state": "/vars/build", "eq": "running"}, "props": {"label": "Tests", "progress": {"$state": "/vars/progress", "fallback": "0"}}},
        "fill": {"type": "Spacer"},
        "kv": {"type": "KeyValue", "props": {"label": "Branch", "value": {"$state": "/vars/branch", "fallback": "main"}}},
    },
    "state": {"vars": {"build": "running", "progress": 0.4}},
}


def main():
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    import minify_page
    with open(SRC, encoding="utf-8") as f:
        cat = json.load(f)
    text = prompt(cat)
    with open(os.path.join(ROOT, "integrations", "json-render", "prompt.md"), "w", encoding="utf-8", newline="\n") as f:
        f.write(text)
    print(f"prompt {len(text)} B")
    # The clock serves both from storage (/sys/catalog.json.gz, /sys/prompt.md.gz): files
    # that come with the update, which scripts/sys_files.py makes with catalog_files().


def catalog_files():
    """The catalog (compact JSON) and its prompt, gzipped: the /sys files for builds with
    json-render (scripts/sys_files.py)."""
    import minify_page
    with open(SRC, encoding="utf-8") as f:
        cat = json.load(f)
    compact = json.dumps(cat, separators=(",", ":"), ensure_ascii=True).encode("utf-8")
    return [("catalog.json.gz", minify_page.gzip_page(compact)),
            ("prompt.md.gz", minify_page.gzip_page(prompt(cat).encode("utf-8")))]


if __name__ == "__main__":
    main()

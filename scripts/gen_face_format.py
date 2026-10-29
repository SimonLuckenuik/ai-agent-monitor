"""Regenerate the skill's reference/face-format.md from the API specification.

    python scripts/gen_face_format.py          # write it
    python scripts/gen_face_format.py --check  # fail if it is out of date (CI)

docs/specifications/integrations-api.md is the source of truth. The skill gets its
"Variables" to "Gradients" sections (the face format and the variables a face reads),
with headings one level up, under a short introduction.
"""
from pathlib import Path
import sys

root = Path(__file__).resolve().parents[1]
spec = (root / "docs/specifications/integrations-api.md").read_text(encoding="utf-8").replace("\r\n", "\n")
out = root / "plugins/ai-agent-monitor/skills/ai-agent-monitor/reference/face-format.md"

start = spec.index("### Variables\n")
end = spec.index("## Home Assistant dashboards")
body = spec[start:end].rstrip() + "\n"
body = "\n".join(line[1:] if line.startswith("###") else line for line in body.split("\n"))

intro = """# Face format

A face is a JSON object: a background, up to 32 items drawn in order, and optional
palettes, maps and rotation rules. Values that change are variables, pushed
separately. `face.schema.json` next to this file is the JSON Schema of a face: check a
face against it before pushing (`python scripts/clock.py check face.json`).

Generated from `docs/specifications/integrations-api.md` in the firmware repository
(SimonLuckenuik/ai-agent-monitor), the source of truth, by `scripts/gen_face_format.py`.

"""
text = intro + body
if "--check" in sys.argv:
    current = out.read_text(encoding="utf-8").replace("\r\n", "\n") if out.exists() else ""
    if current != text:
        sys.exit(f"{out.relative_to(root)} is out of date: run python scripts/gen_face_format.py")
    print("face-format.md is up to date")
else:
    out.write_bytes(text.encode("utf-8"))
    print(f"wrote {out.relative_to(root)}")

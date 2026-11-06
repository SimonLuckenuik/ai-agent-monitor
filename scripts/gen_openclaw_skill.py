"""Copy the Claude Code skill's shared files into the OpenClaw skill (spec 044).

    python scripts/gen_openclaw_skill.py          # write the copies
    python scripts/gen_openclaw_skill.py --check  # fail if a copy is out of date (CI)

The OpenClaw skill (integrations/openclaw/skill/ai-agent-monitor/) has its own SKILL.md,
written for OpenClaw's agents and its `nodes` tool. The face format, its JSON Schema, the
HTTP API reference, clock.py and the example faces are the Claude Code skill's
(plugins/ai-agent-monitor/skills/ai-agent-monitor/), copied here so the folder installs
on its own (`openclaw skills install <folder>` copies one folder). Edit the Claude Code
skill's files, never the copies, then run this script.
"""
from pathlib import Path
import sys

root = Path(__file__).resolve().parents[1]
src = root / "plugins/ai-agent-monitor/skills/ai-agent-monitor"
dst = root / "integrations/openclaw/skill/ai-agent-monitor"
SHARED = ["reference/face-format.md", "reference/face.schema.json", "reference/api.md", "scripts/clock.py"]
SHARED += sorted(f"examples/{p.name}" for p in (src / "examples").glob("*.json"))


def text(p):
    return p.read_bytes().replace(b"\r\n", b"\n")


def main():
    check = "--check" in sys.argv[1:]
    stale = []
    for rel in SHARED:
        want, out = text(src / rel), dst / rel
        if out.exists() and text(out) == want:
            continue
        if check:
            stale.append(rel)
        else:
            out.parent.mkdir(parents=True, exist_ok=True)
            out.write_bytes(want)
            print("wrote", out.relative_to(root).as_posix())
    extra = [p.relative_to(dst).as_posix() for d in ("reference", "scripts", "examples") if (dst / d).exists()
             for p in (dst / d).iterdir() if p.is_file() and p.relative_to(dst).as_posix() not in SHARED]
    if check and (stale or extra):
        sys.exit("the OpenClaw skill's copies are out of date (" + ", ".join(stale + [x + " (not in the Claude Code skill)" for x in extra])
                 + "): run python scripts/gen_openclaw_skill.py")
    for x in extra:
        print("not in the Claude Code skill (delete it?):", x)


if __name__ == "__main__":
    main()

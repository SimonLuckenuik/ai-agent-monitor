"""Regenerate the README's images from the emulator: clock faces, custom faces, agent
screens and the settings page.

    python scripts/screenshots.py              # build and start an emulator, capture, stop
    python scripts/screenshots.py --no-build   # reuse the last emulator image
    python scripts/screenshots.py --reuse      # use the emulator already on 18000/18080
    python scripts/screenshots.py --no-web     # clock screens only, no browser

Uses the integration tests' emulator (tests/integration/emulator.py): fixed time
(Monday 5 October 2026, 10:09) and weather, so the images only change when the
firmware does. Writes to docs/images/:

  faces.png                the built-in faces
  custom-faces.png         examples/faces/*.json, with their samples.json values
  faces/<name>.png         each of those faces alone (examples/faces/README.md)
  agent-screens.png        messages, the Agents board and the skill's example faces
  settings-overview.png    the settings page (desktop)
  settings-integrations.png, settings-system.png, settings-phone.png
  emulator.png             the emulator's screen page, with the Scenarios panel

The settings page is captured with Playwright (pip install playwright). It uses the
installed Microsoft Edge or Google Chrome when there is one; otherwise run
`python -m playwright install chromium` once.
"""
import argparse
import json
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tests" / "integration"))
from emulator import FIXED_TIME, TZ, Emulator, open_meteo, sheet  # noqa: E402

IMAGES = ROOT / "docs" / "images"
EXAMPLES = ROOT / "examples" / "faces"
SKILL_EXAMPLES = ROOT / "plugins" / "ai-agent-monitor" / "skills" / "ai-agent-monitor" / "examples"


def baseline(emu):
    emu.fix_time(FIXED_TIME)
    emu.fix_weather(open_meteo())
    emu.settings(tz=TZ, lang="en", celsius=1, hour12=0, mile=0, nightmode=0, themeInterval=0,
                 brightness=100, facesapi=1)


def agents(emu):
    rows = [("api", "api-server", "Waiting for review of PR 42", "waiting", None),
            ("web", "web-app", "Running the end-to-end tests", "running", 45),
            ("fw", "firmware", "Build failed: 2 errors", "error", None),
            ("docs", "docs", "README and screenshots updated", "done", None)]
    for i, (mid, title, text, icon, pct) in enumerate(rows):
        extra = {"pct": pct} if pct is not None else {}
        emu.message(id=mid, title=title, text=text, icon=icon, ttl=7200 + 3600 * i, **extra)


def clock_images(emu):
    shots, labels = [], []

    def take(label):
        shots.append(emu.screenshot())
        labels.append(label)

    # Built-in faces, with a full Agents board and one message.
    emu.clear_messages()
    agents(emu)
    for fid, label in ((0, "Classic"), (1, "Weather"), (5, "Forecast"), (4, "Simple"),
                       (7, "Big time"), (3, "Big date"), (8, "Big weather"), (10, "Weather today"),
                       (11, "Forecast - Day 1"), (9, "Agents")):
        emu.show(fid)
        take(label)
    emu.message(id="api", title="api-server", text="Waiting for your approval to merge PR 42.",
                icon="waiting", ttl=7200, show=True)
    take("Message")
    emu.set("lang", "fr")
    emu.show(10)
    take("Weather today (French)")
    emu.set("lang", "en")
    sheet(shots, 4, IMAGES / "faces.png", labels=labels)

    # Custom faces from examples/faces, with their sample values.
    shots, labels = [], []
    samples = json.loads((EXAMPLES / "samples.json").read_text(encoding="utf-8"))
    (IMAGES / "faces").mkdir(exist_ok=True)
    for path in sorted(EXAMPLES.glob("*.json")):
        if path.name == "samples.json":
            continue
        emu.clear_vars()
        emu.clear_messages()
        emu.apply_sample(samples.get(path.stem, {}))
        emu.push_face(path.stem, json.loads(path.read_text(encoding="utf-8")), show=True)
        take(path.stem)
        shots[-1].save(IMAGES / "faces" / f"{path.stem}.png", optimize=True)
        emu.delete_face(path.stem)
    emu.clear_vars()
    emu.clear_messages()
    sheet(shots, 6, IMAGES / "custom-faces.png", labels=labels)

    # Agent screens: message layouts, the Agents board, the skill's example faces.
    shots, labels = [], []
    emu.clear_messages()
    for icon, title, text, pct in (
            ("waiting", "ai-agent-monitor", "Waiting for your approval to push to main.", None),
            ("running", "Refactor", "Moving the weather code into its own module.", 62),
            ("error", "Build failed", "3 tests failed in faces.cpp: see the log.", None),
            ("done", "Release 1.4.0", "Published with both images and the notes.", None),
            ("warning", "Disk 91% full", "The build cache needs a clean-up soon.", None),
            ("success", "Tests passed", "179 checks, 0 failures.", None)):
        extra = {"pct": pct} if pct is not None else {}
        emu.message(id=icon, title=title, text=text, icon=icon, ttl=7200, show=True, **extra)
        time.sleep(0.3)
        take(f"message: {icon}" + (" + pct" if pct else ""))
    emu.clear_messages()
    agents(emu)
    emu.show(9)
    take("Agents board")
    emu.clear_messages()
    for name, values in (("attention", {"title": "ai-agent-monitor", "msg": "Approve the release?"}),
                         ("progress", {"title": "Integration tests", "pct": 64, "step": "Custom faces"}),
                         ("done", {"title": "Release 1.4.0", "msg": "Published"}),
                         ("error", {"title": "CI", "msg": "2 tests failed"})):
        emu.vars(**values)
        emu.push_face(f"agent-{name}", json.loads((SKILL_EXAMPLES / f"{name}.json").read_text(encoding="utf-8")), show=True)
        take(f"skill face: {name}")
        emu.delete_face(f"agent-{name}")
    sheet(shots, 4, IMAGES / "agent-screens.png", labels=labels)


def web_images(emu):
    try:
        from playwright.sync_api import sync_playwright
    except ImportError:
        raise SystemExit("The settings page needs Playwright: pip install playwright (or use --no-web)")
    url = f"{emu.api}/#password={emu.password()}"   # the emulator build's sign-in link
    # The rotation with a few custom faces: one with its own time, one turned off.
    shown = ("calendar", "scoreboard", "pomodoro")
    for name in shown:
        emu.push_face(name, json.loads((EXAMPLES / f"{name}.json").read_text(encoding="utf-8")))
    emu.settings(themeInterval=10, turns="#7=20,scoreboard=30", customoff="pomodoro")

    def card(page, title):
        return page.locator(f"section.card:has(h2:has-text('{title}'))").first

    with sync_playwright() as p:
        browser = None
        for channel in ("msedge", "chrome", None):
            try:
                browser = p.chromium.launch(channel=channel) if channel else p.chromium.launch()
                break
            except Exception:   # noqa: BLE001 - try the next browser
                continue
        if browser is None:
            raise SystemExit("No browser: install Edge or Chrome, or run python -m playwright install chromium")

        def open_page(width, height, scheme):
            page = browser.new_page(viewport={"width": width, "height": height}, color_scheme=scheme)
            page.goto(url)
            page.wait_for_selector("#login", state="hidden")
            page.wait_for_load_state("networkidle")
            time.sleep(1.5)   # /config and /status fill the cards
            return page

        page = open_page(960, 900, "light")
        top = card(page, "Overview").bounding_box()
        night = card(page, "Night").bounding_box()
        page.screenshot(path=str(IMAGES / "settings-overview.png"),
                        clip={"x": 0, "y": 0, "width": 960, "height": night["y"] + night["height"] + 16},
                        full_page=True)
        integrations = card(page, "Integrations")
        integrations.scroll_into_view_if_needed()
        integrations.screenshot(path=str(IMAGES / "settings-integrations.png"))
        page.close()
        del top

        page = open_page(480, 900, "light")
        system = card(page, "System")
        system.scroll_into_view_if_needed()
        system.screenshot(path=str(IMAGES / "settings-system.png"))
        page.close()

        page = open_page(400, 720, "dark")
        page.screenshot(path=str(IMAGES / "settings-phone.png"))
        page.close()

        # The emulator's own page: the screen, its controls and the Scenarios panel.
        page = browser.new_page(viewport={"width": 1100, "height": 1000}, color_scheme="dark")
        page.goto(emu.screen + "/")
        page.wait_for_load_state("networkidle")
        emu.show(10)   # redraw the face for the page that just connected
        time.sleep(2)
        box = page.locator("section.scenarios").bounding_box()
        height = page.evaluate("document.documentElement.scrollHeight")
        page.screenshot(path=str(IMAGES / "emulator.png"), full_page=True,
                        clip={"x": box["x"] - 16, "y": 16, "width": box["width"] + 32,
                              "height": height - 16})
        page.close()
        browser.close()
    for name in shown:
        emu.delete_face(name)
    emu.settings(themeInterval=0, turns="", customoff="")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--no-build", action="store_true")
    ap.add_argument("--reuse", action="store_true")
    ap.add_argument("--keep", action="store_true")
    ap.add_argument("--no-web", action="store_true")
    args = ap.parse_args()
    emu = Emulator()
    if not args.reuse:
        emu.start(build=not args.no_build)
    try:
        emu.sign_in()
        baseline(emu)
        clock_images(emu)
        if not args.no_web:
            emu.clear_messages()
            agents(emu)
            emu.show(10)
            web_images(emu)
    finally:
        if not args.keep and not args.reuse:
            emu.stop()
    for f in sorted(IMAGES.glob("*.png")):
        print(f.relative_to(ROOT))


if __name__ == "__main__":
    main()

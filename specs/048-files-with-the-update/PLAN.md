# 048 Plan

Nothing is built until the owner approves the spec. Each step is measured with
`pio run` and logged in PROGRESS.md.

## Phase 1: the settings page as a file

Steps 1-9 are built (PROGRESS.md has the commits and what was checked); step 10 waits
for a run on a clock.

1. **Bundle format and tool.** `scripts/make_update.py` (built under that name): header, files, `.bin.gz`;
   called by `scripts/build_options.py` after the `.bin.gz` (`firmware.bundle` beside
   it) and by `scripts/make_release.py`. The files' hash goes into a generated header
   (`files_hash.h`) the firmware compiles in. The page is built as today
   (`scripts/minify_page.py`) but written to the bundle, not to `web_index.h`.
   Check: a Python unit test reads back what it writes; a corrupted byte fails the CRC.
2. **Fallback page.** `web/fallback.html`: the setup steps (WiFi, switch networks,
   sign in, Install, or a file), restart, format in recovery; minified and gzipped into
   `web_fallback.h`. Check: `tests/web/` for its step logic (which step opens for a
   clock on or off WiFi, loaded from the AP or the LAN, GitHub reachable or not); the
   emulator's screenshots of each step; the size printed by the build.
3. **Update handler** (`src/main.cpp`, `handleOtaUpload`): tell a bundle from an image
   by its magic; stream files to `/sys.new/` with a CRC each; then the image through the
   existing `FirmwareValidation` path; swap the folders only after `Update.end`. Every
   failure cleans `/sys.new/`. Check: a host unit test of the bundle reader (truncated,
   wrong CRC, too big, unknown version); the safety tests (an image is never committed
   before the files are complete; `/sys/` is not written outside the handler).
4. **Serving and the manifest.** `sendIndex()` streams `/sys/index.html.gz` when the
   manifest matches, otherwise the fallback page with the reason. `/status` gains
   `files`. Recovery always serves the fallback page. Check: integration cases (blank
   storage: fallback page and `files: missing`; after the bundle: the full page; a
   stale manifest: fallback with the line; recovery: fallback).
5. **Protection.** The photo, face and storage APIs refuse paths under `/sys/`.
   Check: a safety-test assertion and an integration case.
6. **Emulator.** `emulator/build.sh` seeds `/sys/` from the build when the volume has
   none (`EMU_SEED_FILES=0` turns it off for the tests of the blank path).
   `scripts/screenshots.py` and the screen page's Settings button keep working.
7. **Install from GitHub.** The Firmware update card (full and fallback pages): the
   latest release from `api.github.com`, its notes, **Install** (download the
   `.update` from GitHub Pages, push it with progress). `release.yml` publishes the
   `.update` to Pages. Check: a `tests/web/` case with a mocked API; the emulator
   (its page online); the owner on a clock.
8. **Docs.** `docs/specifications/updates-and-recovery.md` (the bundle, the fallback,
   the first install), `footprint.md` (sizes), README (updating), the release notes'
   first-install note (`release.yml`), CHANGELOG, CLAUDE.md (release files, "Flash
   budget").
9. **Starter image** (if the owner says go): move the update handler to
   `src/update.cpp`; `src/starter.cpp` (setup screen, WiFi, sign-in, the fallback page,
   the update handler; GitHub's names answered with their real addresses and NAT for
   the setup network once on WiFi, so the setup page's browser download works there as
   from the home network); `env:starter`
   with `build_src_filter` and its size guard; the
   emulator builds it too (`emulator/build.sh` with a flag), and integration cases run
   it on blank storage and install a `.update` from it. `make_release.py` and
   `release.yml` publish `SDPro_AIAgentMonitor_starter_v<version>.bin`; the
   first-install note points at it. Check: the owner flashes it over stock only if a
   stock clock is at hand (the owner's clocks run our firmware: they install it as a
   plain image from our update page, then Install).
10. **README summary, after the owner's tests** (the owner, 2026-11-08: "TODO after the
   testing is completed: on the readme page, a TLDR explain upgrade from original
   firmware to starter to full"): a short box at the top of the README: stock firmware,
   then the starter, then Install.
11. **Release.** MINOR. The release publishes the `.bin.gz`, the `.update` (and the
   starter); `release.yml` checks they exist. Before it: the owner installs the `.bin.gz` and then
   the bundle on a clock (ade9c0 gets new releases first), and checks the page, a
   restart, recovery (power cycles) and a factory format.

## Phase 2 (after the owner weighs open question 3)

- Spec 047's catalog and prompt as files (in 047's own plan, once 048 is out).
- The weather icons and the Message face, each with its fallback, if the owner wants
  the room.

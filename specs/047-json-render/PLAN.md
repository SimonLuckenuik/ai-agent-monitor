# 047 Plan

For the native direction (SPEC.md, 2026-11-07), in the one firmware with every feature
kept (the owner, same day; docs/decisions/2026-11-07-json-render-one-build.md). Nothing
reaches a release until the owner approves the spec. Each step is measured with
`pio run` (the standard build, and `PLATFORMIO_BUILD_FLAGS=-DSMARTCLOCK_JSONRENDER=1`
in its own build directory) and logged in PROGRESS.md.

## Phase 0: room and heap (no feature on the clock yet)

1. Room (RESEARCH.md, "Room for json-render with every feature kept"; 60 KB short
   after LTO and the gzipped catalog):
   - LTO of our sources (`scripts/lto_options.py`, measured: about 20 KB of room): its
     own decision, and a run on a clock (crash decoding, IRAM, timing);
   - read-only data as LittleFS files that come with the update: the settings page,
     the catalog and prompt, weather icons (40-70 KB of room): a spec of its own
     (update format, fallback page in the image, first install from older firmware);
   - the compiler rewritten for size: components as a table, colours as numbers
     (15-20 KB estimated).
2. Heap: a 3.9 KB spec needs about 10.8 KB at once and fails on the clock's arena.
   Measure in the emulator (default `EMU_HEAP`): compiling without the current face
   loaded, an ArduinoJson filter, a smaller thunk. Set the limits (source size,
   elements, compiled items) from that.
3. Check json-render's `repeat` and spec `state` shapes against the source. Check that
   `validateSpec()` accepts specs built from the clock catalog. This needs a session
   where npm installs are allowed.

## Phase 1: image catalog, compile on save

1. `integrations/json-render/catalog.mjs`: the `@json-render/image` components with
   their own props, plus `Clock`, `WeatherIcon`, `Icon`, `Ring`, `Sparkline`, `Sprite`.
   Descriptions say what the clock does with each prop. `scripts/gen_catalog.mjs` →
   `catalog.json` → gzipped into the firmware (like the settings page).
2. `src/jsonrender.cpp` (new; added to `emulator/build.sh` and the test copy lists):
   parse, node array, layout, colours, emit items. Host unit tests: golden spec →
   items, one per layout feature and component.
3. `src/integrations.cpp`: format detection in `POST /api/faces`; source kept as
   `/faces/<name>.jr.json`; warnings in the reply; `/api/catalog` routes in
   `src/routes.h`. Backup and restore keep the source.
4. Examples: `examples/json-render/*.json` (specs written for the image renderer at
   240×240, and clock ones). An integration case that pushes and captures each one, and
   the face preview renders them.
5. Docs and skill: integrations-api.md (a json-render section), the skill (SKILL.md, a
   json-render reference generated from `catalog.json`, `clock.py push` accepts a spec,
   `clock.py catalog`), the OpenClaw skill copy (`gen_openclaw_skill.py`), CHANGELOG,
   CLAUDE.md "Things to keep in step", settings.md if a setting is added.
6. Release: MINOR, one `.bin.gz` as today, once phase 0 has made the room.

Checked by: unit tests, the face preview, the integration suite, emulator screenshots
compared by eye with `@json-render/image` PNGs of the same specs, then the owner's clock.

## Phase 2: the shadcn display set

The display components, then (if the owner wants them) the interactive ones in their
resting state. Same checks; each component gets an example.

## Phase 3 (decided after phase 1 on the clock)

Run-time relayout for `visible` and growing text. SpecStream patches
(`POST /api/faces/patch`, RFC 6902) if agents need incremental edits beyond variables.

## Shipping (approved 2026-11-09)

1. Branch rebuilt on 2.6.0 (`feat/047-json-render`; the earlier one kept as
   `feat/047-json-render-old`).
2. `SMARTCLOCK_JSONRENDER=1` in the firmware's build flags, not the starter's; the build
   adds the catalog files only for the firmware.
3. `examples/json-render/` (six specs and `samples.json`) and the `json_render` integration
   case.
4. Docs: the API reference's json-render section (and the skill's face-format.md from it),
   the skill (SKILL.md, api.md, `clock.py push`/`check`/`catalog`, plugin 0.8.0), the
   OpenClaw skill copy, CHANGELOG, CLAUDE.md.
5. Unit, safety, web and integration tests; a 2.7.0-dev image on agentmonitor-ade1f0 (the
   owner installs it); then the release, 2.7.0.

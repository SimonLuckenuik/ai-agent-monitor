# 047 Plan

For the native direction (SPEC.md, 2026-11-07). Nothing is built until the owner
approves the spec and answers its open questions. Each step is measured with
`pio run` for both builds, and logged in PROGRESS.md.

## Phase 0: groundwork (no feature yet)

1. Build options: `SMARTCLOCK_OPENCLAW`, `SMARTCLOCK_MQTT` (and `SMARTCLOCK_HACS` if it
   is worth it) beside `SMARTCLOCK_MDNS`, and `env:esp12e_jr` in `platformio.ini`.
   `scripts/build_options.py` prints both builds' sizes. The safety tests check that
   both stay within their limits.
2. Measure the heap freed in the emulator (`EMU_HEAP`) with those features out. Set
   the json-render limits (source size, elements, compiled items) from that.
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
6. Release: MINOR, with two `.bin.gz` files. `release.yml` and `make_release.py` build
   both.

Checked by: unit tests, the face preview, the integration suite, emulator screenshots
compared by eye with `@json-render/image` PNGs of the same specs, then the owner's clock.

## Phase 2: the shadcn display set

The display components, then (if the owner wants them) the interactive ones in their
resting state. Same checks; each component gets an example.

## Phase 3 (decided after phase 1 on the clock)

Run-time relayout for `visible` and growing text. SpecStream patches
(`POST /api/faces/patch`, RFC 6902) if agents need incremental edits beyond variables.

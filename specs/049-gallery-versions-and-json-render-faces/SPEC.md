# 049 The gallery knows firmware versions, and json-render faces in it

Status: First set done in 2.7.0 (approved 2026-11-10, the owner: "Approve, first six":
sky, week-ahead, today, home-energy, agent-session, printer); the other six (pr-queue,
on-call, now-playing, departures, activity-rings, laundry) later. Drafted
2026-11-10. From the owner, after the 2.7.0-dev run: "should we add those
json-render to the gallery ? or maybe create new fancy one instead with a specific usage
so that users can try them", then "should the items in the gallery expose the minimum
version so that they are used with the right version ?", then "go".

## Problem

1. **The gallery's version field says nothing.** Every entry in
   `examples/faces/marketplace.json` has `"firmware"`, which spec 026 defines as the
   firmware the face needs, but `scripts/gen_marketplace.py` writes one constant, `2.0.0`,
   into all 74 entries. It is already inexact: 27 faces use the Bebas or Outfit fonts,
   which came in 2.1.0.
2. **Nothing reads it.** The settings page's gallery and `clock.py install` install any
   face on any clock.
3. **json-render faces would fail on older clocks.** A spec sent to a clock before 2.7.0
   gets 400 "Expected JSON with an items array": safe, but a puzzling answer to an
   Install button. Settings pages from 2.1.0 to 2.6.0 read the same `marketplace.json`
   from `main`, and cannot be changed.
4. **No json-render face is worth installing.** The six in `examples/json-render/` were
   written to exercise the compiler (one component of each kind); three overlap gallery
   faces that look better (`ci-pipeline`, `homelab`, `kpi`).

## Goals

1. Each gallery entry states the oldest firmware that draws it correctly, worked out from
   what it uses, not typed by hand.
2. The settings page and `clock.py` do not offer to install a face the clock cannot draw,
   and say which version it needs.
3. json-render faces made for a use, agent and everyday ones, in the gallery, for clocks on 2.7.0 and later,
   invisible to the settings pages that cannot install them.

## Non-goals

- Updating the clock from the gallery (the update check already offers it).
- Versions for clocks before 2.1.0: they have no gallery.
- A json-render editor on the settings page; the face editor already takes a spec as is.
- Firmware changes, also for animation: everything here is the generator, the settings
  page (a file that comes with the update), `clock.py` and examples. No flash cost.

## Requirements

### Versions

- R1. `gen_marketplace.py` computes each entry's `"firmware"`: the highest version among
  the features the face uses, from a table in the script (feature, version, the CHANGELOG
  section it comes from). The floor is 2.1.0, the first firmware with the gallery.
- R2. The table covers at least: the fonts `bebas`, `outfit`, `seg7`, `pixel`, `"off"` and
  `"weight"` (2.1.0); `now.rain_in`, `now.snow_in`, `now.rain_mm`, `now.snow_cm` (2.1.0);
  `msg.kind`, `msg.shown`, `msg.anim` (2.2.0); a json-render spec (2.7.0). Every other
  item type, field and placeholder group is checked against the CHANGELOG from 2.0.0 on
  (PLAN step 1) and added when it is newer than 2.1.0.
- R3. `--check` fails when a face uses an item type, field or placeholder group that
  neither the table nor the list of 2.0.0-era names knows, so a new feature cannot reach
  the gallery without a version.
- R4. The settings page compares an entry's `firmware` with the clock's version (`/status`
  `version`, compared as MAJOR.MINOR.PATCH; `2.7.0-dev` counts as 2.7.0). An entry that
  needs more shows "Needs <version>" in place of Install, disabled, with a line pointing
  at System > Firmware update.
- R5. `clock.py gallery` shows each entry's version; `clock.py install` refuses a face the
  clock is too old for, naming the version it needs (`--force` installs anyway).
- R6. The API reference and the skill document `firmware` as the minimum version, and the
  new `specs` list (R8).

### json-render faces in the gallery

- R7. json-render faces made for a use, to show what a spec can do. Each one has
  sample values, a preview, and a description of what feeds it (a hook, a script, Home
  Assistant over MQTT, a phone shortcut). Agents:
  - **agent-session**: what a coding agent is doing: its task, a to-do list with done and
    pending items, the current step, the last test result.
  - **pr-queue**: open pull requests in a table (number, title, review, CI), and how many
    wait on you.
  - **on-call**: open alerts as status lines by severity, who is paged, time since the
    last incident.

  Everyday, no agent (added 2026-11-10, the owner: "no firmware change just to show the
  possibilities of json render, only new faces"):
  - **today**: the clock, the next meeting with a countdown, the day's agenda, a weather
    line from the clock's own weather.
  - **sky**: sunrise and sunset, the moon's phase in a Ring (`/moon/lit`), day or night
    colours by `$cond` on `/sun/up`. Clock data only: works the moment it is installed.
  - **week-ahead**: five days of weather from the clock's forecast (`/days/N`): icons,
    highs and lows, rain chance as bars. Clock data only.
  - **home-energy**: solar, house and grid power as Metrics with trend arrows (`$cond`:
    importing or exporting), the battery in a Ring, the day's solar as a live Sparkline
    (`$history`). Home Assistant over MQTT.
  - **now-playing**: title, artist, a ProgressBar for the position, a Spinner while it
    plays (`visible`), the source as a Badge. Home Assistant.
  - **departures**: the next buses or trains in a Table (line, destination, minutes), the
    first one large, red when it is about to leave (`$cond`). A small timetable script.
  - **activity-rings**: three Rings (move, steps, water) against their goals, a streak
    Metric, the week as a Sparkline. A phone shortcut.
  - **printer**: a 3D print: progress Ring, layer, time left, nozzle and bed
    temperatures, a Spinner while printing and a StatusLine when done. Home Assistant
    (OctoPrint, Bambu, Prusa).
  - **laundry**: washer and dryer as Cards with state, time left and a Spinner while
    running; "Done" in green until someone empties it. Home Assistant (a smart plug's
    power).

  Each under 6 KB and 96 elements, compiled by the clock without a warning. Movement is
  what a spec can do without firmware changes: a Spinner, the seconds (`/time/sec` in a
  Ring), live bars, rings and sparklines as values arrive, colours and visibility
  switched by `$cond`. Smooth motion (sprites, 20 frames a second) stays with the face
  format.
- R7a. Every face looks complete right after Install, before anything sends it a value:
  plausible demo values in the spec's `state` (applied only to variables not set yet),
  so trying one needs no setup.
- R8. They are listed in `marketplace.json` under a new key, `"specs"`, with the fields of
  `"faces"` plus `"format": "json-render"`. Settings pages before this spec read only
  `"faces"`, so they never offer a spec. The new page lists both, the specs under their
  own category.
- R9. They live in `examples/json-render/gallery/` with a README table like
  `examples/faces/README.md`, values in `samples.json`. The six test examples stay as the
  compiler's examples, outside the gallery.
- R10. The `json_render` integration case pushes and captures the gallery specs too;
  `scripts/screenshots.py` writes their previews to `docs/images/faces/`.
- R11. The skill's SKILL.md points at them as starting points ("install one, then change
  it").

## User-visible behaviour

- Settings > Faces > Gallery: a "json-render" category (on 2.7.0 and later) with the
  faces, each showing its demo values once installed; on a clock too old for a face, "Needs 2.7.0" instead of Install.
- `clock.py gallery` prints the version beside each name; `clock.py install today` on a
  2.6.0 clock: "today needs firmware 2.7.0 (the clock runs 2.6.0): update first, or
  --force".

## Flash and RAM budget

None on the clock: no firmware change. The settings page grows by a few hundred bytes
(gzipped), in a file that comes with the update.

## Open questions

1. Release: with 2.7.0, on the same branch (decided 2026-11-10: the settings page's check
   comes with an update, and the specs need 2.7.0 anyway).
2. Twelve faces is a lot for one piece of work: all of them, or a first set (proposed:
   sky, week-ahead, today, home-energy, agent-session, printer: two that need no data,
   two everyday, one agent, one Home Assistant) and the rest later?
4. Home Assistant recipes (the automation that pushes the values) in the examples'
   README, or only the variable names?
3. Show faces a clock cannot draw (disabled, "Needs 2.7.0") or hide them? Proposed: show
   them, so people see what an update brings.

# 049 Progress

## 2026-11-10

- Came out of the 2.7.0-dev clock run: the owner asked whether the json-render examples
  should go into the gallery or new faces be made for it, then whether gallery entries
  should carry the minimum firmware. Found that they do (`"firmware"`), but as a constant
  2.0.0 that nothing reads. The owner: "go".
- Written: SPEC, PLAN, RESEARCH (draft). Waiting for approval.
- The owner asked for more faces, everyday ones too, with no firmware change: "think about
  no firmware change just to show the possibilities of json render, only new faces". R7
  now lists twelve (eight everyday), with demo values in each spec (R7a); open question 2
  proposes a first set of six.

## 2026-11-10: approved ("Approve, first six")

- First set: sky, week-ahead, today, home-energy, agent-session, printer. Lands with 2.7.0
  on `feat/047-json-render`.
- Version table: the face schema and `clock.py`'s placeholder names compared release by
  release (v2.0.0 to HEAD): everything new arrived in 2.1.0 (the fonts bebas, outfit,
  seg7, pixel, `off`, `under`, `weight`; `now.rain_in`, `now.snow_in`, `now.rain_mm`,
  `now.snow_cm`), nothing after it. `msg.kind`, `msg.shown`, `msg.anim` (2.2.0) were never
  added to `clock.py`'s `FIELDS`: added now.

## 2026-11-10: first set done, released in 2.7.0

- `gen_marketplace.py`: `firmware` per entry from `scripts/gallery_versions.json` (floor
  2.1.0 for faces, 2.7.0 for specs; `--check` fails on a name the schema, `clock.py` or
  the catalog know and the file does not; `--snapshot`); the `specs` list. All 74 faces
  are 2.1.0, the six specs 2.7.0.
- Settings page: "Needs <version>" (disabled) on an older clock, the specs in their own
  category; `tests/web/gallery_versions.test.js` (7 checks, in CI). `clock.py gallery`
  shows versions, `install` refuses an older clock (`--force`).
- The six faces, written against the catalog and looked at in the emulator: week-ahead's
  title wrapped over the first row and today's agenda times overlapped their lines
  (fixed: a shorter title, rows with a fixed time column); the 16 px font has no `°`
  (dropped from small text). All under 4.3 KB minified, no warnings.
- Found on the way: a face upload still redrew the face on screen (the upload hook frees
  its document; now resumed without a full draw: issue record updated); deleting a
  variable kept its history, so a chart pushed again showed old points (now cleared);
  `clock.py`'s `FIELDS` lacked `msg.kind`, `msg.shown`, `msg.anim`.
- The `json_render` integration case pushes and captures the six; previews from it.
- Not checked: the faces and the settings page's gallery on a clock.

## 2026-11-10: the other six faces (the owner: "The other six faces")

- pr-queue, on-call, now-playing, departures, activity-rings, laundry, written with the
  first set's generator and looked at in the emulator (2.7.0), each in its demo state and
  its other one (nothing on you, all quiet, paused, the bus leaving, goals met, machines
  off and running). 2.3 to 4.7 KB minified, 16 to 26 elements, no warnings.
- Fixed on the way:
  - **The 40 variables.** The first drafts of pr-queue and departures read 20 and 18
    variables; pushed together they filled the clock's 40, and later faces lost their demo
    values. Slimmed (a PR's number goes in its title, four departures, no alert ages): 15
    at most. Names chosen not to meet the first set's (`state` would have shown printer's
    value on now-playing): R7b.
  - **Live text is not cut to its box.** pr-queue's titles ran over the review column
    (`maxChars` and `width` size the box; the value draws in full): the columns that must
    stay readable come first, the title last, where an overflow runs off the screen.
  - **Badges with a live label** are sized when the spec compiles, before its state is
    applied (the label measured as "0000"): `maxChars` on each.
  - **Hidden rows.** A dot whose `visible` reads a variable that is not set still drew;
    its colour now falls back to the background instead (`$cond` on the severity or CI).
    on-call's "All quiet" was first placed over the first alert row and lost its tick when
    the rows cleared live (each repaint drew over the other): it has its own line now.
- Once, pr-queue (then 5.5 KB) was refused with 400 "Expected JSON with an "items" array"
  on the 2.7.0 emulator after many pushes; after a reset it compiled every time, also
  with less free heap. Issue record 2026-11-10-spec-refused-as-not-a-face.
- `json_render` integration case: the 12 gallery faces pass on a fresh emulator; previews
  copied from it; `marketplace.json` lists 12 specs, all `firmware` 2.7.0.
- Not checked: on a clock. They reach the settings page's gallery once `main` has them
  (it reads `marketplace.json` from `main`); no release needed.

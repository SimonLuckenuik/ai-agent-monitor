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

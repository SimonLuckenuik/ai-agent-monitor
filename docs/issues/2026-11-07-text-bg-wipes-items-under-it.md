# Text with a `bg` wipes the items under it (a ring cut by its own label)

Date: 2026-11-07

Status: open. Fixed for json-render faces (spec 047, emulator build); the face format's
guidance is not fixed yet

## Symptom

Found by the owner on the spec 047 A/B sheet (`specs/047-json-render/ab/sheet.png`):

- In the face format (arm A, meeting face), the minute count inside the progress ring
  has a square behind it that cuts the top of the ring.
- In json-render (arm B, CI face while running), the ring's centre label `64%` cut
  notches into the left and right of the ring.

## Cause

Opaque text. A text item with `bg` fills a box behind its glyphs, and the panel cannot
be read back, so whatever was under that box is gone:

- fonts 2 and 4 fill `pad` pixels along the alignment;
- the line font and the digit fonts fill the text's box widened by `pad` on each side.

The face's author (agent A) wrote `"bg": "@bg", "pad": 6` on a Bebas number centred in a
ring, as the format suggests for values that change (`specs/047-json-render/ab/A-meeting.json`).
My json-render compiler added `bg` and a `pad` as wide as the layout box to every live
text. That box is the ring's full width for its label.

## Fix

- json-render (spec 047): the compiler no longer gives text a `bg`. When a value
  changes, the renderer repaints the area around the old and new text off screen, with
  the items under and over it (spec 016). Checked in the emulator: ring intact, `64%` →
  `7%` and `402` → `1234` leave no stale pixels; a repaint took 104 ms.
- Face format: not changed. integrations-api.md ("Smooth updates") recommends `bg`/`pad`
  for changing text and does not say that the box covers what is under it. Proposed: say
  so, and recommend leaving `bg` out for text over another item (inside a ring, on a
  picture or a gradient). Then regenerate the skill's `face-format.md` and the OpenClaw
  copy.

## Update 2026-11-07: face-format guidance fixed

Status: fixed (guidance; the firmware was right and is unchanged).

- `docs/specifications/integrations-api.md` ("Smooth updates" in Face JSON, the `text`
  row of the item table, and Gradients) now says that a text `bg` is opaque and wipes
  what is under its box (fonts 2 and 4: `pad` pixels along the alignment; the line font
  and the digit fonts: the text's box widened by `pad` on the left and the right; the
  firmware never widens it up or down), and recommends leaving `bg` out for text drawn
  over another item. The Gradients section used to say that text over a gradient
  "needs its own `bg`": it now says the opposite.
- Regenerated the skill's `reference/face-format.md` and the OpenClaw copy. The skill's
  `SKILL.md` ("Redraws") and the OpenClaw skill's own `SKILL.md` ("No frame buffer")
  limit "give changing text a `bg` and a `pad`" to text on the plain background and
  warn about the box. Plugin 0.7.4.
- Example faces: checked every text with a `bg` in `examples/faces/` against the rings,
  circles, paths, images and rects under it, by box and on the screenshots in
  `docs/images/faces/`. None cuts an item (closest: synthwave's grid ends at y 185 and
  its time's box starts at 188; orbit's ring of dots ends above its time). The skill's
  `examples/progress.json` (`pad` 90 for font 4 inside a ring of inner radius 52) stays
  inside the ring. No face changed.
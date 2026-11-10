# Saving any custom face redrew the one on screen (flicker)

Date: 2026-11-10

Status: Fixed on branch `feat/047-json-render` (for 2.7.0), not released. Seen by the owner
on agentmonitor-ade1f0 (2.7.0-dev).

## Symptom

While the json-render example `jr-status` (Home lab) was on screen, it flickered: the whole
face was drawn again several times in a few seconds. In rotation it showed correctly.

## Cause

At that moment the limit tests were saving and deleting other faces (`jr-keep`, `jr-ok`,
`jr-deep`: seven saves and deletes in about four seconds). Every save or delete bumps the
faces version, and the custom face on screen reloaded and redrew in full whenever the
version changed, whatever face was saved (`custom()` in `src/faces.cpp`, since
SmartClock-OSS 1.2.0). An agent keeping several faces up to date would make the shown one
flicker the same way. Nothing in the Home lab spec itself.

## Fix

- `customFaceChanged(name)`, called by `storeFace()` on a save or delete, marks the face
  on screen stale only when it is that face; the faces version still drives the MQTT
  discovery and the night face's check.
- The `json_render` integration case shows a face, saves and deletes three others (no
  full redraw, from the emulator's `/timing`), then saves the shown one (redrawn).

## 2026-11-10, later: a second cause

The integration check failed on its next run: a face upload still redrew the face on
screen. Every `POST /api/faces` first frees the shown face's document (a hook before the
body is read, spec 024, so the upload fits the heap); the face was then loaded again and
drawn in full. Now `customRelease()` remembers which face it let go of, and when that face
is loaded again unchanged and still on the panel, it is compared with what is drawn (as on
any tick) instead of drawn in full. A save of the face itself still redraws it
(`customFaceChanged()` also matches the released name). Checked in the emulator: uploads
and deletes of other faces, no draw; a save of the shown one, one full draw.

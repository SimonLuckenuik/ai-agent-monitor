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

# 008 Rotation order, night colour and the night face

Status: Done in 1.4.0, extended in 1.6.0 (written after the fact, 2026-10-30)

## Problem

The owner wants to choose the order of the rotation, give a face its own time on screen,
include custom faces, darken the screen at night, and show one face all night.

## Requirements

1. Rotation chips dragged into any order (`faceorder`, then `order` in 1.6.0).
2. Night color (`nighttint`, `nightcolor`): every face in one dim colour, dark grey by
   default; photos and urgent messages keep their colours.
3. At night show (`nightface`): one face all night; a message shown on purpose still
   comes through.
4. 1.6.0: custom faces in the same rotation list (marked custom, and `when` when they
   have a rule), in the night face list and the Display face picker; a per-face time on
   screen (`turns`) for built-in and custom faces; `customoff`.
5. "Use night mode" (was "Dim at night") names what the switch does.

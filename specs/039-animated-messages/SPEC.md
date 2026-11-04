# 039 Animated waiting and done screens

Status: Done in 2.2.0 (approved 2026-11-04)

## Problem

A message is a still screen: the icon, the title and the text. With spec 019 the clock
says when a Claude Code session waits or finishes, but from across the room a still
yellow "?" is easy to miss. smalltv-mod's overlays move: a mascot waves for "waiting",
jumps for "done", an alert flashes.

## Goals

1. On the Message face, in place of the icon, **Moni, the project's robot**: a new, generic
   pixel robot (not Claude's mascot) whose head is a little screen, drawn at 3x scale
   (78 x 84 px). Fixed colours for its shell, steel and blue joints; the message's colour
   for its antenna, its face on the screen and its chest light.
   - `waiting`: a questioning face; it waves for its first 30 s on screen, then 3 s in
     every 10, and blinks now and then;
   - `done`: a happy face, arms up, hopping for 6 s, then still with its arms up;
   - `error`: crossed-out eyes, the head sunk one step.
2. `warning` and `error` flash the whole screen (the panel's own colour inversion, no
   redraw) for their first 4 s. Other icons stay as they are.
3. It starts again each time the message comes back on screen.
4. Still with *Animated messages* off (Display card, `msganim`, on by default) and in
   the night hours.
5. Within the flash room: a target of 3 KB or less (compressed image).
6. The robot is also the project's logo (`docs/images/robot.png`, the README).

## Decisions (owner, 2026-11-04)

- **A figure or moving icons:** a figure: it costs about the same, because the renderer
  already draws sprites.
- **Which figure:** first the claude-mascot face's figure; the owner then asked for
  something generic, not Claude, "some kind of cool robot", with detail and colour, to
  be the project's mascot and logo too. The robot is our own drawing, after the owner's
  references for the genre (a boxy robot, a screen face, antenna, round hands).
- **The name:** Moni (from Monitor; its head is one), the owner's pick.
- **The Agents board:** not animated; only the Message face.

## Design

The Message face is a face template in flash, drawn by the custom-face renderer. The
robot is five sprite items there: body (with the shoulders), left arm (down, up), right
arm (down, up, waving out), head shell, and the face on its screen (questioning, happy,
crossed out, blinking; opaque, so a new face redraws only the screen). A hop moves every
part 6 px up; the error lowers the head and face 3 px. The icon's `show`, `maps` for
which kinds get the robot, the slump or the flash, and `invert` complete it. Three new
`{msg.*}` fields feed them: `kind`, `shown` and `anim`. No new drawing code.
`scripts/gen_robot.py` draws the robot and writes these items (and the logo).

## Non-goals

- New icons or images; animating `info`, `success`, `running` or `bell`.
- The Agents board.

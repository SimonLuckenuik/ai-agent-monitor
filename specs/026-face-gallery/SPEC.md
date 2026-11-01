# 026 The smalltv-pomodoro faces, and a face gallery

Status: In progress (approved 2026-10-31; built; the settings page's card to check once pushed)

## Problem

Faces reach the clock only by hand: a `curl` per face, the skill's `clock.py push`, or the
owner copying JSON. The repo's 39 example faces are listed in a README, but neither the
settings page nor the skill can browse or install them.

[smalltv-pomodoro](https://github.com/davicorrea0/smalltv-pomodoro) (GPL-3.0, the same
licence as this project), firmware for the GeekMagic SmallTV Ultra, publishes 41 faces
in its own layer format, with a `catalog.json` its settings page reads to offer a gallery.
Its faces would add analog dials, pixel-art scenes and display styles we do not have.

## Goals

1. **The 41 faces in our format**, in `examples/faces/smalltv-pomodoro/`, each within the
   4 KB and 32-item limits, converted by a script kept in the repo
   (`scripts/convert_pomodoro_faces.py`) so a newer upstream face can be converted again.
   - English names; five that clash with ours are renamed (`aquarium-reef`,
     `bauhaus-blocks`, `orbit-sun`, `synthwave-sun`, `terminal-green`).
   - No pomodoro: their focus count and daily goal become the variables `done` and `goal`
     (tasks done today, labelled TASKS), which an agent can push. The tomato stays as a
     mascot.
   - Credit: a README in that folder names the author (davicorrea0), links the project and
     the licence, and says what changed; each face's catalog entry says the same.
   - Improved where the conversion allows: animated sprites keep moving, a repeated sprite
     is drawn once with `repeat`, and a few scenes move by expression (the aquarium's
     clownfish swims, the koi circles the pond).
2. **A face gallery file**, `examples/faces/marketplace.json`. It lists every face in the
   repo, ours and the converted ones. Each entry has:
   - its name, description, author, source and licence, a category, and the least
     firmware version it needs;
   - its size and the variables it reads;
   - links to its JSON file and its preview image, as full `raw.githubusercontent.com`
     URLs on `main`, so anyone can fetch both without cloning the repo; also the paths
     in the repo. One script (`scripts/gen_marketplace.py`) builds it from
   the face files and a small metadata file, and CI fails when it is out of date, as for
   the skill's face reference.
3. **Previews** for every gallery face, drawn by the emulator (the integration suite's
   `example_faces` case already saves them) into `docs/images/faces/`.
4. **The settings page browses and installs** from the gallery: a "Face gallery" card reads
   `marketplace.json` from the repo's `main` branch on GitHub (as the update check reads
   the releases), shows each face with its preview, marks the installed ones, and installs
   one with a click (`POST /api/faces`, signed in). It shows how many of the 40 places
   are used, and refuses with that message when they are full.
5. **The skill can use it**: `clock.py gallery` lists the gallery, `clock.py install
   <name>` installs a face from it.
6. **Within the budgets.** The compressed update stays under its limit (98% today: this
   waits for the flash that spec 027 frees); the faces pass `example_faces_memory` on the
   emulator's heap.

## Non-goals

- A server or an upload flow: the gallery is a file in the public repo; new faces arrive
  by pull request.
- Their fonts (spec 030 takes their rendering technique instead), their pomodoro,
  streaks or Alexa.
- Installing from any URL other than this repo's.

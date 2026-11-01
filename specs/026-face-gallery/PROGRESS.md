# 026 Progress

- 2026-10-31: proposed. A scratch converter turns all 41 upstream faces into ours within
  4 KB and 32 items; the owner saw them in the emulator beside their previews and asked
  for the pomodoro concept to go and for improvements where useful.
- 2026-11-01: built (goals 1-5; goal 6's flash came from spec 027).
  - `scripts/convert_pomodoro_faces.py` writes the 41 faces and `faces.json` into
    `examples/faces/smalltv-pomodoro/`, with a README for the credit. Five names
    clashed with ours, not three. A label rule turned "ONE FOCUS AT A TIME" into
    "ONE TASKS"; fixed.
  - Integration: `example_faces` and `example_faces_memory` run all 80 faces (the
    converted ones with `done` 5, `goal` 8, `streak` 3). All 30 cases pass; under the
    clock's memory the lowest free heap was 5,040 B, with no crash.
    `faces_beside_big_faces` stays on our own faces: 80 do not fit the 40 places.
  - `scripts/gen_marketplace.py` builds `marketplace.json` from the README tables and
    `faces.json`, and copies previews from an integration run. CI checks it, and the
    skill's schema check now covers the converted faces.
  - The settings page's gallery card and the skill's `gallery` and `install` read the
    file from `main` on GitHub; they work once it is pushed.

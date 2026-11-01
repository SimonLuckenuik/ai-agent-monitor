# 026 Plan

1. **Converter.** `scripts/convert_pomodoro_faces.py <their faces dir> <out dir>`, from the
   scratch converter: the table in RESEARCH, the size steps, the by-hand changes, English
   names, and a report of each face's size and items. Output in
   `examples/faces/smalltv-pomodoro/`, with a README (credit, licence, upstream commit,
   what changed, the variables `done` and `goal`).
2. **Tests.** `example_faces` and `example_faces_memory` include the subfolder; samples
   for `done`, `goal`, `streak`. Run the memory case on the emulator's heap.
3. **Gallery file.** `examples/faces/gallery-meta.json` (name, description, category,
   author, source, licence per face) and `scripts/gen_marketplace.py` building
   `examples/faces/marketplace.json` (adds file, size, variables read, preview, least
   firmware). CI check: out of date fails, a face over 4 KB fails, a missing preview fails.
4. **Previews.** The integration run saves each face's screen; copy them to
   `docs/images/faces/` (ours are there already; add the 41).
5. **Settings page.** A "Face gallery" card: fetch `marketplace.json`, a grid of previews
   with name and description, Installed / Install, the count of 40. Install fetches the
   face JSON and posts it. Measure the panel's compressed size before and after.
6. **Skill.** `clock.py gallery` and `clock.py install <name>`; the skill's docs; plugin
   version bump.
7. **Docs.** `examples/faces/README.md` (the new folder, the gallery), the integrations
   API doc (where the gallery lives), CHANGELOG, STATUS, roadmap.

Order: 1-4 can land before spec 027; 5 waits for its flash.

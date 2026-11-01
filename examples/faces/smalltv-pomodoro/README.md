# Faces from smalltv-pomodoro

41 faces by **davicorrea0**, from
[smalltv-pomodoro](https://github.com/davicorrea0/smalltv-pomodoro) (firmware for the
GeekMagic SmallTV Ultra), converted to this clock's face format. Both projects are
licensed under the GPL-3.0, and these faces stay under it.

The originals are layers for that firmware's own renderer. They were converted from
commit `2f6836d` by `scripts/convert_pomodoro_faces.py` (spec 026), which can be run
again on a newer copy of their `faces/` folder:

```bash
python scripts/convert_pomodoro_faces.py <their faces/ folder>
```

## What changed

- **No pomodoro.** Their focus count and daily goal are the variables `done` (tasks done
  today) and `goal` (8 when not set), labelled TASKS. An agent pushes them:
  `curl -X POST "http://<clock>/api/faces/vars?key=<api key>" -d '{"done": 5, "goal": 8}'`.
  `streak` replaces their streak of days. The tomato stays, as a mascot.
- **Our fonts.** Their pixel font and their digit fonts (Bebas Neue, Outfit, Righteous,
  DSEG7, Geo) became our big line font at the same height and width, or fonts 2 and 4
  where the text needs lower case. Unlit seven-segment digits were dropped.
- **Within our limits** (4 KB and 32 items a face):
  - a sprite placed several times is drawn once by a `repeat`, which handles the snow in
    winter and the rain in window;
  - long animations keep every other frame, at the same speed;
  - in aquarium-reef, the water is one gradient and the clownfish swims back and forth;
  - in koi, one koi circles the pond;
  - nixie's filament lines are repeats.
- **Names.** English, and five renamed where we already had the name: aquarium-reef,
  bauhaus-blocks, orbit-sun, synthwave-sun, terminal-green.

`faces.json` lists each face with its title, description, author and the original file.

# Faces from smalltv-pomodoro

33 faces by **davicorrea0**, from
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

- **No pomodoro, no tasks.** Their focus counts, daily goals and slogans are gone; the
  bars and rings that showed them show the day's progress instead. greenhouse keeps a
  streak of days (`streak`, pushed by an agent). The tomato stays, as a mascot.
- **Fonts.** Bebas Neue, Outfit, seven segments (with the unlit segments) and pixel
  digits are drawn with the clock's own versions of them (spec 031); Righteous, Geo and
  their pixel font became our big line font at the same height and width, or fonts 2
  and 4 where the text needs lower case.
- **Within our limits** (4 KB and 32 items a face):
  - a sprite placed several times is drawn once by a `repeat`, which handles the snow in
    winter and the rain in window;
  - long animations keep every other frame, at the same speed;
  - in koi, one koi circles the pond;
  - nixie's filament lines are repeats.
- **Reviewed against ours** (2026-11-01): 8 faces too close to ours or to each other were
  dropped (their aquarium, orbit, terminal, giant Bebas time, duo, mono, station and
  roman), and a few were improved (compass has a seconds hand, gradient's rings are
  smooth, fitness has one day row).
- **Names.** English, and two renamed where we already had the name: bauhaus-blocks and
  synthwave-sun. Their project's name on the faces became the clock's ("SMALLTV" reads
  "MONITOR").
- **Categories.** Each face sits in one of the gallery's categories, beside ours.

`faces.json` lists each face with its title, description, author and the original file.

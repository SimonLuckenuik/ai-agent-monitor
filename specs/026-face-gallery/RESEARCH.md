# 026 Research

## Their face format (smalltv-pomodoro `facepack.cpp`, commit 2f6836d)

A face is `{"format":1, "id", "name", "desc", "name_en", "desc_en", "bg", "sprites", "layers"}`.
Layer types and what each becomes here:

| Theirs | Ours |
|---|---|
| `text` in `pixel` (6×8 cells × `size`) | `"font": "big"` at 8 × size px; font 2 or 4 for sizes 2 and 3 without `°` (lower case and symbols) |
| `text` in `bebas`, `outfit`, `righteous`, `geo`, `dseg`, `dm` (digits only) | `"font": "big"` at their height, `w` set to their width (Bebas 1.35×, the others 1.1×, never past the screen) |
| `text` `glow`, `pulse` | One copy behind, 2 × spread px larger, in the glow colour; a pulse shows it half of each period |
| `text` `off` (unlit segments) | Dropped: "88" in our font does not line up with the digits |
| `rect`, `circle`, `stroke` | `rect`, `circle`; a stroke becomes a second, unfilled item with `border` |
| `line`, `bar` | `line` (`size` → `w`), `bar` |
| `arc` | `ring` |
| `hand` (`hour`, `min`, `sec`, `len`, `tail`) | `line` with `sin`/`cos` expressions |
| `ticks` (`count`, `r`, `len`) | `repeat` of `count` lines |
| `weather` | `icon` `{now.icon}`, 48 × size/10 px |
| `sprite` (`pal`, `frames` or `px`, `fps`) | `sprite` with `palette` and `frames`; `frame` from `{time.sec}`, `{time.ms}` and their fps |
| `mascot` (the tomato, by mood) | Their `tomate` sprite (from `tomate-pulando.json`), bouncing |
| `plant` (grows with the streak) | A small plant sprite |

`when` → `show`: `day`/`night` → `{sun.up}`; `rain`/`dry` → WMO codes 51-67, 80-82, 95+;
`goal`/`nogoal` → `{done|0}` against `{goal|8}`; `weather` → always (our placeholders show
`--` until the weather loads); `noweather` → dropped.

Placeholders: `{HH}` → `{time.hour:02}`, `{MM}` → `{time.min}`, `{WD}` → `{date.wday3}`,
`{MON}` → `{date.month3:u}`, `{TEMP}` → `{now.temp}`, `{TMIN}`/`{TMAX}` → `{day0.low}`/
`{day0.high}`, `{WX}` → `{now.text:u}`, `{CITY}` → `{loc.city:u}`, `{FOCOS}` → `{done|0}`,
`{META}` → `{goal|8}`. Their named colours (`card`, `muted`, `work`…) have fixed values;
`accent` (the user's number colour) becomes `@time`.

## Sizes

As converted, 31 of 41 fit in 4 KB. The other 10 are pixel scenes:
- **Repeated sprites:** a sprite placed several times (13 snowflakes in `inverno`, 3 waves
  and 3 leaves in `koi`) becomes one `repeat` with its places in two `maps`.
- **Frames:** while a face is over 4 KB, the biggest animation keeps every other frame
  at half the rate (the same speed); at 2 frames, it becomes one frame that drifts.
- **By hand:** `aquario`'s eight water bands become one gradient; `koi` keeps one fish,
  which circles the pond; `nixie`'s 14 filament lines become repeats (41 items → 31).

All 41 then fit, and draw in the emulator next to their previews
(scratch sheet, 2026-10-31). Free heap during the 40 uploads fell to 4,144 B (emulator):
the memory case must pass before they are added.

## Their gallery

`faces/catalog.json` lists `id`, `name`, `author`, `desc`, `file`, `preview` (a PNG drawn
by their simulator) and `size`. Their simulator builds the catalog and previews; the
settings page offers "More faces" from their website; new faces arrive by pull request
with the JSON, the preview and the updated catalog (`docs/PACOTES.md`).

Ours can read the file from `raw.githubusercontent.com`, which answers with
`Access-Control-Allow-Origin: *`, as the update check already reads the GitHub API.

## Their fonts

`fonts.cpp`: outline fonts drawn anti-aliased by a scanline fill (4 sub-lines per row,
1/16 px edge coverage, blended with a solid background colour, `malloc` of 3 × width
bytes per string). Digits and `:` only for Bebas Neue, Outfit and Righteous (737-840
outline points each, about 3-3.4 KB of flash), Outfit with letters for text (2,653 points,
about 10.6 KB). DSEG7 and Geo are drawn by code; DM is 5×7 pixel art. All are SIL OFL.

Ours: TFT_eSPI fonts 2, 4 and 6 (bitmaps, no anti-aliasing), and the "big" line font at
any size (strokes, capitals, digits and symbols). A digits-only outline font would give the
large, smooth time most faces are built around, for about 3 KB each plus the renderer.
Not imported: spec 030 takes their row-by-row rendering for our own stroke font instead.

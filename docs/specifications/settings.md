# Settings: rotation, night face, palettes and roles

The settings page writes these through `POST /api/set` (`key`, `value`), signed in.
They are saved in `/config.json`, appear in `GET /config`, and travel in a settings
backup. Values that are lists or objects are sent as text: a comma list, or JSON.

## Faces: built-in and custom, in one list

A **face entry** names one face: `#<id>` for a built-in face (`#0` Classic, `#7` Big
time, … the ids of `/api/set?key=theme`), or the name of a custom face (`calendar`).
Custom face names are `a-z 0-9 _ -`, so they never start with `#`.

| Key | Value | Meaning |
|---|---|---|
| `order` | Comma list of entries: `#7,#3,calendar,#0` | The rotation's order. Built-in faces missing from it go after it, in id order; custom faces missing from it go after those, so a newly pushed face joins at the end. Entries for faces that no longer exist are skipped (a face pushed again under the same name takes its old place) |
| `faces` | Bit mask of built-in ids | Built-in faces that take part in the rotation (as before) |
| `customoff` | Comma list of custom face names | Custom faces left out of the rotation. A custom face is in the rotation unless listed here |
| `turns` | Comma list of `entry=seconds`: `#7=20,calendar=45` | A face's own time on screen, 3 to 3600 seconds. Without one, a custom face uses its `duration`, then the rotation interval (`themeInterval`) |
| `nightface` | An entry, or empty | During night mode, this face only (built-in or custom). Empty, or `-1`, is the rotation. A plain number is a built-in id (older settings) |
| `customface` | A custom face name | Shows that face now (as `theme` does for a built-in face) |
| `faceorder` | Base-36 built-in ids (older settings) | Accepted: puts the built-in faces in that order, custom faces after them |

Custom faces appear in these lists only while the Custom faces integration is on.
Agent messages take their turn after the list, as before. A custom face's `when`
still applies in the rotation; it does not apply to the night face, which shows all
night. Showing a face on purpose (`theme`, `customface`, `/api/faces/show`) ignores
`when` and the rotation switches.

`GET /config` gives `order` as the complete list (every built-in face, then every
custom face, in rotation order), `customoff`, `turns`, `nightface` (an entry, or ""),
and `customs`: one object per custom face, `{"name": "calendar", "when": true,
"duration": 20}` (`when`: the face has a `when` rule; `duration`: its own, or 0).

## The screen

The display controller (ST7789) does these itself, so they cost no drawing time.

| Key | Value | Meaning |
|---|---|---|
| `orientation` | `0`, `90`, `180` or `270` | Turns everything on the screen clockwise by that many degrees, for a clock standing on its side or upside down (MADCTL). Faces are square, so they need no change |
| `gamma` | `2.2`, `1.8`, `2.5` or `1.0` | The panel's contrast curve (GAMSET): `2.2` standard, `1.8` lighter mid-tones, `2.5` deeper, `1.0` flat |
| `nightgamma` | as `gamma`, or empty | The curve during night mode; empty keeps the day's |
| `nightoff` | `1` or `0` | During night mode the panel is turned off (DISPOFF and sleep), not only its backlight. A warning or error message wakes it while it is on screen |

A custom face's `invert` (see the integrations API) flips the panel's colours the same
way, for flashing.

## Palettes: named colours

A **palette** is a set of named colours. **Day** and **night** usually hold the same
names with different values, so every face that uses the names changes with them.

| Key | Value | Meaning |
|---|---|---|
| `palettes` | JSON: `{"day": {"accent1": "#ffa826", …}, "night": {"accent1": "#8a5a14"}}` | Every palette. Up to 8 palettes, up to 16 colours each; names are 1 to 15 of `A-Z a-z 0-9 _`; colours are `#rrggbb` |
| `palette` | A palette name (`day`) | The palette in use by day |
| `nightpalette` | A palette name, or empty | The palette in use during night mode; empty uses the day palette |
| `color1`, `color2`, `color3` | RGB565 number (older settings) | Set `accent1`, `accent2`, `accent3` in the `day` palette |

A palette **inherits** from `day`: a colour it does not define is the `day` palette's
colour of that name, so a colour added to `day` exists in every palette. The default
`night` palette has every colour of the default `day` palette, dimmer, for a dark
room. `day` cannot be removed.

Default `day` palette: `accent1` `#ffa826` (amber), `accent2` `#6ab7ff` (sky blue),
`accent3` `#4cd9a0` (mint), `black` `#000000`, `white` `#ffffff`, `rain` `#5ab2ff`,
`green` `#30d158`, `orange` `#ff9f0a`, `red` `#ff453a`, `blue` `#0a84ff`. Default `night`
palette: `accent1` `#a86a14`, `accent2` `#3a6694`, `accent3` `#2a7a5c`, `black`
`#000000`, `white` `#8e8e93`, `rain` `#2f6496`, `green` `#1e7a35`, `orange` `#a85f0a`,
`red` `#a3271f`, `blue` `#0a4f99`. Settings from before palettes keep their
`color1`-`color3` as the day's `accent1`-`accent3`.

"Night color" (`nighttint`, `nightcolor`) is separate and unchanged: it turns every
colour drawn at night into shades of one colour, photos aside.

## Roles: which colour each part of a face uses

A **role** is a part of the faces. Each is bound to a colour:

| Role | Used for | Default |
|---|---|---|
| `bg` | Background | `black` |
| `text` | Main text and numbers | `white` |
| `muted` | Labels, units, secondary text | automatic: 60% of the way from `bg` to `text` |
| `panel` | Cards, tracks, dividers | automatic: 10% of the way from `bg` to `text` |
| `time` | Clock digits and hands | `accent1` |
| `seconds` | The seconds bar | `accent2` |
| `date` | Day and month names | `accent1` |
| `highlight` | Today's card, the weather description | `accent2` |
| `icon` | Weather icons | `accent1` |
| `weather` | Classic's temperature | `accent3` |
| `high`, `low` | A day's high and low temperatures | `@text`, `@muted` |
| `rain` | Chance of precipitation | `rain` |
| `sun` | Sunrise and sunset times | `@text` |
| `ok`, `warn`, `error`, `info` | Agent messages by icon (success and done; warning, bell and waiting; error; info and running), on the Agents board and the Message face's icon and title | `green`, `orange`, `red`, `blue` |

| Key | Value |
|---|---|
| `roles` | JSON: `{"time": "accent1", "weather": "{now.temp}<0?@blue:@orange", …}`. Roles left out take their default |

A role's value is one of:

- a **palette colour's name**: `accent1`;
- **another role**: `@text`;
- a **colour**: `#336699`;
- empty: the default (for `muted` and `panel`, the automatic mix);
- an **expression**, as in custom faces (at most 120 characters): placeholders and
  `?:` choose between colours, written `@name` (a role, else a palette colour):
  `"{now.temp}<0?@blue:{now.temp}<20?@accent3:@orange"`, `"{sun.up}?@accent1:@blue"`,
  `"{build}==fail?@red:@accent2"`. It is evaluated about once a second; when its
  result changes, the face on screen is redrawn.

A role cannot refer to itself, directly or through others (the chain stops after 4
steps and the default is used).

## In custom faces

- `@name` in a face is, in order: the face's own palette (or the one of its `palettes`
  that `palette` selects); the clock palette that `palette` names, when the face has
  no palette of that name (`"palette": "night"`); a **role** (`@time`, `@bg`, `@warn`);
  a colour of the clock's palette in use (`@accent1`, `@red`).
- A face without `bg` has the `bg` role's colour; an item without `color` has the
  `text` role's; bar and ring tracks default to a mix of `panel` and `text`; a weather
  `icon` defaults to the `icon` role. A face that sets its own colours looks the same
  whatever the palette.
- `{theme.<role>}` is a role's colour as `#rrggbb` (`{theme.time}`); `{theme.color1}`,
  `{theme.color2}`, `{theme.color3}` remain, as `time`, `highlight` and `weather`;
  `{theme.palette}` is the palette in use.

## Settings page

- **Display:** the face picker lists the built-in faces, then the custom faces (with
  a custom-face mark), and shows the one tapped. The rotation is one list of chips,
  built-in and custom alike: a tap turns a face on or off, a drag moves it, and a
  custom face's chip has a `{ }` mark (and `when` when it has a rule). Each chip's ⋯
  sets its own time on screen.
- **Night mode:** "At night show" lists the rotation, the built-in faces and the
  custom faces.
- **Colours:** palette tabs (one per palette, and +) with their colours (pick, rename,
  remove, add), the day and night palette choices, and the roles: the main seven
  always shown, the others under "More roles", each a drop-down of the palette's
  colours, the other roles, and "Rule…" for an expression.

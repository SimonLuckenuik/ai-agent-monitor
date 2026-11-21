# Drawing a message left the clock 224 B of stack

Date: 2026-11-21

Status: Fixed on `feat/047-json-render` (for the next release), not released: 304 B more
stack under every face's drawing. Measured on the chip build; not yet seen on a clock.

## Symptom

On agentmonitor-ade1f0 (2.7.1), `stackFree` in `/status` (the least free of the 4 KB stack
since boot) fell from 464 B to 224 B while it showed Moni's waiting, done, error and
warning messages (spec 039). On agentmonitor-adec8a it fell from 484 to 352 B on the
"done" message (Moni hopping), and stayed there for the others. The other clocks, which
had shown no message, were at 640 to 984 B. No crash: the chip's stack overflow is a
panic, so a deeper path would have reset the clock.

## Cause

The face drawing runs at the bottom of `loop()`, so `loop()`'s whole frame is on the
stack under it. A moving item (Moni hopping, waving) is drawn through the deepest path:
`loop` → `faceDraw` → `renderItems` → `repaintBox` (the box redrawn off screen) →
`drawItem` → the expression evaluator (`num`, `evalNum`, `resolve`, `evalStr`).

The chip's frame sizes, from a build with `-fstack-usage -save-temps=obj` added to the
link (LTO decides the frames there; the per-file `.su` files have none for `src/`):

| Function | Frame |
|---|---|
| `loop` | 656 B |
| `faceDraw` | 224 B |
| `renderItems` | 336 B |
| `repaintBox` | 176 B |
| `drawItem` | 496 B |
| `evalNum`, `resolve`, `evalStr` | 336, 272, 352 B |

`loop`'s 656 B is mostly what LTO inlined into it: the OpenClaw node's `readFrame()` (a
125-byte buffer and the handlers it inlines, signing included) and
`startOptionalServices()` (run once after boot). Their slots stay in `loop`'s frame while
a face draws.

The emulator does not show it: its frames are x86 ones. There a message took 1,920 B and
the weather's geocoding (which runs only when the city changes) 3,168 B, the deepest
path (with `EMU_STACK` at 3,000 and 3,140 B it overflowed there first).

## Fix

`readFrame()`, `startOptionalServices()` and `faceDraw()` are `noinline`: `loop` is 352 B
and `faceDraw` 224 B, 576 B on the drawing path instead of 880 B. 304 B more stack under
every face; the image +192 B, its `.bin.gz` +56 B. The safety tests check the three
attributes stay.

Tried on the way (frames with LTO, image and `.bin.gz` growth):

| Out of line | `loop` (+ `faceDraw`) | Image, `.bin.gz` |
|---|---|---|
| `startOptionalServices` | 624 + 224 | -32, -578 |
| `startOptionalServices`, the web and DNS servers (a wrapper) | 640 + 224 | +16, +679 |
| `serviceWeather` | 656 + 224 | +48, -438 |
| `openclawLoop` | 368 (`faceDraw` inlined) | +544, +1,170 |
| `readFrame` | 464 (`faceDraw` inlined) | +720, +463 |
| `readFrame`, `faceDraw` | 448 + 224 | +112, -49 |
| `readFrame`, `faceDraw`, `startOptionalServices` (kept) | 352 + 224 | +192, +56 |

(`.bin.gz` sizes move by several hundred bytes with any change of layout.)

## Checked

Unit tests (436), safety tests, the integration suite (40 cases). Not checked: `stackFree` on a clock
showing messages, after the release.

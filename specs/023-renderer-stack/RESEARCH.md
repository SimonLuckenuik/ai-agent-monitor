# Research

Chip stack frames from `-fstack-usage` (xtensa-lx106 gcc, the release build's flags),
2026-10-31.

| Function | Before | After |
|---|---|---|
| `renderItems` | 1,008 B | 400 B |
| `drawItem` | 448 B | 448 B |
| `faceDraw` | 272 B | 272 B |
| `resolve` | 272 B | 272 B |
| `itemBox` (recursive for repeats) | 160 B | 160 B |
| `repaintBox` | inlined into `renderItems` | 144 B |
| Parser, per level of parentheses | about 500 B (`ternary` 96, `orOp`, `andOp` 48, `cmp` 112, `add` 64, `mul` 80, `unary` 96, `primary`) | 0 |
| Parser, in all | grows with nesting | about 600 B (`evalNum` 336 with the stacks, `parse` 80, `apply` 112, `pop` 48) |

Worst paths after the change, on the chip's frames (plus the core's loop wrapper):

- retro-runner: `loop` 112, `faceDraw` 272, `renderItems` 400, `itemBox` 160 x 2, `inum`
  48, `num` 64, `evalNum` 336, `parse` 80, `apply` 112: about 1.8 KB (was about 3.8 KB).
- flip-clock-wide (repaint): `loop`, `faceDraw`, `renderItems`, `repaintBox` 144,
  `drawItem` 448, `itemBox` 160, `bigLayout` 112, `evalStr` 352, `parse`, `apply`: about
  2.3 KB.

Flash: +364 B compressed (419,433 B, 96% of the update limit).

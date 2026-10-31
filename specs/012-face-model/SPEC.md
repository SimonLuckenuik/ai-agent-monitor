# 012 A richer custom face model

Status: Done in 1.5.0 (written after the fact, 2026-10-30)

## Problem

Faces needed more built-in data (weather today and forecast, sun, moon, network,
system), clearer names, conditional rendering and more item types, so agents can build
dynamic UIs. The owner asked for short, clear, semantic names and to drop the old ones.

## Requirements

1. Placeholders in groups: `time.`, `date.`, `now.`, `day0.`-`day3.`, `sun.`, `moon.`,
   `units.`, `loc.`, `theme.`, `msg.`, `net.`, `sys.`, `age.<var>`. Old names dropped
   (1.5.0 CHANGELOG lists old to new).
2. Modifiers (`:u`, `:03`, `:12`, `:.1`, `:+`), maps, expressions in text, `rand(n)`;
   double precision.
3. Palettes (`@name`, chosen by an expression).
4. Items: `sprite` (frames), `spark` (history), `repeat` (days, messages, a number),
   `border`/`bw`. No base64 images.
5. Rotation: `when` and `duration`.
6. Variable options: `$ttl`, `$persist`, `$history`, `$add`; `null` deletes; names
   `A-Z a-z 0-9 _`, 1-15.
7. Up to 40 faces and 40 variables. JSON Schema with the skill. Docs for agents.

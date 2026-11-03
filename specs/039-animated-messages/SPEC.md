# 039 Animated waiting and done screens

Status: Draft (proposed 2026-11-03, after smalltv-mod's notification overlays; not approved)

## Problem

A message is a still screen: the icon, the title and the text. With spec 019 the clock
says when a Claude Code session waits or finishes, but from across the room a still
yellow "?" is easy to miss. smalltv-mod's overlays move: a figure waves for "waiting",
jumps for "done", flashes for an alert.

## Goals (to agree)

1. A short loop per icon on the Message face: `waiting` waves or pulses, `done` bounces
   once and settles, `warning` and `error` flash their colour a few times, `running`
   spins (it already turns), others stay still.
2. Drawn from what the clock has: the icons' own shapes moved, scaled or recoloured, and
   only the icon's box repainted, so a frame costs a few milliseconds and no new images
   are stored.
3. An option to keep messages still (Settings), and night mode keeps them still.
4. Within the flash room (28 KB): a target of 3 KB or less.

## Open questions

- A small figure of our own (smalltv-mod uses a pixel mascot; a Claude-like one would be
  a new drawing, not theirs) or only the icons moving?
- The Agents board too, or only the Message face?

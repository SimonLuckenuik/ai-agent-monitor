# 017 Example faces gallery

Status: Done (1.4.0 to 1.8.0); grows with new faces

## Problem

Agents and users learn the format best from working faces covering each feature, with
pictures.

## Requirements

1. `examples/faces/*.json`, each at most 4 KB minified and 32 items, valid for
   `clock.py check`.
2. `samples.json` holds each face's variables, histories, messages, time shift and
   weather; the integration tests push and capture every face.
3. `examples/faces/README.md`: a picture and what each face shows how to do.
4. Faces requested by the owner: imaginative uses of time, date and weather; a Claude
   Code mascot, something Minecraft-like, a retro game; an alarm that flashes; flip
   clocks (two rows, one line).

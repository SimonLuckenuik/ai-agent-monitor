# 006 Claude Code plugin and skill

Status: Done (0.2.0 in 1.4.0, now 0.5.3); kept in step with the firmware

## Problem

Agents need to know how to reach the clock, send messages, design and validate faces,
without reading the firmware.

## Goals and requirements

1. A plugin in this repository (`plugins/ai-agent-monitor/`) with a marketplace file at
   the root, enabled in the repository's `.claude/settings.json`.
2. The skill explains the API, messages, the face format with examples, limits, and how
   to reach the clock or the emulator.
3. `scripts/clock.py`: `message`, `push`, `show`, `vars`, `faces`, `check`, `screenshot`.
4. `reference/face.schema.json`, packaged with the skill; `clock.py check` validates
   faces and placeholder names before pushing.
5. `reference/face-format.md` is generated from the API spec
   (`scripts/gen_face_format.py`, checked in CI).
6. Every firmware change to messages, faces, placeholders or limits updates the skill
   and bumps the plugin version (CLAUDE.md "Things to keep in step").

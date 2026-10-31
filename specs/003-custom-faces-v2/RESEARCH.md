# Research

- Brainstorm with the owner (2026-10-25): templates and raw drawing mixed; one face per
  agent, several if needed; animation wanted; overlay icons and MCP dropped; a full
  frame buffer considered and rejected (RAM).
- Flash: libc float parsing ~9 KB, `atan2f`/`fmodf` and float printf are expensive;
  ArduinoJson lookups inline badly (go through `js()`, `num()`, `colorOf()`).
- mDNS (~20 KB) became a build option to make room (later back in the full image once
  updates were compressed, spec 009).

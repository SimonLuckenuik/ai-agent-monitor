# A new face variable is dropped silently when the table is full

Status: Open

Date: 2026-10-29

## Summary

`setVar()` in `src/integrations.cpp` ignores a new variable when all 40 slots are in use;
the API reports success. An error (or evicting expired values first) was proposed and
not yet done.

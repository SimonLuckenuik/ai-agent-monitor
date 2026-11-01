# A new face variable is dropped silently when the table is full

Status: Fixed (unreleased, after 2.0.0)

Date: 2026-10-29

## Summary

`setVar()` in `src/integrations.cpp` ignores a new variable when all 40 slots are in use;
the API reports success. An error (or evicting expired values first) was proposed and
not yet done.

## 2026-10-31: fixed

`setVar()` now returns false when there is no room. A new variable first clears the
expired ones (`freeVar()`); if none has expired, `POST /api/faces/vars` answers 507
"No room for variable \"<name>\": all 40 in use; delete some with null". The other
variables in the request are still set, a change to one already there still works, and
deleting a missing one is not an error. MQTT's `<base>/vars` sets what fits (no reply to
carry an error). Integration case `variables_table_full`.

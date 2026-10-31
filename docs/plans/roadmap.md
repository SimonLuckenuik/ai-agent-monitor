# Roadmap

Updated: 2026-10-31. The order of work. Each line is a spec in [specs/](../../specs/README.md)
or an idea that becomes one when work on it starts; nothing is built before its spec is
approved.

## In progress

1. [023 Custom faces within the 4 KB stack](../../specs/023-renderer-stack/SPEC.md): the fix
   for the faces that restarted the clock.
2. [022 The emulator with the clock's memory](../../specs/022-emulator-device-memory/SPEC.md):
   heap, stack, crash traces, live device statistics.
3. [021 Crash records and crash-loop protection](../../specs/021-crash-report/SPEC.md).

## Next (proposed, waiting for approval)

1. [018 Check 1.5-1.8 on the real clock](../../specs/018-first-run-on-the-clock/SPEC.md):
   nothing after 1.4.0 has run on the clock.
2. [019 Claude Code hooks that drive the clock](../../specs/019-claude-code-hooks/SPEC.md).

## Ideas (no spec yet)

- An error, or eviction of expired values, when the variable table is full
  ([issue](../issues/2026-10-29-new-variable-dropped-when-table-full.md)).
- [020 ESP-NOW between clocks](../../specs/020-esp-now/SPEC.md): dropped for now (2026-10-31).
- Hardware scrolling and the panel's idle (8-colour) mode (spec 014 research).
- A way back to stock over WiFi (upstream idea).
- Flash: about 25 KB of update room left (618,496 B limit, 96% used). The next large
  feature needs a saving first (mDNS is ~21 KB).

## Done

Specs 001-017, released from 1.4.0 to 1.8.0: see [specs/README.md](../../specs/README.md)
and `CHANGELOG.md`.

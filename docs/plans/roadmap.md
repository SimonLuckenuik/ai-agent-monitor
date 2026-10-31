# Roadmap

Updated: 2026-10-30. The order of work. Each line is a spec in [specs/](../../specs/README.md)
or an idea that becomes one when work on it starts; nothing is built before its spec is
approved.

## In progress

None.

## Next (proposed, waiting for approval)

1. [018 Check 1.5-1.8 on the real clock](../../specs/018-first-run-on-the-clock/SPEC.md):
   nothing after 1.4.0 has run on the clock.
2. [019 Claude Code hooks that drive the clock](../../specs/019-claude-code-hooks/SPEC.md).

## Ideas (no spec yet)

- An error, or eviction of expired values, when the variable table is full
  ([issue](../issues/2026-10-29-new-variable-dropped-when-table-full.md)).
- [020 ESP-NOW between clocks](../../specs/020-esp-now/SPEC.md) (explained, not requested).
- Hardware scrolling and the panel's idle (8-colour) mode (spec 014 research).
- A way back to stock over WiFi; crash reports kept across a reboot (upstream ideas).
- Flash: about 29 KB of update room left (618,496 B limit, 95% used). The next large
  feature needs a saving first (mDNS is ~21 KB).

## Done

Specs 001-017, released from 1.4.0 to 1.8.0: see [specs/README.md](../../specs/README.md)
and `CHANGELOG.md`.

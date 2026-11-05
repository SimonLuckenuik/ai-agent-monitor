# Roadmap

Updated: 2026-11-05. The order of work. Each line is a spec in [specs/](../../specs/README.md)
or an idea that becomes one when work on it starts; nothing is built before its spec is
approved.

## Next

1. **Check 2.2.0 on the clock**: Moni on waiting, done and error messages, the flashes,
   *Animated messages* off ([039](../../specs/039-animated-messages/SPEC.md)).
2. [042 Flash room, nothing lost](../../specs/042-flash-room-no-loss/SPEC.md): approved
   2026-11-05, built on `flash-room-042`; merge into `faces-v2`, release with the next
   version.
3. [041 The clock as an OpenClaw node](../../specs/041-openclaw-node/SPEC.md): draft
   (native node chosen), needs 042's room; waiting for approval.
4. [040 Scrolling text](../../specs/040-scrolling-text/SPEC.md): proposed, waiting for
   approval.
3. [041 The clock as an OpenClaw node](../../specs/041-openclaw-node/SPEC.md): proposed,
   output only, **a native node in the firmware** (owner, 2026-11-05). Needs about 10 KB
   of update room won back first (MQTT, mDNS or a separate image: the owner's choice).
   A tested bridge prototype on branch `openclaw-node` is the fallback.

## Waiting

- [037 A Claude usage face](../../specs/037-claude-usage-face/SPEC.md): set aside by the
  owner (2026-11-03).
- [034 Custom faces in the clock's language](../../specs/034-face-languages/SPEC.md):
  deferred by the owner (2026-11-02).

## Ideas (no spec yet)

- Hardware scrolling and the panel's idle (8-colour) mode (spec 014 research).
- A way back to stock over WiFi (upstream idea).
- [020 ESP-NOW between clocks](../../specs/020-esp-now/SPEC.md): dropped for now (2026-10-31).

## Done

Specs 001-017, released from 1.4.0 to 1.8.0; 021, 023 in 1.9.0; 024 in 1.9.1; 025 in
2.0.0; 026-033 and 035 in 2.1.0; 036 and 038 in 2.1.1; 039 in 2.2.0; 018, 022 closed on the clock; 019
in the plugin (0.7.2). See [specs/README.md](../../specs/README.md) and `CHANGELOG.md`.

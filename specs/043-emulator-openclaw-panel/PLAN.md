# Plan

1. Move `tests/integration/fake_gateway.py` to `emulator/openclaw_gateway.py`; the
   integration compose mounts `emulator/`. Add CORS, `/setup`, `/config`, the frames
   log, the connect checklist, pending devices, node details. *Check:* the integration
   case `openclaw_node`.
2. `emulator/docker-compose.yml`: a `gateway` service (python:3.12-alpine, the file
   mounted, 127.0.0.1:8790).
3. `emulator/emulator.cpp`: `GET /openclaw`, `POST /openclaw/pair|off|forget`.
4. `emulator/screen.html`: the OpenClaw panel (pairing, approval, node, checklist,
   commands with examples, results, frames). *Check:* in a browser against the
   emulator: pair, run every command, drop, revoke.
5. Docs: `emulator/README.md`, CLAUDE.md's emulator paragraph, `docs/specifications/testing.md`,
   CHANGELOG, specs index, STATUS, roadmap.

Lands with spec 041 (branch `openclaw-native`).

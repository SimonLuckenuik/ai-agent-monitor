# Progress

- 2026-11-05: approved by the owner's request in the chat. Built: the gateway moved to
  `emulator/openclaw_gateway.py` (CORS, `/setup`, `/config`, frames log, connect
  checklist, devices awaiting approval, node details), the compose service `gateway`,
  `GET /openclaw` and `POST /openclaw/pair|off|forget` in the screen server, and the
  OpenClaw panel in `screen.html`. In the browser: the clock paired, the seven checks
  passed, its 10 commands listed and invoked. Found: with a message on screen the
  emulated clock had 10.6 KB free, under the node's 12 KB gate ("waiting for memory");
  the gate is now 9 KB (spec 041).
- 2026-11-05: added, at the owner's request, the real OpenClaw Gateway as an optional
  service (`--profile openclaw`) and `emulator/openclaw_real.py` (see spec 041 PROGRESS
  for what it showed).
- Not verified: the panel's *Pair the clock* button from a real click (pairing worked
  through the same endpoints from a script; one click in the browser pane did not pair,
  not explained).

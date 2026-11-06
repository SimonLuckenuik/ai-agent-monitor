# Research

- **No node tester exists** (spec 041 RESEARCH, "Testing a node without OpenClaw"):
  OpenClaw's npm packages have validators, the ESP32 SDK has unit tests of its own C, and
  OpenClaw's live node checks need a real gateway and a phone. The stand-in gateway the
  integration tests already use is the thing to show.
- **Why the page calls the gateway itself (CORS) rather than through the screen server:**
  the screen server runs in the firmware's process, in the same loop as the node. A
  proxied `invoke` would block that loop while it waits for the node's answer, which the
  node can only give once the loop runs again. So the gateway's control API answers the
  browser directly, on 127.0.0.1 only.
- **Where the gateway runs:** a compose service on `python:3.12-alpine` (the image the
  integration tests already pull for mDNS), the file mounted read-only: nothing to add to
  the emulator's image. Inside the compose network the clock reaches it as `gateway`.

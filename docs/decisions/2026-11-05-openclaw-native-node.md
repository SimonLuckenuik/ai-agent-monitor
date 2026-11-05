# The clocks become native OpenClaw nodes, not bridged ones

Date: 2026-11-05

Status: Accepted (the owner's choice); the details wait for spec 041's approval

## Context

Spec 041 lets OpenClaw's agents drive the clocks, screen output only. It proposed two
ways: a native node in the firmware (option A), or a bridge on the gateway's machine
that pairs each clock as a node and calls the clock's HTTP API (option B). B was
recommended, prototyped and tested (branch `openclaw-node`, `integrations/openclaw/`),
because A did not seem to fit the flash left.

## Decision

The owner wants the firmware itself to be the node: "an actual node, not just using a
bridge". Option A is the plan; the bridge stays on the branch as a fallback and as a test
tool (its fake gateway), not as the product.

## Evidence (measured 2026-11-05 on 2.2.0, fc657da)

- Room: `firmware.bin` 593,328 B and `firmware.bin.gz` 420,307 B leave 24,576 B of update
  room. A byte of new code costs about 1.7 B of room.
- Ed25519 sign with SHA-512 (TweetNaCl, public domain), built for the lx106 with `-Os` and
  section garbage collection: 5,898 B. Monocypher's Ed25519: 20,576 B.
- Estimated for the rest: SHA-256 about 1.2 KB, a minimal WebSocket client about 2 KB, the
  handshake and dispatch about 3.5 KB, settings and pairing about 1.5 KB, the page about
  0.5 KB gzipped (counted twice). In all about 14-15 KB of code, 23-26 KB of room: **at or
  past the edge**, so room must be won back first.
- Signing time, from the source (5,630 field multiplications of 256 software 64-bit
  multiply-adds each, about 11,000 cycles apiece): about 0.4 s at 160 MHz, once per
  connection.
- Heap free at rest on both clocks: 24-27 KB (lowest since boot 16-19 KB), enough for one
  plain `ws://` socket. TLS (`wss://`) does not fit, in flash or heap.

## Alternatives

- The bridge (option B): no firmware change, every command, already tested; but a program
  must run beside the gateway, and the owner wants the clock to stand alone.
- A native node with TLS: not possible on this chip and flash.

## Consequences

- About 10 KB of room has to be found (MQTT, mDNS, or a separate image: spec 041's open
  questions) before the node is built.
- The gateway must be reachable from the clocks' IoT VLAN over plain `ws://`.

# 020 ESP-NOW between clocks

Status: Dropped (2026-10-31: the owner rejected it for now)

## Problem

Several clocks could share messages or a face without WiFi infrastructure. ESP-NOW (the
ESP8266's connectionless 250-byte frames) was explained; no request to build it yet.

## Open questions

- What should travel between clocks (messages, a "now showing", time sync)?
- Coexistence with the STA connection and the setup network on one channel.
- Flash cost.

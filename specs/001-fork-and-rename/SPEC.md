# 001 Fork SmartClock-OSS as the AI Agent Monitor

Status: Done in 1.4.0 (written after the fact, 2026-10-30)

## Problem

The owner's JUZIPi SD PRO clock runs SmartClock-OSS 1.3.2 (michaeldale/smartclock-oss).
The owner wants a clock that AI agents can push messages and screens to, developed in
their own repository without proposing changes upstream.

## Goals

1. Work in the fork github.com/SimonLuckenuik/ai-agent-monitor, cloned in
   `C:\dev\ai-agent-monitor`; upstream stays the `upstream` remote, no pull requests.
2. One name everywhere: "AI Agent Monitor" on the settings page, in the setup network,
   the hostname, MQTT and the docs.
3. Clocks side by side must not share a name.
4. The settings page stays English only and self-contained (no CDN).

## Requirements

1. Setup network `AI Agent Monitor XXXXXX`, hostname and default MQTT topic base
   `agentmonitor-XXXXXX`, where XXXXXX comes from the chip id.
2. The settings page is titled AI Agent Monitor; its project link and update check point
   to the fork.
3. The Dial face is removed (the owner found it useless); its id 3 is reused.
4. New clocks start on 24 h, km/h and °C; no default photo in the build.

## Non-goals

- Upstream pull requests. Moving the settings page to a CDN (the build stays
  self-contained). Accents in fonts.

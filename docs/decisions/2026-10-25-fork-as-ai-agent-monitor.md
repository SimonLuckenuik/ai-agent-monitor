# Fork SmartClock-OSS as the AI Agent Monitor

Date: 2026-10-25

Spec: [001](../../specs/001-fork-and-rename/SPEC.md)

## Decisions

- Work in the owner's fork, github.com/SimonLuckenuik/ai-agent-monitor, with `upstream`
  as a remote and no pull requests to it for now.
- One name: AI Agent Monitor (setup network `AI Agent Monitor XXXXXX`, hostname and MQTT
  base `agentmonitor-XXXXXX`, the chip id's suffix so clocks side by side differ).
- The settings page stays English and self-contained: a CSS/JS library from a CDN was
  considered to shrink it and refused, so the build works without internet.
- No MCP server and no Home Assistant dependency for agents: HTTP and MQTT.

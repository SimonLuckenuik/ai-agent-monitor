# 004 Agent messages and the Agents board

Status: Done in 1.4.0 (written after the fact, 2026-10-30)

## Problem

AI agents need a simple way to get the owner's attention: a title, a message, an icon,
over HTTP (and MQTT), shown in the face rotation.

## Goals

1. A built-in Message face with a title, a message and one of a few icons, so an agent
   needs no face design.
2. HTTP and MQTT; no MCP, no Home Assistant dependency.
3. Messages are a turn of their own in the rotation; urgent ones can take the screen.

## Requirements

1. `POST /api/message` (and MQTT `<base>/message`): `title`, `text`, `icon` (info,
   success, warning, error, waiting, running, bell, done), optional `pct` progress,
   `ttl`, `show`. No sign-in.
2. Up to 4 at a time, in RAM, each a rotation turn; warning and error light the backlight
   fully.
3. The Agents board (face 9): every active message with icon, title, first line, time
   left; "All quiet" when none; in the rotation only while messages are active.
4. Message screens: a solid background in a dark shade of the message colour.

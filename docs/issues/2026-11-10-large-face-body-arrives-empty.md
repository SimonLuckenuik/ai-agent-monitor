# A face body too big for the web server arrived empty, and deleted the face

Date: 2026-11-10

Status: Fixed on branch `feat/047-json-render` (for 2.7.0), not released. Found by the
`json_render` integration case (emulator), not seen on a clock.

## Symptom

`POST /api/faces?name=x` with a json-render spec of 8 KB or more answered `OK`, stored
nothing, and removed any face called `x`.

## Cause

The core's web server reads a request body into a String, then copies it into the
`plain` argument while the first copy is alive (`ESP8266WebServer` `Parsing-impl.h`,
`arg.value = plainBuf`): twice the body in one piece each. When the copy does not fit the
heap (largest block about 14 KB with json-render, at rest), the argument is left empty.
The faces handler treats an empty body as "delete this face". Spec 047 had set a 12 KB
spec limit without a body that size ever reaching the handler; 6 KB arrives, 8 KB does
not.

## Fix

- `Content-Length` is collected; a body that was sent but arrived empty gets 507 ("Not
  enough memory for a body that size"), and the face is left as it was.
- The spec limit is 6 KB (`JR_SOURCE_MAX`), in the firmware, the API reference, the skill,
  `clock.py` and the OpenClaw bridge.
- The `json_render` integration case sends a 13 KB spec over an existing face (507, face
  kept) and a 6.2 KB one (400, "larger than 6 KB").

Faces in our own format are at most 4 KB, so they were not affected in practice.

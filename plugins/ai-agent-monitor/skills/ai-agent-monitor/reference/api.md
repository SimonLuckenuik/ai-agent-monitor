# Clock API

Base URL: `CLOCK_URL` (the clock's address, or `http://127.0.0.1:8000` for the
emulator).

## Messages (no key)

| Method and path | Body | Does |
|---|---|---|
| `POST /api/message` | message JSON | Stores a message; a message with an existing `id` replaces it |
| `GET /api/message` | | `{"messages":[{"id","title","text","icon","color","pct","ttl","showing"}]}` (`ttl`: seconds left) |
| `DELETE /api/message?id=<id>` | | Removes that message; without `id`, all of them |

| Field | Meaning (default) |
|---|---|
| `id` | Who it is from; same id replaces (`agent`, at most 15 characters) |
| `title` | One line in the icon's colour (at most 40 characters) |
| `text` | Up to 4 wrapped lines, 3 with a progress bar (at most 160 characters) |
| `icon` | `info`, `success`, `warning`, `error`, `waiting`, `running`, `bell`, `done` (`info`) |
| `color` | `"#rrggbb"` instead of the icon's colour |
| `pct` | 0–100: a progress bar |
| `ttl` | Seconds until it goes away (600; 5 to 86400) |
| `show` | `true`: on screen now; otherwise it waits for its turn in the rotation |

```bash
curl -X POST "$CLOCK_URL/api/message" -d '{"id":"build","title":"Build","text":"3 tests failed in faces.cpp","icon":"error","show":true}'
```

Up to 4 messages at a time, kept in RAM (lost on restart). `warning` and `error` light
the screen fully while shown. Accents and typographic quotes are folded to ASCII.

## Custom faces (API key)

Authenticate with the header `X-Api-Key: <key>` (or `?key=<key>`). The key is on the
settings page under **Integrations → Custom faces**, which must be turned on;
otherwise these endpoints answer 404 `Custom faces are turned off`.

| Method and path | Body | Does |
|---|---|---|
| `POST /api/faces?name=<name>` | face JSON | Saves the face; `&show=1` (or `"show": true` in the JSON) shows it now. An empty body deletes it |
| `GET /api/faces` | | `{"faces":[...], "showing":"<name>", "vars":{...}}` |
| `GET /api/faces/get?name=<name>` | | The face's JSON |
| `POST /api/faces/show?name=<name>` | | Shows the face now; the rotation carries on from it |
| `POST /api/faces/delete?name=<name>` | | Deletes the face |
| `POST /api/faces/vars` | `{"key":"value",...}` | Sets variables, merged with the existing ones; numbers allowed |
| `GET /status` | | Time, weather, network, `face` and `version`; no key needed |

```bash
curl -X POST "$CLOCK_URL/api/faces/vars" -H "X-Api-Key: $CLOCK_API_KEY" -d '{"msg":"Build finished","pct":100}'
```

```bash
curl -X POST "$CLOCK_URL/api/faces?name=done&show=1" -H "X-Api-Key: $CLOCK_API_KEY" -H "Content-Type: application/json" --data-binary @done.json
```

Errors come back as HTTP 400 or 401 with a plain-text reason (`Face is larger than 4
KB`, `At most 32 items`, `At most 12 custom faces`, `name: 1-20 of a-z 0-9 _ -`).

## MQTT

When the user has MQTT set up on the clock (settings page → Integrations), the same
works through their broker, with the topic base shown there (default
`agentmonitor-XXXXXX`):

- `<base>/custom/<name>`: a face JSON (empty payload deletes). Payloads are limited to
  about 1.5 KB.
- `<base>/vars`: `{"key":"value",...}`. Publish with **retain** so the values come back
  after the clock restarts.
- `<base>/face/set`: the name of a face to show.
- `<base>/message`: a message JSON; `<base>/message/clear`: an id, or empty for all.
  These work without Custom faces turned on.

## Emulator only

The emulator (`docker compose -f emulator/docker-compose.yml up --build` in the
firmware repository) also serves, on port 8080:

| | |
|---|---|
| `GET /fb` | The screen: 240×240 RGB565 little-endian, 115,200 bytes (`clock.py screenshot` turns it into a PNG) |
| `GET /password` | The emulator's device password (for signing in without an API key) |
| `GET /fs?path=/faces` | The clock's storage, read-only |

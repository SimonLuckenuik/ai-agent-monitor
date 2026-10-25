# Clock API for custom faces

Base URL: `CLOCK_URL` (the clock's address, or `http://127.0.0.1:8000` for the
emulator). Authenticate with the header `X-Api-Key: <key>` (or `?key=<key>`). The key
is on the settings page under **Integrations → Custom faces**, which must be turned on;
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
`smartclock-XXXXXX`):

- `<base>/custom/<name>`: a face JSON (empty payload deletes). Payloads are limited to
  about 1.5 KB.
- `<base>/vars`: `{"key":"value",...}`. Publish with **retain** so the values come back
  after the clock restarts.
- `<base>/face/set`: the name of a face to show.

## Emulator only

The emulator (`docker compose -f emulator/docker-compose.yml up --build` in the
firmware repository) also serves, on port 8080:

| | |
|---|---|
| `GET /fb` | The screen: 240×240 RGB565 little-endian, 115,200 bytes (`clock.py screenshot` turns it into a PNG) |
| `GET /password` | The emulator's device password (for signing in without an API key) |
| `GET /fs?path=/faces` | The clock's storage, read-only |

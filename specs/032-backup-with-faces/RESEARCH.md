# 032 Research

- Today: `handleSettingsExport()` sends `configJson()` with `type: smartclock-oss-settings`;
  `handleSettingsImport()` applies each known key and counts the rest as skipped.
- Faces: up to 40, each up to 4 KB, in `/faces/<name>.json` on the clock; `GET /api/faces`
  lists names and variables, `GET /api/faces/get?name=` returns one face.
- Variables: up to 40 in RAM, `$persist` ones also in `/vars.json`.
- smalltv-pomodoro's backup (README): settings, focus history and downloaded faces in one
  `.json`, without passwords or Wi-Fi details.

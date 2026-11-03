# 032 Progress

- 2026-11-01: written and approved, from smalltv-pomodoro's backups (theirs keep their
  downloaded faces).
- 2026-11-02: built. The page's `backupBuild` and `backupRestore` (web/index.html, between
  markers) take a `call` function, so `tests/web/backup.test.js` runs them on a mock clock
  (in CI). The firmware's only change: `GET /api/faces` names the `$persist` values
  (`persist`), which the list did not tell apart. Restore imports the settings twice:
  first (they may turn custom faces on), then after the faces, for the keys that name
  a face. On the dev emulator: three faces, a saved value and the face shown came back
  after a wipe with custom faces off. Update budget 99% (about 1.3 KB).

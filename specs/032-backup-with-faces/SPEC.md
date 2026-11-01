# 032 Backups that keep the custom faces

Status: Approved (2026-11-01; the owner agreed to backups with faces)

## Problem

"Download backup" saves the settings only (`/api/settings/export`). A reset, a new clock or
a crash loop that turns custom faces off and makes the owner start again loses every
custom face and every pushed variable.

## Goals

1. **One backup file** holds the settings (as today), every custom face (name and JSON),
   the persisted variables (`$persist`), and the clock's version and date. Its type is
   `ai-agent-monitor-backup`, format 2.
2. **The settings page builds and restores it,** through the existing API: it reads
   `/api/settings/export`, `/api/faces`, each `/api/faces/get`; it restores with
   `/api/settings/import`, `POST /api/faces` per face and `/api/faces/vars`. The clock
   never holds more than one face at a time (faces can total 160 KB).
3. **Restore reports** what it did: settings applied and skipped, faces restored, faces
   refused (with the clock's reason, such as "At most 40 custom faces"), variables set.
4. **Old backups still restore:** a settings-only file (type `smartclock-oss-settings`)
   is restored as today.
5. **No secrets in the file,** as today: no Wi-Fi password, device password or API key.

## Non-goals

- Photos in the backup (too large; they stay on the clock).
- Backups made by the clock on its own.

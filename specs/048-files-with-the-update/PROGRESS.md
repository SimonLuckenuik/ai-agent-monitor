# 048 Progress

## 2026-11-07

- Came out of spec 047's size research (the owner: "investigate how to reduce size
  without dropping features"). Moving the settings page and the catalog to LittleFS
  was the largest lever found, and the only one that makes json-render fit with every
  feature kept. The owner: "Write spec 048".
- Written: SPEC, PLAN, RESEARCH (draft). Looked at: the update handler and
  `FirmwareValidation` (2.3.x refuses any byte past the image: the bundle needs a
  `.bin.gz` first), recovery (must not depend on storage: the fallback page), the
  read-only data in 2.3.1 by symbol size.
- Nothing built. Waiting for the owner's approval and answers to the open questions.
- The owner on the open questions: downloading from GitHub "is interesting, by default
  maybe we could show the latest release that gets downloaded and pushed to the
  device" (now requirement 0); the icons and Message face as files "good idea". Checked
  GitHub's CORS headers: the API, raw and Pages allow any origin, release downloads do
  not (RESEARCH.md), so the workflow copies the `.update` to Pages. Suggested the name
  `.update`; recommended 048 in its own release before 047 (to confirm).

## 2026-11-07 (later): approved; the starter image proposed

- The owner: "lets release 048". Status set to Approved.
- The owner asked for a proposal: a small plain `SDP...` image, released every time,
  that only gets a stock clock onto WiFi and installs the latest release from GitHub.
  Written into SPEC.md ("The starter image") and PLAN.md step 9, waiting for the go.
- The 048 work will go on its own branch cut from `faces-v2` (it releases without
  047), with these spec commits brought over.
- The owner: WiFi must come first, since the setup network has no internet; a step by
  step page. Written as "The setup steps" in SPEC.md (WiFi, switch back to the home
  network and open the new address, sign in, Install; a file by hand always there), and
  PLAN.md step 2.
- The owner: "once the clock is connected to the new network, can't it fetch itself
  whatever is required?" Checked GitHub's TLS (no smaller records: about 30 KB of heap
  for a download; two root certificates). The starter downloads by itself; the full
  firmware cannot afford TLS and keeps the browser path. SPEC, RESEARCH and PLAN
  updated.

## 2026-11-07 to 2026-11-08: built (branch feat/048-update-files, from faces-v2 at 2.4.0)

The owner: "Go, with starter". Built in this order (commits on the branch):

- **The update file** (`include/update_bundle.h`, `scripts/make_update.py`): "AMUP", the
  files with their sizes and CRCs, then the `.bin.gz`. Every build writes
  `firmware.update`. Unit tests read files written by the Python writer in pieces of
  every size, and damaged, truncated and badly named ones (74 checks).
- **`src/update.cpp`**: the update handler, moved out of main.cpp and shared with the
  starter. Files go to `/sys.new` and move to `/sys` after `Update.end()`; the manifest
  last. In recovery or without storage the image installs and the files are skipped. The
  safety harness runs it on mock storage: files only after the commit, old files removed,
  photos untouched, damaged file or image, truncated, storage full, recovery, a failed
  rename, the manifest states.
- **The settings page from `/sys`**, the setup page (`web/fallback.html`, 4.3 KB gzipped)
  otherwise and at `/setup`. `/status` has `files`. The settings page's update notice
  links to Install.
- **Emulator**: `update.cpp` and the seeded `/sys` files; updates complete (emulated
  flash); restarts no longer abort (the route tables are on the heap now: the server's
  destructor deleted the static ones, exit 134 on every restart); an upload checks the
  host once, not per piece.
- **The starter** (`src/starter.cpp`, `[env:starter]`, 500,704 B): tried in the emulator
  against GitHub (BearSSL built for the host, TLS's stack charged to the heap). Lookups
  and downloads ran out of memory three ways before the design settled: TLS (~33 KB)
  plus the image check (4 KB) plus the updater's buffer (4 KB) do not fit in 44 KB, and
  GitHub's redirect means two handshakes. Now: the release notes are left out of the
  lookup, the roots are freed after the handshake, the image check keeps 28 bytes of a
  `.bin.gz`, and the download goes to storage before the install. Lowest free heap 5.4 KB
  (lookup) and 9.4 KB (download). A release without an `.update` (2.4.0) installs its
  `.bin.gz`; the firmware's setup page then gets the files.
- **Release**: `make_release.py` builds and names the `.update`, the `.bin.gz` and the
  starter; `release.yml` publishes them, notes which file is for which clock, and copies
  the `.update` to GitHub Pages; `build.yml` builds the starter.
- **Docs**: README (release files, install from stock in two steps, updating, the
  stock-firmware warning and related projects the owner asked for), CHANGELOG,
  updates-and-recovery.md, testing.md, emulator/README.md, CLAUDE.md.

Measured: the firmware 587,584 B and 410,889 B, 36 KB of update room (2.4.0: 4 KB).

Not in this release: phase 2 (icons and the Message face as files), spec 047's catalog.

Not verified yet: anything on a clock (the starter over stock, TLS's real heap, the
page from storage, an update from 2.4.0); GitHub Pages (not turned on); the browser's
Install against a real release with an `.update` (none published yet).

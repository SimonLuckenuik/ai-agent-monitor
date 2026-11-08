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

## 2026-11-08: the starter downloads in the browser, not on the clock

- The owner asked for a way without BearSSL, with the browser doing the download and the
  upload, the same from the home network and from the clock's own network, keeping the
  captive portal "with a special case just for the github dns we need and have it nat".
  (In between I had started, without asking, to turn the captive portal off once the
  clock is on WiFi; the owner had been describing the problem, not asking for that. Taken
  back: the firmware's setup network behaves as before.)
- Built: the starter's own name responder (GitHub's two names looked up on the home
  network once on WiFi, every other name the clock, no IPv6 answers) and NAPT for the
  setup network (64 entries), lwIP with features for the starter only
  (`scripts/starter_options.py`: the core picks lwIP before `build_unflags` applies). The
  starter's HTTPS code, `/api/update/*`, the root certificates and the emulator's BearSSL
  are gone. The setup page has one path: the browser, on either network.
- Both builds' web servers queue at most 4 waiting connections (the core's 255 let a
  phone's burst of connections take the heap).
- Measured: the starter 381,568 B (was 500,720 B); the firmware 587,424 B and 410,730 B.
  In the emulator the responder answered api.github.com and simonluckenuik.github.io
  with their real addresses (as the host's own lookup), www.msftconnecttest.com and
  connectivitycheck.gstatic.com with the clock, and no IPv6 answer.
- To do after the owner's tests: the README summary (PLAN step 10).

Not verified: NAT (lwIP on the chip only), and phones' behaviour on the setup network
with it (whether each sends the browser's traffic through the clock once it is online).

## 2026-11-08: on the owner's clock (agentmonitor-ade1f0, 2.4.0 before)

Images built from 0a8d162 (2.5.0-dev), installed by the owner:

- The starter ran; on its own network (a Windows laptop, the page opened as
  www.msftconnecttest.com through the captive portal) it signed in, joined the WiFi, and
  read the latest release and its notes from api.github.com: GitHub's names and the NAT
  work on the chip.
- From there the full firmware (2.5.0-dev) came up without its files: the setup page
  said so. One upload of the .update stopped at about 20% ("Upload failed", no reply,
  no crash recorded); the next, from http://192.168.4.1, installed it. After it: files
  ok, the settings page served from storage at the clock's home address, 24.8 KB free
  heap, no crash.
- Found: opened as www.msftconnecttest.com, the full firmware's setup page took itself
  for the home network and gave the generic message; fixed in ab94844.
- Not explained: the failed upload (the connection dropped; the clock did not crash).
- Not tried yet: Install from GitHub (2.4.0 has no .update and GitHub Pages is off), the
  .bin.gz from 2.4.0 then Install, recovery, a phone on the setup network for a while.
- GitHub Pages: the owner turned it on (Source: GitHub Actions; the user site
  simonluckenuik.github.io is a separate repository and unaffected). The site is built by
  `.github/workflows/pages.yml` alone, since a deploy replaces the whole site (the Studio
  will be added there): `update/` with the latest release's .update and latest.json.
  release.yml calls it after publishing (a release made by a workflow starts no other
  workflow). A browser cannot fetch a release download directly (no CORS header; checked
  from a page: the API answers, the download fails), hence the copy.

## 2026-11-08: 2.5.0

- The owner: "Prepare 2.5.0". FW_VERSION 2.5.0, CHANGELOG section with the upgrade paths,
  the README summary at the top (stock firmware, starter, Install; PLAN step 10).
- Checked for the release: unit, safety, page and integration tests (below in the
  commit's run). On the owner's clock: the starter, its GitHub lookup through its own
  network, the .update installed from it, the settings page from storage.
- Not checked before the release: Install from GitHub (needs this release and its Pages
  copy), a phone on the setup network for a while, recovery on 2.5.0; the one upload
  that stopped at 20% is not explained.

## 2026-11-08: 2.5.0 published

- The owner: "Yes, both" (rename faces-v2 to dev, publish). GitHub's branch rename made
  `faces-v2` `dev`; the release commit (15bdf57) went to `dev` and `main`. The release
  run passed: the release has the `.update`, the `.bin.gz`, the starter, the `.elf` and
  the checksums; its pages job deployed `update/AIAgentMonitor_v2.5.0.update` and
  `latest.json` to https://simonluckenuik.github.io/ai-agent-monitor/, with
  `Access-Control-Allow-Origin: *` (checked). Before it, the full integration suite on
  the release commit: 39 cases passed.
- The CI's actions moved to their latest versions (Node 24) in the same release; the
  "Build and test" runs on it are its first with them.
- Next: Install from GitHub on a clock (from the home network and from the clock's own),
  then phase 2.

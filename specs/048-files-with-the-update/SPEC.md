# 048 Read-only data as files that come with the update

Status: Done in 2.5.0 (phase 1 and the starter; approved 2026-11-07: "lets release
048", "Go, with starter"); phase 2 (weather icons and the Message face as files; the
owner, 2026-11-08: "spec 048 phase 2 + 047") done in 2.6.0.

## Problem

An update must fit the firmware image and its `.bin.gz` together in the 1 MB sketch
area (spec 009). 2.3.1 has 4 KB of that room left. Every new feature now competes for
it, and spec 047 (json-render on the clock) is 60 KB short even with LTO, with every
current feature kept (the owner, 2026-11-07: "try to keep everything", "investigate how
to reduce size without dropping features").

Part of the image is not code but data the firmware only reads and sends: the settings
page (24.4 KB, gzipped), and with spec 047 the json-render catalog and prompt (7.7 KB,
gzipped). Data that is already compressed costs twice: once in the image, and again in
the `.bin.gz`, where zopfli cannot shrink it. The 24.4 KB page costs about 49 KB of
update room.

The clock has about 3 MB of LittleFS beside the sketch area, mostly free. Data kept
there costs no update room at all.

## Goals

1. Read-only data that the clock can run without moves from the image to LittleFS
   files: first the settings page, then spec 047's catalog and prompt.
2. **One update file**, as today, once a clock runs this firmware: the files come with
   the update and are written by the clock as it installs it.
3. **The clock is never left without a way to update or set up.** The image keeps a
   small fallback page (sign in, WiFi, firmware update, restart). Recovery, the setup
   network, a failed mount and missing or stale files all fall back to it.
4. About 35 KB of update room back for every build, json-render or not.

## Non-goals

- Code: only data moves. The fonts (TFT_eSPI reads them from flash itself), the line
  font's glyphs and the drawing tables stay in the image.
- Data the clock cannot run without stays in the image for now: the Message face's JSON
  (agent messages must always show) and the weather icons (drawn on most faces). They
  are measured and weighed in phase 2.
- The clock fetching anything from the internet (no TLS budget; spec 045's rule).
- A new flash layout: LittleFS stays at 0x100000-0x3FA000 (CLAUDE.md, stock layout).
- Changing how older firmware takes updates: what it accepts is fixed in the field.

## Requirements (draft)

0. **Update from GitHub, by default** (the owner, 2026-11-07: "by default maybe we
   could show the latest release that gets downloaded and pushed to the device"). The
   settings page asks `api.github.com` for the latest release and shows its version and
   notes beside the running one, with an **Install** button: the browser downloads that
   release's bundle and pushes it to the clock (`POST /update`, signed in). The release
   workflow publishes each bundle to GitHub Pages too, because release downloads cannot
   be fetched from another site (RESEARCH.md). Uploading a file by hand stays, for a
   browser without internet (the setup network). The clock itself fetches nothing.
1. **The bundle.** A release publishes `AIAgentMonitor_v<version>.update` (one file that
   holds everything an update installs): a small header (magic, format version, the firmware version, sizes, CRC),
   then the files (path, size, CRC, bytes: already gzipped where it helps), then the
   `.bin.gz` exactly as today. The settings page's Firmware update card and
   `POST /update` take it.
2. **Installing a bundle.** The update handler streams the files into a staging folder
   (`/sys.new/`), checks each CRC, then stages the image as today. Only after the image
   is committed does it swap `/sys.new/` for `/sys/` and restart. A failure at any point
   leaves `/sys/` and the running firmware as they were. It checks free space before
   writing, and refuses (with the reason) when LittleFS lacks room for the files.
3. **The `.bin.gz` alone still works** on every firmware, this one included: the image
   installs, the files stay as they were. Clocks on 2.3.x and older can only take the
   `.bin.gz` (their update check refuses anything else); after it, they install the
   bundle once to get the files.
4. **The files match the firmware.** The image carries a hash of the files it was built
   with; `/sys/manifest` holds the hash of the files on storage. Different hash, missing
   files or a failed mount: the clock serves the fallback page, with a line saying which
   file to install to finish ("install AIAgentMonitor_v<version>.bundle"). `/status`
   reports `files: ok | missing | stale`.
5. **The fallback page** (in the image, gzipped, about 4-5 KB, hand-written): the setup
   steps below (WiFi, sign in, Install from GitHub, a file by hand), restart, and in
   recovery the explicit format after a failed mount. Recovery always uses it (recovery never reads
   LittleFS beyond what it does today).
6. **Serving** `/` and `/index.html` from `/sys/index.html.gz` with `Content-Encoding:
   gzip`, streamed (no heap for the page), `Cache-Control: no-cache` as today. Spec
   047's `/api/catalog` and `/api/catalog/prompt` from `/sys/` the same way.
7. **Protection.** `/sys/` cannot be written, renamed or deleted through any other API
   (photos, faces, the emulator's storage browser excepted). A storage format or a
   factory reset removes it like everything else; the clock then shows the fallback page
   until the next bundle.
8. **The emulator** starts with empty storage: its image seeds `/sys/` from the build
   (so the screen page's Settings button opens the full page), and the integration
   tests install a bundle on blank storage to test the path.

## The starter image (approved 2026-11-07)

The owner: "create a second build ... small enough so that it can become the official
upgrade file for a clock running the initial firmware (with proper SDP naming) ...
packages only the basic page and does almost nothing and can then pick the latest
release from github ... always release it ... original clock firmware -> that new bin ->
upgrade to full."

**What it is.** `SDPro_AIAgentMonitor_starter_v<version>.bin`: a plain image (stock's
update page cannot unpack `.bin.gz` and only takes names starting with `SDP`), built
from this repository with every release. It does one thing: get the clock onto WiFi
and install the full firmware.

- **On the screen:** "AI Agent Monitor setup", the setup network's name and password,
  and once on WiFi the address to open. Font 2/4 text, no faces.
- **The browser downloads the release, as in the full firmware** (the owner,
  2026-11-08: "the starter page should work the same way both when connected to the
  device from own network (wifi where device connects) and from its own SSID
  connection, having browser responsible for download and upload to device"). The page
  is the setup page: it reads the latest release from `api.github.com`, downloads its
  `.update` from GitHub Pages and sends it to the clock. The clock has no HTTPS.
- **GitHub reachable from the clock's own network** (the owner: "keep actual behaviour
  and have a special case just for the github dns we need and have it nat"): the captive
  portal stays (every name answers with the clock, so the phone opens the setup page:
  Windows' `www.msftconnecttest.com`, Android's `connectivitycheck.gstatic.com`, Apple's
  `captive.apple.com` all land on it), except `api.github.com` and
  `simonluckenuik.github.io`: once the clock is on WiFi, it looks them up on the home
  network and answers with their real addresses (IPv4; no IPv6 answer), and forwards
  the setup network's traffic (NAPT, 64 entries). In practice only GitHub's traffic goes
  out: every other name still leads to the clock. Before the clock is on WiFi those
  names have no answer, and the page asks for WiFi first anyway.
- An earlier version (built 2026-11-07, given up 2026-11-08) downloaded on the clock over
  HTTPS (BearSSL, two built-in roots, NTP); RESEARCH.md keeps what it measured: 76 KB of
  image and about 33 KB of heap, which forced a download to storage before installing.
- **What it installs:** the `.update` (files and image) or a `.bin.gz`, with the same
  update handler and checks as the full firmware (including our bootloader, which comes
  with any image and unpacks `.bin.gz`).
- **What it keeps:** the WiFi it was given, saved in `/config.json` as the full
  firmware saves it, so the full firmware comes up on WiFi; the device password (in
  EEPROM, as today); stock's LittleFS volume as today's first install does (never
  formatted without the user's click).
- **What it leaves out:** faces, weather, photos, messages, MQTT, OpenClaw, mDNS,
  crash records, HTTPS. 381,568 B (lwIP with its features, for NAT), under stock's
  first-flash limit (about 552 KB) and our plain-update limit (516,096 B).

**The paths it opens:**

| From | Today | With the starter |
|---|---|---|
| Stock firmware | v1.4.0's `_nomqtt.bin`, then the latest `.bin.gz` (then, with 048, the `.update`) | the starter, then **Install** (one click): 2 steps |
| AI Agent Monitor 1.4.0-2.4.x | the latest `.bin.gz`, then the `.update` from the fallback page | the same (or the starter, then Install) |
| 048 on | Install, or the `.update` | the same |

**Why not just the fallback mode of the full firmware?** The full image (about 600 KB)
is over stock's first-flash limit and cannot be a plain image; the starter is the
smallest thing that can bridge stock to a `.update`. It replaces v1.4.0's `_nomqtt.bin`
as the documented first install, so the first-install note stops pointing at an old
release. It is also a clean way back from a broken install: any of our firmware takes
it as a plain image.

**How it is built.** `env:starter` in `platformio.ini` with its own `src/starter.cpp`
(`build_src_filter`), sharing `config.h`, `firmware_validation.h`, the update handler
(moved to `src/update.cpp` so both builds use it), the device password and the
fallback page. Its own size guard: under 516,096 B (so it is always a valid plain
image for us and for stock).

## The setup steps (the starter's page, and the fallback page)

The owner: "Needs to connect to wifi first to have internet access before being able to
fetch from github, so most likely the setup page should push for it ... like step by
step that enables the webpage when action is done?"

A browser on the clock's setup network has no internet, so GitHub is out of reach until
the clock is on WiFi and the browser is back on the home network. The page is a short
series of steps; each one opens when the one before it is done, and says what to do
next:

1. **WiFi.** Open on the setup network. Pick the network, type its password, Connect.
   The page waits for the clock to join (`/status`) and shows its new address; the
   screen shows it too. Then: "Switch this phone or computer back to <network> and open
   http://<address>". A wrong password says so and stays on this step.
2. **Sign in** with the device password shown on the clock's screen (needed to update).
3. **Install.** Opens when the page was loaded from the clock's address on the home
   network and the browser reaches `api.github.com`. It shows the latest version and
   its notes beside the running one, then **Install**: download (progress), send to the
   clock (progress), the clock restarts, the page waits for the new version in
   `/status` and then opens the full settings page. If GitHub cannot be reached, it says
   so (no internet, or blocked) and offers the file instead.
4. **Or a file**, always there under the steps: upload a `.update` or `.bin.gz` by hand
   (offline, or a version other than the latest).

Steps already done show as done (a clock already on WiFi, opened from its address,
starts at step 2 or 3). These steps are the full firmware's fallback page (files
missing or stale; usually at step 3), where the browser downloads for the clock: the
full firmware has neither the flash nor the heap for TLS. The starter does the
download itself and needs only step 1 and an Install button (above).

## User-visible behaviour

- The Firmware update card shows the latest release and installs it in one click
  (browser online). By hand: the `.update` file.
- Updating from 2.4.x or older: install the `.bin.gz` (as today); the new firmware then
  shows the fallback page with **Install from GitHub** (or the `.update` file) once.
  Every later update: one click, or the `.update` file.
- Stock clocks: the starter, then Install (it downloads the release itself): one step
  fewer than v1.4.0's `_nomqtt.bin`, then the `.bin.gz`.
- A clock whose storage was formatted shows the fallback page until the bundle is
  installed again: settings, faces and photos are gone anyway in that case.
- Nothing else changes: the same page at the same address.

## Flash and RAM budget

| Change | Image | `.bin.gz` | Update room |
|---|---|---|---|
| Settings page leaves the image | -24.4 KB | -24.4 KB | +48.8 KB |
| Fallback page | +4 KB | +4 KB | -8 KB |
| Bundle reader, file serving, manifest check (about 1.5-2 KB of code) | +2 KB | +1.4 KB | -3.4 KB |
| **Phase 1 total (estimate)** | **about -18 KB** | **about -19 KB** | **about +37 KB** |
| **Measured** (2.4.0 then this branch, `pio run`) | 602 KB to 587,584 B | 427 KB to 410,889 B | 4 KB to **36 KB** |

The starter: 381,568 B (plain limit 516,096 B; stock's first flash allows about 552 KB).
It was 500,720 B when it downloaded over HTTPS itself.

RAM: the starter's NAT table takes about 1.8 KB (64 entries of 28 bytes), and what it
forwards passes through its heap (about 44 KB free; no TLS on the clock now). The image
check keeps 28 bytes of a `.bin.gz` (4,120 only for a plain image): 4 KB less during
every compressed update, in the firmware too. Both builds' web servers queue at most 4
connections not yet served (the core's default is 255): a phone on the setup network,
whose every name leads to the clock, could take the heap with them.
| Spec 047's catalog and prompt as files (when 047 lands) | -7.7 KB | -7.7 KB | +15 KB |

RAM: the bundle reader keeps one file open and a CRC while an update runs (well under
the 4.1 KB the image check already takes); serving a file uses the file system's buffer
(about 0.5 KB) for the length of the request. No new resident state beyond the
manifest check's result.

LittleFS: about 30 KB for `/sys/`, twice that while an update stages the new files.

## Open questions

1. ~~How the files arrive~~ The owner: downloaded from GitHub by the settings page by
   default (requirement 0); the bundle is what it pushes, and the file for offline use.
2. The file's name: `AIAgentMonitor_v<version>.update` (suggested 2026-11-07, built so;
   the owner can still rename it). The stock update page refuses names not starting
   with `SDP`, which is fine: it cannot install it.
3. Phase 2, the weather icons (7.5 KB, about 13 KB of room) and the Message face (3.9 KB
   of text, about 5 KB): the owner, "good idea". Files go missing only in rare cases
   (once after the move from 2.4.x, after a storage format, a storage that does not
   mount, or a `.bin.gz` installed alone, which leaves them stale), so the fallback is
   minimal, not a second copy: no weather icon drawn, a message as plain title and text.
   Not in 2.5.0; built 2026-11-08 (PROGRESS.md): 576,672 B and 404,175 B, 56 KB of room.
4. Release order: 048 alone in a MINOR release, before spec 047 uses it (recommended:
   useful without 047, it changes the update path, which is worth checking on the
   clocks by itself, and 047 does not fit yet), or both in one release? The owner to
   answer.
5. ~~The starter image~~ The owner: "Go, with starter". In the same release; the
   release notes' first-install line points at it.
6. GitHub Pages must be turned on for the repository (Settings > Pages > Source: GitHub
   Actions) for the browser's Install: the owner's setting. Until then the release
   workflow's Pages job fails, the release itself is published, and Install says the
   file is not on Pages yet (the starter is not affected: it downloads from the release).

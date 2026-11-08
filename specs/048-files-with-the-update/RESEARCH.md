# 048 Research

## Why data costs twice

The update limit (`scripts/build_options.py`, spec 009): the plain image, rounded up to
whole 4 KB sectors, must fit in 1 MB less two sectors and less the rounded `.bin.gz`.
Code compresses to about 0.7 of its size, so a byte of code costs about 1.7 bytes of
room. A byte that is already gzipped is in the image and again, as it is, in the
`.bin.gz`: 2 bytes of room.

Measured for spec 047 (its RESEARCH.md, "Room for json-render with every feature
kept"): 2.3.1 is 602,384 B and 427,330 B, 4 KB of room. Storing 047's 19 KB of catalog
and prompt text gzipped (7.7 KB) took the image down 11 KB but the `.bin.gz` up 2.3 KB.

## Read-only data in 2.3.1 (symbol sizes, `nm -S`)

| Data | Size | Read how | Could move? |
|---|---|---|---|
| `INDEX_HTML_GZ`, the settings page | 24,405 B (gzipped) | sent as it is | yes: phase 1 |
| TFT_eSPI fonts 2, 4, 6 (217 tables) | 9,170 B | TFT_eSPI reads `PROGMEM` itself, per glyph | no: library internals |
| `WI_DATA`, Weather Icons polygons | 7,557 B | `pgm_read_byte` while drawing | phase 2, with a fallback |
| `MESSAGE_FACE`, the Message face's JSON | 3,895 B | parsed at each message | phase 2, with a fallback |
| `MONO_PTS`, `OUTFIT_PTS`, the line fonts | 2,016 + 1,474 B | read per stroke while drawing | no: drawn constantly, small |
| Spec 047's catalog and prompt (not in 2.3.1) | 7,682 B (gzipped) | sent as it is | yes, with 047 |

## How the update path works today

- `handleOtaUpload()` (`src/main.cpp`) reads the first 4,120 bytes into
  `FirmwareValidation` (`include/firmware_validation.h`). A `.bin.gz` must have exactly
  the project's gzip header with the "AM" field (XLEN 16), and its declared file size.
  Any byte past that size fails ("Image exceeds declared length").
- So **2.3.x and older cannot take anything but an image**: neither appended files,
  nor a bigger extra field, nor a second gzip member. A clock moving to 048 installs
  the `.bin.gz` first, then the bundle. That is the one extra step.
- The core's updater stages the file in free sketch space and eboot unpacks it into
  place at the next boot. The files cannot ride inside the image: anything in the
  unpacked image is in the sketch area and costs the same room.
- Recovery (power cycles, a crash at startup, a failed mount) serves OTA, status,
  restart and an explicit format. It never formats on its own and must not depend on
  storage: hence a fallback page in the image.

## Delivery options weighed

| Option | Files per update | Works offline | First install from 2.3.x | Notes |
|---|---|---|---|---|
| **Bundle: files then the `.bin.gz` in one file** | 1 | yes | `.bin.gz`, then the bundle | recommended |
| Separate files archive per release | 2 | yes | same | two uploads every time |
| The settings page fetches the files from GitHub and uploads them | 1 (`.bin.gz`) | no (needs the browser online) | `.bin.gz`, then a click | raw.githubusercontent.com sends `Access-Control-Allow-Origin: *`; release downloads do not. Could be added later on top of the bundle |
| The clock fetches them | 1 | no | automatic | needs TLS on the clock: tens of KB of BearSSL, the cost we are trying to cut |
| Files inside the image (appended to the unpacked image) | 1 | yes | n/a | still in the sketch area: no room gained |
| A different flash layout (LittleFS smaller, a raw staging area) | 1 | yes | breaks OTA over stock | ruled out by CLAUDE.md's flash layout rule |

## Can the settings page download a release? (checked 2026-11-07, curl with an Origin)

| URL | `Access-Control-Allow-Origin` |
|---|---|
| `api.github.com/repos/SimonLuckenuik/ai-agent-monitor/releases/latest` | `*` (latest is v2.4.0 that day) |
| `raw.githubusercontent.com/...` | `*` |
| `simonluckenuik.github.io` (Pages) | `*` |
| `github.com/.../releases/download/v2.4.0/AIAgentMonitor_v2.4.0.bin.gz` | none: 302 to `release-assets.githubusercontent.com`, which sends none either |

So the page can read the latest release (version, notes) from the API, but must
download the file from GitHub Pages (or raw), where the release workflow copies it. A
page served over http from the clock may fetch https; the push back to the clock is
same-origin.

## Can the clock download from GitHub itself? (checked 2026-11-07, openssl s_client)

GitHub is HTTPS only. The four hosts involved answer with no Max Fragment Length
extension (asked for 512 and 4096 bytes), so an ESP8266 client must keep BearSSL's full
16 KB receive buffer: with the TX buffer, BearSSL's context and the stack thunk, about
30 KB of heap during a download. Certificate chains: `api.github.com` and `github.com`
end at USERTrust ECC Certification Authority (Sectigo); `release-assets.githubusercontent.com`
and `*.github.io` at ISRG Root X1 (Let's Encrypt). Two trust anchors, about 2 KB, and
the time (NTP) to check validity.

The full firmware has about 24 KB of free heap and 4 KB of update room: no. The starter
draws almost nothing and keeps no faces or weather, so it has the heap, and BearSSL
(about 60 KB of flash, to measure) still leaves it under 516,096 B. So the starter
downloads by itself; the full firmware's fallback page has the browser do it.

## Files, then image, or image, then files?

Files first, into `/sys.new/`, swapped after `Update.end()`: a failed upload or a bad
image leaves the running firmware with its own files. Image first would leave a staged
image with no files if the upload broke halfway (the next boot would show the fallback
page: survivable, but worse). The swap is two renames; a power cut between them leaves
both folders, and the next boot keeps whichever matches the image's hash.

## Sizes to check when building

- The fallback page: about 4 KB gzipped is a guess (the full page is 24.4 KB for all
  cards). Measure while writing it.
- The bundle reader: modelled on `handlePhotoUpload()`'s file writing plus a CRC
  (`FirmwareValidation` already computes elf2bin's CRC over the image: the same one
  can cover each file); about 1.5-2 KB of code.
- LittleFS free space on the owner's clocks: photos are the only large files.

# Updates and recovery

Installed over stock SD Pro V1.0.6 from its own update page on 2026-10-05, and updated
through our own updater several times since
([record](../decisions/2026-10-05-first-flash-over-stock.md)). One unit has been tested.
Going back to stock still needs UART, so a full flash backup over UART beforehand is the
only way to keep the vendor firmware; the vendor update binaries are not full-device
backups. Since 2.5.0 (spec 048) the first install over stock is the starter,
`SDPro_AIAgentMonitor_starter_v<version>.bin` (a plain image, built with every release:
`pio run -e starter`), which joins WiFi; its page downloads the latest release's
`.update` file from GitHub in the browser and sends it to the clock. Every update after it is the `.update` file (the
firmware's `.bin.gz` and the settings page as a file); clocks on 1.4.0 to 2.4.x take the
`.bin.gz` first. All come from `python scripts/make_release.py`; only the starter has
the `SDP` prefix the stock page requires.

## Build and first flash

1. Build with `pio run`. The image is `.pio/build/esp12e/firmware.bin`.
2. Run `python tests/run_safety_tests.py` against that build (see compiler requirements
   in the script). These are offline tests, not hardware validation.
3. Verify that the device is ESP8266 with 4 MB flash and supports the raw sketch image.
   The build uses DIO at 40 MHz and the **4m3m** layout, the same one the stock firmware
   uses. Both stock images mount LittleFS at `0x40300000`–`0x405FA000` (flash offsets
   0x100000–0x3FA000) with 256 B pages and 8 KB blocks, and our image carries identical
   constants (checked by the safety tests). So after OTA our firmware mounts the existing
   stock volume. Our `/config.json` does not collide with stock's `/theme_config.txt` or
   `/photo_config.json`.
   With 4m3m the first 1 MB holds the running sketch **and** the staged update. A plain
   image must stay under 0x7E000 bytes (516,096 B); the full firmware is over that since
   1.4.0, so it is installed as `firmware.bin.gz`, whose limit is the image plus the
   `.gz` within 1 MB (`scripts/build_options.py` prints both and fails above). The first
   flash through the stock updater allows about 552 KB (1 MB minus the 488 KB stock
   image); the starter (about 500 KB, held under 516,096 B by the build) is made for it.
4. Establish recovery and back up the device before using its stock OTA page. The stock
   updater controls the first transfer; our validation applies only to subsequent uploads.
5. If proceeding with the stock updater, use the built firmware image and stable power.
   **Rename it to start with `SDP`** (for example `SDPro_AIAgentMonitor.bin`): the stock
   page refuses any other filename in the browser, with "Please use the correct
   firmware". It does not check the contents
   ([record](../decisions/2026-10-05-first-flash-over-stock.md)). Our own update page
   ignores the filename.
   Do not interrupt power during upload or reboot/copying.

## Recovery access

The firmware starts its setup network, **AI Agent Monitor XXXXXX** (the hostname's suffix),
WPA2 with the device password, at **http://192.168.4.1/**, and starts its embedded web
server before any storage or display initialization. No button is needed to reach it.
Since spec 046 it closes `apoff` minutes (default 60) after the clock joins the home
network, unless someone is on it, and opens again by itself whenever the link drops and
at every boot: a clock whose router changed or disappeared always has it, and unplugging
the clock and plugging it back in opens it for another `apoff` minutes. While the saved
network is missing, the STA side searches in 30 s windows every 5 minutes, because
continuous searching makes the ESP8266 hop channels and drop AP clients. On the LAN the
clock's DHCP hostname is **agentmonitor-XXXXXX**, where XXXXXX is from the chip id, so
the router's client list shows it. The full firmware also answers at
**http://agentmonitor-XXXXXX.local/** (mDNS), from the same subnet only; the
first-install image (`_nomqtt.bin`, v1.4.0) leaves mDNS out.

The unit has no usable button, so GPIO4 is never read to choose recovery. Recovery starts
when any of these happen:

- **Three quick power cycles** (power-on resets only; a crash or restart clears the
  count). Power the clock on for less than 10 s, twice, then
  power it on and leave it. The count is stored in a 5-byte record at offset 0xFF0 of the
  EEPROM sector (RTC memory does not survive power loss). The whole sector is read and
  rewritten, so stock EEPROM bytes are preserved. A boot that lasts 10 s clears the count.
  A crash loop shorter than 10 s triggers recovery the same way.
- **A crash during startup.** An RTC marker makes the next reset enter recovery if
  normal operation has not survived its first 30 seconds. Every deliberate reboot
  (`/restart`, `/connect`, OTA, format) goes through `safeRestart()`, which clears both
  triggers so a fast save is never mistaken for a crash.
- **A filesystem mount failure.** The firmware sets `LittleFSConfig(false)`, so it does
  **not** format an unknown or corrupt volume. Recovery then offers an explicit **Format
  storage** button (POST `/format_storage?confirm=erase`). It is accepted only after a
  mount actually failed and erases the whole FS region.

In recovery the screen shows "Recovery", the AP name, address and password, and the LAN IP
if one is connected. The AP never closes in recovery. It is WPA2 with that password (spec
046; open before 2.4.0); if the panel itself reset the previous recovery boot, the panel
is skipped and the AP is open, since nothing can show the password. The web server needs
no sign-in in recovery. Settings requests return HTTP 503. Only OTA, format (after a
failed mount), status, and restart work. **Restart** leaves recovery, and so does 30
minutes in recovery with nobody on its network and no request (spec 046: a recovery
started by accident, by a flickering power supply say, ends by itself; a broken normal
start comes back to recovery). After a failed mount the clock stays in recovery.

### Crash records, crash loops and safe mode

On any crash (exception, watchdog, panic, stack overflow) the firmware keeps a record in
RTC memory: the cause, the exception registers, the code addresses on the stack, the
face being drawn, free heap and stack. The next boot moves it into `/crashes.bin` (the
last 8), which the settings page shows under **System > Crashes** and
[`GET /api/crashes`](integrations-api.md#device-status-and-crashes) returns.

A crash within 10 minutes of boot counts toward a loop; 10 minutes up clears the count.

- **3 in a row:** custom faces are off. The built-in faces run, and the address screen
  says "Custom faces off: crashed".
- **2 more:** safe mode. No face is drawn: the screen shows "Safe mode", the address,
  the name and the version, while the settings page and the API work, to remove or fix
  the face (or install an update).
- **Resume** (System > Crashes, or `POST /api/crashes/resume`) goes back to normal.

A crash in the first 30 s after boot still starts recovery mode (below), as before.

### Build-time WiFi defaults (optional)

Copy `secrets.example.ini` to `secrets.ini` (gitignored) and set `[wifi] ssid/password`.
These are compiled in and used only when no WiFi is saved, and also in recovery. So even
with broken storage the clock joins the LAN and can be updated there. The values are
stored in plain text inside `firmware.bin`, so do not share that build. The WiFi page on
the clock (first card at http://192.168.4.1/) remains the normal way to set WiFi.

### Returning to stock

Stock images lack the length/CRC fields that our updater requires, so **you cannot OTA
back to the vendor firmware**. Returning to stock needs UART. Back up the full flash over
UART before the first flash if you may ever want to go back.

The setup network and the updater are protected by the device password (since 0.1.2;
the clock shows it on request). Firmware recovery cannot rescue an image that never
reaches this code; UART remains the fallback.

## Subsequent updates

Use the embedded **Firmware Update (OTA)** form (or **Install**, which downloads the
latest release's `.update` from GitHub Pages in the browser), or POST a single multipart
file to `/update_ota`. Accepted:

- the update file `AIAgentMonitor_v<version>.update` (spec 048, `include/update_bundle.h`):
  "AMUP", the files the firmware reads from storage (each with its size and CRC), then the
  firmware's `.bin.gz`. The files go to `/sys.new/` as they arrive and move to `/sys/` only
  after the image is committed; `/sys/manifest` then holds their hash, which the image
  compares with its own (`FILES_HASH`) at boot. In recovery, or when storage did not
  mount, the image installs and the files are skipped (the reply says so);
- the compressed `AIAgentMonitor_v<version>.bin.gz` (gzip with an "AM" header field:
  unpacked size, file size, CRC; the bootloader unpacks it);
- raw project-compatible ESP8266 Arduino images that fit a plain update: 4 MB/DIO/40 MHz,
  with the embedded image length and CRC produced by `elf2bin.py`.

Filesystem and vendor binaries are refused. The filename never selects a flash region;
uploads always use `U_FLASH`. The code is `src/update.cpp`, shared by the firmware and
the starter.

**The settings page is a file** (`/sys/index.html.gz`, since 2.5.0). When `/sys/manifest`
matches the image, `/` serves it; otherwise (files missing after a `.bin.gz` alone or a
storage format, files from another version, storage not mounted, recovery) `/` serves the
setup page in flash (`web/fallback.html`): sign in, WiFi, install the latest release or a
file. `/setup` serves it always. `/status` reports `files`: `ok`, `missing`, `stale` or
`unmounted`.

**The starter** (`src/starter.cpp`) has only the setup page and the update handler; the
browser downloads the release, as with the firmware. On its own network it keeps the
captive portal (every name answers with the clock, so a phone opens the setup page)
except GitHub's names (`api.github.com`, `simonluckenuik.github.io`): once the clock is on
WiFi it looks them up on the home network and answers with their real addresses, and
forwards the setup network's traffic (NAPT, 64 entries; lwIP with its features, which the
firmware leaves out: `scripts/starter_options.py`). So the page works the same from the
home network and from the clock's own. An earlier version downloaded over HTTPS on the
clock (BearSSL): 76 KB of the image and about 33 KB of heap, given up for this.

**Both builds' web servers queue at most 4 connections not yet served** (the core's
default is 255): on the setup network every name leads to the clock, and a phone's burst
of connections could take the heap.

The updater buffers and validates the image header before staging (28 bytes of a
`.bin.gz`; 4,120 of a plain image, whose second header ends there). It checks the declared
length against available sketch space, accounts for every received byte, and verifies the
CRC of the entire image with the embedded length/CRC fields zeroed. It checks each updater
operation and withholds the final byte until the complete HTTP request is validated.
Truncated, corrupt, oversized, aborted, and multiple-file uploads are rejected without
committing or rebooting. Only successful `Update.end(false)` causes reboot. CRC detects
accidental corruption; it does not authenticate the publisher or prove hardware compatibility.

## Offline checks

`tests/run_safety_tests.py` compiles the actual OTA handler with a mocked updater. It
checks valid images across several chunk sizes, renamed firmware, truncation, corruption,
extra bytes, invalid headers, filesystem data, multiple files, empty requests, aborted
uploads followed by retries, and begin/write/end failures. It also simulates the EEPROM power-cycle counter
(trigger, reset, clamp, no needless erase, stock bytes preserved). It checks that the
built image's FS constants match stock and that the image fits the OTA budget, and it
checks the storage/recovery invariants in the source. Actual AP access, button polarity, reset behavior,
flash preservation, and OTA boot must still be tested on recoverable hardware.

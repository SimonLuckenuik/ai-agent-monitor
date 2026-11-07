# 046 Progress

## 2026-11-06

- Read how the setup network works (RESEARCH.md): up in every mode for the clock's whole
  life; WPA2 with the device password, open and with no sign-in in recovery; requests
  on it skip the DNS-rebinding guard. `updates-and-recovery.md` still calls it "an open
  AP" (true only in recovery): to correct with this spec.
- Options weighed (RESEARCH.md): recommended B, the AP off `apoff` minutes after the
  station link comes up, back when the link drops and at every boot.
- **Measured** a firmware-only prototype of B (setting `apoff`, `apOn()`,
  `serviceAp()`, `/status` `ap`; no settings page field), built on 2.3.1 and **not
  committed** (spec not approved):

  | Build | `firmware.bin` | `firmware.bin.gz` |
  |---|---|---|
  | 2.3.1 (`5a9fb81`) | 602,384 B | 427,332 B |
  | prototype | 602,880 B (+496) | 427,721 B (+389) |

  885 B of update room; 1 MB less two sectors less both images goes from 10,668 B to
  9,783 B, still two whole sectors (8,192 B) of room. The settings page's select adds
  perhaps 100-150 B more, in both images.
- Not done: the emulator and integration runs (the host core's WiFi mock does not model
  the AP; needs step 3 of the plan), and anything on the clock. The key unknown is
  whether `softAPdisconnect(true)` / `enableAP(true)` keep the station associated:
  only the clock can tell.
- Waiting for the owner: approval and the open questions in SPEC.md.

## 2026-11-06, the owner's answers

- Default 60 minutes for existing clocks too; the AP reopens both when the link drops
  and at every boot; no settings-page button to reopen it (reaching the page means
  there is a network). SPEC.md updated.
- Searched online and in the SDK (RESEARCH.md): turning the AP off while connected is
  what ESPHome does on ESP8266 when its station connects, with the same SDK call; not
  documented by Espressif either way, so still to confirm on the clock.
- The "saved stock AP at boot" question is withdrawn: core 3 keeps the radio off at boot
  until our code starts it.
- Still open: questions 3 (WPA2 on the recovery AP) and 4 (leave an unused recovery
  after 30 minutes). Spec still Draft, not approved.

## 2026-11-07

- The owner: WPA2 on the recovery AP, yes (requirement 9). Found while writing it: when
  the panel is skipped in recovery, the password is not on the screen, so the AP stays
  open in that one case, or the clock could not be joined at all.
- Recommended to the owner: an unused recovery restarts normally after 30 idle minutes,
  except after a failed mount (requirement 10, open question 4). Waiting for the go and
  for the spec's approval.

## 2026-11-07, built

- Approved by the owner ("Approve, build it", with the 30-minute recovery exit).
- Firmware (`src/main.cpp`, `src/config.h`): setting `apoff` (default 60, 0 or 5-1440);
  `apOn()` and `serviceAp()` (closes `apoff` minutes after the link came up, not while a
  client is joined, reopens when the link drops; a failed switch retries after a
  minute); recovery's AP is WPA2 unless the panel is skipped; an unused recovery
  (no client, no request, storage mounted) restarts normally after 30 minutes;
  `/status` `ap` = `wpa2` / `open` / `off`. The strings are in flash (`F()`): the first
  build put "off"/"open"/"wpa2" in RAM and went 4 B over the safety test's limit.
- Settings page: WiFi card, "Setup network" select (Always on, 15 min, 1 h, 4 h, 24 h);
  System shows it "off (restart the clock to open it)".
- Emulator: `build.sh` patches the core's WiFi mock to remember the mode set and to
  report no AP clients; `EMU_MINUTE_MS` shortens the firmware's minute (1 s in the
  integration tests' compose file).
- Tests: safety test guard replaced ("persistent AP" became: closes only in
  `serviceAp()`, never from recovery); integration cases `setup_network_timer` and
  `recovery_network`.
- Docs: `updates-and-recovery.md` (also fixes "an open AP"), `settings.md`,
  `integrations-api.md` (`ap`; skill's `face-format.md` regenerated, both skills),
  `testing.md`, `emulator/README.md`, README's Recovery, CHANGELOG; plugin 0.7.4.

  | Build | `firmware.bin` | `firmware.bin.gz` | Room (whole sectors) |
  |---|---|---|---|
  | 2.3.1 | 602,384 B | 427,332 B | 8,192 B (10,668 B unrounded) |
  | spec 046 | 603,392 B (+1,008) | 428,139 B (+807) | 8,192 B (8,853 B unrounded) |
  Less than 700 B of growth is left before the room drops to one sector (4 KB).
- Checked: `pio run`; safety tests; unit tests (all suites, with mDNS, guards and
  OpenClaw); `tests/web` (backup, minified page, time zones); `claude plugin validate .`;
  integration tests, 37 cases pass, `setup_network_timer` and `recovery_network`
  included, no screen changed. (The integration image was built a few minutes before
  the strings moved to `F()`; the firmware's behaviour is the same.)
- Not checked (only the clock can): that the station link survives the AP closing (the
  emulator's WiFi is a mock), the heap the AP frees, the AP reopening on a real link
  drop (the emulator cannot drop the link), joining recovery's WPA2 network from a phone.

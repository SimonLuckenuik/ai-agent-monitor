# 046 The setup network closes once the clock is on the router

Status: Done in 2.4.0 (approved 2026-11-07, the owner, in the chat: "Approve, build it", with the 30-minute recovery exit)

## Problem

The clock's own access point, **AI Agent Monitor XXXXXX** at 192.168.4.1, is up for the
clock's whole life, in every mode (`startAP()` in `setup()`, never turned off; see
RESEARCH.md). Once the clock is on the home network it serves no purpose, but it stays
a second way in that bypasses the owner's VLAN, firewall and router: anyone within radio
range who has, guesses or cracks the device password (the WPA2 key, ~49 bits) reaches
the web server directly, without the DNS-rebinding guard of spec 029. In recovery the
AP is open and the web server needs no sign-in, until someone presses Restart.

The owner's idea: after about 60 minutes the AP is no longer there, and a restart (a
power cycle) brings it back. The constraint: a clock whose Wi-Fi changes must stay
recoverable without opening the case (the USB port is power only; serial means
soldering onto the ESP-12F's pads).

## Goals (to agree)

1. Once the clock has been on the router for a while (60 minutes by default), its setup
   network is off: no beacons, nothing to join.
2. A clock that is not on a network always has its setup network, exactly as today:
   first setup, a router that is gone, a changed Wi-Fi password.
3. A power cycle always brings the setup network back for the same time.
4. The owner can choose the time, or keep the network always on (today's behaviour).
5. Recovery's network is no longer open to anyone in range: WPA2 with the device
   password that the recovery screen shows (the owner, 2026-11-06).
6. A recovery that nobody uses ends by itself (recommended, waiting for the owner's go:
   open question 4).

## Non-goals

- No change to the device password, the sign-in or the LAN side.
- No button: the clock has no usable one (GPIO4 is never read).
- No "reopen the setup network" button on the settings page (the owner, 2026-11-06):
  reaching the settings page already means the clock has a network.
- No routing between the AP and the LAN (there is none today: lwIP without NAPT).

## Requirements

1. A setting `apoff` (minutes, 0 = always on): how long the setup network stays up after
   the station link comes up. Default 60; accepted 0 or 5-1440 (5 is the smallest, so
   the first setup's page can still show the new address before the AP goes).
2. The AP goes off when all hold: not in recovery; `apoff` > 0; the station link is up
   (`MODE_RUN`); it has been up for `apoff` minutes without a break; no client is
   joined to the AP (`softAPgetStationNum() == 0`; the switch waits for the last client
   to leave). An update in progress already pauses the loop.
3. Off means the soft AP is disabled (`WiFi.softAPdisconnect(true)`: station mode only)
   and the captive DNS is stopped. The SDK's saved configuration is not written
   (`persistent(false)` stays).
4. The AP comes back on, with the same name, password and address, when the station
   link drops (the clock goes back to the setup screen, as today), and at every boot.
   The timer starts again from the next time the link comes up.
5. Recovery and safe mode are not affected by `apoff` (recovery's AP is never turned off
   by the timer).
6. Turning the AP off never drops the station link. The switch is the same call
   ESPHome makes on ESP8266 when its station connects (`wifi_set_opmode_current` to
   station only, RESEARCH.md); turning it on only happens while the link is down or at
   boot. Still to confirm on the clock (no reconnect, no gap in pings); if it does
   reconnect, the design is revisited before release.
7. `/status` reports `"ap": true|false`; the settings page shows the setup network as
   "off (restart the clock to open it)" when it is off, and offers the setting
   (Always on, 15 min, 1 h, 4 h, 24 h).
8. Backup and restore carry `apoff` like any other setting.
9. **Recovery's AP uses WPA2 with the device password** whenever the recovery screen is
   drawn, since that screen shows the password. When the panel is skipped (the panel
   itself reset the previous recovery boot, `recoveryDisplay` false), the password
   cannot be read off the clock, so the AP stays open as today: a recovery that cannot
   be joined would leave only the soldering iron. Recovery's web server still needs no
   sign-in (being on the WPA2 network already proves the password).
10. **An unused recovery ends by itself** (if the owner agrees, open question 4): after
    30 minutes in recovery with no client ever joined to its network and no web request,
    the clock restarts normally (`safeRestart()`). Not when the storage failed to mount
    (a restart cannot fix it, and recovery's Format button is the way out). A client
    joining or any request stops the countdown for good on that boot.

## Behaviour (settings, API, screens)

- Settings page, WiFi card: "Setup network: Always on / Off 15 min / 1 h / 4 h / 24 h
  after joining", default 1 h, with one line: "Restart the clock to open it again. It
  opens by itself when the clock loses its WiFi."
- API: `GET /api/set?key=apoff&value=<minutes>` (signed in); `GET /config` has
  `apoff`; `GET /status` has `ap`.
- Screens: nothing new. The setup screen still appears whenever the link is down, and
  the AP it names is then up.
- After an update to this release, a clock that has been on its router for an hour
  closes its setup network: the default applies to existing clocks too (the owner,
  2026-11-06).

## Budget (flash, RAM, draw time)

- Flash: measured on a prototype (PROGRESS.md): +496 B plain, +389 B compressed, 885 B
  of update room, plus the settings page field. Built: +1,008 B plain, +807 B
  compressed; both images still round to the same sectors, so 2.4.0 keeps 2.3.1's 4 KB
  of room (PROGRESS.md).
- Recovery (requirements 9, 10): a few dozen bytes; measured with the rest.
- RAM: one `int` in `Config`. Turning the AP off should free 1-2 KB of heap (the SDK's
  soft-AP and DHCP server, the DNS socket); to measure on the clock.
- Draw time: none.

## Security gain

- After the first hour, the clock is reachable only through the owner's network: no
  WPA2 handshake to capture, no PSK to crack offline, nothing for a neighbour or a
  passer-by to join, and no ESP8266 soft-AP stack exposed to pre-authentication bugs.
- What it does not change: anyone in range can still knock the station off the router
  (the NONOS SDK has no 802.11w), which reopens the AP; the AP is then exactly what it
  is today (WPA2, device password), never weaker. The owner chose this over "only on a
  boot" (2026-11-06): a changed router needs no gesture.
- A power cycle opens it: that needs physical access, which already gives the screen
  and the three-cycle recovery gesture.

## Open questions

Answered by the owner on 2026-11-06:

1. Default for existing clocks: **60 minutes for everyone** after the update.
2. Reopen on a link drop or on a boot: **both** (requirement 4).
5. A settings-page button to reopen it: **no** (non-goals).
6. The SDK's saved AP at power-on: **not needed** (RESEARCH.md). Arduino core 3 turns
   the radio off at boot (`__disableWiFiAtBootTime()`) before the SDK can bring up a
   saved configuration, so a stock network is not broadcast at boot by our firmware.
   The 2026-10-25 issue stays open and unexplained.

3. Recovery's AP: **WPA2 with the device password** (requirement 9), open only when the
   panel is skipped and so cannot show the password.

Still open:

4. **Recovery that nobody uses** (recommendation: yes, requirement 10). Recovery is
   entered by three quick power cycles, a crash in the first 30 s, or a storage that
   does not mount, and lasts until someone presses Restart. A clock that lands there
   by accident (power flickering three times in a storm, one crash at boot) and is not
   noticed stays there for good, showing "Recovery" instead of faces. With WPA2 on its
   network (question 3) that is no longer a security hole, but the clock is out of
   service until someone notices. Restarting after 30 idle minutes heals the accident;
   if the normal boot really is broken, it crashes back into recovery (a crash in the
   first 30 s re-enters it), so the owner still finds it there, at worst one restart
   every half hour. The mount failure is excluded. Agree?

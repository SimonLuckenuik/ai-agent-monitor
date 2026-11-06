# 046 The setup network closes once the clock is on the router

Status: Draft

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
5. Recovery mode is unchanged (open, no sign-in) unless the owner chooses otherwise
   (open questions 3 and 4).

## Non-goals

- No change to the device password, the sign-in or the LAN side.
- No button: the clock has no usable one (GPIO4 is never read).
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
5. Recovery and safe mode: unchanged by this setting (recovery's AP is never turned off).
6. Turning the AP off or on never drops the station link. If the clock shows that it
   does (a reconnect on each switch), the design is revisited before release.
7. `/status` reports `"ap": true|false`; the settings page shows the setup network as
   "off (restart the clock to open it)" when it is off, and offers the setting
   (Always on, 15 min, 1 h, 4 h, 24 h).
8. Backup and restore carry `apoff` like any other setting.

## Behaviour (settings, API, screens)

- Settings page, WiFi card: "Setup network: Always on / Off 15 min / 1 h / 4 h / 24 h
  after joining", default 1 h, with one line: "Restart the clock to open it again. It
  opens by itself when the clock loses its WiFi."
- API: `GET /api/set?key=apoff&value=<minutes>` (signed in); `GET /config` has
  `apoff`; `GET /status` has `ap`.
- Screens: nothing new. The setup screen still appears whenever the link is down, and
  the AP it names is then up.
- After an update to this release, a clock that has been on its router for an hour
  closes its setup network (the default applies to existing clocks too: open
  question 1).

## Budget (flash, RAM, draw time)

- Flash: measured on a prototype (PROGRESS.md): +496 B plain, +389 B compressed, 885 B
  of update room (2.3.1 keeps two whole sectors, 8 KB, of room), plus the settings page
  field; at ~1.7 KB of update room per KB of code, well under one 4 KB sector of room.
- RAM: one `int` in `Config`. Turning the AP off should free 1-2 KB of heap (the SDK's
  soft-AP and DHCP server, the DNS socket); to measure on the clock.
- Draw time: none.

## Security gain

- After the first hour, the clock is reachable only through the owner's network: no
  WPA2 handshake to capture, no PSK to crack offline, nothing for a neighbour or a
  passer-by to join, and no ESP8266 soft-AP stack exposed to pre-authentication bugs.
- What it does not change: anyone in range can still knock the station off the router
  (the NONOS SDK has no 802.11w), which reopens the AP; the AP is then exactly what it
  is today (WPA2, device password), never weaker. Open question 2 removes even that.
- A power cycle opens it: that needs physical access, which already gives the screen
  and the three-cycle recovery gesture.

## Open questions

1. **Default for existing clocks**: 60 minutes for everyone after the update (more
   secure by default), or 0 (always on) until the owner turns it on?
2. **Reopen on a link drop, or only on a boot?** Reopening when the link drops (the
   recommendation) keeps a clock whose router changed recoverable with no gesture, but
   lets a deauth attacker in range reopen the WPA2 AP. Only on a boot: an attacker
   cannot reopen it, and a changed router costs one power cycle (the setup screen
   would then say "Restart the clock to open the setup network").
3. **Recovery's AP**: keep it open (today), or WPA2 with the device password, which the
   recovery screen already shows (no flash cost)? Today anyone in range of a clock in
   recovery can install firmware on it without a password.
4. **Recovery that nobody uses**: leave it on until Restart (today), or restart normally
   after, say, 30 minutes with no client and no request?
5. **A LAN button to reopen it** ("Open the setup network for 15 minutes", signed in),
   or is a power cycle enough?
6. **The SDK's saved AP at power-on** (RESEARCH.md, the open issue of 2026-10-25): the
   stock firmware's saved soft-AP configuration may come up for a moment at every boot
   before our code replaces it. Write a harmless saved configuration once (STA only,
   or our own AP) so nothing else is ever broadcast? Needs a look on the clock first.

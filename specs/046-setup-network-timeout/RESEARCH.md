# 046 Research: the setup network today, and turning it off

Read on 2026-11-06 from `faces-v2` at 2.3.1 (`5a9fb81`), the ESP8266 Arduino core in
`~/.platformio/packages/framework-arduinoespressif8266`, and
`docs/specifications/updates-and-recovery.md`.

## When the clock's own access point is up today

**Always.** `setup()` calls `startAP()` before anything else (`src/main.cpp`, "WiFi"):

```cpp
WiFi.persistent(false);
WiFi.mode(WIFI_AP_STA);   // "Keep recovery accessible even when the router disappears."
WiFi.softAPConfig(192.168.4.1, 192.168.4.1, 255.255.255.0);
restartAP();              // WiFi.softAP(setupSsid(), devicePassword), open in recovery
dns.start(53, "*", WiFi.softAPIP());   // captive DNS: every name is 192.168.4.1
```

Nothing ever calls `softAPdisconnect()` or changes the mode again (`grep`: no use in
`src/`). The loop calls `dns.processNextRequest()` on every pass, in every mode.

| State | AP | Security | Sign-in on the web server |
|---|---|---|---|
| Normal, joined to the router (`MODE_RUN`) | up, alongside the station | WPA2-PSK, key = the device password | needed (session cookie) |
| Normal, no router (`MODE_AP`, setup screen) | up; the station searches 30 s every 5 min so AP clients are not dropped by channel hops | WPA2-PSK, device password | needed |
| Recovery (3 quick power cycles, a crash in the first 30 s, a failed mount) | up until **Restart** | **open** (`restartAP()`: "open in recovery, so recovery is always reachable") | **none**: `requestAuthorized()` returns true in recovery, so anyone joined can upload firmware |
| Safe mode (5 crashes) | up | WPA2 | needed |

`docs/specifications/updates-and-recovery.md` still says "The firmware starts an open
AP"; that is true only in recovery (the setup network has had WPA2 since 0.1.2). Its
"Recovery access" section is to be corrected with this spec.

The device password: 10 characters from a 31-letter alphabet (~49 bits) from the
hardware RNG on first boot, in the EEPROM sector at 0xFC0; shown on the setup screen,
the recovery screen, and for 60 s on "Show on clock". The same secret is the WPA2 key
and the settings sign-in (5 tries, then a minute's lockout).

### What a client of the AP reaches

- The whole web server: the same routes as on the LAN, with sign-in where the LAN needs
  it. Unauthenticated routes (`/status`, `/version`, `/config`, the login page,
  `/api/auth/reveal`) answer there too.
- `hostAllowed()` (spec 029's DNS-rebinding guard) is skipped for requests arriving on the
  AP address, and in recovery, because the captive portal sends every name to the clock.
- Nothing beyond the clock: lwIP is built without `LWIP_FEATURES`
  (`platformio.ini`), so there is no IP forwarding or NAPT to the home network.

So the AP is a second way in that **does not go through the owner's VLAN, firewall or
router at all**: anyone within radio range who has (or guesses, or cracks) the device
password talks to the clock directly. In recovery anyone within range does, with no
password.

### Other observations

- **The SDK's saved AP may start before our code.** `WiFi.persistent(false)` means our
  AP settings are never written to the SDK's configuration sector, so the opmode and
  soft-AP configuration saved there (by the stock firmware, presumably "Smart Weather
  Clock" in AP_STA) are what the SDK brings up at power-on, until `startAP()` replaces
  them. This would explain the open issue
  [2026-10-25 setup network kept its old name](../../docs/issues/2026-10-25-setup-network-kept-old-name-after-reboot.md).
  Not measured: how long the stock AP is visible at boot, and with what security.
- **No 802.11w (protected management frames)** in the NONOS SDK the core uses: anyone in
  range can send deauthentication frames and knock the station off the router. This
  matters for any design where "the station link is down" reopens the AP (see options).
- `softAPdisconnect(true)` (core, `ESP8266WiFiAP.cpp`) clears the soft-AP configuration
  with `wifi_softap_set_config_current()` (not saved, since `persistent(false)`), then
  `enableAP(false)`, which calls `mode(STA)`. Bringing it back is `enableAP(true)` (or
  `mode(WIFI_AP_STA)`), `softAPConfig()`, `softAP()`. `softAPgetStationNum()` returns
  how many clients are joined.
- Whether `mode(STA)` from AP_STA keeps the station associated without a reconnect is
  not documented for the NONOS SDK. To be checked on the clock (the emulator's WiFi is
  a mock and always "connected").

## Options weighed

| Option | Exposure | Recovery without opening the case | Verdict |
|---|---|---|---|
| A. Status quo: AP always up | WPA2 AP within radio range for the clock's life; open, no sign-in, in recovery until Restart | always | the baseline |
| B. **AP off N minutes after the station link comes up; back at once when the link drops; back at every boot** | AP for the first N minutes after each boot, and whenever the router is gone | the router gone or changed: the link drops, the AP is back with the setup screen, as today | **recommended** |
| C. AP off N minutes after boot, whatever the link (the owner's first idea, literally) | as B | a clock that never joined, or whose router changed, has no AP after N minutes: a power cycle reopens it for N minutes | loses to B: a clock with no network would also have no AP; the setup screen would point at a network that is not there |
| D. AP only while there is no link (N = 0) | smallest | as B | first setup breaks: joining the router closes the AP under the browser before it can show the new address. Kept as "the smallest N", not as 0 |
| E. As B, but the AP returns only on a boot, never on a link drop | smaller than B: a deauth attack cannot reopen it | router changed: the owner power cycles once; the screen says so | strongest; costs one power cycle when the router changes (asked as an open question) |
| F. Hide the SSID (`softAP(..., hidden)`) | none really: the SSID shows in probe responses and to any sniffer | always | security by obscurity; not worth the confusion |
| G. A LAN button / API to reopen the AP for N minutes | adds a way to open it, signed in only | adds one | optional; a power cycle already does it (open question) |

**Why B is not weaker than today when it reopens the AP.** A deauth attacker in range
can force the station off the router and so bring the AP back, but the AP is still
WPA2 with the device password: the attacker is back to exactly today's position, never
better. Everyone else gets a clock whose AP is off almost all of the time.

**Why "back at every boot" is safe as the recovery gesture.** A power cycle needs
physical access; anyone with that already has the clock's screen (which shows the
password on request) and can do the three-cycle recovery gesture anyway.

**Keep the AP while a client is joined.** If the timer runs out while someone is on the
setup network (setting up, uploading firmware over the AP), turning it off would cut
them off mid-task. Deferring the switch while `softAPgetStationNum() > 0` costs nothing
in security: a client has to know the password to be joined. An update in progress
already stops the loop (`otaFiles || otaStarted`).

### Recovery

Recovery's AP is open and its web server needs no sign-in, so that a lost or corrupted
password never locks the owner out. Turning it off after N minutes would undo that
purpose when the router is gone (recovery is then the only way in). Two separate
hardenings are possible and are listed as open questions rather than goals:

- WPA2 on the recovery AP too, with the device password that the recovery screen
  already shows (`drawRecoveryScreen()`); `loadPassword()` regenerates a missing or
  invalid one, so there is always one on the screen. About 0 bytes of code.
- Leaving recovery after a long idle time (no client, no request for, say, 30 minutes)
  with a normal restart. Risky if the normal boot is what is broken (a crash loop
  then re-enters recovery after a few boots anyway).

## Costs on the ESP8266

- **Flash**: the switch is a few lines in the loop, one setting (load, save, JSON, the
  settings page), and `softAPdisconnect` / `softAPgetStationNum` from the core. See the
  measurement in PROGRESS.md.
- **Heap**: turning the AP off frees what the SDK holds for it (the DHCP server's lease
  table, the soft-AP's buffers) and the DNSServer's UDP socket. Expected around 1-2 KB;
  not measurable in the emulator (mock WiFi). To measure on the clock: `/status`'s
  `heap` with the AP on and off.
- **Radio**: none to speak of. In AP_STA the AP is tied to the router's channel; STA alone
  is no different for the clock's traffic. Beacons every 100 ms stop.
- **Time**: one comparison per loop pass.
- **RAM for the setting**: one `uint16_t` in `Config`.

## Links

- ESP8266 Arduino core soft-AP API: `libraries/ESP8266WiFi/src/ESP8266WiFiAP.cpp`
  (`softAPdisconnect`, `softAPgetStationNum`), `ESP8266WiFiGeneric.cpp` (`enableAP`, `mode`).
- Spec 029 (network guards: DNS rebinding, WPA downgrade), spec 018 (first run on the
  clock), `docs/specifications/updates-and-recovery.md`.

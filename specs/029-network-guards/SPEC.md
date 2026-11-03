# 029 Network guards: Wi-Fi downgrade, request host, clock time

Status: Done in 2.1.0 (confirmed on the clock 2026-11-03)

## Problem

Three attacks on the local network that smalltv-pomodoro guards against and we do not:

1. **Wi-Fi downgrade** (CVE-2020-12638): a forged beacon can make the ESP8266 SDK
   reconnect to a look-alike open network. Espressif did not fix it in the NONOS SDK the
   Arduino core uses.
2. **DNS rebinding:** a web page the owner visits points its own name at the clock's IP
   and then reads and calls the clock's API from the browser as same-origin. Our sign-in
   cookie is `SameSite=Strict` and keys travel in a header, so this reaches the endpoints
   that need neither: `/status`, `/config`, face and brightness through `/api/set`, the
   faces list.
3. **Clock time:** SNTP takes any reply. One forged packet moves the clock, which shows
   the wrong time, expires `$ttl` variables and messages early or late, and skews the
   crash-loop window.

## Goals

1. **Wi-Fi:**
   - a saved network with a password connects only with WPA or better (the SDK's
     `threshold.authmode`; networks saved by the stock firmware are raised);
   - when the connection's auth mode weakens, the clock drops it and reconnects to the
     saved network;
   - each event is counted in `/status` (`wifiDowngrades`; the clock keeps no event log).
2. **Request host:**
   - on the station interface, a request is answered only when its `Host` is an IPv4
     address, the clock's mDNS name, its DHCP host name, or a name the owner lists in a
     new setting ("Other names for this clock", for a router DNS name); otherwise 403
     "Unknown host name";
   - the setup network and recovery keep answering any name (the captive portal needs
     it);
   - the API key and sign-in rules are unchanged.
3. **Clock time:**
   - the first SNTP time after boot is taken as now;
   - a later SNTP change of more than 2 minutes is held until a second source agrees
     within 2 minutes: the `Date` header of the weather reply; without weather, the same
     jump repeated by every SNTP reply for 6 hours (amended while building: a second
     SNTP reply proves little, as a forger can send two) (plain HTTP, but over TCP: forging it takes a machine in the path,
     where one UDP packet from anywhere on the network forges SNTP);
   - until then the clock keeps its own time and reports `timeHeld` in `/status`.
4. **No regressions:**
   - the integration suite passes, with new cases for the host check (allowed and refused
     names, the setup network) and a held time jump;
   - Home Assistant through the IP or the mDNS name, and the skill, work unchanged.
5. **Within the budgets:** under 2 KB of flash for all three.

## Non-goals

- Signed firmware updates (about 6.5 KB of flash; a later spec once room exists).
- A CSRF header for the settings page: the `SameSite=Strict` cookie already covers the
  signed-in actions.
- FragAttacks (no fix exists for this SDK).

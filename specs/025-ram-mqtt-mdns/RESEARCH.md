# Research

The measurements are in the [footprint report](../../docs/specifications/footprint.md)
(2026-10-31). In short:

## Free heap over time

| | Free at rest | Source |
|---|---|---|
| 0.1.1 | about 32 KB (weather running) | STATUS, 2026-10-05 |
| 1.8.0 | 15.3-16.4 KB | the clock's `/status` |
| 1.9.1 | 14.1-14.6 KB | the clock's `/status` |

Static RAM grew 11.2 KB (0.1.2 40,752 B → 1.9.1 51,944 B): our strings +4.3 KB, the faces'
tables and state +3.8 KB, the integrations' tables +2.6 KB. The SDK, runtime, core and
lwIP have not moved. The rest of the drop is heap held at runtime, and the routes are the
largest part of it.

## What MQTT and mDNS take

- **MQTT** (PubSubClient):
  - **Held:** the message buffer of 1,536 B (`setBufferSize`) and the TCP connection.
  - **At connect:** discovery builds one String per entity (400-900 B; the face list
    grows with each custom face).
  - **Emulator:** measured against a local broker; the arena missed the buffer, which
    is allocated before `setup()`.
  - **Gate:** 16 KB (`integrations.cpp`).
- **mDNS** (LEAmDNS):
  - **Measured in the emulator with a test build:** 420 B held after start (host,
    service and TXT records), and nothing allocated while answering 120 queries.
  - **On the chip:** add the UDP socket, the multicast group and the network buffers
    per packet.
  - **Gate:** 28 KB (`MDNS_MIN_HEAP`, `main.cpp`).

## The RAM to win back

| Holder | Bytes | How |
|---|---|---|
| `firmware` (update check, `firmware_validation.h`) | 4,148 static | allocate on upload |
| Our strings in `.rodata` | 7,579 static | `F()`, `PSTR`, `FPSTR`; keep the per-frame keyword tables |
| Web server routes | about 4,700 heap | one `RequestHandler` with a table in flash |

`ESP8266WebServer::on()` allocates a `FunctionRequestHandler` (the `std::function`, a
`Uri` and its String) per route. `addHandler()` takes one handler that can answer every
path (`canHandle`, `canUpload`, `handle`, `upload`), which is what the dispatcher uses.

## Options weighed

- **Drop mDNS for 21 KB of flash.** Not needed for RAM (it takes under 2 KB). Kept for
  `<host>.local`. Can still be dropped later if flash runs out.
- **A smaller MQTT buffer.** 1,536 B is set for discovery messages (up to about 900 B
  with many faces). Kept.
- **Moving the keyword tables to flash too.** Read many times a frame; flash reads go
  through the cache and are slower. Measure the drawing time before and after if they
  move.

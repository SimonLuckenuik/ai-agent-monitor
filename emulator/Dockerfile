# SmartClock-OSS emulator: the firmware source built on the esp8266/Arduino core's host
# emulation (tests/host), with the panel served to a browser. Build from the repo root:
#
#   docker compose -f emulator/docker-compose.yml up --build
#
# See emulator/README.md.

# ---- Dependencies: the same core and library versions as platformio.ini resolves.
FROM debian:bookworm-slim AS deps
RUN apt-get update && apt-get install -y --no-install-recommends \
      build-essential git python3 ca-certificates \
    && rm -rf /var/lib/apt/lists/*
ARG CORE_VERSION=3.1.2
RUN git clone --quiet --depth 1 --branch ${CORE_VERSION} https://github.com/esp8266/Arduino /esp8266 \
    && cd /esp8266 && git submodule update --quiet --init --depth 1 \
         libraries/LittleFS/lib/littlefs libraries/ESP8266SdFat
RUN mkdir /libs && cd /libs \
    && git clone --quiet --depth 1 --branch V2.5.43 https://github.com/Bodmer/TFT_eSPI \
    && git clone --quiet --depth 1 --branch v7.4.3 https://github.com/bblanchon/ArduinoJson \
    && git clone --quiet --depth 1 --branch v2.8 https://github.com/knolleary/pubsubclient PubSubClient \
    && git clone --quiet --depth 1 --branch V1.1.0 https://github.com/Bodmer/TJpg_Decoder

# ---- Build: the firmware source plus the emulator glue.
FROM deps AS build
COPY src /app/src
COPY include /app/include
COPY web /app/web
# The panel config and F_CPU (emu_build_flags.h, from gen_headers.py).
COPY platformio.ini /app/platformio.ini
COPY emulator /app/emulator
RUN sh /app/emulator/build.sh /app /smartclock

# ---- Run: the emulator binary, the screen page and the supervisor.
FROM debian:bookworm-slim
COPY --from=build /smartclock /emulator/smartclock
COPY emulator/screen.html emulator/presets.json emulator/run.sh /emulator/
# The Scenarios panel loads the example faces and their sample values.
COPY examples/faces /emulator/faces
RUN chmod +x /emulator/run.sh
ENV EMU_DATA=/data EMU_SCREEN_PAGE=/emulator/screen.html
VOLUME /data
EXPOSE 80 8080
ENTRYPOINT ["/emulator/run.sh"]

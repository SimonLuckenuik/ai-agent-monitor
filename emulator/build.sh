#!/bin/sh
# Builds the emulator binary from a source tree, on the core's host emulation.
#   emulator/build.sh <project dir> <output binary>
# Runs inside the image's "deps" stage (/esp8266 and /libs); the Dockerfile calls it,
# and so can a container with the repo mounted, to iterate without rebuilding the image.
set -eu
root=$(cd "$1" && pwd)
out=$2
work=/tmp/smartclock-build
mkdir -p "$work/sketch"
python3 "$root/emulator/gen_headers.py" "$root" "$work/generated"
touch "$work/sketch/smartclock.ino"

# The clock's real TFT_eSPI, with its ESP8266 processor layer (SPI registers) swapped
# for tft_host/, which hands every byte to the simulated panel (panel.cpp).
rm -rf "$work/TFT_eSPI"
cp -r /libs/TFT_eSPI "$work/TFT_eSPI"
cp "$root/emulator/tft_host/TFT_eSPI_Host.h" "$root/emulator/tft_host/TFT_eSPI_Host.c" "$work/TFT_eSPI/Processors/"
sed -i 's|"Processors/TFT_eSPI_ESP8266.h"|"Processors/TFT_eSPI_Host.h"|' "$work/TFT_eSPI/TFT_eSPI.h"
sed -i 's|"Processors/TFT_eSPI_ESP8266.c"|"Processors/TFT_eSPI_Host.c"|' "$work/TFT_eSPI/TFT_eSPI.cpp"
grep -q TFT_eSPI_Host.h "$work/TFT_eSPI/TFT_eSPI.h" && grep -q TFT_eSPI_Host.c "$work/TFT_eSPI/TFT_eSPI.cpp" ||
  { echo "build.sh: could not point TFT_eSPI at tft_host/" >&2; exit 1; }

cd /esp8266/tests/host
# The core's mockWrite() polls again forever when a peer stops reading (poll() times
# out and the loop retries), which freezes the whole emulator. Give up on a timeout
# instead: the caller gets a short write, as a real socket would. Applied once.
mock=common/ClientContextSocket.cpp
if ! grep -q 'emulator: give up' "$mock"; then
  sed -i 's|^\(\s*\)int ret  = poll(&p, 1, timeout_ms);|&\n\1if (ret == 0) break;   // emulator: give up when the peer stops reading|' "$mock"
  grep -q 'emulator: give up' "$mock" || { echo "build.sh: could not patch $mock" >&2; exit 1; }
fi
# emu_build_flags.h: platformio.ini's build_flags, in every source. F_CPU also goes on
# the command line: the core's forced includes (common/mock.h) read it before that
# header. The core's objects do not track flags, so a new F_CPU rebuilds everything.
fcpu=$(sed -n 's/^#define F_CPU //p' "$work/generated/emu_build_flags.h")
if [ "$(cat "$work/f_cpu" 2>/dev/null)" != "$fcpu" ]; then
  make clean >/dev/null
  echo "$fcpu" > "$work/f_cpu"
fi
# -no-pie: TFT_eSPI keeps font addresses in 32-bit variables (pgm_read_dword), so the
# binary's data must sit below 4 GB, as it does on the chip.
make -j"$(nproc)" FORCE32=0 OPTZ=-O1 R=noexec USERLDFLAGS=-no-pie \
  ULIBDIRS=/libs/ArduinoJson:/libs/PubSubClient \
  USERCXXSOURCES="$root/emulator/emulator.cpp $root/emulator/fw_main.cpp $root/emulator/panel.cpp \
                  $root/src/faces.cpp $root/src/integrations.cpp $root/src/weather.cpp $root/src/ascii.cpp \
                  $work/TFT_eSPI/TFT_eSPI.cpp" \
  USERCSOURCES=/libs/TJpg_Decoder/src/tjpgd.c \
  USERCFLAGS="-UF_CPU -DF_CPU=$fcpu -DSMARTCLOCK_EMULATOR=1 -fno-pie -include $work/generated/emu_build_flags.h -I$root/src -I$root/include -I$work/generated \
              -I$work/TFT_eSPI -I/libs/TJpg_Decoder/src" \
  "$work/sketch/smartclock"
cp -L bin/smartclock/smartclock "$out"
echo "build.sh: $out"

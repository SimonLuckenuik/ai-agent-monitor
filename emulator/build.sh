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
# The clock's memory (memory.cpp) replaces the mock's fixed heap and stack numbers, its
# reset reason and its restart(). Taken out of a pristine copy at each build.
git -C /esp8266 checkout -q -- tests/host/common/MockEsp.cpp
python3 - common/MockEsp.cpp <<'PY'
import re, sys
path = sys.argv[1]
s = open(path).read()
for name in ("getFreeHeap", "getMaxFreeBlockSize", "getFreeContStack", "getResetReason",
             "restart", "getHeapStats"):
    s, n = re.subn(r"\n[^\n]*EspClass::%s\([^)]*\)\n\{.*?\n\}\n" % name, "\n", s, flags=re.S)
    if not n:
        sys.exit("build.sh: could not take EspClass::%s out of %s" % (name, path))
open(path, "w").write(s)
PY
# emu_build_flags.h: platformio.ini's build_flags, in every source. F_CPU also goes on
# the command line: the core's forced includes (common/mock.h) read it before that
# header. The core's objects do not track flags, so a new F_CPU rebuilds everything.
# 32-bit (the chip's sizes for pointers, String and JSON slots, so memory use matches
# it; needs g++-multilib) unless EMU_BITS=64.
fcpu=$(sed -n 's/^#define F_CPU //p' "$work/generated/emu_build_flags.h")
bits=${EMU_BITS:-32}
if [ "$bits" = 32 ]; then force32=1; bindir=bin32; else force32=0; bindir=bin; fi
if [ "$(cat "$work/f_cpu" 2>/dev/null)" != "$fcpu $bits" ]; then
  make clean >/dev/null
  echo "$fcpu $bits" > "$work/f_cpu"
fi
# -no-pie: TFT_eSPI keeps font addresses in 32-bit variables (pgm_read_dword), so the
# binary's data must sit below 4 GB, as it does on the chip.
make -j"$(nproc)" FORCE32=$force32 OPTZ=-O1 R=noexec USERLDFLAGS=-no-pie \
  ULIBDIRS=/libs/ArduinoJson:/libs/PubSubClient \
  USERCXXSOURCES="$root/emulator/emulator.cpp $root/emulator/fw_main.cpp $root/emulator/panel.cpp $root/emulator/memory.cpp \
                  $root/src/faces.cpp $root/src/integrations.cpp $root/src/weather.cpp $root/src/ascii.cpp $root/src/crash.cpp \
                  $work/TFT_eSPI/TFT_eSPI.cpp" \
  USERCSOURCES=/libs/TJpg_Decoder/src/tjpgd.c \
  USERCFLAGS="-g -finstrument-functions -finstrument-functions-exclude-file-list=/emulator/,/esp8266/,/usr/ -UF_CPU -DF_CPU=$fcpu -DSMARTCLOCK_EMULATOR=1 -fno-pie -include $work/generated/emu_build_flags.h -I$root/src -I$root/include -I$work/generated \
              -I$work/TFT_eSPI -I/libs/TJpg_Decoder/src" \
  "$work/sketch/smartclock"
cp -L $bindir/smartclock/smartclock "$out"
echo "build.sh: $out"

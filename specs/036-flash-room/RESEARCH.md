# 036 Research

Measured 2026-11-03 on the 2.1.0 build (`firmware.bin` 597,104 B, `.bin.gz` 433,260 B,
room 8,192 B).

## Where the flash goes

`scripts/footprint_files.py` and `footprint_sections.py` on the ELF: the SDK's WiFi and
lwIP blobs about 165 KB; `faces.cpp` 55 KB; the C library 37 KB; ArduinoJson 35 KB; the
core 28 KB; TFT_eSPI 27 KB; the settings page 27.5 KB (gzipped, so it counts twice);
`main.cpp` 26 KB; LittleFS 22 KB; `integrations.cpp` 15.6 KB; the weather icons 7.6 KB.

## Goal 1: zopfli, and is it backward compatible

- zlib level 9 (today): 434,253 B of deflate; zopfli (15 iterations): 425,422 B,
  8.8 KB smaller. The page: 27,531 B gzipped, 26,607 B with zopfli.
- Every clock unpacks an update with the bootloader in its flash: the core's eboot
  (`bootloaders/eboot/eboot.c`, `copy_raw`), uzlib with a 32 KB dictionary, reading the
  file 4 KB at a time and writing 4 KB at a time, built with `-DRUNTIME_BITS_TABLES`.
  Zopfli writes standard deflate blocks (stored, fixed and dynamic Huffman) with
  distances within 32 KB, which uzlib decodes like any other.
- Checked: uzlib built for the host with eboot's flags, driven as `copy_raw` does,
  unpacks both the zlib and the zopfli `.bin.gz` of 2.1.0 to a byte-identical
  `firmware.bin`. Every release since 1.4.0 used the same core (`platform =
  espressif8266`), and SmartClock-OSS's bootloader is the same eboot, so clocks on any
  of them can take a zopfli image. The firmware's own check of an upload
  (`include/firmware_validation.h`) reads the gzip header's "AM" field and the trailer,
  which stay as they are.

## Goal 2: what links sscanf

`configTime(const char* tz, ...)` sets `TZ` and calls `tzset`; newlib's `_tzset_unlocked_r`
(921 B) parses the rule with `sscanf` (`__ssvfscanf_r` 1,112 B, `nano-vfscanf` 2,445 B,
`nano-vfscanf_i` 912 B). Nothing else in the firmware calls `scanf`. The core already
avoids `tzset` for `configTime(offset, dst)` by filling `__gettzinfo()` itself
(`cores/esp8266/time.cpp`, "-7584 flash bytes").

## Goal 3: minifying the page

- Comments and indentation only: 27,480 → 25,905 B gzipped.
- `minify-html` 0.15 (Python): 27,168 B; its JavaScript minifier changed nothing.
- terser 5.36 on the script (50 KB): 18,434 → 15,815 B gzipped with mangling (local
  names only), 16,563 B compressing only. With CSS and markup trimmed and zopfli, the page
  should come to about 23.7 KB gzipped: about 3.8 KB less, counted twice.

## Not worth it

- Our own sin/cos: TFT_eSPI's arcs use `sinf`/`cosf` too, so the float range reduction
  (`__kernel_rem_pio2f`, about 3.5 KB) stays.
- The C++ unwinder (`unwind-dw2-fde`, 3.3 KB) comes with the toolchain's startup code.
- TFT_eSPI fonts 2, 4, 6 (about 9 KB): part of the face format.

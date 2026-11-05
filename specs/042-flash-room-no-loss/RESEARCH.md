# Research

## The parser copies in 2.2.0

`nm -S -C` on the 2.2.0 ELF, ArduinoJson symbols grouped by reader type and filter kind
(each group also holds the reader's own helpers, so groups overlap a little):

| Reader | Without filter | With filter |
|---|---|---|
| `fs::File` | 3,503 B | 810 B |
| `const String` | 2,094 B | 819 B |
| `WiFiClient` | 2,116 B | 862 B |
| `String` | 1,401 B | |
| `const char*` | 1,319 B | |
| **Total** | | **13,104 B** (with 144 B shared) |

Callers: settings import, messages, variables, faces, palettes and roles (`String`);
configuration, variables and faces from storage (`File`); the weather (`WiFiClient`); the
palettes cache and the Message face template (`const char*`).

`const char*` input is copied into the document by ArduinoJson 7, as `String` input is, so
passing `s.c_str()` changes nothing. `fs::File` and `WiFiClient` are both `Stream`s; one
`Stream&` reader reads them through the virtual `read()`, as the per-type reader did. A
filter of `true` keeps everything.

## HTTPClient

4,277 B (`nm`, symbols named `HTTPClient`), for two GETs to Open-Meteo (forecast and
geocoding), plain HTTP, HTTP/1.0. Replaced by about 30 lines on `WiFiClient`. Error codes
kept as HTTPClient's (-1 connection failed, -11 read timeout), since they appear in the
error text users see.

## LTO, tried and dropped

- On everything: does not link (the core's archives, built as LTO objects, leave
  `malloc`, `String::invalidate` and others unresolved).
- On our sources only (`build_src_flags -flto`, `-flto -Os` at link): LTO's output files
  (`*.ltrans.o`) miss the linker script's `*.cpp.o` rule and land in IRAM, which
  overflows; with a rule added for them it links, but `firmware.bin` grows from 593,328
  to 608,224 B (+14,896 B) and the update no longer fits. Cross-file inlining costs more
  than it saves at `-Os`.

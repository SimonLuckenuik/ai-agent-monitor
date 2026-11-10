// json-render specs (https://github.com/vercel-labs/json-render) compiled on the clock
// into a custom face (spec 047). In builds with SMARTCLOCK_JSONRENDER=1: the firmware
// (platformio.ini) and the emulator; not the starter.
#pragma once
#include <Arduino.h>

#ifndef SMARTCLOCK_JSONRENDER
#define SMARTCLOCK_JSONRENDER 0
#endif

// The spec as sent: the web server needs it twice in one piece while it reads it, which
// a larger one does not find in the clock's heap (measured: 6 KB arrive, 8 KB do not).
constexpr size_t JR_SOURCE_MAX = 6144;
constexpr size_t JR_FACE_MAX = 8192;      // the face it compiles to
constexpr int JR_ITEMS_MAX = 64;          // the face's items
constexpr int JR_ELEMENTS_MAX = 96;
constexpr int JR_DEPTH_MAX = 8;           // nesting: bounds the compile's stack (5.5 KB)

// True when body looks like a json-render spec ("root" and "elements").
bool jrIsSpec(const String& body);
// Compiles the spec in the file at path (LittleFS) into face JSON (out). Returns "" or
// the reason it cannot be drawn; warnings gets one line per thing drawn approximately or
// left out; seed gets the spec's "state" values as a variables object ("" when none).
String jrCompile(const char* path, String& out, String& warnings, String& seed);
// The deepest the compile's own stack has gone since boot, in bytes (of 5,632).
uint32_t jrStackUsed();
// The catalog (GET /api/catalog) and its prompt (GET /api/catalog/prompt).
void jrSendCatalog(bool prompt);

// json-render specs (https://github.com/vercel-labs/json-render) compiled on the clock
// into a custom face (spec 047). Only in builds with SMARTCLOCK_JSONRENDER=1 (for now the
// emulator): the device build compiles none of it.
#pragma once
#include <Arduino.h>

#ifndef SMARTCLOCK_JSONRENDER
#define SMARTCLOCK_JSONRENDER 0
#endif

constexpr size_t JR_SOURCE_MAX = 12288;   // the spec as sent
constexpr size_t JR_FACE_MAX = 8192;      // the face it compiles to
constexpr int JR_ITEMS_MAX = 64;          // the face's items
constexpr int JR_ELEMENTS_MAX = 96;
constexpr int JR_DEPTH_MAX = 12;          // nesting: bounds the compile's stack (4.5 KB)

// True when body looks like a json-render spec ("root" and "elements").
bool jrIsSpec(const String& body);
// Compiles the spec in the file at path (LittleFS) into face JSON (out). Returns "" or
// the reason it cannot be drawn; warnings gets one line per thing drawn approximately or
// left out; seed gets the spec's "state" values as a variables object ("" when none).
String jrCompile(const char* path, String& out, String& warnings, String& seed);
// The deepest the compile's own stack has gone since boot, in bytes (of 4,608).
uint32_t jrStackUsed();
// The catalog (GET /api/catalog) and its prompt (GET /api/catalog/prompt).
void jrSendCatalog(bool prompt);

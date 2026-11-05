// Every JSON parse in the firmware goes through these (spec 042): ArduinoJson compiles its
// parser once per input type and per filter kind (about 2 KB each), so text is always read
// as `const char*`, files and sockets as `Stream&`, and always through a filter (an
// allow-all one when nothing is filtered). Two copies of the parser instead of nine.
#pragma once
#include <Arduino.h>   // Stream, String
#include <ArduinoJson.h>

DeserializationError parseJson(JsonDocument& doc, const char* json, JsonVariantConst filter);
DeserializationError parseJson(JsonDocument& doc, Stream& in, JsonVariantConst filter);
DeserializationError parseJson(JsonDocument& doc, const char* json);
DeserializationError parseJson(JsonDocument& doc, Stream& in);
inline DeserializationError parseJson(JsonDocument& doc, const String& json) { return parseJson(doc, json.c_str()); }
inline DeserializationError parseJson(JsonDocument& doc, const String& json, JsonVariantConst filter) { return parseJson(doc, json.c_str(), filter); }

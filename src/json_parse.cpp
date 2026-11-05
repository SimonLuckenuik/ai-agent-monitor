#include "json_parse.h"

// `true` as a filter keeps everything.
static JsonVariantConst allowAll() {
  static JsonDocument all;
  if (all.isNull()) all.set(true);
  return all.as<JsonVariantConst>();
}

__attribute__((noinline)) DeserializationError parseJson(JsonDocument& doc, const char* json, JsonVariantConst filter) {
  return deserializeJson(doc, json, DeserializationOption::Filter(filter));
}
__attribute__((noinline)) DeserializationError parseJson(JsonDocument& doc, Stream& in, JsonVariantConst filter) {
  return deserializeJson(doc, in, DeserializationOption::Filter(filter));
}
DeserializationError parseJson(JsonDocument& doc, const char* json) { return parseJson(doc, json, allowAll()); }
DeserializationError parseJson(JsonDocument& doc, Stream& in) { return parseJson(doc, in, allowAll()); }

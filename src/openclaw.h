// The clock as an OpenClaw node (spec 041): one WebSocket to an OpenClaw Gateway, an
// Ed25519 identity, and the screen commands of integrations.cpp (openclawCommand).
// Off by default; settings ocon, ocurl, ocname, ocpair, ocforget.
#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>

void openclawLoop();                      // from loop(), while the WiFi is up
void openclawSettingsChanged();           // reconnect with the new settings
String openclawPair(const String& code);  // a setup code, or token:<t> / password:<p>; "" or why not
void openclawForget();                    // a new identity next time (pairs again from scratch)
void openclawStatus(JsonDocument& doc);   // /status: openclaw, openclawId, openclawHeap, openclawSignMs

// JSON written as text (frames and command results): o is an object or array begun
// with '{' or '['; a comma is added before each member but the first.
String jsonQuote(const String& s);
void jsonAddRaw(String& o, const __FlashStringHelper* key, const String& raw);   // a number, true, [...]
void jsonAdd(String& o, const __FlashStringHelper* key, const String& text);     // a string, quoted
// Reading: v's member at a dotted path ("payload.auth.deviceToken"), through one
// function rather than ArduinoJson's lookups inlined at every call.
JsonVariant jsonAt(JsonVariant v, const __FlashStringHelper* path);
const char* jsonText(JsonVariant v, const __FlashStringHelper* path);   // "" unless a string

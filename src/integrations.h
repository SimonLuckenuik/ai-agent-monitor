#pragma once
#include <Arduino.h>
#include "ascii.h"   // asciiFold()

// Optional integrations, each off by default (settings page, Integrations card):
//  - Custom faces: JSON-described faces over HTTP (/api/faces) and MQTT, with
//    placeholders ({time}, {temp}, ...) and variables pushed separately.
//  - Stock SD Pro photo API (/theme/*, /photo/*) so geekmagic-hacs can push
//    Home Assistant dashboards as photos. Those endpoints need no sign-in.
//  - MQTT with Home Assistant discovery.
// See docs/specifications/integrations-api.md.

void integrationsRoutes();          // registers the HTTP endpoints
void integrationsLoop();            // MQTT connection, state publishing
void integrationsSettingsChanged(); // MQTT settings may have changed: reconnect

// Custom faces, for faces.cpp and the rotation.
constexpr size_t FACE_JSON_MAX = 4096;
constexpr int FACES_MAX = 40;          // custom faces saved on the clock
uint32_t customFacesVersion();      // bumped whenever a face is saved or deleted
int customFaceCount();
String customFaceAt(int index);     // "" when out of range
int customFaceNames(String* out, int max);   // every custom face's name; returns how many
bool customFaceExists(const String& name);
String faceVar(const String& key);  // a variable pushed with /api/faces/vars, or ""
long faceVarAge(const String& key); // seconds since it was pushed, or -1
constexpr int HISTORY_LEN = 32;     // values kept per "$history" variable
// Copies up to max of its last values, oldest first; returns how many. pushes (if not
// null) counts every value ever pushed to it, so a change is visible.
int faceHistory(const String& key, float* out, int max, uint32_t* pushes);

// Agent messages (POST /api/message, MQTT <base>/message): kept in RAM until their
// ttl runs out, each one a turn of its own in the face rotation (FACE_MESSAGE).
constexpr int MESSAGE_MAX = 4;
bool messageActive(int slot);                       // set and not expired
String messageField(int slot, const String& key);   // id, title, text, icon, color, pct
bool messageUrgent(int slot);                       // warning or error: full backlight
int  messageShowing();                              // the slot the Message face shows
void messageShowSlot(int slot);
uint32_t messageVersion();                          // bumped whenever a message changes

bool integrationKeyOk();   // the request carries the custom faces' API key

// Photos: per-photo slideshow switch, shared with the stock-compatible API.
bool photoEnabled(const String& name);

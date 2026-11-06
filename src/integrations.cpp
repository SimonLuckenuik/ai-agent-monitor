#include "integrations.h"
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ArduinoJson.h>
#include "json_parse.h"
#include <LittleFS.h>
#ifndef SMARTCLOCK_MQTT
#define SMARTCLOCK_MQTT 1   // 0: a build without MQTT
#endif
#if SMARTCLOCK_MQTT
#include <PubSubClient.h>
#endif
#include "config.h"
#include "faces.h"
#include "weather.h"
#include "routes.h"
#include "crash.h"
#include "openclaw.h"

// ---- From main.cpp
extern ESP8266WebServer server;
extern bool storageReady, recoveryMode, brightnessDirty, backlightOff;
extern uint32_t faceSince, faceTurnSec;
bool requestAuthorized();
bool requireAuth();
bool sameSecret(const String& a, const String& b);
String deviceHost();
bool syncWeather();
const char* firmwareVersion();
void handlePhotoUpload();
void finishPhotoUpload();
void handlePhotoDelete();
void handlePhotoList();
String mqttStatus();

uint32_t mqttHeap = 0;   // /status mqttHeap: what MQTT took to connect (spec 025)

namespace {

const char* const FACE_NAMES[FACE_COUNT] = {"Classic", "Weather", "Photo", "Big date", "Simple", "Forecast", "Weather forecast - Day 4", "Big time", "Big weather", "Agents",
                                            "Weather today", "Weather forecast - Day 1", "Weather forecast - Day 2", "Weather forecast - Day 3"};
uint32_t facesVersion = 1;

bool saveConfig() {
  if (!storageReady || recoveryMode) return false;
  return cfg.save();
}

// ---------------------------------------------------------------- photos
String photoKey(const String& name) { return "|" + name + "|"; }

void setPhotoEnabled(const String& name, bool on) {
  String key = photoKey(name);
  bool off = cfg.photoOff.indexOf(key) >= 0;
  if (on && off) cfg.photoOff.replace(key, "|");
  else if (!on && !off) cfg.photoOff = (cfg.photoOff.length() ? cfg.photoOff : String("|")) + name + "|";
  if (cfg.photoOff == "|") cfg.photoOff = "";
}

// ---------------------------------------------------------------- custom faces
String faceName(const String& raw) {   // [a-z0-9_-], 1..20 chars, or ""
  if (raw.isEmpty() || raw.length() > 20) return "";
  for (unsigned i = 0; i < raw.length(); ++i) {
    char c = raw[i];
    if (!(isalnum((unsigned char)c) || c == '-' || c == '_')) return "";
  }
  String n = raw; n.toLowerCase();
  return n;
}

String facePath(const String& name) { return "/faces/" + name + ".json"; }

// Variables pushed for custom faces ({name}). Kept in RAM; "$persist" ones are also
// written to /vars.json (at most once a minute, to spare the flash) and read at boot.
struct Var { String key, value; uint32_t at = 0, until = 0; bool keep = false; };   // at, until: Unix time
constexpr int VARS_MAX = 40;
constexpr unsigned VAR_VALUE_MAX = 160;   // a short message; strings hold only their length
Var vars[VARS_MAX];
bool varsDirty = false, varsLoaded = false;
uint32_t varsSavedAt = 0;

// "$history": the last HISTORY_LEN numbers pushed to a few variables, for spark items.
struct History { String key; float v[HISTORY_LEN]; uint8_t n = 0, head = 0; uint32_t pushes = 0; };
constexpr int HISTORIES = 4;
History histories[HISTORIES];

uint32_t epochNow() { time_t t = time(nullptr); return t > 1600000000 ? (uint32_t)t : 0; }

bool varName(const String& key) {   // 1-15 of A-Z a-z 0-9 _
  if (key.isEmpty() || key.length() > 15) return false;
  for (unsigned i = 0; i < key.length(); ++i) if (!isalnum((unsigned char)key[i]) && key[i] != '_') return false;
  return true;
}

Var* findVar(const String& key) {
  for (Var& v : vars) {
    if (v.key != key) continue;
    if (v.until && epochNow() >= v.until) { if (v.keep) varsDirty = true; v = Var(); return nullptr; }   // expired
    return &v;
  }
  return nullptr;
}

struct VarOptions { uint32_t ttl = 0; bool keep = false, history = false, add = false; };

void addHistory(const String& key, float value) {
  History* h = nullptr;
  for (History& e : histories) if (e.key == key) h = &e;
  if (!h) {   // a free one, or the one pushed to longest ago
    for (History& e : histories) if (!h || e.key.isEmpty() || (h->key.length() && e.pushes < h->pushes)) h = &e;
    *h = History();
    h->key = key;
  }
  h->v[h->head] = value;
  h->head = (h->head + 1) % HISTORY_LEN;
  if (h->n < HISTORY_LEN) h->n++;
  h->pushes++;
}

Var* freeVar() {   // an empty slot, after clearing the expired ones if none is
  for (Var& e : vars) if (e.key.isEmpty()) return &e;
  uint32_t now = epochNow();
  Var* free = nullptr;
  for (Var& e : vars) {
    if (!e.until || now < e.until) continue;
    if (e.keep) varsDirty = true;
    e = Var();
    if (!free) free = &e;
  }
  return free;
}

// false: no room for a new variable (all VARS_MAX in use, none expired).
bool setVar(const String& key, JsonVariant raw, const VarOptions& o) {
  if (!varName(key)) return true;
  String value = asciiFold(raw.as<String>()).substring(0, VAR_VALUE_MAX);
  Var* v = findVar(key);
  if (!v && raw.isNull()) return true;   // deleting one that is not there
  if (!v) v = freeVar();
  if (!v) return false;
  if (o.add && v->key.length()) value = String(v->value.toInt() + value.toInt());
  if (raw.isNull()) { if (v->keep) varsDirty = true; *v = Var(); return true; }   // null deletes
  v->key = key; v->value = value;
  v->at = epochNow();
  v->until = o.ttl && v->at ? v->at + o.ttl : 0;
  if (o.keep || v->keep) varsDirty = true;
  v->keep = o.keep;
  if (o.history && (raw.is<float>() || o.add)) addHistory(key, o.add ? value.toInt() : raw.as<float>());
  return true;
}

// {"name": value, ...} with options "$ttl" (seconds), "$persist", "$history", "$add".
// Returns "" or the first bad name; *full: the first new name that found no room.
String applyVars(JsonObject d, String* full = nullptr) {
  VarOptions o;
  o.ttl = d["$ttl"] | 0;
  o.keep = d["$persist"] | false;
  o.history = d["$history"] | false;
  o.add = d["$add"] | false;
  String bad;
  for (JsonPair kv : d) {
    String key = kv.key().c_str();
    if (key[0] == '$') continue;
    if (!varName(key)) { if (bad.isEmpty()) bad = key; continue; }
    if (!setVar(key, kv.value(), o) && full && full->isEmpty()) *full = key;
  }
  return bad;
}

void saveVars() {
  if (!storageReady || recoveryMode) return;
  JsonDocument doc;
  for (Var& v : vars) {
    if (!v.keep || v.key.isEmpty()) continue;
    JsonObject o = doc[v.key].to<JsonObject>();
    o[F("v")] = v.value; o[F("at")] = v.at;
    if (v.until) o[F("until")] = v.until;
  }
  File file = LittleFS.open("/vars.json", "w");
  if (file) { serializeJson(doc, file); file.close(); }
  varsDirty = false;
  varsSavedAt = millis();
}

void loadVars() {
  File file = storageReady ? LittleFS.open("/vars.json", "r") : File();
  if (!file) return;
  JsonDocument doc;
  bool ok = !parseJson(doc, file);
  file.close();
  if (!ok) return;
  for (JsonPair kv : doc.as<JsonObject>()) {
    String key = kv.key().c_str();
    if (findVar(key)) continue;   // pushed since the boot: newer
    Var* slot = nullptr;
    for (Var& e : vars) if (!slot && e.key.isEmpty()) slot = &e;
    if (!slot) break;
    Var& v = *slot;
    v.key = key; v.value = kv.value()["v"] | "";
    v.at = kv.value()["at"] | 0; v.until = kv.value()["until"] | 0; v.keep = true;
  }
}

// ---------------------------------------------------------------- agent messages
// POST /api/message {"id","title","text","icon","color","pct","ttl","show"}: shown by
// the built-in Message face. No sign-in, by design: a message is the one thing any
// agent on the network may do. Kept in RAM; each expires after its ttl.
struct Message { String id, title, text, icon, color; int pct = -1; uint32_t until = 0; bool used = false; };
Message messages[MESSAGE_MAX];
int messageSlot = 0;   // the one the Message face shows
uint32_t messagesVersion = 1;
const char* const MESSAGE_ICONS[] = {"info", "success", "warning", "error", "waiting", "running", "bell", "done"};

bool isHexColor(const String& c) {
  if ((c.length() != 4 && c.length() != 7) || c[0] != '#') return false;
  for (unsigned i = 1; i < c.length(); ++i) if (!isxdigit((unsigned char)c[i])) return false;
  return true;
}

void showMessage(int slot) {
  messageSlot = slot;
  cfg.theme = FACE_MESSAGE;   // in RAM only: a message never becomes the saved face
  faceSince = millis();
  faceInvalidate();
}

// Stores (or replaces, by id) a message.
void storeMessage(JsonObjectConst m) {
  String id = asciiFold(m["id"] | "agent").substring(0, 15);
  int slot = -1, freeSlot = -1, soonest = 0;
  for (int i = 0; i < MESSAGE_MAX; ++i) {
    if (messageActive(i) && messages[i].id == id) slot = i;
    if (!messageActive(i) && freeSlot < 0) freeSlot = i;
    if ((int32_t)(messages[i].until - messages[soonest].until) < 0) soonest = i;
  }
  if (slot < 0) slot = freeSlot >= 0 ? freeSlot : soonest;   // full: the one ending first goes
  Message& msg = messages[slot];
  msg.id = id;
  msg.title = asciiFold(m["title"] | "").substring(0, 40);
  msg.text = asciiFold(m["text"] | "").substring(0, 160);
  msg.icon = "info";
  for (const char* name : MESSAGE_ICONS) if (!strcmp(m["icon"] | "", name)) msg.icon = name;
  msg.color = m["color"] | "";
  if (!isHexColor(msg.color)) msg.color = "";
  msg.pct = m["pct"].is<float>() ? constrain((int)lroundf(m["pct"].as<float>()), 0, 100) : -1;
  long ttl = constrain((long)(m["ttl"] | 600), 5L, 86400L);
  msg.until = millis() + (uint32_t)ttl * 1000;
  msg.used = true;
  messagesVersion++;
  if (m["show"] | false) showMessage(slot);
}

void clearMessages(const String& id) {
  for (Message& m : messages) if (id.isEmpty() || m.id == id) { m.used = false; m.until = millis(); }
  messagesVersion++;
}

void handleMessage() {   // POST: store; GET: list; DELETE ?id=: clear (all without id)
  if (server.method() == HTTP_DELETE) { clearMessages(server.arg("id")); server.send(200, PSTR("text/plain"), PSTR("OK")); return; }
  if (server.method() == HTTP_POST) {
    JsonDocument doc;
    if (parseJson(doc, server.arg("plain")) || !doc.is<JsonObject>()) {
      server.send(400, PSTR("text/plain"), PSTR("Expected a JSON object"));
      return;
    }
    storeMessage(doc.as<JsonObjectConst>());
    server.send(200, PSTR("text/plain"), PSTR("OK"));
    return;
  }
  JsonDocument doc;
  JsonArray list = doc[F("messages")].to<JsonArray>();
  for (int i = 0; i < MESSAGE_MAX; ++i) {
    if (!messageActive(i)) continue;
    JsonObject o = list.add<JsonObject>();
    o[F("id")] = messages[i].id; o[F("title")] = messages[i].title; o[F("text")] = messages[i].text;
    o[F("icon")] = messages[i].icon;
    if (messages[i].color.length()) o[F("color")] = messages[i].color;
    if (messages[i].pct >= 0) o[F("pct")] = messages[i].pct;
    o[F("ttl")] = (messages[i].until - millis()) / 1000;
    o[F("showing")] = cfg.theme == FACE_MESSAGE && messageSlot == i;
  }
  String out; serializeJson(doc, out);
  server.send(200, "application/json", out);
}

void showFace(const String& name) {
  cfg.theme = FACE_CUSTOM; cfg.customFace = name;
  faceSince = millis();
  faceTurnSec = 0;
  faceInvalidate();
}

// Saves (or, with an empty body, deletes) a face. Returns "" or the reason.
#define NO_MEMORY "Not enough memory"

String storeFace(const String& name, const String& body) {
  if (!storageReady || recoveryMode) return F("Storage unavailable");
  if (body.length() == 0 || body == "{}") {
    LittleFS.remove(facePath(name));
    facesVersion++;
    if (cfg.theme == FACE_CUSTOM && cfg.customFace == name) cfg.theme = FACE_CLASSIC;
    return "";
  }
  if (body.length() > FACE_JSON_MAX) return F("Face is larger than 4 KB");
  // Checked with only the items' types kept: about 1 KB instead of the whole face (spec 024).
  JsonDocument filter, doc;
  filter[F("items")][0]["type"] = true;
  filter[F("show")] = true;
  DeserializationError e = parseJson(doc, body, filter);
  if (e == DeserializationError::NoMemory) return F(NO_MEMORY);
  if (e || !doc[F("items")].is<JsonArray>()) return F("Expected JSON with an \"items\" array");
  if (doc[F("items")].size() > 32) return F("At most 32 items");
  if (!LittleFS.exists("/faces")) LittleFS.mkdir("/faces");
  if (!LittleFS.exists(facePath(name)) && customFaceCount() >= FACES_MAX) return F("At most 40 custom faces");
  File f = LittleFS.open(facePath(name), "w");
  if (!f) return F("Could not save the face");
  bool ok = f.print(body) == body.length();
  f.close();
  if (!ok) return F("Could not save the face");
  facesVersion++;
  if (doc[F("show")] | false) showFace(name);
  return "";
}

bool apiKeyOk() {
  if (cfg.apiKey.length() < 16) return false;
  String k = server.header("X-Api-Key");
  if (k.isEmpty()) k = server.arg("key");
  return sameSecret(k, cfg.apiKey);
}

// Faces API guard: feature on, and a signed-in session or the API key.
bool facesAllowed() {
  if (!cfg.facesApi) { server.send(404, PSTR("text/plain"), PSTR("Custom faces are turned off (Integrations)")); return false; }
  if (requestAuthorized() || apiKeyOk()) return true;
  server.send(401, PSTR("text/plain"), PSTR("Sign in or send the API key (X-Api-Key)"));
  return false;
}

void handleFaces() {   // GET: list; POST ?name=: save (empty body deletes)
  if (!facesAllowed()) return;
  if (server.method() == HTTP_POST) {
    String name = faceName(server.arg("name"));
    if (name.isEmpty()) { server.send(400, PSTR("text/plain"), PSTR("name: 1-20 of a-z 0-9 _ -")); return; }
    String err = storeFace(name, server.arg("plain"));
    if (err.length()) { server.send(err == F(NO_MEMORY) ? 507 : 400, "text/plain", err); return; }
    if (server.arg("show") == "1") showFace(name);
    server.send(200, PSTR("text/plain"), PSTR("OK"));
    return;
  }
  JsonDocument doc;
  JsonArray list = doc[F("faces")].to<JsonArray>();
  for (int i = 0, n = customFaceCount(); i < n; ++i) list.add(customFaceAt(i));
  if (cfg.theme == FACE_CUSTOM) doc[F("showing")] = cfg.customFace;
  JsonObject v = doc[F("vars")].to<JsonObject>();
  JsonArray keep = doc[F("persist")].to<JsonArray>();   // for backups (spec 032)
  for (const Var& var : vars) if (var.key.length() && findVar(var.key)) {
    v[var.key] = var.value;
    if (var.keep) keep.add(var.key);
  }
  String out; serializeJson(doc, out);
  server.send(200, "application/json", out);
}

void handleFaceGet() {   // the JSON of one face, for the editor
  if (!facesAllowed()) return;
  String name = faceName(server.arg("name"));
  File f = name.length() ? LittleFS.open(facePath(name), "r") : File();
  if (!f) { server.send(404, PSTR("text/plain"), PSTR("No such face")); return; }
  server.streamFile(f, "application/json");
  f.close();
}

void handleFaceDelete() {
  if (!facesAllowed()) return;
  String name = faceName(server.arg("name"));
  if (name.isEmpty() || !customFaceExists(name)) { server.send(404, PSTR("text/plain"), PSTR("No such face")); return; }
  storeFace(name, "");
  saveConfig();
  server.send(200, PSTR("text/plain"), PSTR("OK"));
}

void handleFaceShow() {
  if (!facesAllowed()) return;
  String name = faceName(server.arg("name"));
  if (name.isEmpty() || !customFaceExists(name)) { server.send(404, PSTR("text/plain"), PSTR("No such face")); return; }
  showFace(name);
  saveConfig();
  server.send(200, PSTR("text/plain"), PSTR("OK"));
}

void handleFaceVars() {   // POST {"name":value,...,"$ttl":s,...}: merged; null deletes
  if (!facesAllowed()) return;
  JsonDocument doc;
  if (parseJson(doc, server.arg("plain")) || !doc.is<JsonObject>()) {
    server.send(400, PSTR("text/plain"), PSTR("Expected a JSON object"));
    return;
  }
  String full;
  String bad = applyVars(doc.as<JsonObject>(), &full);
  if (bad.length()) { server.send(400, "text/plain", String(F("Bad variable name \"")) + bad + F("\": 1-15 of A-Z a-z 0-9 _")); return; }
  if (full.length()) {
    String msg = F("No room for variable \"");
    msg += full;
    msg += F("\": all 40 in use; delete some with null");
    server.send(507, "text/plain", msg);
    return;
  }
  server.send(200, PSTR("text/plain"), PSTR("OK"));
}

// ---------------------------------------------------------------- settings page data
void handleIntegrations() {   // GET: signed-in view; POST action=newkey: regenerate
  if (!requireAuth()) return;
  if (server.method() == HTTP_POST && server.arg("action") == "newkey") {
    char key[33];
    for (int i = 0; i < 4; ++i) snprintf(key + i * 8, 9, "%08x", (unsigned)ESP.random());
    cfg.apiKey = key;
    if (!saveConfig()) { server.send(503, PSTR("text/plain"), PSTR("Settings could not be saved")); return; }
  }
  JsonDocument doc;
  doc[F("apiKey")] = cfg.apiKey;
  doc[F("mqttStatus")] = mqttStatus();
  doc[F("mqttPassSet")] = cfg.mqttPass.length() > 0;
  doc[F("mqttTopic")] = cfg.mqttBase.length() ? cfg.mqttBase : deviceHost();
  String out; serializeJson(doc, out);
  server.send(200, "application/json", out);
}

// ---------------------------------------------------------------- stock SD Pro API
// Shapes follow the stock firmware, as geekmagic-hacs expects them. No sign-in: that
// integration cannot send one. Only photos, face choice and brightness are reachable.
bool compatAllowed() {   // also for a signed-in settings page (photo slideshow switches)
  if (!recoveryMode && (cfg.haCompat || requestAuthorized())) return true;
  server.send(404, PSTR("text/plain"), PSTR("Not found"));
  return false;
}

void handleThemeList() {
  if (!compatAllowed()) return;
  JsonDocument doc;
  JsonArray themes = doc[F("themes")].to<JsonArray>();
  for (int i = 0; i < FACE_COUNT; ++i) {
    if (!*FACE_NAMES[i]) continue;   // a retired face (none since 6 became Day 4)
    JsonObject t = themes.add<JsonObject>();
    t["id"] = i; t["name"] = FACE_NAMES[i]; t["enabled"] = (cfg.faceMask >> i & 1) != 0;
  }
  doc[F("interval")] = cfg.themeInterval;
  String out; serializeJson(doc, out);
  server.send(200, "application/json", out);
}

void handleThemeToggle() {
  if (!compatAllowed()) return;
  int id = server.arg("id").toInt();
  if (id < 0 || id >= FACE_COUNT) { server.send(400, PSTR("text/plain"), PSTR("Bad id")); return; }
  if (server.arg("state") == "1") cfg.faceMask |= 1 << id; else cfg.faceMask &= ~(1 << id);
  saveConfig();
  server.send(200, PSTR("text/plain"), PSTR("OK"));
}

void handleThemeInterval() {
  if (!compatAllowed()) return;
  cfg.themeInterval = constrain(server.arg("val").toInt(), 0, 3600);
  saveConfig();
  server.send(200, PSTR("text/plain"), PSTR("OK"));
}

void handlePhotoToggle() {
  if (!compatAllowed()) return;
  setPhotoEnabled(server.arg("name"), server.arg("state") == "1");
  saveConfig();
  facePhotosChanged();
  server.send(200, PSTR("text/plain"), PSTR("OK"));
}

void handlePhotoInterval() {
  if (!compatAllowed()) return;
  cfg.photoInterval = constrain(server.arg("val").toInt(), 1, 3600);
  saveConfig();
  server.send(200, PSTR("text/plain"), PSTR("OK"));
}

// ---------------------------------------------------------------- MQTT
#if SMARTCLOCK_MQTT
WiFiClient mqttNet;
PubSubClient mqtt(mqttNet);
uint32_t mqttRetryAt = 0, mqttStateAt = 0, mqttFacesSent = 0;
bool mqttReconnect = false;
String mqttLastError = "off";
// What connecting took (the client's buffer, the connection, discovery at its peak),
// for /status mqttHeap; the gate is that plus a margin (spec 025).
constexpr uint32_t MQTT_MIN_HEAP = 6144;
uint32_t mqttHeapBefore = 0, mqttHeapLow = 0;
void mqttHeapSample() { mqttHeapLow = std::min<uint32_t>(mqttHeapLow, ESP.getFreeHeap()); }

String base() { return cfg.mqttBase.length() ? cfg.mqttBase : deviceHost(); }
String nodeId() { char id[16]; snprintf(id, sizeof(id), "sc_%06x", (unsigned)(ESP.getChipId() & 0xffffff)); return id; }

String faceLabel() {
  if (cfg.theme == FACE_CUSTOM) return cfg.customFace;
  if (cfg.theme == FACE_MESSAGE) return "message";
  return cfg.theme >= 0 && cfg.theme < FACE_COUNT ? FACE_NAMES[cfg.theme] : FACE_NAMES[0];
}

// Discovery and state JSON are built as strings: every value is either fixed, a
// sanitised face name or a number, so nothing needs escaping, and seven
// JsonDocuments cost several KB of flash.
void publishConfig(const char* component, const char* object, const String& fields) {
  String id = nodeId(), b = base();
  String json = "{" + fields + F(",\"uniq_id\":\"") + id + "_" + object + F("\",\"avty_t\":\"") + b +
                F("/status\",\"dev\":{\"ids\":[\"") + id + F("\"],\"name\":\"AI Agent Monitor ") + deviceHost().substring(deviceHost().lastIndexOf('-') + 1) +
                F("\",\"mf\":\"SmartClock-OSS\",\"mdl\":\"SD Pro\",\"sw\":\"") + firmwareVersion() +
                F("\",\"cu\":\"http://") + WiFi.localIP().toString() + F("/\"}}");
  String topic = String(F("homeassistant/")) + component + "/" + id + "/" + object + F("/config");
  mqttHeapSample();   // the discovery message at its largest
  mqtt.publish(topic.c_str(), json.c_str(), true);
}

String q(const String& key, const String& value) { return "\"" + key + "\":\"" + value + "\""; }

void publishDiscovery() {
  String b = base();
  publishConfig("light", "backlight", q("name", "Backlight") + "," + q("schema", "json") +
                F(",\"brightness\":true,\"brightness_scale\":100,") + q("cmd_t", b + "/backlight/set") + "," +
                q("stat_t", b + "/backlight"));
  String options = "\"Classic\",\"Weather\",\"Photo\",\"Big date\",\"Simple\",\"Forecast\",\"Big time\",\"Big weather\",\"Agents\",\"Weather today\",\"Weather forecast - Day 1\",\"Weather forecast - Day 2\",\"Weather forecast - Day 3\"";
  if (cfg.facesApi) for (int i = 0, n = customFaceCount(); i < n; ++i) options += ",\"" + customFaceAt(i) + "\"";
  publishConfig("select", "face", q("name", "Face") + "," + q("cmd_t", b + "/face/set") + "," +
                q("stat_t", b + "/face") + ",\"options\":[" + options + "]");
  const char* const sensors[][5] = {   // object, name, device class, unit, template field
      {"temp", "Temperature", "temperature", "\xC2\xB0" "C", "temp"},
      {"hum", "Humidity", "humidity", "%", "hum"},
      {"cond", "Weather", "", "", "cond"},
      {"rssi", "WiFi signal", "signal_strength", "dBm", "rssi"}};
  for (auto& sn : sensors) {
    String f = q("name", sn[1]) + "," + q("stat_t", b + "/state") + "," + q("val_tpl", String("{{ value_json.") + sn[4] + " }}");
    if (*sn[2]) f += "," + q("dev_cla", sn[2]);
    if (*sn[3]) f += "," + q("unit_of_meas", sn[3]);
    if (!strcmp(sn[0], "rssi")) f += "," + q("ent_cat", "diagnostic");
    publishConfig("sensor", sn[0], f);
  }
  publishConfig("button", "sync", q("name", "Update weather") + "," + q("cmd_t", b + "/sync"));
  mqttFacesSent = customFacesVersion();
}

void publishState() {
  String b = base();
  String st = "{\"rssi\":" + String(WiFi.RSSI());
  if (wx.valid) st += ",\"temp\":" + String(lroundf(wx.temp)) + ",\"hum\":" + String(wx.humidity) + "," + q("cond", wx.desc);
  st += "}";
  mqtt.publish((b + "/state").c_str(), st.c_str(), true);
  String bl = "{" + q("state", backlightOff ? "OFF" : "ON") + ",\"brightness\":" + String(cfg.brightness) + "}";
  mqtt.publish((b + "/backlight").c_str(), bl.c_str(), true);
  mqtt.publish((b + "/face").c_str(), faceLabel().c_str(), true);
  mqttStateAt = millis();
}

void onMqtt(char* topicRaw, byte* payload, unsigned int len) {
  String topic = topicRaw, b = base();
  String body; body.reserve(len);
  for (unsigned i = 0; i < len; ++i) body += (char)payload[i];
  if (topic == b + "/backlight/set") {
    JsonDocument d;
    if (parseJson(d, body)) return;
    if (d[F("state")].is<const char*>()) backlightOff = d[F("state")] == "OFF";
    if (d[F("brightness")].is<int>()) { cfg.brightness = constrain(d[F("brightness")].as<int>(), 0, 100); backlightOff = false; saveConfig(); }
    brightnessDirty = true;
  } else if (topic == b + "/face/set") {
    for (int i = 0; i < FACE_COUNT; ++i)
      if (*FACE_NAMES[i] && body == FACE_NAMES[i]) { cfg.theme = i; faceSince = millis(); faceInvalidate(); saveConfig(); }
    if (cfg.facesApi && customFaceExists(body)) { showFace(body); saveConfig(); }
  } else if (topic == b + "/sync") {
    syncWeather();
  } else if (topic == b + "/message") {
    JsonDocument d;
    if (!parseJson(d, body) && d.is<JsonObject>()) storeMessage(d.as<JsonObjectConst>());
  } else if (topic == b + "/message/clear") {
    clearMessages(body);
  } else if (cfg.facesApi && topic.startsWith(b + "/custom/")) {
    String name = faceName(topic.substring(b.length() + 8));
    if (name.length()) storeFace(name, body);
  } else if (cfg.facesApi && topic == b + "/vars") {
    JsonDocument d;
    if (!parseJson(d, body) && d.is<JsonObject>()) applyVars(d.as<JsonObject>());
  }
  publishState();
}

#endif  // SMARTCLOCK_MQTT

}  // namespace

// ---------------------------------------------------------------- OpenClaw node (spec 041)
// The node's commands, run on the clock itself (src/openclaw.cpp carries them). Screen
// output and read-only status only. Parameters and results are the bridge's
// (integrations/openclaw/lib/commands.mjs), so agents can use either. Results are
// written as JSON text (jsonAdd): smaller here than building documents.
namespace {

const char OC_ICONS[] PROGMEM = ",info,success,warning,error,waiting,running,bell,done,";
// Built-in faces by name, in id order (face ids are an API); "tomorrow" is day-1.
const char OC_FACES[] PROGMEM = ",classic,weather,photo,big-date,simple,forecast,day-4,big-time,big-weather,agents,today,day-1,day-2,day-3,";

// Where a name sits in a ",a,b,c," list (in flash): its index, or -1.
int listIndex(const char* listP, const String& name) {
  String list = FPSTR(listP);
  int at = name.length() ? list.indexOf("," + name + ",") : -1;
  if (at < 0) return -1;
  int n = 0;
  for (int i = 1; i <= at; ++i) if (list[i] == ',') n++;
  return n;
}

// Reads and checks a command's parameters; the first problem is kept.
struct OcParams {
  JsonObject p;
  String err;
  void fail(const __FlashStringHelper* key, const __FlashStringHelper* why) {
    if (err.isEmpty()) err = String(F("INVALID_PARAMS ")) + key + why;
  }
  void only(const char* keysP) {   // ",a,b,": nothing else is taken
    for (JsonPair kv : p)
      if (listIndex(keysP, kv.key().c_str()) < 0 && err.isEmpty()) err = String(F("INVALID_PARAMS unknown parameter ")) + kv.key().c_str();
  }
  JsonVariant get(const __FlashStringHelper* k) { return jsonAt(p, k); }
  bool has(const __FlashStringHelper* k) { return !get(k).isNull(); }
  String str(const __FlashStringHelper* k, unsigned max, const char* def = "") {
    JsonVariant v = get(k);
    if (v.isNull()) return def;
    if (!v.is<const char*>() || strlen(v.as<const char*>()) > max) { fail(k, F(": text too long or not text")); return def; }
    return v.as<const char*>();
  }
  long num(const __FlashStringHelper* k, long lo, long hi, long def) {
    JsonVariant v = get(k);
    if (v.isNull()) return def;
    if (!v.is<long>() || v.as<long>() < lo || v.as<long>() > hi) { fail(k, F(": whole number out of range")); return def; }
    return v.as<long>();
  }
  bool flag(const __FlashStringHelper* k, bool def) {
    JsonVariant v = get(k);
    if (v.isNull()) return def;
    if (!v.is<bool>()) { fail(k, F(": true or false")); return def; }
    return v.as<bool>();
  }
};

String clip(const String& s, unsigned n) { return s.length() > n ? s.substring(0, n - 1) + "~" : s; }

String ocFaceLabel() {
  if (cfg.theme == FACE_CUSTOM) return cfg.customFace;
  if (cfg.theme == FACE_MESSAGE) return F("message");
  return FACE_NAMES[cfg.theme >= 0 && cfg.theme < FACE_COUNT ? cfg.theme : 0];
}

const char* jbool(bool b) { return b ? "true" : "false"; }

}  // namespace

// The commands a node declares, as JSON strings (the connect frame lists them as they are).
extern const char OPENCLAW_COMMANDS[] PROGMEM;
const char OPENCLAW_COMMANDS[] PROGMEM =
    "\"display.show\",\"display.clear\",\"display.status\",\"display.brightness\",\"face.set\",\"device.info\","
    "\"device.status\",\"clock.face.show\",\"clock.faces.list\",\"clock.vars.set\",\"clock.help\"";

// params: the request's object (changed in place); r: the result, "{" so far, the
// caller closes it. Returns "" or "<CODE> <message>".
String openclawCommand(const String& cmd, JsonObject params, String& r) {
  OcParams P{params, ""};
  // The command's place in OPENCLAW_COMMANDS: 0 display.show, 1 display.clear, 2 display.status,
  // 3 display.brightness, 4 face.set, 5 device.info, 6 device.status, 7 clock.face.show,
  // 8 clock.faces.list, 9 clock.vars.set, 10 clock.help.
  String all = FPSTR(OPENCLAW_COMMANDS);
  int c = cmd.length() ? all.indexOf("\"" + cmd + "\"") : -1;
  if (c > 0) { int n = 0; for (int i = 0; i < c; ++i) n += all[i] == ','; c = n; }
  all = "";
  if (c == 0 || c == 4) {
    bool mood = c == 4;
    String title, text = P.str(F("text"), 512), icon, color, id;
    bool show = true;
    int k = 0;
    if (mood) {
      // Moni, the Message face's robot, has three poses (spec 039): hops for done, waves
      // while waiting, slumps for an error. neutral and sleepy take the mood away.
      P.only(PSTR(",mood,text,"));
      k = listIndex(PSTR(",happy,excited,thinking,sad,neutral,sleepy,"), title = P.str(F("mood"), 16));
      if (k < 0) P.fail(F("mood"), F(": neutral happy excited thinking sad sleepy"));
      jsonAdd(r, F("mood"), title);
      id = F("mood");
      title = k < 2 ? F("Done") : k == 2 ? F("Waiting") : F("Problem");
      icon = k < 2 ? F("done") : k == 2 ? F("waiting") : F("error");
    } else {
      P.only(PSTR(",heading,text,icon,color,pct,ttl,id,show,"));
      title = P.has(F("heading")) ? P.str(F("heading"), 64) : String(F("OpenClaw"));
      if (!P.has(F("text"))) P.fail(F("text"), F(" is required"));
      icon = P.str(F("icon"), 16); color = P.str(F("color"), 7); id = P.has(F("id")) ? P.str(F("id"), 15) : String(F("openclaw"));
      if (icon.length() && listIndex(OC_ICONS, icon) < 0) P.fail(F("icon"), F(": info success warning error waiting running bell done"));
      if (color.length() && !isHexColor(color)) P.fail(F("color"), F(": #rgb or #rrggbb"));
      for (unsigned i = 0; i < id.length(); ++i)
        if (!isalnum((unsigned char)id[i]) && id[i] != '_' && id[i] != '.' && id[i] != '-') P.fail(F("id"), F(": letters, digits, _ . -"));
      P.num(F("pct"), 0, 100, 0); P.num(F("ttl"), 5, 86400, 0);
      show = P.flag(F("show"), true);
    }
    if (P.err.length()) return P.err;
    if (mood) {
      jsonAddRaw(r, F("moni"), jbool(k < 4));
      if (k >= 4) { clearMessages(id); return ""; }
    } else {
      jsonAdd(r, F("id"), id);
      jsonAddRaw(r, F("shown"), jbool(show));
      jsonAddRaw(r, F("clipped"), jbool(title.length() > 40 || text.length() > 160));
    }
    // The message is the request's own object, the fields storeMessage() reads set over it
    // (heading and mood stay: it ignores them).
    String fields[4] = {id, clip(title, 40), clip(text, 160), icon};
    const __FlashStringHelper* names[4] = {F("id"), F("title"), F("text"), F("icon")};
    for (int i = 0; i < 4; ++i) params[String(names[i])] = fields[i];
    params[String(F("show"))] = show;
    storeMessage(params);
  } else if (c == 1) {
    P.only(PSTR(",id,all,"));
    String id = P.str(F("id"), 15);
    bool all = P.flag(F("all"), false);
    if (P.err.length()) return P.err;
    if (all) id = "";
    clearMessages(id.length() || all ? id : String(F("openclaw")));
    if (!all && id.isEmpty()) clearMessages(F("mood"));
    jsonAddRaw(r, F("cleared"), all ? String(F("\"all\"")) : id.length() ? "[" + jsonQuote(id) + "]" : String(F("[\"openclaw\",\"mood\"]")));
  } else if (c == 2) {
    P.only(PSTR(",,"));
    if (P.err.length()) return P.err;
    jsonAdd(r, F("face"), ocFaceLabel());
    String list = "[";
    for (int i = 0; i < MESSAGE_MAX; ++i) {
      if (!messageActive(i)) continue;
      String o = "{";
      jsonAdd(o, F("id"), messages[i].id); jsonAdd(o, F("title"), messages[i].title);
      jsonAdd(o, F("text"), messages[i].text); jsonAdd(o, F("icon"), messages[i].icon);
      jsonAddRaw(o, F("ttl"), String((messages[i].until - millis()) / 1000));
      jsonAddRaw(o, F("showing"), jbool(cfg.theme == FACE_MESSAGE && messageSlot == i));
      if (list.length() > 1) list += ',';
      list += o + '}';
    }
    jsonAddRaw(r, F("messages"), list + ']');
  } else if (c == 3) {
    P.only(PSTR(",percent,"));
    if (!P.has(F("percent"))) P.fail(F("percent"), F(" is required"));
    long pc = P.num(F("percent"), 0, 100, 0);
    if (P.err.length()) return P.err;
    cfg.brightness = pc; backlightOff = false; brightnessDirty = true;
    saveConfig();
    jsonAddRaw(r, F("percent"), String(pc));
  } else if (c == 5 || c == 6) {
    P.only(PSTR(",,"));
    if (P.err.length()) return P.err;
    if (c == 5) {
      jsonAdd(r, F("model"), F("SD PRO")); jsonAdd(r, F("platform"), F("esp8266"));
      jsonAdd(r, F("firmware"), firmwareVersion()); jsonAdd(r, F("host"), deviceHost());
      return "";
    }
    extern uint32_t heapLow;
    jsonAddRaw(r, F("uptimeS"), String(millis() / 1000)); jsonAddRaw(r, F("heap"), String(ESP.getFreeHeap()));
    jsonAddRaw(r, F("heapLow"), String(heapLow)); jsonAdd(r, F("ip"), WiFi.localIP().toString());
    jsonAddRaw(r, F("rssi"), String(WiFi.RSSI()));
    JsonDocument c;
    crashStatus(c);   // crashes, crashLoop, safeMode, lastCrash
    String s;
    serializeJson(c, s);
    if (s[0] == '{' && s.length() > 2) r += ',' + s.substring(1, s.length() - 1);   // "null" without crashes
  } else if (c == 7) {
    P.only(PSTR(",name,builtin,"));
    String name = faceName(P.str(F("name"), 20));
    JsonVariant b = P.get(F("builtin"));
    int id = b.is<int>() ? b.as<int>() : !b.is<const char*>() ? -1
           : strcmp_P(b.as<const char*>(), PSTR("tomorrow")) ? listIndex(OC_FACES, b.as<const char*>()) : 11;
    if (P.has(F("name")) == !b.isNull()) P.fail(F("name"), F(" or builtin: one of them"));
    else if (P.has(F("name")) && !customFaceExists(name)) P.fail(F("name"), F(": no such custom face"));
    else if (!b.isNull() && (id < 0 || id >= FACE_COUNT)) P.fail(F("builtin"), F(": unknown face"));
    if (P.err.length()) return P.err;
    if (name.length()) { showFace(name); jsonAdd(r, F("face"), name); }
    else { cfg.theme = id; faceSince = millis(); faceTurnSec = 0; faceInvalidate(); jsonAddRaw(r, F("face"), String(id)); }
    saveConfig();
  } else if (c == 8) {
    P.only(PSTR(",,"));
    if (P.err.length()) return P.err;
    String list = "[";
    for (int i = 0, n = customFaceCount(); i < n; ++i) { if (i) list += ','; list += jsonQuote(customFaceAt(i)); }
    jsonAddRaw(r, F("faces"), list + ']');
    jsonAdd(r, F("showing"), ocFaceLabel());
    list = "{";
    for (const Var& e : vars) {
      if (e.key.isEmpty() || !findVar(e.key)) continue;
      if (list.length() > 1) list += ',';
      list += jsonQuote(e.key) + ':' + jsonQuote(e.value);
    }
    jsonAddRaw(r, F("vars"), list + '}');
  } else if (c == 9) {
    P.only(PSTR(",vars,ttl,history,add,persist,"));
    JsonObject in = P.get(F("vars"));
    if (in.isNull() || !in.size()) P.fail(F("vars"), F(": an object of name: value"));
    long ttl = P.num(F("ttl"), 1, 86400, 0);
    String set = "[";
    for (JsonPair kv : in) {
      JsonVariant val = kv.value();
      if (!varName(kv.key().c_str()) || (!val.isNull() && !val.is<float>() && !(val.is<const char*>() && strlen(val.as<const char*>()) <= VAR_VALUE_MAX)))
        P.fail(F("vars"), F(": bad name or value"));
      if (set.length() > 1) set += ',';
      set += jsonQuote(kv.key().c_str());
    }
    if (ttl) in[String(F("$ttl"))] = ttl;
    for (const __FlashStringHelper* o : {F("history"), F("add"), F("persist")})
      if (P.flag(o, false)) in["$" + String(o)] = true;
    if (P.err.length()) return P.err;
    String full;
    applyVars(in, &full);
    if (full.length()) return String(F("UNAVAILABLE no room for ")) + full + F(" (40 variables at most)");
    jsonAddRaw(r, F("set"), set + ']');
  } else if (c == 10) {
    // Where the instructions are (spec 045): the OpenClaw skill of this release on GitHub,
    // and its SKILL.md as plain text an agent can fetch. A -dev version has no tag: main.
    P.only(PSTR(",,"));
    if (P.err.length()) return P.err;
    const char* v = firmwareVersion();
    char ref[16] = "main";
    if (!strchr(v, '-')) snprintf_P(ref, sizeof(ref), PSTR("v%s"), v);
    std::unique_ptr<char[]> b(new (std::nothrow) char[400]);
    if (!b) return F("UNAVAILABLE out of memory");
    // r is "{" so far. The repository and the skill's folder appear twice.
#define OC_REPO "SimonLuckenuik/ai-agent-monitor/"
#define OC_SKILL "/integrations/openclaw/skill/ai-agent-monitor"
    snprintf_P(b.get(), 400, PSTR("\"firmware\":\"%s\",\"skill\":\"https://github.com/" OC_REPO "tree/%s" OC_SKILL
        "\",\"guide\":\"https://raw.githubusercontent.com/" OC_REPO "%s" OC_SKILL "/SKILL.md\","
        "\"install\":\"openclaw skills install <skill folder> --global\""), v, ref, ref);
    r += b.get();
  } else {
    return String(F("UNKNOWN_COMMAND ")) + cmd;
  }
  return "";
}

// The API key alone (the crash log's API accepts it as well as a signed-in session).
bool integrationKeyOk() { return apiKeyOk(); }

// ---------------------------------------------------------------- public
uint32_t customFacesVersion() { return facesVersion; }

int customFaceCount() {
  if (!storageReady) return 0;
  int n = 0;
  Dir dir = LittleFS.openDir("/faces");
  while (dir.next()) if (dir.isFile() && dir.fileName().endsWith(".json")) n++;
  return n;
}

String customFaceAt(int index) {
  if (!storageReady) return "";
  Dir dir = LittleFS.openDir("/faces");
  int n = 0;
  while (dir.next()) {
    if (!dir.isFile() || !dir.fileName().endsWith(".json")) continue;
    if (n++ == index) { String f = dir.fileName(); return f.substring(0, f.length() - 5); }
  }
  return "";
}

int customFaceNames(String* out, int max) {
  if (!storageReady) return 0;
  Dir dir = LittleFS.openDir("/faces");
  int n = 0;
  while (n < max && dir.next()) {
    if (!dir.isFile() || !dir.fileName().endsWith(".json")) continue;
    String f = dir.fileName();
    out[n++] = f.substring(0, f.length() - 5);
  }
  return n;
}

bool customFaceExists(const String& name) {
  return storageReady && name.length() && LittleFS.exists(facePath(name));
}

String faceVar(const String& key) {
  const Var* v = findVar(key);
  return v ? v->value : String();
}

long faceVarAge(const String& key) {
  const Var* v = findVar(key);
  uint32_t now = epochNow();
  return v && v->at && now ? (long)(now - v->at) : -1;
}

int faceHistory(const String& key, float* out, int max, uint32_t* pushes) {
  for (const History& h : histories) {
    if (h.key != key || key.isEmpty()) continue;
    if (pushes) *pushes = h.pushes;
    int n = std::min<int>(h.n, max);
    for (int i = 0; i < n; ++i) out[i] = h.v[(h.head + HISTORY_LEN - n + i) % HISTORY_LEN];
    return n;
  }
  return 0;
}

bool photoEnabled(const String& name) { return cfg.photoOff.indexOf(photoKey(name)) < 0; }

bool messageActive(int slot) {
  return slot >= 0 && slot < MESSAGE_MAX && messages[slot].used && (int32_t)(messages[slot].until - millis()) > 0;
}
String messageField(int slot, const String& key) {
  if (!messageActive(slot)) return "";
  const Message& m = messages[slot];
  if (key == F("title")) return m.title;
  if (key == F("text")) return m.text;
  if (key == F("icon")) return m.icon;
  if (key == F("color")) return m.color;
  if (key == F("pct")) return String(m.pct);
  if (key == F("id")) return m.id;
  if (key == F("ttl")) return String((m.until - millis()) / 1000);   // seconds left
  return "";
}
bool messageUrgent(int slot) { return messageActive(slot) && (messages[slot].icon == "warning" || messages[slot].icon == "error"); }
int messageShowing() { return messageSlot; }
void messageShowSlot(int slot) { messageSlot = slot; }
uint32_t messageVersion() { return messagesVersion; }

String mqttStatus() {
#if SMARTCLOCK_MQTT
  if (!cfg.mqttOn) return "off";
  return mqtt.connected() ? "connected" : mqttLastError;
#else
  return "not in this build";
#endif
}

void integrationsSettingsChanged() {
  openclawSettingsChanged();   // its settings go through changed() too
#if SMARTCLOCK_MQTT
  mqttReconnect = true;
#endif
}

// The integrations' routes, in flash (routes.h). Stock SD Pro shapes (/theme, /photo)
// only answer while "Home Assistant dashboards" is on.
const Route ROUTES[] PROGMEM = {
  {"/api/faces",        HTTP_ANY,  handleFaces, nullptr},
  {"/api/faces/get",    HTTP_ANY,  handleFaceGet, nullptr},
  {"/api/faces/delete", HTTP_POST, handleFaceDelete, nullptr},
  {"/api/faces/show",   HTTP_POST, handleFaceShow, nullptr},
  {"/api/faces/vars",   HTTP_POST, handleFaceVars, nullptr},
  {"/api/integrations", HTTP_ANY,  handleIntegrations, nullptr},
  {"/api/message",      HTTP_ANY,  handleMessage, nullptr},
  {"/theme/list",       HTTP_ANY,  handleThemeList, nullptr},
  {"/theme/toggle",     HTTP_ANY,  handleThemeToggle, nullptr},
  {"/theme/interval",   HTTP_ANY,  handleThemeInterval, nullptr},
  {"/photo/list",       HTTP_ANY,  []{ if (compatAllowed()) handlePhotoList(); }, nullptr},
  {"/photo/upload",     HTTP_POST, finishPhotoUpload, handlePhotoUpload},
  {"/photo/toggle",     HTTP_ANY,  handlePhotoToggle, nullptr},
  {"/photo/delete",     HTTP_ANY,  []{ if (compatAllowed()) handlePhotoDelete(); }, nullptr},
  {"/photo/interval",   HTTP_ANY,  handlePhotoInterval, nullptr},
};

void integrationsRoutes() {
  // Before the web server reads a face's body (up to 4 KB, then copied once): the custom
  // face on screen gives its document back, so the upload fits beside it (spec 024).
  server.addHook([](const String& method, const String& url, WiFiClient*, ESP8266WebServer::ContentTypeFunction) {
    if (method == F("POST") && url == F("/api/faces")) customRelease();
    return ESP8266WebServer::CLIENT_REQUEST_CAN_CONTINUE;
  });
  static RouteTable routes(ROUTES, sizeof(ROUTES) / sizeof(ROUTES[0]));
  server.addHandler(&routes);
}

void integrationsLoop() {
  if (!varsLoaded && storageReady && !recoveryMode) { varsLoaded = true; loadVars(); }   // storage mounts after the routes
  if (varsDirty && (!varsSavedAt || millis() - varsSavedAt > 60000)) saveVars();   // at most once a minute
#if SMARTCLOCK_MQTT
  if (mqttReconnect || !cfg.mqttOn || WiFi.status() != WL_CONNECTED) {
    if (mqtt.connected()) { mqtt.publish((base() + "/status").c_str(), "offline", true); mqtt.disconnect(); }
    if (mqttReconnect) { mqttReconnect = false; mqttRetryAt = 0; }
    if (!cfg.mqttOn) mqttLastError = "off";
    return;
  }
  if (!mqtt.connected()) {
    if ((int32_t)(millis() - mqttRetryAt) < 0) return;
    mqttRetryAt = millis() + 30000;   // a missing broker must not stall the clock often
    if (cfg.mqttHost.isEmpty()) { mqttLastError = "no broker set"; return; }
    if (ESP.getFreeHeap() < MQTT_MIN_HEAP) { mqttLastError = F("waiting for memory"); return; }
    mqttHeapBefore = mqttHeapLow = ESP.getFreeHeap();
    mqtt.setServer(cfg.mqttHost.c_str(), cfg.mqttPort);
    mqtt.setBufferSize(1536);
    mqtt.setCallback(onMqtt);
    mqttNet.setTimeout(3000);
    String will = base() + "/status";
    bool ok = mqtt.connect(nodeId().c_str(), cfg.mqttUser.length() ? cfg.mqttUser.c_str() : nullptr,
                           cfg.mqttPass.length() ? cfg.mqttPass.c_str() : nullptr, will.c_str(), 0, true, "offline");
    if (!ok) { mqttLastError = "cannot connect (" + String(mqtt.state()) + ")"; return; }
    String b = base();
    mqtt.publish(will.c_str(), "online", true);
    for (const char* t : {"/backlight/set", "/face/set", "/sync", "/custom/+", "/vars", "/message", "/message/clear"}) mqtt.subscribe((b + t).c_str());
    publishDiscovery();
    publishState();
    mqttHeapSample();
    mqttHeap = mqttHeapBefore - mqttHeapLow;
  }
  mqtt.loop();
  if (mqttFacesSent != customFacesVersion()) publishDiscovery();   // face list changed
  if (millis() - mqttStateAt > 60000) publishState();
#endif
}

#if SMARTCLOCK_EMULATOR
// ---- The emulator's Scenarios panel (emulator.cpp, port 8080)
// A push as if it came from the MQTT broker: topic is what follows the base topic
// ("vars", "message", "custom/<name>", "face/set", ...). Returns "" or why it was ignored.
String emuMqtt(const String& topic, const String& body) {
#if SMARTCLOCK_MQTT
  if (!cfg.facesApi && (topic == "vars" || topic.startsWith("custom/")))
    return F("Custom faces are turned off (Integrations)");
  String full = base() + "/" + topic;
  if (full.length() > 127) return F("Topic too long");
  char t[128];
  strlcpy(t, full.c_str(), sizeof(t));
  onMqtt(t, (byte*)body.c_str(), body.length());
  return "";
#else
  (void)topic; (void)body;
  return F("This build has no MQTT");
#endif
}

// {"facesApi":b,"showing":face,"faces":[...],"vars":{name:{"value","age","ttl","persist","history":[...]}}}
String emuVarsJson() {
  JsonDocument doc;
  doc[F("facesApi")] = cfg.facesApi;
  doc[F("showing")] = cfg.theme == FACE_CUSTOM ? cfg.customFace : cfg.theme == FACE_MESSAGE ? String("message")
                 : String(FACE_NAMES[cfg.theme >= 0 && cfg.theme < FACE_COUNT ? cfg.theme : 0]);
  JsonArray faces = doc[F("faces")].to<JsonArray>();
  for (int i = 0, n = customFaceCount(); i < n; ++i) faces.add(customFaceAt(i));
  JsonObject list = doc[F("vars")].to<JsonObject>();
  uint32_t now = epochNow();
  for (const Var& v : vars) {
    if (v.key.isEmpty() || !findVar(v.key)) continue;
    JsonObject o = list[v.key].to<JsonObject>();
    o[F("value")] = v.value;
    o[F("age")] = v.at && now >= v.at ? (long)(now - v.at) : -1;
    if (v.until) o[F("ttl")] = (long)(v.until - now);
    if (v.keep) o[F("persist")] = true;
    float h[HISTORY_LEN];
    int n = faceHistory(v.key, h, HISTORY_LEN, nullptr);
    if (n) { JsonArray a = o[F("history")].to<JsonArray>(); for (int i = 0; i < n; ++i) a.add(h[i]); }
  }
  String out; serializeJson(doc, out);
  return out;
}
#endif

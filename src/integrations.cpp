#include "integrations.h"
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ArduinoJson.h>
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

namespace {

const char* const FACE_NAMES[FACE_COUNT] = {"Classic", "Weather", "Photo", "Big date", "Simple", "Forecast", "", "Big time", "Big weather", "Agents",
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

void setVar(const String& key, JsonVariant raw, const VarOptions& o) {
  if (!varName(key)) return;
  String value = asciiFold(raw.as<String>()).substring(0, VAR_VALUE_MAX);
  Var* v = findVar(key);
  if (!v) for (Var& e : vars) if (e.key.isEmpty()) { v = &e; break; }
  if (!v) return;
  if (o.add && v->key.length()) value = String(v->value.toInt() + value.toInt());
  if (raw.isNull()) { if (v->keep) varsDirty = true; *v = Var(); return; }   // null deletes
  v->key = key; v->value = value;
  v->at = epochNow();
  v->until = o.ttl && v->at ? v->at + o.ttl : 0;
  if (o.keep || v->keep) varsDirty = true;
  v->keep = o.keep;
  if (o.history && (raw.is<float>() || o.add)) addHistory(key, o.add ? value.toInt() : raw.as<float>());
}

// {"name": value, ...} with options "$ttl" (seconds), "$persist", "$history", "$add".
// Returns "" or the first bad name.
String applyVars(JsonObject d) {
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
    setVar(key, kv.value(), o);
  }
  return bad;
}

void saveVars() {
  if (!storageReady || recoveryMode) return;
  JsonDocument doc;
  for (Var& v : vars) {
    if (!v.keep || v.key.isEmpty()) continue;
    JsonObject o = doc[v.key].to<JsonObject>();
    o["v"] = v.value; o["at"] = v.at;
    if (v.until) o["until"] = v.until;
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
  bool ok = !deserializeJson(doc, file);
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
  if (server.method() == HTTP_DELETE) { clearMessages(server.arg("id")); server.send(200, "text/plain", "OK"); return; }
  if (server.method() == HTTP_POST) {
    JsonDocument doc;
    if (deserializeJson(doc, server.arg("plain")) || !doc.is<JsonObject>()) {
      server.send(400, "text/plain", "Expected a JSON object");
      return;
    }
    storeMessage(doc.as<JsonObjectConst>());
    server.send(200, "text/plain", "OK");
    return;
  }
  JsonDocument doc;
  JsonArray list = doc["messages"].to<JsonArray>();
  for (int i = 0; i < MESSAGE_MAX; ++i) {
    if (!messageActive(i)) continue;
    JsonObject o = list.add<JsonObject>();
    o["id"] = messages[i].id; o["title"] = messages[i].title; o["text"] = messages[i].text;
    o["icon"] = messages[i].icon;
    if (messages[i].color.length()) o["color"] = messages[i].color;
    if (messages[i].pct >= 0) o["pct"] = messages[i].pct;
    o["ttl"] = (messages[i].until - millis()) / 1000;
    o["showing"] = cfg.theme == FACE_MESSAGE && messageSlot == i;
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
String storeFace(const String& name, const String& body) {
  if (!storageReady || recoveryMode) return F("Storage unavailable");
  if (body.length() == 0 || body == "{}") {
    LittleFS.remove(facePath(name));
    facesVersion++;
    if (cfg.theme == FACE_CUSTOM && cfg.customFace == name) cfg.theme = FACE_CLASSIC;
    return "";
  }
  if (body.length() > FACE_JSON_MAX) return F("Face is larger than 4 KB");
  JsonDocument doc;
  if (deserializeJson(doc, body) || !doc["items"].is<JsonArray>()) return F("Expected JSON with an \"items\" array");
  if (doc["items"].size() > 32) return F("At most 32 items");
  if (!LittleFS.exists("/faces")) LittleFS.mkdir("/faces");
  if (!LittleFS.exists(facePath(name)) && customFaceCount() >= FACES_MAX) return F("At most 40 custom faces");
  File f = LittleFS.open(facePath(name), "w");
  if (!f) return F("Could not save the face");
  bool ok = f.print(body) == body.length();
  f.close();
  if (!ok) return F("Could not save the face");
  facesVersion++;
  if (doc["show"] | false) showFace(name);
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
  if (!cfg.facesApi) { server.send(404, "text/plain", "Custom faces are turned off (Integrations)"); return false; }
  if (requestAuthorized() || apiKeyOk()) return true;
  server.send(401, "text/plain", "Sign in or send the API key (X-Api-Key)");
  return false;
}

void handleFaces() {   // GET: list; POST ?name=: save (empty body deletes)
  if (!facesAllowed()) return;
  if (server.method() == HTTP_POST) {
    String name = faceName(server.arg("name"));
    if (name.isEmpty()) { server.send(400, "text/plain", "name: 1-20 of a-z 0-9 _ -"); return; }
    String err = storeFace(name, server.arg("plain"));
    if (err.length()) { server.send(400, "text/plain", err); return; }
    if (server.arg("show") == "1") showFace(name);
    server.send(200, "text/plain", "OK");
    return;
  }
  JsonDocument doc;
  JsonArray list = doc["faces"].to<JsonArray>();
  for (int i = 0, n = customFaceCount(); i < n; ++i) list.add(customFaceAt(i));
  if (cfg.theme == FACE_CUSTOM) doc["showing"] = cfg.customFace;
  JsonObject v = doc["vars"].to<JsonObject>();
  for (const Var& var : vars) if (var.key.length() && findVar(var.key)) v[var.key] = var.value;
  String out; serializeJson(doc, out);
  server.send(200, "application/json", out);
}

void handleFaceGet() {   // the JSON of one face, for the editor
  if (!facesAllowed()) return;
  String name = faceName(server.arg("name"));
  File f = name.length() ? LittleFS.open(facePath(name), "r") : File();
  if (!f) { server.send(404, "text/plain", "No such face"); return; }
  server.streamFile(f, "application/json");
  f.close();
}

void handleFaceDelete() {
  if (!facesAllowed()) return;
  String name = faceName(server.arg("name"));
  if (name.isEmpty() || !customFaceExists(name)) { server.send(404, "text/plain", "No such face"); return; }
  storeFace(name, "");
  saveConfig();
  server.send(200, "text/plain", "OK");
}

void handleFaceShow() {
  if (!facesAllowed()) return;
  String name = faceName(server.arg("name"));
  if (name.isEmpty() || !customFaceExists(name)) { server.send(404, "text/plain", "No such face"); return; }
  showFace(name);
  saveConfig();
  server.send(200, "text/plain", "OK");
}

void handleFaceVars() {   // POST {"name":value,...,"$ttl":s,...}: merged; null deletes
  if (!facesAllowed()) return;
  JsonDocument doc;
  if (deserializeJson(doc, server.arg("plain")) || !doc.is<JsonObject>()) {
    server.send(400, "text/plain", "Expected a JSON object");
    return;
  }
  String bad = applyVars(doc.as<JsonObject>());
  if (bad.length()) { server.send(400, "text/plain", "Bad variable name \"" + bad + "\": 1-15 of A-Z a-z 0-9 _"); return; }
  server.send(200, "text/plain", "OK");
}

// ---------------------------------------------------------------- settings page data
void handleIntegrations() {   // GET: signed-in view; POST action=newkey: regenerate
  if (!requireAuth()) return;
  if (server.method() == HTTP_POST && server.arg("action") == "newkey") {
    char key[33];
    for (int i = 0; i < 4; ++i) snprintf(key + i * 8, 9, "%08x", (unsigned)ESP.random());
    cfg.apiKey = key;
    if (!saveConfig()) { server.send(503, "text/plain", "Settings could not be saved"); return; }
  }
  JsonDocument doc;
  doc["apiKey"] = cfg.apiKey;
  doc["mqttStatus"] = mqttStatus();
  doc["mqttPassSet"] = cfg.mqttPass.length() > 0;
  doc["mqttTopic"] = cfg.mqttBase.length() ? cfg.mqttBase : deviceHost();
  String out; serializeJson(doc, out);
  server.send(200, "application/json", out);
}

// ---------------------------------------------------------------- stock SD Pro API
// Shapes follow the stock firmware, as geekmagic-hacs expects them. No sign-in: that
// integration cannot send one. Only photos, face choice and brightness are reachable.
bool compatAllowed() {   // also for a signed-in settings page (photo slideshow switches)
  if (!recoveryMode && (cfg.haCompat || requestAuthorized())) return true;
  server.send(404, "text/plain", "Not found");
  return false;
}

void handleThemeList() {
  if (!compatAllowed()) return;
  JsonDocument doc;
  JsonArray themes = doc["themes"].to<JsonArray>();
  for (int i = 0; i < FACE_COUNT; ++i) {
    if (!*FACE_NAMES[i]) continue;   // a retired face (6, Flip)
    JsonObject t = themes.add<JsonObject>();
    t["id"] = i; t["name"] = FACE_NAMES[i]; t["enabled"] = (cfg.faceMask >> i & 1) != 0;
  }
  doc["interval"] = cfg.themeInterval;
  String out; serializeJson(doc, out);
  server.send(200, "application/json", out);
}

void handleThemeToggle() {
  if (!compatAllowed()) return;
  int id = server.arg("id").toInt();
  if (id < 0 || id >= FACE_COUNT) { server.send(400, "text/plain", "Bad id"); return; }
  if (server.arg("state") == "1") cfg.faceMask |= 1 << id; else cfg.faceMask &= ~(1 << id);
  saveConfig();
  server.send(200, "text/plain", "OK");
}

void handleThemeInterval() {
  if (!compatAllowed()) return;
  cfg.themeInterval = constrain(server.arg("val").toInt(), 0, 3600);
  saveConfig();
  server.send(200, "text/plain", "OK");
}

void handlePhotoToggle() {
  if (!compatAllowed()) return;
  setPhotoEnabled(server.arg("name"), server.arg("state") == "1");
  saveConfig();
  facePhotosChanged();
  server.send(200, "text/plain", "OK");
}

void handlePhotoInterval() {
  if (!compatAllowed()) return;
  cfg.photoInterval = constrain(server.arg("val").toInt(), 1, 3600);
  saveConfig();
  server.send(200, "text/plain", "OK");
}

// ---------------------------------------------------------------- MQTT
#if SMARTCLOCK_MQTT
WiFiClient mqttNet;
PubSubClient mqtt(mqttNet);
uint32_t mqttRetryAt = 0, mqttStateAt = 0, mqttFacesSent = 0;
bool mqttReconnect = false;
String mqttLastError = "off";

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
    if (deserializeJson(d, body)) return;
    if (d["state"].is<const char*>()) backlightOff = d["state"] == "OFF";
    if (d["brightness"].is<int>()) { cfg.brightness = constrain(d["brightness"].as<int>(), 0, 100); backlightOff = false; saveConfig(); }
    brightnessDirty = true;
  } else if (topic == b + "/face/set") {
    for (int i = 0; i < FACE_COUNT; ++i)
      if (*FACE_NAMES[i] && body == FACE_NAMES[i]) { cfg.theme = i; faceSince = millis(); faceInvalidate(); saveConfig(); }
    if (cfg.facesApi && customFaceExists(body)) { showFace(body); saveConfig(); }
  } else if (topic == b + "/sync") {
    syncWeather();
  } else if (topic == b + "/message") {
    JsonDocument d;
    if (!deserializeJson(d, body) && d.is<JsonObject>()) storeMessage(d.as<JsonObjectConst>());
  } else if (topic == b + "/message/clear") {
    clearMessages(body);
  } else if (cfg.facesApi && topic.startsWith(b + "/custom/")) {
    String name = faceName(topic.substring(b.length() + 8));
    if (name.length()) storeFace(name, body);
  } else if (cfg.facesApi && topic == b + "/vars") {
    JsonDocument d;
    if (!deserializeJson(d, body) && d.is<JsonObject>()) applyVars(d.as<JsonObject>());
  }
  publishState();
}

#endif  // SMARTCLOCK_MQTT

}  // namespace

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
  if (key == "title") return m.title;
  if (key == "text") return m.text;
  if (key == "icon") return m.icon;
  if (key == "color") return m.color;
  if (key == "pct") return String(m.pct);
  if (key == "id") return m.id;
  if (key == "ttl") return String((m.until - millis()) / 1000);   // seconds left
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
#if SMARTCLOCK_MQTT
  mqttReconnect = true;
#endif
}

void integrationsRoutes() {
  server.on("/api/faces", handleFaces);
  server.on("/api/faces/get", handleFaceGet);
  server.on("/api/faces/delete", HTTP_POST, handleFaceDelete);
  server.on("/api/faces/show", HTTP_POST, handleFaceShow);
  server.on("/api/faces/vars", HTTP_POST, handleFaceVars);
  server.on("/api/integrations", handleIntegrations);
  server.on("/api/message", handleMessage);
  // Stock SD Pro shapes (only answer while "Home Assistant dashboards" is on).
  server.on("/theme/list", handleThemeList);
  server.on("/theme/toggle", handleThemeToggle);
  server.on("/theme/interval", handleThemeInterval);
  server.on("/photo/list", []{ if (compatAllowed()) handlePhotoList(); });
  server.on("/photo/upload", HTTP_POST, finishPhotoUpload, handlePhotoUpload);
  server.on("/photo/toggle", handlePhotoToggle);
  server.on("/photo/delete", []{ if (compatAllowed()) handlePhotoDelete(); });
  server.on("/photo/interval", handlePhotoInterval);
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
    if (ESP.getFreeHeap() < 16000) { mqttLastError = "low memory"; return; }
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
  }
  mqtt.loop();
  if (mqttFacesSent != customFacesVersion()) publishDiscovery();   // face list changed
  if (millis() - mqttStateAt > 60000) publishState();
#endif
}

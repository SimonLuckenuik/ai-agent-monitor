// The starter (spec 048): a small plain image, built from this repository with every
// release as SDPro_AIAgentMonitor_starter_v<version>.bin, so a clock on the stock
// firmware (which only takes plain images named SDP...) gets to AI Agent Monitor in two
// steps: install the starter, then press Install. It puts the clock on WiFi, downloads
// the latest release's .update file from GitHub itself (HTTPS: the full firmware has no
// room for TLS) and installs it with the full firmware's update handler (update.cpp).
// Its page is the full firmware's setup page (web/fallback.html).
//
// Built by [env:starter] only (platformio.ini: build_src_filter); the firmware and the
// emulator compile none of it.
#if SMARTCLOCK_STARTER
#include <Arduino.h>
#include <ArduinoJson.h>
#include <DNSServer.h>
#include <EEPROM.h>
#include <ESP8266HTTPClient.h>
#include <ESP8266WebServer.h>
#include <ESP8266WiFi.h>
#include <LittleFS.h>
#include <TFT_eSPI.h>
#include <WiFiClientSecureBearSSL.h>
#include <memory>
#include <time.h>
#include "build_info.h"      // FW_BUILD, FW_RELEASE
#include "config.h"
#include "pins.h"
#include "starter_roots.h"   // the roots GitHub's hosts chain to
#include "update.h"
#include "web_fallback.h"

ESP8266WebServer server(80);
bool storageReady = false, recoveryMode = false;   // update.cpp reads both
Config cfg;
String devicePassword;   // the emulator's screen server reads it too
bool requestAuthorized();
void safeRestart();

namespace {
const char REPO[] PROGMEM = "SimonLuckenuik/ai-agent-monitor";
DNSServer dns;
TFT_eSPI tft;
bool storageMountFailed = false;

// ---- The device password: the full firmware's (main.cpp), in the EEPROM sector, so the
// setup network and the settings page keep it after the full firmware is installed.
constexpr int PASS_ADDR = 0xfc0;
constexpr uint32_t PASS_MAGIC = 0x53435057;
constexpr size_t PASS_MIN = 8, PASS_MAX = 31;

void loadPassword() {
  EEPROM.begin(SPI_FLASH_SEC_SIZE);   // the whole sector, so the stock firmware's bytes stay
  uint32_t magic = 0;
  for (int i = 0; i < 4; ++i) magic |= uint32_t(EEPROM.read(PASS_ADDR + i)) << (8 * i);
  String p;
  if (magic == PASS_MAGIC)
    for (size_t i = 0; i < PASS_MAX; ++i) { char c = EEPROM.read(PASS_ADDR + 4 + i); if (!c) break; p += c; }
  bool ok = p.length() >= PASS_MIN;
  for (unsigned i = 0; ok && i < p.length(); ++i) ok = p[i] >= 0x21 && p[i] <= 0x7e;
  if (!ok) {   // 10 characters, no 0/o or 1/i/l (as main.cpp)
    static const char alphabet[] = "23456789abcdefghjkmnpqrstuvwxyz";
    p = "";
    for (int i = 0; i < 10; ++i) p += alphabet[ESP.random() % (sizeof(alphabet) - 1)];
    for (int i = 0; i < 4; ++i) EEPROM.write(PASS_ADDR + i, uint8_t(PASS_MAGIC >> (8 * i)));
    for (size_t i = 0; i <= PASS_MAX; ++i) EEPROM.write(PASS_ADDR + 4 + i, i < p.length() ? p[i] : 0);
  }
  EEPROM.end();
  devicePassword = p;
}

// ---- Sign-in: as main.cpp, a few sessions in RAM.
String sessions[3];
String requestToken() {
  String c = server.header(F("Cookie"));
  int i = c.indexOf(F("sc="));
  if (i < 0) return "";
  int end = c.indexOf(';', i);
  return c.substring(i + 3, end < 0 ? c.length() : end);
}
bool requireAuth();

String setupSsid() {
  char name[32];
  snprintf_P(name, sizeof(name), PSTR("AI Agent Monitor %06x"), (unsigned)(ESP.getChipId() & 0xffffff));
  return name;
}

// ---- The download (one at a time, run from loop()).
enum Download : uint8_t { DL_IDLE, DL_PENDING, DL_RUNNING, DL_DONE, DL_FAILED } dl = DL_IDLE;
String dlTag, dlUrl, dlError;
int32_t dlTotal = 0;
uint32_t dlGot = 0;
uint32_t dlHeapLow = 0;   // the least free heap while installing: the margin, in /status

// ---- The screen: what to do now, in large type.
String line;   // a status line under the steps (connecting, an error, the progress)
void centre(const String& s, int y, uint8_t font, uint16_t colour) {
  tft.setTextColor(colour, TFT_BLACK);
  tft.setTextDatum(MC_DATUM);
  tft.drawString(s, 120, y, font);
}
void drawScreen() {
  tft.fillScreen(TFT_BLACK);
  centre(F("AI Agent Monitor"), 24, 4, TFT_WHITE);
  centre(F("Setup"), 50, 2, TFT_LIGHTGREY);
  if (dl == DL_RUNNING || dl == DL_DONE) {
    centre(F("Installing"), 100, 4, TFT_WHITE);
    centre(dlTag, 130, 4, TFT_CYAN);
    centre(dl == DL_DONE ? String(F("Restarting...")) : String(F("Keep the clock powered")), 200, 2, TFT_YELLOW);
    return;
  }
  centre(F("Join WiFi:"), 80, 2, TFT_WHITE);
  centre(setupSsid(), 100, 2, TFT_CYAN);
  centre(F("Password:"), 124, 2, TFT_WHITE);
  centre(devicePassword, 148, 4, TFT_YELLOW);
  centre(String(F("then open ")) + WiFi.softAPIP().toString(), 178, 2, TFT_WHITE);
  if (WiFi.status() == WL_CONNECTED) centre(String(F("On WiFi: ")) + WiFi.localIP().toString(), 202, 2, TFT_GREEN);
  if (line.length()) centre(line, 224, 2, TFT_ORANGE);
}
void drawProgress() {
  int w = dlTotal > 0 ? int(int64_t(dlGot) * 200 / dlTotal) : 0;
  tft.drawRect(19, 159, 202, 12, TFT_DARKGREY);
  tft.fillRect(20, 160, w, 10, TFT_CYAN);
}

// ---- HTTPS to GitHub: BearSSL with the two roots, the time from NTP, HTTP/1.0 (no
// chunked bodies to undo). GitHub's hosts do not negotiate smaller TLS records, so the
// full 16 KB receive buffer (RESEARCH.md).
bool waitForTime() {
  for (int i = 0; i < 150 && time(nullptr) < 1700000000; ++i) { delay(100); server.handleClient(); }
  return time(nullptr) >= 1700000000;
}
std::unique_ptr<BearSSL::X509List> roots() {
  std::unique_ptr<BearSSL::X509List> list(new (std::nothrow) BearSSL::X509List);
  if (!list) return list;
  // The core parses certificates byte by byte: give it copies in RAM, not flash.
  for (int i = 0; i < 2; ++i) {
    const uint8_t* src = i ? ROOT_ISRG_ROOT_X1 : ROOT_USERTRUST_ECC;
    size_t len = i ? ROOT_ISRG_ROOT_X1_LEN : ROOT_USERTRUST_ECC_LEN;
    std::unique_ptr<uint8_t[]> der(new (std::nothrow) uint8_t[len]);
    if (!der) return nullptr;
    memcpy_P(der.get(), src, len);
    if (!list->append(der.get(), len)) return nullptr;
  }
  return list;
}
// Opens url; "" or why not. client must outlive http. The roots are needed for the
// handshakes only (GET() follows the redirects): freed once it answers, for the heap.
String httpsGet(HTTPClient& http, BearSSL::WiFiClientSecure& client, const String& url) {
  if (WiFi.status() != WL_CONNECTED) return F("The clock is not on WiFi");
  if (!waitForTime()) return F("No time from the internet yet (NTP): try again");
  std::unique_ptr<BearSSL::X509List> list = roots();
  if (!list) return F("Not enough memory");
  client.setTrustAnchors(list.get());
  client.setX509Time(time(nullptr));
  client.setBufferSizes(16384, 512);
  http.useHTTP10(true);
  http.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
  http.setTimeout(20000);
  http.setUserAgent(F("AIAgentMonitor-starter/" FW_RELEASE));
  if (!http.begin(client, url)) return F("Bad address");
  int code = http.GET();
  client.setTrustAnchors(nullptr);
  list.reset();
  if (code == HTTP_CODE_OK) return "";
  String why = code < 0 ? HTTPClient::errorToString(code) : String(F("GitHub answered ")) + code;
  char err[64];
  if (client.getLastSSLError(err, sizeof(err))) why += String(F(" (")) + err + ')';
  http.end();
  return why;
}

// GET /api/update/latest: the latest release, as the clock reads it from GitHub.
void handleLatest() {
  if (!requireAuth()) return;
  if (dl == DL_RUNNING) { server.send(409, F("text/plain"), F("Installing")); return; }
  JsonDocument out;
  {
    BearSSL::WiFiClientSecure client;
    HTTPClient http;
    String why = httpsGet(http, client, String(F("https://api.github.com/repos/")) + FPSTR(REPO) + F("/releases/latest"));
    if (why.length()) {
      out[F("error")] = why;
    } else {
      // Only what is needed of the 10-20 KB answer; not the notes (a few KB more while
      // TLS holds 30 KB): the page links to the release instead.
      JsonDocument filter;
      filter[F("tag_name")] = true;
      filter[F("html_url")] = true;
      filter[F("assets")][0][F("name")] = true;
      filter[F("assets")][0][F("browser_download_url")] = true;
      filter[F("assets")][0][F("size")] = true;
      JsonDocument rel;
      DeserializationError e = deserializeJson(rel, http.getStream(), DeserializationOption::Filter(filter));
      http.end();
      if (e) {
        out[F("error")] = String(F("GitHub's answer could not be read: ")) + e.c_str();
      } else {
        dlTag = rel[F("tag_name")] | "";
        dlUrl = "";
        // The .update file; a release without one (before spec 048): its .bin.gz, and the
        // full firmware's setup page then installs the files.
        for (int pass = 0; pass < 2 && dlUrl.isEmpty(); ++pass)
          for (JsonObjectConst a : rel[F("assets")].as<JsonArrayConst>()) {
            const char* name = a[F("name")] | "";
            size_t n = strlen(name);
            const char* end = pass ? ".bin.gz" : ".update";
            size_t e = strlen(end);
            if (n > e && !strcmp(name + n - e, end) && !strncmp_P(name, PSTR("AIAgentMonitor_v"), 16)) {
              dlUrl = a[F("browser_download_url")] | "";
              dlTotal = a[F("size")] | 0;
            }
          }
        if (dlUrl.isEmpty()) out[F("error")] = String(F("Release ")) + dlTag + F(" has no update file");
        out[F("tag")] = dlTag;
        out[F("url")] = rel[F("html_url")] | "";
        out[F("size")] = dlTotal;
      }
    }
  }
  String s;
  serializeJson(out, s);
  server.send(200, F("application/json"), s);
}

// POST /api/update/github: start installing the release that /api/update/latest found.
void handleGithub() {
  if (!requireAuth()) return;
  if (dl == DL_RUNNING || updateBusy()) { server.send(409, F("text/plain"), F("An update is already running")); return; }
  if (dlUrl.isEmpty() || server.arg(F("tag")) != dlTag) { server.send(400, F("text/plain"), F("Ask for the latest release first")); return; }
  dl = DL_PENDING;
  dlError = ""; dlGot = 0; dlHeapLow = UINT32_MAX;
  server.send(200, F("text/plain"), F("OK"));
}

// Runs from loop(), in two steps so TLS (~33 KB with its buffers and stack) and the
// updater (its 4 KB sector buffer) never hold the heap at the same time: they did not fit
// together in the starter's ~44 KB (measured in the emulator with the chip's heap).
//   1. download the .update file to storage (TLS only), serving the page between pieces;
//   2. close the connection, install from storage with the full firmware's handler.
// Restarts into the full firmware once it is in.
const char DOWNLOADED[] = "/download.update";

bool downloadToFile() {
  FSInfo fs;
  if (!storageReady || !LittleFS.info(fs)) { dlError = F("Storage is not in use: install the .update file by hand"); return false; }
  uint32_t need = (dlTotal > 0 ? dlTotal : 600 * 1024) + 64 * 1024;   // and the files it brings, staged later
  if (fs.totalBytes - fs.usedBytes < need) {
    dlError = String(F("Storage is too full for the download (")) + ((fs.totalBytes - fs.usedBytes) / 1024) +
              F(" KB free): format it below, or install the .update file by hand");
    return false;
  }
  File out = LittleFS.open(DOWNLOADED, "w");
  if (!out) { dlError = F("Could not write to storage"); return false; }
  BearSSL::WiFiClientSecure client;
  HTTPClient http;
  String why = httpsGet(http, client, dlUrl);
  if (why.length()) { dlError = why; out.close(); return false; }
  int32_t size = http.getSize();
  if (size > 0) dlTotal = size;
  WiFiClient* s = http.getStreamPtr();
  uint8_t buf[512];
  uint32_t lastData = millis();
  int shown = -1;
  while (dlTotal <= 0 || dlGot < (uint32_t)dlTotal) {
    size_t n = s->available();
    if (n) {
      n = s->readBytes(buf, std::min(n, sizeof(buf)));
      if (out.write(buf, n) != n) { dlError = F("Could not write to storage"); break; }
      dlGot += n;
      lastData = millis();
      dlHeapLow = std::min<uint32_t>(dlHeapLow, ESP.getFreeHeap());
      int pct = dlTotal > 0 ? int(int64_t(dlGot) * 100 / dlTotal) : 0;
      if (pct / 5 != shown) { shown = pct / 5; drawProgress(); }
    } else if (!http.connected() && !s->available()) {
      break;
    } else if (millis() - lastData > 30000) {
      dlError = F("The download stopped");
      break;
    } else {
      delay(1);
    }
    dns.processNextRequest();
    server.handleClient();
  }
  http.end();
  out.close();
  if (dlError.isEmpty() && dlTotal > 0 && dlGot != (uint32_t)dlTotal) dlError = F("The download was cut short");
  return dlError.isEmpty();
}

bool installFromFile() {
  File in = LittleFS.open(DOWNLOADED, "r");
  if (!in) { dlError = F("The download is gone"); return false; }
  updateBegin();
  uint8_t buf[512];
  bool ok = updateBusy();
  while (ok && in.available()) {
    size_t n = in.read(buf, sizeof(buf));
    ok = n && updateFeed(buf, n);
    dlHeapLow = std::min<uint32_t>(dlHeapLow, ESP.getFreeHeap());
    server.handleClient();
  }
  in.close();
  ok = ok && updateFinish();
  if (!ok) dlError = updateError().length() ? updateError() : String(F("Could not install the download"));
  return ok;
}

void runDownload() {
  dl = DL_RUNNING;
  drawScreen();
  drawProgress();
  dlError = "";
  bool ok = downloadToFile() && installFromFile();
  LittleFS.remove(DOWNLOADED);
  if (!ok) {
    dl = DL_FAILED;
    updateFail(dlError);
    line = F("Install failed: see the page");
    drawScreen();
    return;
  }
  dl = DL_DONE;
  drawScreen();
  uint32_t until = millis() + 4000;   // let the page see "done"
  while ((int32_t)(millis() - until) < 0) { server.handleClient(); delay(10); }
  safeRestart();
}

// ---- The web API the setup page uses (web/fallback.html).
void sendSetup() {
  server.sendHeader(F("Cache-Control"), F("no-cache"));
  server.sendHeader(F("Content-Encoding"), F("gzip"));
  server.send_P(200, "text/html", reinterpret_cast<PGM_P>(FALLBACK_HTML_GZ), FALLBACK_HTML_GZ_LEN);
}

void handleStatus() {
  JsonDocument doc;
  bool linked = WiFi.status() == WL_CONNECTED;
  doc[F("starter")] = true;
  doc[F("download")] = true;   // the clock downloads releases itself
  doc[F("version")] = FW_RELEASE;
  doc[F("build")] = FW_BUILD;
  doc[F("files")] = "";
  doc[F("recovery")] = false;
  doc[F("storageReady")] = storageReady;
  doc[F("storageMountFailed")] = storageMountFailed;
  doc[F("signedIn")] = requestAuthorized();
  doc[F("connected")] = linked;
  doc[F("ssid")] = linked ? WiFi.SSID() : cfg.ssid;
  if (linked) doc[F("ip")] = WiFi.localIP().toString();
  else if (cfg.ssid.length() && WiFi.status() == WL_CONNECT_FAILED) doc[F("wifiError")] = F("wrong password or refused");
  else if (cfg.ssid.length() && WiFi.status() == WL_NO_SSID_AVAIL) doc[F("wifiError")] = F("network not found");
  doc[F("apIp")] = WiFi.softAPIP().toString();
  doc[F("apSsid")] = setupSsid();
  doc[F("heap")] = ESP.getFreeHeap();
  if (dl != DL_IDLE) {
    JsonObject u = doc[F("update")].to<JsonObject>();
    u[F("got")] = dlGot;
    u[F("total")] = dlTotal;
    u[F("done")] = dl == DL_DONE;
    if (dlHeapLow != UINT32_MAX) u[F("heapLow")] = dlHeapLow;
    if (dl == DL_FAILED) u[F("error")] = dlError;
  }
  String out;
  serializeJson(doc, out);
  server.send(200, F("application/json"), out);
}

void handleLogin() {
  static uint32_t lockedUntil = 0;
  static uint8_t fails = 0;
  if (millis() < lockedUntil) { server.send(429, F("text/plain"), F("Too many attempts; wait a minute")); return; }
  if (server.arg(F("password")) != devicePassword) {
    delay(800);
    if (++fails >= 5) { fails = 0; lockedUntil = millis() + 60000; }
    server.send(403, F("text/plain"), F("Wrong password"));
    return;
  }
  fails = 0;
  char token[33];
  for (int i = 0; i < 4; ++i) snprintf_P(token + i * 8, 9, PSTR("%08x"), (unsigned)ESP.random());
  static int next = 0;
  sessions[next] = token;
  next = (next + 1) % 3;
  server.sendHeader(F("Set-Cookie"), String(F("sc=")) + token + F("; Path=/; HttpOnly; SameSite=Strict"));
  server.send(200, F("text/plain"), F("OK"));
}

void handleScan() {
  if (!requireAuth()) return;
  int n = WiFi.scanNetworks();
  JsonDocument doc;
  JsonArray a = doc.to<JsonArray>();
  for (int i = 0; i < n; ++i) {
    JsonObject o = a.add<JsonObject>();
    o[F("ssid")] = WiFi.SSID(i);
    o[F("rssi")] = WiFi.RSSI(i);
    o[F("secure")] = WiFi.encryptionType(i) != ENC_TYPE_NONE;
  }
  WiFi.scanDelete();
  String out;
  serializeJson(doc, out);
  server.send(200, F("application/json"), out);
}

// Joins the network now (no restart: the setup network stays up, and so does the page),
// and saves it where the full firmware reads it (/config.json).
void handleConnect() {
  if (!requireAuth()) return;
  cfg.ssid = server.arg(F("ssid"));
  cfg.password = server.arg(F("password"));
  if (cfg.ssid.isEmpty()) { server.send(400, F("text/plain"), F("Choose a network")); return; }
  bool saved = storageReady && cfg.save();
  WiFi.begin(cfg.ssid.c_str(), cfg.password.c_str());
  line = String(F("Joining ")) + cfg.ssid;
  drawScreen();
  server.send(200, F("text/plain"), saved ? F("OK") : F("OK: joining; not saved (storage not in use)"));
}

void handleFormat() {
  if (!requireAuth()) return;
  if (!storageMountFailed || server.arg(F("confirm")) != F("erase")) {
    server.send(400, F("text/plain"), F("Formatting is only available when storage failed to mount"));
    return;
  }
  bool ok = LittleFS.format();
  server.send(ok ? 200 : 500, F("text/plain"), ok ? F("Storage formatted. Rebooting...") : F("Format failed"));
  if (ok) safeRestart();
}

bool requireAuth() {
  if (requestAuthorized()) return true;
  server.send(401, F("text/plain"), F("Sign in first"));
  return false;
}
}  // namespace

bool requestAuthorized() {
  String t = requestToken();
  if (t.length() != 32) return false;
  for (const String& s : sessions) if (s == t) return true;
  return false;
}

void safeRestart() {
  delay(500);
  ESP.restart();
}

void setup() {
  loadPassword();
  pinMode(PIN_BACKLIGHT, OUTPUT);
  analogWriteRange(BL_PWM_RANGE);
  analogWrite(PIN_BACKLIGHT, BL_PWM_RANGE);   // off while the panel starts
  // Storage is never formatted without a click: a stock clock has a volume of its own.
  LittleFS.setConfig(LittleFSConfig(false));
  storageReady = LittleFS.begin();
  storageMountFailed = !storageReady;
  if (storageReady) cfg.load();
  tft.init();
  tft.setRotation(cfg.orientation);
  tft.fillScreen(TFT_BLACK);
  analogWrite(PIN_BACKLIGHT, BL_PWM_RANGE - BL_PWM_RANGE * 70 / 100);
  if (storageMountFailed) line = F("Storage not mounted");
  WiFi.persistent(false);
  WiFi.mode(WIFI_AP_STA);
  WiFi.hostname(String(F("agentmonitor-")) + String(ESP.getChipId() & 0xffffff, HEX));
  WiFi.softAPConfig(IPAddress(192, 168, 4, 1), IPAddress(192, 168, 4, 1), IPAddress(255, 255, 255, 0));
  WiFi.softAP(setupSsid().c_str(), devicePassword.c_str());
  dns.start(53, "*", WiFi.softAPIP());
  if (cfg.ssid.length()) WiFi.begin(cfg.ssid.c_str(), cfg.password.c_str());
  configTime(0, 0, "pool.ntp.org", "time.google.com");
  server.collectHeaders("Cookie");
  server.on(F("/"), sendSetup);
  server.on(F("/setup"), sendSetup);
  server.on(F("/status"), handleStatus);
  server.on(F("/api/auth/login"), HTTP_POST, handleLogin);
  server.on(F("/api/auth/reveal"), HTTP_POST, [] { server.send(200, F("text/plain"), F("OK")); });   // always on screen
  server.on(F("/scanwifi"), handleScan);
  server.on(F("/connect"), HTTP_POST, handleConnect);
  server.on(F("/restart"), HTTP_POST, [] { if (!requireAuth()) return; server.send(200, F("text/plain"), F("Restarting")); safeRestart(); });
  server.on(F("/format_storage"), HTTP_POST, handleFormat);
  server.on(F("/api/update/latest"), handleLatest);
  server.on(F("/api/update/github"), HTTP_POST, handleGithub);
  // An upload while a download runs would replace it: refused.
  server.on(F("/update_ota"), HTTP_POST,
            [] { if (dl == DL_RUNNING) { server.send(409, F("text/plain"), F("Installing from GitHub")); return; } finishOta(); },
            [] { if (dl != DL_RUNNING) handleOtaUpload(); });
  server.onNotFound([] {   // the captive portal: everything leads to the setup page
    server.sendHeader(F("Location"), F("/"), true);
    server.send(302, F("text/plain"), "");
  });
  server.begin();
  drawScreen();
}

void loop() {
  static bool linked = false;
  dns.processNextRequest();
  server.handleClient();
  if (updateBusy()) { delay(1); return; }   // an upload is staging
  bool now = WiFi.status() == WL_CONNECTED;
  if (now != linked) { linked = now; if (now) line = ""; drawScreen(); }
  if (dl == DL_PENDING) runDownload();
  delay(5);
}
#endif  // SMARTCLOCK_STARTER

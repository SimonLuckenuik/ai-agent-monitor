// The starter (spec 048): a small plain image, built from this repository with every
// release as SDPro_AIAgentMonitor_starter_v<version>.bin, so a clock on the stock
// firmware (which only takes plain images named SDP...) gets to AI Agent Monitor in two
// steps: install the starter, then press Install on its page. Its page is the full
// firmware's setup page (web/fallback.html), and as there the browser downloads the
// release from GitHub and sends it to the clock (update.cpp, the full firmware's handler):
// the clock itself has no HTTPS.
//
// From its own network, the browser reaches GitHub through the clock: the captive portal
// stays (every name is the clock, so the phone opens the setup page), except GitHub's
// names, which the clock looks up on the home network once it is on it, and whose
// traffic it forwards (NAT). From the home network the browser has internet anyway.
//
// Built by [env:starter] only (platformio.ini: build_src_filter); the firmware compiles
// none of it, the emulator only with EMU_STARTER=1.
#if SMARTCLOCK_STARTER
#include <Arduino.h>
#include <ArduinoJson.h>
#include <EEPROM.h>
#include <ESP8266WebServer.h>
#include <ESP8266WiFi.h>
#include <LittleFS.h>
#include <TFT_eSPI.h>
#include <WiFiUdp.h>
#if LWIP_FEATURES && !LWIP_IPV6 && !SMARTCLOCK_EMULATOR
#include <lwip/napt.h>
#define STARTER_NAPT 1
#else
#define STARTER_NAPT 0   // the emulator: no lwIP, no forwarding
#endif
#include "build_info.h"      // FW_BUILD, FW_RELEASE
#include "config.h"
#include "pins.h"
#include "update.h"
#include "web_fallback.h"

ESP8266WebServer server(80);
bool storageReady = false, recoveryMode = false;   // update.cpp reads both
Config cfg;
String devicePassword;   // the emulator's screen server reads it too
bool requestAuthorized();
void safeRestart();

namespace {
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

// ---- Names on the setup network. The captive portal answers every name with the clock,
// so a phone opens the setup page (Windows asks for www.msftconnecttest.com, Android
// connectivitycheck.gstatic.com, Apple captive.apple.com: all of them land here). The
// names the page needs to reach GitHub are the exception once the clock is on WiFi: the
// clock looks them up on the home network and answers with the real address, and forwards
// their traffic (NAT, below). Anything else still lands on the clock.
const char* const GITHUB_NAMES[] = {"api.github.com", "simonluckenuik.github.io"};
WiFiUDP dnsUdp;
struct Known { IPAddress ip; uint32_t at = 0; } known[sizeof(GITHUB_NAMES) / sizeof(GITHUB_NAMES[0])];

int githubName(const char* name) {
  for (size_t i = 0; i < sizeof(GITHUB_NAMES) / sizeof(GITHUB_NAMES[0]); ++i)
    if (!strcasecmp(name, GITHUB_NAMES[i])) return int(i);
  return -1;
}

void serviceDns() {
  int n = dnsUdp.parsePacket();
  if (n <= 0) return;
  uint8_t q[300];   // a question is far shorter; anything longer is not one we answer
  if (n > (int)sizeof(q)) { dnsUdp.flush(); return; }
  dnsUdp.read(q, n);
  // A query (QR 0, opcode 0) with one question.
  if (n < 17 || (q[2] & 0xf8) || q[4] || q[5] != 1) return;
  char name[256];
  int at = 12, len = 0;
  while (at < n && q[at]) {
    int label = q[at++];
    if (label > 63 || at + label > n || len + label + 1 >= (int)sizeof(name)) return;
    if (len) name[len++] = '.';
    memcpy(name + len, q + at, label);
    len += label; at += label;
  }
  name[len] = 0;
  if (at + 5 > n) return;
  uint16_t type = (q[at + 1] << 8) | q[at + 2];
  int end = at + 5;   // the end of the question: name's 0, type, class
  IPAddress ip = WiFi.softAPIP();
  bool failed = false;
  int g = githubName(name);
  if (g >= 0) {
    if (WiFi.status() != WL_CONNECTED) {
      failed = true;   // not on WiFi yet: no answer to give (the page asks for WiFi first)
    } else if (known[g].at && millis() - known[g].at < 300000) {
      ip = known[g].ip;
    } else if (WiFi.hostByName(name, ip, 3000) == 1) {
      known[g].ip = ip; known[g].at = millis() ? millis() : 1;
    } else {
      failed = true;
    }
  }
  uint8_t r[sizeof(q) + 16];
  memcpy(r, q, end);
  r[2] = 0x80 | (q[2] & 0x01);           // a response; recursion desired, as asked
  r[3] = 0x80 | (failed ? 2 : 0);        // recursion available; SERVFAIL when not found
  r[6] = 0; r[8] = 0; r[9] = 0; r[10] = 0; r[11] = 0;
  bool answer = !failed && type == 1;    // A records only: anything else (AAAA) has none
  r[7] = answer ? 1 : 0;
  int out = end;
  if (answer) {
    const uint8_t rr[] = {0xc0, 0x0c, 0, 1, 0, 1, 0, 0, 0, uint8_t(g >= 0 ? 60 : 10), 0, 4,
                          ip[0], ip[1], ip[2], ip[3]};   // the name at 12, A, IN, TTL, 4 bytes
    memcpy(r + out, rr, sizeof(rr));
    out += sizeof(rr);
  }
  dnsUdp.beginPacket(dnsUdp.remoteIP(), dnsUdp.remotePort());
  dnsUdp.write(r, out);
  dnsUdp.endPacket();
}

// Forwarding for the setup network once the clock is on WiFi, so the names above lead
// somewhere. A browser fetching from GitHub needs a handful of connections: 64 entries
// (28 bytes each), from the heap the clock's HTTPS no longer needs.
bool natOn = false;
void serviceNat() {
#if STARTER_NAPT
  if (natOn || WiFi.status() != WL_CONNECTED) return;
  if (ip_napt_init(64, 1) == ERR_OK && ip_napt_enable_no(SOFTAP_IF, 1) == ERR_OK) natOn = true;
#endif
}

// ---- The screen: what to do now.
String line;   // a status line (connecting, an error)
bool installing = false;
void centre(const String& s, int y, uint8_t font, uint16_t colour) {
  tft.setTextColor(colour, TFT_BLACK);
  tft.setTextDatum(MC_DATUM);
  tft.drawString(s, 120, y, font);
}
void drawScreen() {
  tft.fillScreen(TFT_BLACK);
  centre(F("AI Agent Monitor"), 24, 4, TFT_WHITE);
  centre(F("Setup"), 50, 2, TFT_LIGHTGREY);
  if (installing) {
    centre(F("Installing"), 110, 4, TFT_WHITE);
    centre(F("Keep the clock powered"), 200, 2, TFT_YELLOW);
    return;
  }
  centre(F("Join WiFi:"), 80, 2, TFT_WHITE);
  centre(setupSsid(), 100, 2, TFT_CYAN);
  centre(F("Password:"), 124, 2, TFT_WHITE);
  centre(devicePassword, 148, 4, TFT_YELLOW);
  centre(String(F("then open ")) + WiFi.softAPIP().toString(), 178, 2, TFT_WHITE);
  if (WiFi.status() == WL_CONNECTED) centre(String(F("or http://")) + WiFi.localIP().toString(), 202, 2, TFT_GREEN);
  if (line.length()) centre(line, 224, 2, TFT_ORANGE);
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
  doc[F("version")] = FW_RELEASE;
  doc[F("build")] = FW_BUILD;
  doc[F("uptime")] = millis() / 1000;
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
  doc[F("nat")] = natOn;   // the setup network reaches GitHub through the clock
  doc[F("heap")] = ESP.getFreeHeap();
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
  dnsUdp.begin(53);
  if (cfg.ssid.length()) WiFi.begin(cfg.ssid.c_str(), cfg.password.c_str());
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
  server.on(F("/update_ota"), HTTP_POST, finishOta, handleOtaUpload);
  server.onNotFound([] {   // the captive portal: everything leads to the setup page
    server.sendHeader(F("Location"), F("/"), true);
    server.send(302, F("text/plain"), "");
  });
  // A short queue of connections not yet served: a phone's burst of them took the heap.
  server.getServer().begin(80, 4);
  drawScreen();
}

void loop() {
  static bool linked = false;
  serviceDns();
  server.handleClient();
  bool busy = updateBusy();
  if (busy != installing) { installing = busy; drawScreen(); }
  if (busy) { delay(1); return; }   // an upload is staging
  bool now = WiFi.status() == WL_CONNECTED;
  if (now != linked) { linked = now; if (now) line = ""; drawScreen(); }
  serviceNat();
  delay(5);
}
#endif  // SMARTCLOCK_STARTER

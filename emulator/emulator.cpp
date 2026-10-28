// Host emulator glue: runs the firmware on the esp8266/Arduino core's host emulation
// (tests/host) and serves the panel on a second port for emulator/screen.html.
//
//   port 80    the firmware's own web server, unchanged
//   port 8080  GET /          screen.html
//              GET /ws        WebSocket: the screen as it changes (see wsPush)
//              GET /fb        the 240x240 RGB565 framebuffer (little-endian, 115,200 B);
//                             X-Backlight: percent, X-Frame: frame counter
//              GET /timing    the drawing-speed model's totals (panel.cpp); &reset=1 zeroes them
//              POST /power    power cycle (the supervisor restarts with a power-on reset)
//              POST /reset    external reset (same reset reason as the EN pin)
//              GET /password  the device password, for the settings link's auto sign-in
//              GET /fs?path=  a LittleFS directory as JSON (read-only)
//              GET /fs/file?path=[&download=1]  a LittleFS file
//
// The firmware's setup()/loop() are renamed by fw_main.cpp so this file can wrap them.
#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <LittleFS.h>
#include <TFT_eSPI.h>
#include <base64.h>
#include <user_interface.h>
#include <ArduinoJson.h>
#include "weather.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

void fw_setup();
void fw_loop();
extern uint16_t emuPanel[240 * 240];   // panel.cpp: what the panel's glass shows
String emuTimingJson(bool reset);       // panel.cpp
constexpr int PANEL_W = 240, PANEL_H = 240;
extern String devicePassword;

namespace {

WiFiServer screenServer(8080);
int backlightDuty = 1023, backlightRange = 1023;   // active-low: 1023 = off
uint32_t frames = 0;

const char* dataDir() { const char* d = getenv("EMU_DATA"); return d && *d ? d : "."; }

String dataPath(const char* name) { return String(dataDir()) + "/" + name; }

String readFile(const char* path);

}  // namespace

// ---- Fixed time and weather, for tests that need the same screens every run. Both
// are files in $EMU_DATA, so they survive the firmware's restarts.
// POST /time?epoch=N stops the clock at N (Unix seconds; 0 lets it run again).
time_t emuFixedTime = -1;   // -1: not read yet
extern "C" time_t time(time_t* out) {   // replaces libc's for the firmware's time(nullptr)
  if (emuFixedTime < 0) {
    FILE* f = fopen(dataPath("emu_time").c_str(), "r");
    long long v = 0;
    if (!f || fscanf(f, "%lld", &v) != 1) v = 0;
    if (f) fclose(f);
    emuFixedTime = v;
  }
  struct timespec ts;
  clock_gettime(CLOCK_REALTIME, &ts);
  time_t now = emuFixedTime ? emuFixedTime : ts.tv_sec;
  if (out) *out = now;
  return now;
}

// POST /weather with an Open-Meteo forecast reply (plus an optional "city") replaces
// the network fetch until DELETE /weather; weather.cpp asks through emuWeather().
String emuWeather() { return readFile(dataPath("emu_weather.json").c_str()); }

namespace {

String readFile(const char* path) {
  FILE* f = fopen(path, "rb");
  if (!f) return String();
  String s;
  char buf[1024];
  size_t n;
  while ((n = fread(buf, 1, sizeof(buf), f)) > 0) s.concat(buf, n);
  fclose(f);
  return s;
}

// The supervisor (run.sh) reads this marker to pick the next boot's reset reason.
// exit() rather than _exit(): the core's atexit handler writes the flash image back.
void exitWith(const char* reason) {
  FILE* f = fopen(dataPath("next-reset").c_str(), "w");
  if (f) { fputs(reason, f); fclose(f); }
  exit(0);
}

void sendHeaders(WiFiClient& c, const char* type, size_t length, const String& extra = String(),
                 int status = 200) {
  c.printf("HTTP/1.1 %d %s\r\nContent-Type: %s\r\nContent-Length: %u\r\n"
           "Cache-Control: no-store\r\nAccess-Control-Allow-Origin: *\r\n"
           "Access-Control-Expose-Headers: X-Backlight, X-Frame\r\nConnection: close\r\n",
           status, status == 200 ? "OK" : "Error", type, unsigned(length));
  c.print(extra);
  c.print("\r\n");
}

// The host socket takes part of a large buffer per write; keep going until it is all
// out, for at most 2 s. False when the peer stopped reading (a background browser tab):
// the caller drops it rather than stall the clock.
bool writeAll(WiFiClient& c, const uint8_t* p, size_t left) {
  for (uint32_t start = millis(); left && c.connected() && millis() - start < 2000;) {
    size_t sent = c.write(p, left);
    p += sent; left -= sent;
    if (!sent) delay(1);
  }
  return left == 0;
}

void sendText(WiFiClient& c, int status, const char* type, const String& body) {
  sendHeaders(c, type, body.length(), String(), status);
  writeAll(c, reinterpret_cast<const uint8_t*>(body.c_str()), body.length());
}

String urlDecode(const String& s) {
  String out;
  for (unsigned i = 0; i < s.length(); ++i) {
    if (s[i] == '+') out += ' ';
    else if (s[i] == '%' && i + 2 < s.length()) { out += char(strtol(s.substring(i + 1, i + 3).c_str(), nullptr, 16)); i += 2; }
    else out += s[i];
  }
  return out;
}

String queryArg(const String& query, const char* name) {
  String key = String(name) + "=";
  for (int at = 0; at >= 0 && at < int(query.length());) {
    int end = query.indexOf('&', at);
    String part = query.substring(at, end < 0 ? query.length() : end);
    if (part.startsWith(key)) return urlDecode(part.substring(key.length()));
    at = end < 0 ? -1 : end + 1;
  }
  return String();
}

String jsonEscape(const String& s) {
  String out;
  for (unsigned i = 0; i < s.length(); ++i) {
    char ch = s[i];
    if (ch == '"' || ch == '\\') { out += '\\'; out += ch; }
    else if (uint8_t(ch) < 0x20) { char b[8]; snprintf(b, sizeof(b), "\\u%04x", ch); out += b; }
    else out += ch;
  }
  return out;
}

const char* contentType(const String& path) {
  String p = path; p.toLowerCase();
  if (p.endsWith(".json")) return "application/json; charset=utf-8";
  if (p.endsWith(".txt") || p.endsWith(".css") || p.endsWith(".js") || p.endsWith(".csv")) return "text/plain; charset=utf-8";
  if (p.endsWith(".html") || p.endsWith(".htm")) return "text/plain; charset=utf-8";   // shown, never rendered
  if (p.endsWith(".jpg") || p.endsWith(".jpeg")) return "image/jpeg";
  if (p.endsWith(".png")) return "image/png";
  if (p.endsWith(".gif")) return "image/gif";
  return "application/octet-stream";
}

// GET /fs?path=/dir: one directory of the clock's LittleFS volume, read-only.
void serveDir(WiFiClient& c, String path) {
  if (!path.startsWith("/")) path = "/" + path;
  FSInfo info{};
  if (!LittleFS.info(info)) { sendText(c, 503, "text/plain", "Storage is not mounted"); return; }
  String body = "{\"path\":\"" + jsonEscape(path) + "\",\"total\":" + String(unsigned(info.totalBytes)) +
                ",\"used\":" + String(unsigned(info.usedBytes)) + ",\"entries\":[";
  Dir dir = LittleFS.openDir(path);
  bool first = true;
  while (dir.next()) {
    body += first ? "" : ",";
    first = false;
    body += "{\"name\":\"" + jsonEscape(dir.fileName()) + "\",\"dir\":" + (dir.isDirectory() ? "true" : "false") +
            ",\"size\":" + String(unsigned(dir.isDirectory() ? 0 : dir.fileSize())) + "}";
  }
  body += "]}";
  sendText(c, 200, "application/json", body);
}

// GET /fs/file?path=/x[&download=1]: one file's bytes.
void serveFile(WiFiClient& c, const String& path, bool download) {
  File f = LittleFS.open(path, "r");
  if (!f || f.isDirectory()) { sendText(c, 404, "text/plain", "No such file"); return; }
  String name = path.substring(path.lastIndexOf('/') + 1);
  String extra = String("Content-Disposition: ") + (download ? "attachment" : "inline") +
                 "; filename=\"" + jsonEscape(name) + "\"\r\nX-Content-Type-Options: nosniff\r\n";
  sendHeaders(c, download ? "application/octet-stream" : contentType(path), f.size(), extra);
  uint8_t buf[2048];
  for (size_t n; (n = f.read(buf, sizeof(buf))) > 0;) writeAll(c, buf, n);
  f.close();
}

// ---- WebSocket screen stream (GET /ws)
// Binary messages: [0]=1, [1]=backlight percent, [2..3]=first row, [4..5]=row count
// (little-endian), then the rows as RGB565 little-endian. A new client gets the whole
// frame; after that only changed rows go out, at most ~30 times a second.
constexpr int WS_MAX = 4;
constexpr uint32_t WS_INTERVAL_MS = 33;
WiFiClient wsClients[WS_MAX];
uint16_t wsSent[PANEL_W * PANEL_H];   // what the clients have
int wsSentBacklight = -1;
uint32_t wsLastPush = 0;

// SHA-1 for the handshake only (the core's Hash library needs BearSSL, not built here).
void sha1(const uint8_t* msg, size_t len, uint8_t out[20]) {
  uint32_t h[5] = {0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476, 0xC3D2E1F0};
  size_t total = ((len + 8) / 64 + 1) * 64;
  uint8_t block[64];
  for (size_t off = 0; off < total; off += 64) {
    for (size_t i = 0; i < 64; ++i) {
      size_t at = off + i;
      block[i] = at < len ? msg[at] : at == len ? 0x80 : 0;
      if (at >= total - 8) block[i] = uint8_t(uint64_t(len) * 8 >> (8 * (total - 1 - at)));
    }
    uint32_t w[80];
    for (int i = 0; i < 16; ++i) w[i] = uint32_t(block[4 * i]) << 24 | block[4 * i + 1] << 16 | block[4 * i + 2] << 8 | block[4 * i + 3];
    for (int i = 16; i < 80; ++i) { uint32_t x = w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16]; w[i] = x << 1 | x >> 31; }
    uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];
    for (int i = 0; i < 80; ++i) {
      uint32_t f = i < 20 ? (b & c) | (~b & d) : i < 40 ? b ^ c ^ d : i < 60 ? (b & c) | (b & d) | (c & d) : b ^ c ^ d;
      uint32_t k = i < 20 ? 0x5A827999 : i < 40 ? 0x6ED9EBA1 : i < 60 ? 0x8F1BBCDC : 0xCA62C1D6;
      uint32_t t = (a << 5 | a >> 27) + f + e + k + w[i];
      e = d; d = c; c = b << 30 | b >> 2; b = a; a = t;
    }
    h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e;
  }
  for (int i = 0; i < 20; ++i) out[i] = uint8_t(h[i / 4] >> (24 - 8 * (i % 4)));
}

int backlightPercent() { return backlightRange ? 100 - backlightDuty * 100 / backlightRange : 0; }

bool wsSend(WiFiClient& c, int firstRow, int rows) {
  size_t payload = 6 + size_t(rows) * PANEL_W * 2;
  uint8_t head[10] = {0x82};   // FIN, binary; server frames are not masked
  size_t headLen;
  if (payload < 126) { head[1] = uint8_t(payload); headLen = 2; }
  else if (payload < 65536) { head[1] = 126; head[2] = uint8_t(payload >> 8); head[3] = uint8_t(payload); headLen = 4; }
  else { head[1] = 127; for (int i = 0; i < 8; ++i) head[2 + i] = uint8_t(uint64_t(payload) >> (56 - 8 * i)); headLen = 10; }
  uint8_t info[6] = {1, uint8_t(backlightPercent()), uint8_t(firstRow), uint8_t(firstRow >> 8), uint8_t(rows), uint8_t(rows >> 8)};
  // A half-sent frame would corrupt the stream: a viewer that falls behind is dropped.
  return writeAll(c, head, headLen) && writeAll(c, info, sizeof(info)) &&
         writeAll(c, reinterpret_cast<const uint8_t*>(emuPanel + firstRow * PANEL_W), size_t(rows) * PANEL_W * 2) &&
         c.connected();
}

void wsPush(bool now = false);

void wsAccept(WiFiClient& c, const String& key) {
  int slot = -1;
  for (int i = 0; i < WS_MAX; ++i) if (!wsClients[i] || !wsClients[i].connected()) { slot = i; break; }
  if (slot < 0) { sendText(c, 503, "text/plain", "Too many screen viewers"); c.stop(); return; }
  String accept = key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
  uint8_t digest[20];
  sha1(reinterpret_cast<const uint8_t*>(accept.c_str()), accept.length(), digest);
  c.print("HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
          "Sec-WebSocket-Accept: " + base64::encode(digest, 20, false) + "\r\n\r\n");
  c.setTimeout(100);           // a stalled viewer costs the clock at most a moment
  wsPush(true);                 // bring the other viewers (and wsSent) up to date first,
  if (wsSend(c, 0, PANEL_H)) wsClients[slot] = c;   // so the newcomer's whole frame is the shared baseline
  else c.stop();
}

// Drops viewers that closed (the page sends nothing else), then pushes changed rows.
// With no viewers, wsSent still follows the screen, ready for the next one.
void wsPush(bool now) {
  for (WiFiClient& c : wsClients) {
    if (!c) continue;
    if (!c.connected()) { c.stop(); c = WiFiClient(); continue; }
    while (c.available()) {
      int opcode = c.read() & 0x0f;
      if (opcode == 8) { c.stop(); c = WiFiClient(); break; }
      while (c.available()) c.read();   // anything else from the page is ignored
    }
  }
  if (!now && millis() - wsLastPush < WS_INTERVAL_MS) return;
  wsLastPush = millis();
  const uint16_t* fb = emuPanel;
  int first = -1, last = -1;
  for (int y = 0; y < PANEL_H; ++y)
    if (memcmp(fb + y * PANEL_W, wsSent + y * PANEL_W, PANEL_W * 2) != 0) { if (first < 0) first = y; last = y; }
  if (first < 0 && backlightPercent() == wsSentBacklight) return;
  int rows = first < 0 ? 0 : last - first + 1;
  for (WiFiClient& c : wsClients)
    if (c && !wsSend(c, first < 0 ? 0 : first, rows)) { c.stop(); c = WiFiClient(); }
  if (rows) memcpy(wsSent + first * PANEL_W, fb + first * PANEL_W, size_t(rows) * PANEL_W * 2);
  wsSentBacklight = backlightPercent();
}

// Connections wait here until their request headers are complete. Browsers open
// connections ahead of time and send the request later, so nothing may block on them.
struct Pending { WiFiClient c; String head; uint32_t since = 0; };
constexpr int PENDING_MAX = 8;
constexpr uint32_t PENDING_IDLE_MS = 15000;
Pending pending[PENDING_MAX];

void handleRequest(WiFiClient& c, const String& head);

// The headers, and the body when Content-Length announces one.
int contentLength(const String& head) {
  String lower = head;
  lower.toLowerCase();
  int at = lower.indexOf("\r\ncontent-length:");
  return at < 0 ? 0 : atoi(lower.c_str() + at + 17);
}
bool requestComplete(const String& head) {
  int end = head.indexOf("\r\n\r\n");
  return end >= 0 && int(head.length()) >= end + 4 + contentLength(head);
}

void serveScreen() {
  for (WiFiClient fresh = screenServer.accept(); fresh; fresh = screenServer.accept()) {
    Pending* slot = nullptr;
    for (Pending& p : pending) if (!p.c) { slot = &p; break; }
    if (!slot) { fresh.stop(); continue; }   // full: the browser retries
    slot->c = fresh; slot->head = String(); slot->since = millis();
  }
  for (Pending& p : pending) {
    if (!p.c) continue;
    while (p.c.available() && p.head.length() < 16384) p.head += char(p.c.read());
    if (requestComplete(p.head)) {
      WiFiClient c = p.c;
      String head = p.head;
      p.c = WiFiClient(); p.head = String();
      handleRequest(c, head);
    } else if (!p.c.connected() || millis() - p.since > PENDING_IDLE_MS || p.head.length() >= 8192) {
      p.c.stop(); p.c = WiFiClient(); p.head = String();
    }
  }
}

void handleRequest(WiFiClient& c, const String& head) {
  String line = head.substring(0, head.indexOf("\r\n"));
  String wsKey;   // of the headers, only the WebSocket key matters
  int at = head.indexOf("\r\n") + 2;
  while (at > 1 && at < int(head.length())) {
    int end = head.indexOf("\r\n", at);
    if (end <= at) break;
    String h = head.substring(at, end);
    if (h.substring(0, 18).equalsIgnoreCase("Sec-WebSocket-Key:")) { wsKey = h.substring(18); wsKey.trim(); }
    at = end + 2;
  }
  int sp = line.indexOf(' '), sp2 = line.indexOf(' ', sp + 1);
  String method = line.substring(0, sp), target = line.substring(sp + 1, sp2);
  int q = target.indexOf('?');
  String path = q < 0 ? target : target.substring(0, q), query = q < 0 ? String() : target.substring(q + 1);

  if (path == "/ws" && wsKey.length()) {
    wsAccept(c, wsKey);
    return;   // the connection stays open
  } else if (path == "/fb") {
    sendHeaders(c, "application/octet-stream", sizeof(emuPanel),
                "X-Backlight: " + String(backlightPercent()) + "\r\nX-Frame: " + String(frames) + "\r\n");
    writeAll(c, reinterpret_cast<const uint8_t*>(emuPanel), sizeof(emuPanel));
  } else if (path == "/timing") {
    sendText(c, 200, "application/json", emuTimingJson(queryArg(query, "reset") == "1"));
  } else if (path == "/password") {   // for the screen page's sign-in link
    sendText(c, 200, "text/plain; charset=utf-8", devicePassword);
  } else if (path == "/fs") {
    serveDir(c, queryArg(query, "path"));
  } else if (path == "/fs/file") {
    serveFile(c, queryArg(query, "path"), queryArg(query, "download") == "1");
  } else if (method == "POST" && path == "/time") {
    emuFixedTime = atoll(queryArg(query, "epoch").c_str());
    FILE* f = fopen(dataPath("emu_time").c_str(), "w");
    if (f) { fprintf(f, "%lld\n", (long long)emuFixedTime); fclose(f); }
    sendText(c, 200, "text/plain", "OK");
  } else if ((method == "POST" || method == "DELETE") && path == "/weather") {
    String body = head.substring(head.indexOf("\r\n\r\n") + 4);
    JsonDocument doc;
    if (method == "POST" && (deserializeJson(doc, body) || doc["current"].isNull())) {
      sendText(c, 400, "text/plain", "Expected an Open-Meteo forecast reply (JSON with current and daily)");
    } else {
      if (method == "POST") {
        FILE* f = fopen(dataPath("emu_weather.json").c_str(), "w");
        if (f) { fwrite(body.c_str(), 1, body.length(), f); fclose(f); }
      } else {
        remove(dataPath("emu_weather.json").c_str());
      }
      bool ok = fetchWeather();   // applied now; the faces redraw on their next pass
      sendText(c, ok ? 200 : 502, "text/plain", ok ? String("OK") : wx.error);
    }
  } else if (method == "POST" && (path == "/power" || path == "/reset")) {
    sendHeaders(c, "text/plain", 2);
    c.print("OK");
    c.flush();
    c.stop();
    exitWith(path == "/power" ? "power" : "ext");
  } else {
    const char* page = getenv("EMU_SCREEN_PAGE");
    sendText(c, 200, "text/html; charset=utf-8", readFile(page ? page : "screen.html"));
  }
  c.flush();
  c.stop();
}

// RTC user memory survives restarts but not power cycles: a file the supervisor
// deletes on power-on. 512 bytes, addressed in 4-byte blocks like the chip.
bool rtcAccess(uint8_t block, uint32_t* data, size_t size, bool write) {
  if (block * 4 + size > 512) return false;
  uint8_t mem[512] = {};
  String path = dataPath("rtc.bin");
  if (FILE* f = fopen(path.c_str(), "rb")) { fread(mem, 1, sizeof(mem), f); fclose(f); }
  if (!write) { memcpy(data, mem + block * 4, size); return true; }
  memcpy(mem + block * 4, data, size);
  FILE* f = fopen(path.c_str(), "wb");
  if (!f) return false;
  fwrite(mem, 1, sizeof(mem), f);
  fclose(f);
  return true;
}

}  // namespace

// panel.cpp, while it sleeps off a slow draw: the screen page sees it as it happens.
void emuScreenPush() { wsPush(false); }

// ---- Hardware the core's host emulation leaves out
uint32_t EspClass::random() {   // the chip's hardware RNG: device password, API key
  uint32_t r = 0;
  FILE* f = fopen("/dev/urandom", "rb");
  if (f) { fread(&r, sizeof(r), 1, f); fclose(f); }
  return r;
}

void attachInterrupt(uint8_t, void (*)(void), int) {}   // GPIO4: no button on the unit

bool EspClass::rtcUserMemoryRead(uint32_t offset, uint32_t* data, size_t size) {
  return rtcAccess(offset, data, size, false);
}
bool EspClass::rtcUserMemoryWrite(uint32_t offset, uint32_t* data, size_t size) {
  return rtcAccess(offset, data, size, true);
}

// Reset reason for this boot, from the supervisor: power-on, external, software or crash.
struct rst_info* EspClass::getResetInfoPtr() {
  static rst_info info{};
  const char* r = getenv("EMU_RESET");
  String reason = r ? r : "power";
  info.reason = reason == "soft" ? REASON_SOFT_RESTART
              : reason == "ext"  ? REASON_EXT_SYS_RST
              : reason == "crash" ? REASON_EXCEPTION_RST
              : REASON_DEFAULT_RST;
  return &info;
}

// The backlight PWM (GPIO5, active-low) feeds the brightness shown by screen.html.
void emuAnalogWrite(uint8_t pin, int value) {
  if (pin == 5) backlightDuty = value;
}
void emuAnalogWriteRange(uint32_t range) { backlightRange = int(range); }

void setup() {
  // A new emulator starts with a blank flash image. The firmware never formats on its
  // own (a clock coming from stock has a volume to keep), so format it here, as the
  // factory did. EMU_NO_FORMAT=1 skips this to test the "storage not mounted" path.
  const char* noFormat = getenv("EMU_NO_FORMAT");
  if (!noFormat || strcmp(noFormat, "1") != 0) {
    LittleFSConfig fsConfig;
    fsConfig.setAutoFormat(true);
    LittleFS.setConfig(fsConfig);
    LittleFS.begin();
    LittleFS.end();
  }
  screenServer.begin();
  printf("emulator: firmware on port 80, screen on port 8080 (reset: %s)\n",
         getenv("EMU_RESET") ? getenv("EMU_RESET") : "power");
  fw_setup();
}

void loop() {
  fw_loop();
  ++frames;
  serveScreen();
  wsPush();
}

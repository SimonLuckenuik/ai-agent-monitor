// Smart Weather Clock — from-scratch firmware (ESP8266 + ST7789 240x240)
// Hardware reverse-engineered from the stock GeekMagic/SD clone firmware.
// See docs/specifications/hardware.md and stock-firmware-analysis.md for provenance.
//
// This file owns boot, recovery, WiFi, NTP and the web API. The seven clock faces
// are in faces.cpp; Open-Meteo weather fetching is in weather.cpp.

#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ESP8266HTTPClient.h>
#ifndef SMARTCLOCK_MDNS
#define SMARTCLOCK_MDNS 0   // see serviceMdns()
#endif
#if SMARTCLOCK_MDNS
#include <ESP8266mDNS.h>
#endif
#include <DNSServer.h>
#include <EEPROM.h>
#include <LittleFS.h>
#include <TFT_eSPI.h>
#include <Updater.h>
#include <time.h>
#include <algorithm>
#include "pins.h"
#include "config.h"
#include "web_index.h"   // generated from web/index.html by scripts/build_options.py
#include "firmware_validation.h"
#include "weather.h"
#include "faces.h"
#include "integrations.h"
#include "crash.h"


#define FW_VERSION "1.9.0"   // see CHANGELOG.md
#include "build_info.h"   // FW_BUILD: commit, "+" if uncommitted changes, build time
const char* firmwareVersion() { return FW_VERSION; }

// ----------------------------------------------------------------------------
// The panel with the night tint: while `tint` is set, every colour drawn becomes the
// tint scaled by the colour's brightness, so white turns dark gray (or the chosen
// colour) and the room stays darker. Every face draws through these virtual calls;
// photos (pushImage) keep their colours. `depth` maps a colour once, at the outermost
// call, as TFT_eSPI's shapes call each other.
class NightPanel : public TFT_eSPI {
 public:
  uint16_t tint = 0;   // RGB565; 0 = off
  using TFT_eSPI::drawChar;
  void drawPixel(int32_t x, int32_t y, uint32_t c) override { Nest n(this); TFT_eSPI::drawPixel(x, y, n.ink(c)); }
  void drawFastHLine(int32_t x, int32_t y, int32_t w, uint32_t c) override { Nest n(this); TFT_eSPI::drawFastHLine(x, y, w, n.ink(c)); }
  void drawFastVLine(int32_t x, int32_t y, int32_t h, uint32_t c) override { Nest n(this); TFT_eSPI::drawFastVLine(x, y, h, n.ink(c)); }
  void fillRect(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t c) override { Nest n(this); TFT_eSPI::fillRect(x, y, w, h, n.ink(c)); }
  void pushColor(uint16_t c) override { Nest n(this); TFT_eSPI::pushColor(n.ink(c)); }
  int16_t drawChar(uint16_t ch, int32_t x, int32_t y, uint8_t font) override {
    Nest n(this);
    uint32_t fg = textcolor, bg = textbgcolor;
    textcolor = n.ink(fg); textbgcolor = n.ink(bg);
    int16_t w = TFT_eSPI::drawChar(ch, x, y, font);
    textcolor = fg; textbgcolor = bg;
    return w;
  }

 private:
  int depth = 0;
  struct Nest {
    NightPanel* p;
    bool outer;
    explicit Nest(NightPanel* p) : p(p), outer(p->depth++ == 0) {}
    ~Nest() { --p->depth; }
    uint16_t ink(uint32_t c) const { return outer ? tintColor(c, p->tint) : c; }
  };
};
// Construct the driver only during optional startup: even its constructor touches GPIO.
NightPanel& nightPanel() {
  static NightPanel panel;
  return panel;
}
TFT_eSPI& clockDisplay() { return nightPanel(); }
uint16_t panelTint() { return nightPanel().tint; }

uint16_t tintColor(uint32_t c, uint16_t t) {
  if (!t) return c;
  // Brightness of the RGB565 colour, 0..16022 (red and blue doubled to 6 bits).
  uint32_t l = ((c >> 11) & 31) * 154 + ((c >> 5) & 63) * 150 + (c & 31) * 58;
  return uint16_t(((t >> 11) * l / 16022) << 11 | (((t >> 5) & 63) * l / 16022) << 5 | (t & 31) * l / 16022);
}
ESP8266WebServer server(80);
DNSServer dns;
Config cfg;

// Optional build-time WiFi (secrets.ini via scripts/build_options.py). Used only when
// nothing is saved, and in recovery, so the clock still reaches the LAN without storage.
#include "wifi_secrets.h"
#ifndef DEFAULT_WIFI_SSID
#define DEFAULT_WIFI_SSID ""
#endif
#ifndef DEFAULT_WIFI_PASS
#define DEFAULT_WIFI_PASS ""
#endif
// "agentmonitor-1a2b3c": unique per unit, used for mDNS and as the DHCP hostname so
// the router can list the clock by name (mDNS does not cross subnets).
String deviceHost() {
  char name[24];
  snprintf(name, sizeof(name), "agentmonitor-%06x", (unsigned)(ESP.getChipId() & 0xffffff));
  return name;
}

enum Mode { MODE_AP, MODE_RUN } mode = MODE_AP;
bool recoveryMode = false, recoveryDisplay = true, recoveryLinkShown = false;
bool storageReady = false, storageMountFailed = false, displayReady = false;
bool optionalStarted = false, bootHealthy = false, powerCyclesCleared = false;
bool safeShown = false;   // safe mode's screen is up (drawSafeScreen)
uint32_t bootStarted = 0;
// RTC user memory starts after eboot's reserved first 128 bytes (word offset 32).
struct BootMarker { uint32_t magic, state; };
constexpr uint32_t BOOT_MAGIC = 0x53434c4b;
// STARTING: a reset during optional startup forces recovery next boot.
// RECOVERY_DISPLAY: a reset while recovery starts the panel skips the panel next boot.
enum BootState : uint32_t { BOOT_CLEAN = 0, BOOT_STARTING = 1, BOOT_RECOVERY_DISPLAY = 2 };
void markBoot(BootState state) {
  BootMarker marker{BOOT_MAGIC, state};
  ESP.rtcUserMemoryWrite(32, reinterpret_cast<uint32_t*>(&marker), sizeof(marker));
}

// Three consecutive boots that each end within 10 s (quick power cycles, or a crash
// loop) force recovery. RTC memory is lost on power-off, so the count lives in the
// EEPROM sector. The whole sector is loaded so stock-firmware bytes in it survive, and
// EEPROM.end() only erases/writes when a byte actually changed.
constexpr int CYCLE_ADDR = 0xff0;
constexpr uint32_t CYCLE_MAGIC = 0x53435943;
constexpr uint8_t CYCLE_LIMIT = 3;
constexpr uint32_t CYCLE_WINDOW_MS = 10000;
void writeCycles(uint8_t count) {
  for (int i = 0; i < 4; ++i) EEPROM.write(CYCLE_ADDR + i, uint8_t(CYCLE_MAGIC >> (8 * i)));
  EEPROM.write(CYCLE_ADDR + 4, count);
}
uint8_t readCycles() {
  uint32_t magic = 0;
  for (int i = 0; i < 4; ++i) magic |= uint32_t(EEPROM.read(CYCLE_ADDR + i)) << (8 * i);
  return magic == CYCLE_MAGIC ? EEPROM.read(CYCLE_ADDR + 4) : 0;
}
bool countPowerCycle() {   // true when this boot completes the recovery sequence
  EEPROM.begin(SPI_FLASH_SEC_SIZE);
  uint8_t count = std::min<uint8_t>(readCycles(), CYCLE_LIMIT) + 1;
  bool trigger = count >= CYCLE_LIMIT;
  writeCycles(trigger ? 0 : count);
  EEPROM.end();
  return trigger;
}
void clearPowerCycles() {
  EEPROM.begin(SPI_FLASH_SEC_SIZE);
  if (readCycles() != 0) writeCycles(0);
  EEPROM.end();
}

// Every deliberate reboot clears both recovery triggers so it cannot be mistaken
// for a crash or a power-cycle sequence.
void safeRestart() {
  markBoot(BOOT_CLEAN);
  clearPowerCycles();
  delay(500);
  ESP.restart();
}

// ---------------------------- Device password -------------------------------
// One password protects the setup network (WPA2) and the settings page. It lives in
// the EEPROM sector, not LittleFS, so recovery can still read it. Generated on first
// boot from the hardware RNG; shown on the setup screen, the recovery screen, and on
// request from the login page ("Show on clock"), so knowing it needs sight of the clock.
constexpr int PASS_ADDR = 0xfc0;            // 4-byte magic + up to 31 chars + NUL
constexpr uint32_t PASS_MAGIC = 0x53435057;
constexpr size_t PASS_MIN = 8, PASS_MAX = 31;
String devicePassword;

bool validPassword(const String& p) {
  if (p.length() < PASS_MIN || p.length() > PASS_MAX) return false;
  for (unsigned i = 0; i < p.length(); ++i) if (p[i] < 0x21 || p[i] > 0x7e) return false;   // printable, no spaces
  return true;
}

bool storePassword(const String& p) {
  EEPROM.begin(SPI_FLASH_SEC_SIZE);
  for (int i = 0; i < 4; ++i) EEPROM.write(PASS_ADDR + i, uint8_t(PASS_MAGIC >> (8 * i)));
  for (size_t i = 0; i <= PASS_MAX; ++i) EEPROM.write(PASS_ADDR + 4 + i, i < p.length() ? p[i] : 0);
  return EEPROM.end();
}

void loadPassword() {
  EEPROM.begin(SPI_FLASH_SEC_SIZE);
  uint32_t magic = 0;
  for (int i = 0; i < 4; ++i) magic |= uint32_t(EEPROM.read(PASS_ADDR + i)) << (8 * i);
  String p;
  if (magic == PASS_MAGIC)
    for (size_t i = 0; i < PASS_MAX; ++i) { char c = EEPROM.read(PASS_ADDR + 4 + i); if (!c) break; p += c; }
  EEPROM.end();
  if (validPassword(p)) { devicePassword = p; return; }
  // 10 characters from a 31-letter alphabet without 0/o, 1/i/l: ~49 bits.
  static const char alphabet[] = "23456789abcdefghjkmnpqrstuvwxyz";
  p = "";
  for (int i = 0; i < 10; ++i) p += alphabet[ESP.random() % (sizeof(alphabet) - 1)];
  storePassword(p);
  devicePassword = p;
}

bool requireAuth();          // sign-in guard, defined with the web API below
bool requestAuthorized();
bool wxRefresh = true;    // fetch at once: at boot, and when the city changes
// Optional work waits for headroom instead of risking an allocation failure.
constexpr uint32_t MDNS_MIN_HEAP = 28000, WEATHER_MIN_HEAP = 8192;   // a fetch took 6.1 KB in the emulator (spec 024); /status weatherHeap
uint32_t heapLow = UINT32_MAX;   // lowest free heap seen, for /status
// Face draw times in ms, by face id, for /status: the latest draw over 1 ms and the
// slowest. The line font and paths are float maths, and the chip has no FPU.
uint16_t drawLast[FACE_MESSAGE + 1], drawMax[FACE_MESSAGE + 1];
uint32_t faceHoldUntil = 0;   // keep a status screen (e.g. the IP) up until then
bool brightnessDirty = false;  // settings changed: re-evaluate night dimming now
bool backlightOff = false;     // switched off over MQTT (not saved)

volatile bool buttonPressed = false;
IRAM_ATTR void onButton() { buttonPressed = true; }

// ---------------------------- Backlight (active-low PWM) ---------------------
void setBacklight(int percent) {
  if (!displayReady) return;
  percent = constrain(percent, 0, 100);
  // Stock: analogWrite(5, 1023 - brightness*10). Active-low => low duty = bright.
  int duty = BL_PWM_RANGE - (percent * BL_PWM_RANGE) / 100;
  analogWrite(PIN_BACKLIGHT, duty);
}

bool isNight(const struct tm& t) {
  if (!cfg.nightMode) return false;
  if (cfg.nightAuto && wx.sunrise >= 0 && wx.sunset > wx.sunrise) {   // sunset to sunrise
    int m = t.tm_hour * 60 + t.tm_min;
    return m < wx.sunrise || m >= wx.sunset;
  }
  int h = t.tm_hour;
  if (cfg.startHour <= cfg.stopHour) return h >= cfg.startHour && h < cfg.stopHour;
  return h >= cfg.startHour || h < cfg.stopHour;   // wraps midnight
}

// ---------------------------- Display ---------------------------------------
void setGamma(int index);

void displayBegin() {
  displayReady = true;
  pinMode(PIN_BACKLIGHT, OUTPUT);
  analogWriteRange(BL_PWM_RANGE);
  setBacklight(0);                   // off during init (avoid flash of noise)
  clockDisplay().init();
  // The controller turns the picture (MADCTL): stock boots rotation 0, offset (0,0);
  // TFT_eSPI adds the 80-row offset a 240x240 glass on 240x320 RAM needs at 180 and 270.
  clockDisplay().setRotation(cfg.orientation);
  setGamma(cfg.gamma);
  clockDisplay().fillScreen(TFT_BLACK);
  setBacklight(cfg.brightness);
}

// The panel's gamma curve (GAMSET: one of four built-in curves).
void setGamma(int index) {
  static int now = -1;
  if (!displayReady || index == now) return;
  now = index;
  clockDisplay().writecommand(0x26);
  clockDisplay().writedata(1 << index);
}

// The panel itself off (DISPOFF, then sleep), not only the backlight; on again it
// wakes (SLPOUT, DISPON) and the face is redrawn.
bool panelOff = false;
void setPanelOn(bool on) {
  if (!displayReady || on == !panelOff) return;
  panelOff = !on;
  TFT_eSPI& d = clockDisplay();
  if (!on) { d.writecommand(0x28); d.writecommand(0x10); return; }
  d.writecommand(0x11);
  delay(10);   // after SLPOUT, before the next command
  d.writecommand(0x29);
  faceInvalidate();
}

void centerText(const String& s, int y, uint8_t font, uint16_t color) {
  clockDisplay().setTextColor(color, TFT_BLACK);
  clockDisplay().setTextDatum(MC_DATUM);
  clockDisplay().drawString(s, TFT_W / 2, y, font);
}

// ---------------------------- Faces and weather schedule --------------------
uint32_t faceSince = 0;   // when the current face was chosen (rotation timer)
uint32_t faceTurnSec = 0; // the shown face's own time on screen; 0: themeInterval
bool nightNow = false;    // inside the night hours, as of the last minute
bool nightActive() { return nightNow; }

// ---- Faces as entries: "#<id>" a built-in face, else a custom face's name
// (docs/specifications/settings.md).
// item in a comma list ("a,b,c").
bool inList(const String& list, const char* item) {
  size_t n = strlen(item);
  for (const char* p = list.c_str(); *p;) {
    const char* e = strchr(p, ',');
    size_t len = e ? size_t(e - p) : strlen(p);
    if (len == n && !strncmp(p, item, n)) return true;
    if (!e) break;
    p = e + 1;
  }
  return false;
}

// An entry's own time on screen from cfg.turns ("#7=20,calendar=45"), or 0.
uint32_t turnOf(const String& entry) {
  for (const char* p = cfg.turns.c_str(); *p;) {
    const char* e = strchr(p, ','), *eq = strchr(p, '=');
    if (eq && (!e || eq < e) && size_t(eq - p) == entry.length() && !strncmp(p, entry.c_str(), entry.length()))
      return constrain(atoi(eq + 1), 3, 3600);
    if (!e) break;
    p = e + 1;
  }
  return 0;
}

int entryFace(const String& e) { return e[0] == '#' ? e.substring(1).toInt() : FACE_CUSTOM; }

// Every face in rotation order: cfg.order's entries that exist, then the built-in
// faces it lacks, then the custom faces it lacks (a new face joins at the end).
constexpr int FACE_ENTRIES = FACE_COUNT + FACES_MAX;
int faceEntries(String* out) {
  String customs[FACES_MAX];
  int nc = cfg.facesApi && !crashLevel() ? customFaceNames(customs, FACES_MAX) : 0, n = 0;   // off after a crash loop
  auto add = [&](const String& e) {
    for (int i = 0; i < n; ++i) if (out[i] == e) return;
    if (e[0] == '#') {
      int id = e.substring(1).toInt();
      if (id < 0 || id >= FACE_COUNT || id == FACE_FLIP || e != "#" + String(id)) return;
    } else {
      bool known = false;
      for (int i = 0; i < nc && !known; ++i) known = customs[i] == e;
      if (!known) return;
    }
    if (n < FACE_ENTRIES) out[n++] = e;
  };
  for (const char* p = cfg.order.c_str(); *p;) {
    const char* e = strchr(p, ',');
    add(e ? String(p).substring(0, e - p) : String(p));
    if (!e) break;
    p = e + 1;
  }
  for (int id = 0; id < FACE_COUNT; ++id) add("#" + String(id));
  for (int i = 0; i < nc; ++i) add(customs[i]);
  return n;
}

bool entryOn(const String& e) {
  return e[0] == '#' ? (cfg.faceMask >> entryFace(e) & 1) : !inList(cfg.customOff, e.c_str());
}

// The night face, while night mode shows one: an entry that exists. Custom faces are
// checked once a minute (and when the faces change), not every frame.
bool nightTile() {
  if (!nightNow || cfg.nightFace.isEmpty()) return false;
  if (cfg.nightFace[0] == '#') { int id = entryFace(cfg.nightFace); return id >= 0 && id < FACE_COUNT && id != FACE_FLIP; }
  static String seen;
  static uint32_t seenVer = 0, seenAt = 0;
  static bool ok = false;
  if (seen != cfg.nightFace || seenVer != customFacesVersion() || millis() - seenAt > 60000) {
    seen = cfg.nightFace; seenVer = customFacesVersion(); seenAt = millis();
    ok = customFaceExists(seen);
  }
  return cfg.facesApi && ok && !crashLevel();
}

// Moves to the next face: the faces in rotation order (in the rotation, when rotating,
// with something to show, and for a custom face while its "when" holds), then each
// active agent message, one turn each.
void advanceFace(bool rotation) {
  faceTurnSec = 0;
  String list[FACE_ENTRIES];
  int n = faceEntries(list), total = n + MESSAGE_MAX, cur = n - 1;
  String now = cfg.theme == FACE_CUSTOM ? cfg.customFace : "#" + String(cfg.theme);
  for (int i = 0; i < n; ++i) if (list[i] == now) cur = i;
  if (cfg.theme == FACE_MESSAGE) cur = n + messageShowing();
  for (int step = 1; step <= total; ++step) {
    int pos = (cur + step) % total;
    if (pos >= n) {
      int slot = pos - n;
      if (messageActive(slot)) { cfg.theme = FACE_MESSAGE; messageShowSlot(slot); return; }
      continue;
    }
    const String& e = list[pos];
    if (rotation && !entryOn(e)) continue;
    int face = entryFace(e);
    if (face == FACE_CUSTOM) {
      uint32_t secs = 0;
      if (customFaceDue(e, &secs) || !rotation) {
        cfg.theme = FACE_CUSTOM; cfg.customFace = e;
        faceTurnSec = turnOf(e) ? turnOf(e) : secs;
        return;
      }
      continue;
    }
    if (faceAvailable(face)) { cfg.theme = face; faceTurnSec = turnOf(e); return; }
  }
}

// One Open-Meteo request every 15 min (its current data is 15-minute) fills both
// the weather and the forecast. Failures retry after a minute; low heap waits 30 s.
uint32_t wxNext = 0;     // when the next scheduled fetch is due
uint32_t linkUpAt = 0;   // when the station link last came up

// Fetches now and reschedules the next fetch from its result.
bool syncWeather() {
  bool ok = fetchWeather();
  wxNext = millis() + (ok ? 15UL : 1UL) * 60 * 1000;
  return ok;
}

void serviceWeather() {
  uint32_t now = millis();
  // Fetches right after the link came up failed on the device (2026-10-05) while
  // a later one worked, so give DHCP/DNS/ARP a few seconds first.
  if (now - linkUpAt < 5000) return;
  if (wxRefresh) { wxNext = now; wxRefresh = false; }
  if ((int32_t)(now - wxNext) < 0) return;
  // Short of memory: a custom face on screen gives its document back for the fetch
  // (loaded again when drawn); still short, try again in 30 s and say why.
  if (ESP.getFreeHeap() < WEATHER_MIN_HEAP) customRelease();
  if (ESP.getFreeHeap() < WEATHER_MIN_HEAP) { wx.error = F("Waiting for memory"); wxNext = now + 30000; return; }
  syncWeather();
}

// "Sync now" on the settings page: fetch immediately and report the outcome.
// Limited to one request per 10 s so the button cannot hammer the service.
void handleWeatherSync() {
  if (!requireAuth()) return;
  if (recoveryMode || !storageReady) { server.send(503, "text/plain", "Unavailable in recovery"); return; }
  if (WiFi.status() != WL_CONNECTED) { server.send(503, "text/plain", "Not connected to WiFi"); return; }
  if (wx.lastFetch && millis() - wx.lastFetch < 10000) { server.send(429, "text/plain", "Just synced; try again in a few seconds"); return; }
  if (ESP.getFreeHeap() < WEATHER_MIN_HEAP) customRelease();
  if (ESP.getFreeHeap() < WEATHER_MIN_HEAP) { server.send(503, "text/plain", "Low on memory; try again shortly"); return; }
  bool ok = syncWeather();
  server.send(ok ? 200 : 502, "text/plain", ok ? String("OK") : (wx.error.length() ? wx.error : String("Weather update failed")));
}

// ---------------------------- Time ------------------------------------------
// A POSIX TZ rule lets libc apply daylight saving; a bare offset (stock) cannot.
void applyTime() {
  if (!cfg.tz.isEmpty()) configTime(cfg.tz.c_str(), cfg.ntp.c_str());
  else configTime(cfg.timezoneMin * 60, 0, cfg.ntp.c_str());
}

// ---------------------------- Sign-in ---------------------------------------
// A session is a random token in a cookie (HttpOnly, SameSite=Strict), kept in RAM:
// a reboot signs everyone out. Recovery mode needs no sign-in, so a lost password
// can never lock the owner out; entering it takes power cycling the clock.
constexpr int SESSIONS = 4;
String sessions[SESSIONS];
uint8_t loginFails = 0;
uint32_t loginLockedUntil = 0, revealUntil = 0, revealAt = 0;

// Ends "Show on clock" now: the loop sees the deadline has passed and restores the
// face (or the setup/recovery screen). 0 means "not showing", so never store 0.
void endReveal() {
  if (revealUntil) revealUntil = millis() ? millis() : 1;
}

String requestToken() {
  String c = server.header("Cookie");
  int i = c.indexOf("sc=");
  if (i < 0) return "";
  int end = c.indexOf(';', i);
  return c.substring(i + 3, end < 0 ? c.length() : end);
}

bool sameSecret(const String& a, const String& b) {   // constant time for equal lengths
  if (a.length() != b.length()) return false;
  uint8_t diff = 0;
  for (unsigned i = 0; i < a.length(); ++i) diff |= a[i] ^ b[i];
  return diff == 0;
}

bool requestAuthorized() {
  if (recoveryMode) return true;
  String t = requestToken();
  if (t.length() != 32) return false;
  for (const String& s : sessions) if (sameSecret(s, t)) return true;
  return false;
}

// Guard for handlers: answers 401 and returns false when not signed in.
bool requireAuth() {
  if (requestAuthorized()) return true;
  server.send(401, "text/plain", "Sign in first");
  return false;
}

void handleLogin() {
  if (millis() < loginLockedUntil) { server.send(429, "text/plain", "Too many attempts; wait a minute"); return; }
  if (!sameSecret(server.arg("password"), devicePassword)) {
    delay(800);   // slows guessing; with the lockout, ~5 tries a minute
    if (++loginFails >= 5) { loginFails = 0; loginLockedUntil = millis() + 60000; }
    server.send(403, "text/plain", "Wrong password");
    return;
  }
  loginFails = 0;
  endReveal();   // signed in: the password has done its job, take it off the clock
  char token[33];
  for (int i = 0; i < 4; ++i) snprintf(token + i * 8, 9, "%08x", (unsigned)ESP.random());
  static int next = 0;
  sessions[next] = token;
  next = (next + 1) % SESSIONS;
  server.sendHeader("Set-Cookie", String("sc=") + token + "; Path=/; HttpOnly; SameSite=Strict");
  server.send(200, "text/plain", "OK");
}

void handleLogout() {
  String t = requestToken();
  for (String& s : sessions) if (t.length() && sameSecret(s, t)) s = "";
  server.sendHeader("Set-Cookie", "sc=; Path=/; Max-Age=0; SameSite=Strict");
  server.send(200, "text/plain", "OK");
}

void drawPasswordScreen();
// "Show on clock": puts the password on the panel for 60 s (once per 10 s).
void handleReveal() {
  if (!displayReady) { server.send(503, "text/plain", "The screen is not available"); return; }
  if (revealAt && millis() - revealAt < 10000) { server.send(429, "text/plain", "Already shown"); return; }
  revealAt = millis();
  revealUntil = millis() + 60000;
  drawPasswordScreen();
  server.send(200, "text/plain", "OK");
}

void restartAP();
void handlePasswordChange() {
  if (!requireAuth()) return;
  String p = server.arg("password");
  if (!validPassword(p)) { server.send(400, "text/plain", "Use 8 to 31 characters, no spaces"); return; }
  if (!storePassword(p)) { server.send(500, "text/plain", "Could not save the password"); return; }
  devicePassword = p;
  String keep = requestToken();   // other sessions end; this one stays signed in
  for (String& s : sessions) if (!sameSecret(s, keep)) s = "";
  server.send(200, "text/plain", "OK");
  restartAP();   // the setup network now uses the new password
}

// ---------------------------- Web API (stock-compatible) --------------------
String configJson() {
  JsonDocument doc;
  doc["theme"] = cfg.theme; doc["celsius"] = cfg.celsius; doc["hour12"] = cfg.hour12; doc["lang"] = cfg.lang;
  doc["mile"] = cfg.mile;   doc["city"] = cfg.city;       doc["brightness"] = cfg.brightness;
  doc["orientation"] = cfg.orientation * 90;
  doc["gamma"] = Config::GAMMAS[cfg.gamma]; doc["nightgamma"] = cfg.nightGamma < 0 ? "" : Config::GAMMAS[cfg.nightGamma];
  doc["nightoff"] = cfg.nightOff;
  doc["location"] = cfg.geoFor == cfg.city ? cfg.geoName : "";
  doc["nightmode"] = cfg.nightMode; doc["starttime"] = cfg.startHour; doc["stoptime"] = cfg.stopHour;
  doc["nightbrightness"] = cfg.nightBrightness; doc["nightface"] = cfg.nightFace; doc["ntp"] = cfg.ntp;
  doc["nighttint"] = cfg.nightTint; doc["nightcolor"] = cfg.nightColor;
  doc["timezone"] = cfg.timezoneMin; doc["tz"] = cfg.tz; doc["themeInterval"] = cfg.themeInterval;
  doc["faces"] = cfg.faceMask; doc["nightauto"] = cfg.nightAuto;
  {   // the faces in rotation order, and the custom faces' own rules
    String list[FACE_ENTRIES], order;
    int n = faceEntries(list);
    JsonArray customs = doc["customs"].to<JsonArray>();
    for (int i = 0; i < n; ++i) {
      if (i) order += ',';
      order += list[i];
      if (list[i][0] == '#') continue;
      JsonObject c = customs.add<JsonObject>();
      bool when = false;
      uint32_t secs = 0;
      customFaceRule(list[i], &when, &secs);
      c["name"] = list[i]; c["when"] = when; c["duration"] = secs;
    }
    doc["order"] = order;
  }
  doc["customoff"] = cfg.customOff; doc["turns"] = cfg.turns; doc["customface"] = cfg.customFace;
  doc["photointerval"] = cfg.photoInterval; doc["photoclock"] = cfg.photoClock;
  doc["facesapi"] = cfg.facesApi; doc["hacompat"] = cfg.haCompat; doc["mqtt"] = cfg.mqttOn;
  doc["mqtthost"] = cfg.mqttHost; doc["mqttport"] = cfg.mqttPort; doc["mqttuser"] = cfg.mqttUser;
  doc["mqttbase"] = cfg.mqttBase;   // the MQTT password and API key are never sent back
  doc["type"] = "smartclock-oss-settings"; doc["version"] = FW_VERSION;
  doc["palettes"] = serialized(cfg.palettes); doc["roles"] = serialized(cfg.roles);
  doc["palette"] = cfg.palette; doc["nightpalette"] = cfg.nightPalette;
  // NOTE: stock leaked ssid/password/weatherkey here in cleartext. We deliberately
  // DO NOT expose secrets via /config. (See security notes in docs.) Weather comes
  // from Open-Meteo, so stock's "weatherkey" setting is not accepted.
  String out; serializeJson(doc, out); return out;
}

// ---- Palettes and roles (docs/specifications/settings.md)
bool Config::setColor(const char* pal, const char* name, uint16_t c) {
  JsonDocument doc;
  if (deserializeJson(doc, palettes)) return false;
  char hex[8];
  snprintf(hex, sizeof(hex), "#%02x%02x%02x", (c >> 11) * 255 / 31, ((c >> 5) & 63) * 255 / 63, (c & 31) * 255 / 31);
  doc[pal][name] = hex;
  palettes = "";
  serializeJson(doc, palettes);
  colorsRev++;
  return true;
}

bool colorName(const char* s) {   // 1-15 of A-Z a-z 0-9 _
  size_t n = strlen(s);
  if (n < 1 || n > 15) return false;
  for (; *s; ++s) if (!isalnum((unsigned char)*s) && *s != '_') return false;
  return true;
}

bool paletteExists(const String& name) {
  JsonDocument doc;
  return !deserializeJson(doc, cfg.palettes) && doc[name].is<JsonObject>();
}

// Every palette at once: {"day": {"accent1": "#ffa826", ...}, ...}.
String setPalettes(const String& val) {
  JsonDocument doc;
  if (val.length() > 2048 || deserializeJson(doc, val) || !doc.is<JsonObject>()) return F("Expected a JSON object of palettes");
  JsonObject all = doc.as<JsonObject>();
  if (!all["day"].is<JsonObject>()) return F("The day palette is missing");
  if (all.size() > 8) return F("At most 8 palettes");
  for (JsonPair p : all) {
    if (!colorName(p.key().c_str()) || !p.value().is<JsonObject>()) return "Bad palette name \"" + String(p.key().c_str()) + "\": 1-15 of A-Z a-z 0-9 _";
    if (p.value().size() > 16) return F("At most 16 colours a palette");
    for (JsonPair c : p.value().as<JsonObject>()) {
      const char* v = c.value();
      if (!colorName(c.key().c_str())) return "Bad colour name \"" + String(c.key().c_str()) + "\": 1-15 of A-Z a-z 0-9 _";
      if (!v || *v != '#' || (strlen(v) != 7 && strlen(v) != 4) || strspn(v + 1, "0123456789abcdefABCDEF") != strlen(v) - 1)
        return "Bad colour \"" + String(c.key().c_str()) + "\": #rrggbb";
    }
  }
  cfg.palettes = "";
  serializeJson(doc, cfg.palettes);
  if (!all[cfg.palette].is<JsonObject>()) cfg.palette = "day";
  if (cfg.nightPalette.length() && !all[cfg.nightPalette].is<JsonObject>()) cfg.nightPalette = "";
  cfg.colorsRev++;
  return "";
}

// The roles' bindings that differ from the defaults: {"time": "accent1", ...}.
String setRoles(const String& val) {
  JsonDocument doc;
  if (val.length() > 1200 || deserializeJson(doc, val) || !doc.is<JsonObject>()) return F("Expected a JSON object of roles");
  for (JsonPair r : doc.as<JsonObject>()) {
    const char* v = r.value();
    if (!roleKnown(r.key().c_str())) return "Unknown role \"" + String(r.key().c_str()) + "\"";
    if (!v || strlen(v) > 120) return F("A role is a colour name, @role, #rrggbb or an expression (120 characters at most)");
  }
  cfg.roles = "";
  serializeJson(doc, cfg.roles);
  cfg.colorsRev++;
  return "";
}

// Applies one setting to cfg (not saved). Shared by /api/set and settings restore.
// Returns "" on success, otherwise the reason ("Unknown Key" for unknown keys).
bool timeChanged = false;
String applySetting(const String& key, const String& val) {
  if      (key == "city")       { if (val != cfg.city) { cfg.city = val; cfg.geoFor = ""; wxRefresh = true; } }   // re-geocode
  else if (key == "ntp")        { cfg.ntp = val; timeChanged = true; }
  // Stock clients send a minute offset; honour it as a fixed offset (no DST rule).
  else if (key == "timezone")   { cfg.timezoneMin = val.toInt(); cfg.tz = ""; timeChanged = true; }
  else if (key == "tz") {
    if (val.length() > 63) return F("TZ rule too long");
    cfg.tz = val; timeChanged = true;
  }
  else if (key == "theme")      { cfg.theme = constrain(val.toInt(), 0, FACE_COUNT - 1); faceSince = millis(); faceTurnSec = 0; }
  else if (key == "customface") {
    if (!customFaceExists(val)) return F("No such face");
    cfg.customFace = val; cfg.theme = FACE_CUSTOM; faceSince = millis(); faceTurnSec = 0;
  }
  else if (key == "themeInterval") cfg.themeInterval = constrain(val.toInt(), 0, 3600);
  else if (key == "brightness") { cfg.brightness = constrain(val.toInt(), 0, 100); setBacklight(cfg.brightness); }
  else if (key == "gamma" || key == "nightgamma") {   // a curve: 2.2, 1.8, 2.5 or 1.0; "" at night: the day's
    int g = Config::gammaIndex(val);
    if (g < 0 && !(key == "nightgamma" && val.isEmpty())) return F("Gamma: 2.2, 1.8, 2.5 or 1.0");
    (key == "gamma" ? cfg.gamma : cfg.nightGamma) = g;
  }
  else if (key == "nightoff")   cfg.nightOff = (val == "1" || val == "true");
  else if (key == "orientation") {   // degrees, clockwise
    int deg = val.toInt();
    if (deg % 90) return F("Orientation: 0, 90, 180 or 270");
    cfg.orientation = (deg / 90 % 4 + 4) % 4;
    if (displayReady) { clockDisplay().setRotation(cfg.orientation); clockDisplay().fillScreen(TFT_BLACK); }
  }
  else if (key == "celsius")    cfg.celsius = (val == "1" || val == "true");
  else if (key == "hour12")     cfg.hour12 = (val == "1" || val == "true");
  else if (key == "lang")       cfg.lang = val == "fr" ? "fr" : "en";
  else if (key == "nightmode")  cfg.nightMode = (val == "1" || val == "true");
  else if (key == "nightbrightness") cfg.nightBrightness = constrain(val.toInt(), 0, 100);
  else if (key == "nightface") {   // an entry; "" or -1 the rotation; a number a built-in id
    if (val.isEmpty() || val == "-1") cfg.nightFace = "";
    else if (isdigit((unsigned char)val[0]) || val[0] == '#') {
      int id = val.substring(val[0] == '#').toInt();
      if (id < 0 || id >= FACE_COUNT || id == FACE_FLIP) return F("Unknown face");
      cfg.nightFace = "#" + String(id);
    } else if (val.length() <= 20 && strspn(val.c_str(), "abcdefghijklmnopqrstuvwxyz0123456789_-") == val.length()) cfg.nightFace = val;
    else return F("Unknown face");
  }
  else if (key == "nighttint")  cfg.nightTint = (val == "1" || val == "true");
  else if (key == "nightcolor") cfg.nightColor = (uint16_t)val.toInt();
  else if (key == "starttime")  cfg.startHour = constrain(val.toInt(), 0, 23);
  else if (key == "stoptime")   cfg.stopHour = constrain(val.toInt(), 0, 23);
  else if (key == "color1" || key == "color2" || key == "color3") {   // older settings: the day accents
    cfg.setColor("day", key == "color1" ? "accent1" : key == "color2" ? "accent2" : "accent3", (uint16_t)val.toInt());
  }
  else if (key == "palettes")   return setPalettes(val);
  else if (key == "palette" || key == "nightpalette") {
    bool night = key == "nightpalette";
    if (!(night && val.isEmpty()) && !paletteExists(val)) return F("No such palette");
    (night ? cfg.nightPalette : cfg.palette) = val;
    cfg.colorsRev++;
  }
  else if (key == "roles")      return setRoles(val);
  else if (key == "faces")      cfg.faceMask = constrain(val.toInt(), 0, (1 << FACE_COUNT) - 1);
  else if (key == "faceorder")  cfg.order = Config::entriesOf(val);
  else if (key == "order" || key == "customoff" || key == "turns") {
    if (val.length() > 1200) return F("Too long");
    (key == "order" ? cfg.order : key == "customoff" ? cfg.customOff : cfg.turns) = val;
  }
  else if (key == "mile")       cfg.mile = (val == "1" || val == "true");
  else if (key == "nightauto")  cfg.nightAuto = (val == "1" || val == "true");
  else if (key == "lcd_brightness") cfg.brightness = constrain(val.toInt(), 0, 100);   // stock key
  else if (key == "photointerval") cfg.photoInterval = constrain(val.toInt(), 1, 3600);
  else if (key == "photoclock") cfg.photoClock = (val == "1" || val == "true");
  else if (key == "facesapi")   { cfg.facesApi = (val == "1" || val == "true"); integrationsSettingsChanged(); }
  else if (key == "hacompat")   cfg.haCompat = (val == "1" || val == "true");
  else if (key == "mqtt")       { cfg.mqttOn = (val == "1" || val == "true"); integrationsSettingsChanged(); }
  else if (key == "mqtthost")   { cfg.mqttHost = val; integrationsSettingsChanged(); }
  else if (key == "mqttport")   { cfg.mqttPort = constrain(val.toInt(), 1, 65535); integrationsSettingsChanged(); }
  else if (key == "mqttuser")   { cfg.mqttUser = val; integrationsSettingsChanged(); }
  else if (key == "mqttpass")   { cfg.mqttPass = val; integrationsSettingsChanged(); }
  else if (key == "mqttbase")   { cfg.mqttBase = val; integrationsSettingsChanged(); }
  else return F("Unknown Key");
  return "";
}

// After settings change: apply time rules and redraw (colours, units, formats, faces).
void settingsApplied() {
  if (timeChanged) { applyTime(); timeChanged = false; }
  brightnessDirty = true;   // the loop applies day or night brightness
  if (mode != MODE_RUN) setBacklight(cfg.brightness);
  faceInvalidate();
}

bool settingsWritable() {
  if (storageReady && !recoveryMode) return true;
  server.send(503, "text/plain", "Settings unavailable in recovery");
  return false;
}

void handleApiSet() {
  String key = server.arg("key");
  bool compat = cfg.haCompat && (key == "theme" || key == "lcd_brightness" || key == "brightness");
  if (!compat && !requireAuth()) return;   // geekmagic-hacs cannot sign in (Integrations)
  if (!settingsWritable()) return;
  String err = applySetting(server.arg("key"), server.arg("value"));
  if (err == "Unknown Key") { server.send(200, "text/plain", err); return; }   // stock reply
  if (err.length()) { server.send(400, "text/plain", err); return; }
  if (!cfg.save()) { server.send(503, "text/plain", "Settings could not be saved"); return; }
  settingsApplied();
  server.send(200, "text/plain", "OK");
}

// ---- Backup, restore, reset (no WiFi details or password in a backup) ----
void handleSettingsExport() {
  if (!requireAuth() || !settingsWritable()) return;
  server.sendHeader("Content-Disposition", "attachment; filename=\"smartclock-settings.json\"");
  server.send(200, "application/json", configJson());
}

void handleSettingsImport() {
  if (!requireAuth() || !settingsWritable()) return;
  JsonDocument doc;
  if (deserializeJson(doc, server.arg("plain")) || !doc.is<JsonObject>() || doc["type"] != "smartclock-oss-settings") {
    server.send(400, "text/plain", "Not a SmartClock-OSS settings file");
    return;
  }
  int applied = 0, skipped = 0;
  for (JsonPair kv : doc.as<JsonObject>()) {
    String key = kv.key().c_str();
    if (key == "type" || key == "version" || key == "location" || key == "customs") continue;
    JsonVariant v = kv.value();
    String val;
    if (v.is<bool>()) val = v.as<bool>() ? "1" : "0";
    else if (v.is<JsonObject>() || v.is<JsonArray>()) serializeJson(v, val);
    else val = v.as<String>();
    if (applySetting(key, val).length()) skipped++; else applied++;
  }
  if (!cfg.save()) { server.send(503, "text/plain", "Settings could not be saved"); return; }
  settingsApplied();
  server.send(200, "text/plain", String(applied) + " settings restored" + (skipped ? ", " + String(skipped) + " skipped" : ""));
}

// Deletes /config.json (all settings and WiFi details); photos and the password stay.
void handleSettingsReset() {
  if (!requireAuth() || !settingsWritable()) return;
  if (server.arg("confirm") != "reset") { server.send(400, "text/plain", "Confirmation missing"); return; }
  LittleFS.remove("/config.json");
  server.send(200, "text/plain", "Settings reset. Restarting...");
  safeRestart();
}

void handleWifiForget() {
  if (!requireAuth() || !settingsWritable()) return;
  cfg.ssid = ""; cfg.password = "";
  if (!cfg.save()) { server.send(503, "text/plain", "Settings could not be saved"); return; }
  server.send(200, "text/plain", "WiFi forgotten. Restarting...");
  safeRestart();
}

// Plain SSID list (stock shape); ?detail=1 adds signal strength and security.
void handleScanWifi() {
  if (!requireAuth()) return;
  bool detail = server.hasArg("detail");
  int n = WiFi.scanNetworks();
  JsonDocument doc; JsonArray a = doc.to<JsonArray>();
  for (int i = 0; i < n; i++) {
    if (!detail) { a.add(WiFi.SSID(i)); continue; }
    JsonObject o = a.add<JsonObject>();
    o["ssid"] = WiFi.SSID(i); o["rssi"] = WiFi.RSSI(i);
    o["secure"] = WiFi.encryptionType(i) != ENC_TYPE_NONE;
  }
  WiFi.scanDelete();
  String out; serializeJson(doc, out);
  server.send(200, "application/json", out);
}

void handleConnect() {
  if (!requireAuth() || !settingsWritable()) return;
  cfg.ssid = server.arg("ssid");
  cfg.password = server.arg("password");
  if (!cfg.save()) { server.send(503, "text/plain", "WiFi settings could not be saved"); return; }
  server.send(200, "text/html", "<meta http-equiv='refresh' content='3;url=/'>Saved. Rebooting...");
  safeRestart();
}

// Offered only after the existing volume failed to mount; erases the whole FS region.
void handleFormatStorage() {
  if (!requireAuth()) return;
  if (!storageMountFailed || server.arg("confirm") != "erase") {
    server.send(400, "text/plain", "Formatting is only available when storage failed to mount");
    return;
  }
  bool ok = LittleFS.format();
  server.send(ok ? 200 : 500, "text/plain", ok ? "Storage formatted. Rebooting..." : "Format failed");
  if (ok) safeRestart();
}

// ---------------------------- Photos (/photo on LittleFS) -------------------
// The settings page resizes photos to 240x240 baseline JPEGs before upload.
constexpr size_t PHOTO_MAX = 200 * 1024, PHOTO_FREE_MARGIN = 128 * 1024;
File photoFile;
String photoPath, photoError;

String photoName(const String& raw) {   // a safe /photo file name, or "" if not a JPEG
  String n;
  for (unsigned i = 0; i < raw.length() && n.length() < 28; ++i) {
    char c = tolower(raw[i]);
    if (isalnum((unsigned char)c) || c == '-' || c == '_' || c == '.') n += c;
  }
  if (n.endsWith(".jpeg")) n = n.substring(0, n.length() - 5) + ".jpg";
  if (!n.endsWith(".jpg") || n.length() <= 4 || n[0] == '.') return "";
  return n;
}

// Photo changes need a sign-in, except through the stock-compatible /photo/* API
// while "Home Assistant dashboards" is on (geekmagic-hacs cannot sign in).
bool photoAccess() {
  return requestAuthorized() || (cfg.haCompat && server.uri().startsWith("/photo/"));
}

void dropPhotoUpload(const char* why) {
  if (photoFile) { photoFile.close(); LittleFS.remove(photoPath); }
  if (photoError.isEmpty()) photoError = why;
}

void handlePhotoUpload() {
  HTTPUpload& up = server.upload();
  if (up.status == UPLOAD_FILE_START) {
    photoError = "";
    if (!photoAccess()) { photoError = "Sign in first"; return; }
    if (!storageReady || recoveryMode) { photoError = "Storage unavailable"; return; }
    String name = photoName(up.filename);
    if (name.isEmpty()) { photoError = "Only .jpg photos can be uploaded"; return; }
    FSInfo fs; LittleFS.info(fs);
    if (fs.totalBytes - fs.usedBytes < PHOTO_MAX + PHOTO_FREE_MARGIN) { photoError = "Storage is full"; return; }
    LittleFS.mkdir("/photo");
    photoPath = "/photo/" + name;
    photoFile = LittleFS.open(photoPath, "w");
    if (!photoFile) photoError = "Could not create the file";
  } else if (up.status == UPLOAD_FILE_WRITE) {
    if (!photoFile || photoError.length()) return;
    if (photoFile.size() + up.currentSize > PHOTO_MAX) dropPhotoUpload("Photo is larger than 200 KB");
    else if (photoFile.write(up.buf, up.currentSize) != up.currentSize) dropPhotoUpload("Write failed");
  } else if (up.status == UPLOAD_FILE_END) {
    if (photoFile) photoFile.close();
  } else if (up.status == UPLOAD_FILE_ABORTED) {
    dropPhotoUpload("Upload aborted");
  }
}

void finishPhotoUpload() {
  bool ok = photoError.isEmpty() && photoPath.length();
  server.send(ok ? 200 : 400, "text/plain", ok ? String("OK") : (photoError.length() ? photoError : String("No photo uploaded")));
  photoPath = ""; photoError = "";
  facePhotosChanged();
}

void handlePhotoList() {
  JsonDocument doc;
  JsonArray files = doc["files"].to<JsonArray>();
  if (storageReady) {
    Dir dir = LittleFS.openDir("/photo");
    while (dir.next()) {
      if (!dir.isFile()) continue;
      JsonObject f = files.add<JsonObject>();
      f["name"] = dir.fileName(); f["size"] = dir.fileSize(); f["enabled"] = photoEnabled(dir.fileName());
    }
    doc["interval"] = cfg.photoInterval;
    FSInfo fs; LittleFS.info(fs);
    doc["total"] = fs.totalBytes; doc["used"] = fs.usedBytes;
  }
  String out; serializeJson(doc, out);
  server.send(200, "application/json", out);
}

// Deletes any file in /photo by its exact listed name, including ones the stock
// firmware left (GIFs, names with spaces) that photoName() would not produce.
// GET /photo/<name>. serveStatic() matches the raw URI, so "space%20man.gif" was a
// 404; this decodes the name and only serves plain files directly in /photo.
bool servePhoto() {
  String uri = server.uri();
  if (!uri.startsWith("/photo/") || server.method() != HTTP_GET) return false;
  String name = ESP8266WebServer::urlDecode(uri.substring(7));
  if (!storageReady || name.isEmpty() || name.indexOf('/') >= 0 || name == "." || name == "..") return false;
  File f = LittleFS.open("/photo/" + name, "r");
  if (!f || f.isDirectory()) return false;
  String lower = name; lower.toLowerCase();
  const char* type = lower.endsWith(".gif") ? "image/gif" : lower.endsWith(".png") ? "image/png" :
                     (lower.endsWith(".jpg") || lower.endsWith(".jpeg")) ? "image/jpeg" : "application/octet-stream";
  server.sendHeader("Cache-Control", "max-age=86400");
  server.streamFile(f, type);
  f.close();
  return true;
}

void handlePhotoDelete() {
  if (!photoAccess()) { server.send(401, "text/plain", "Sign in first"); return; }
  if (!storageReady || recoveryMode) { server.send(503, "text/plain", "Storage unavailable"); return; }
  String name = server.arg("name");
  bool listed = false;
  if (name.length() && name.indexOf('/') < 0 && name != "." && name != "..") {
    Dir dir = LittleFS.openDir("/photo");
    while (!listed && dir.next()) listed = dir.isFile() && dir.fileName() == name;
  }
  if (!listed || !LittleFS.remove("/photo/" + name)) { server.send(404, "text/plain", "No such photo"); return; }
  facePhotosChanged();
  server.send(200, "text/plain", "OK");
}

// The page is stored gzipped in flash; every browser accepts gzip.
void sendIndex() {
  server.sendHeader("Content-Encoding", "gzip");
  server.sendHeader("Cache-Control", "no-cache");
  server.send_P(200, "text/html", reinterpret_cast<PGM_P>(INDEX_HTML_GZ), INDEX_HTML_GZ_LEN);
}

void handleVersion() { server.send(200, "text/plain", "SmartClock-OSS V" FW_VERSION); }

// Firmware-only OTA, plain .bin or compressed .bin.gz (firmware_validation.h). Stage
// bytes in free sketch space; never write U_FS.
FirmwareValidation firmware;
bool otaStarted = false, otaReady = false, otaFailed = false;
unsigned otaFiles = 0;
String otaError;

void failOta(const char* message) {
  otaFailed = true;
  otaReady = false;
  otaError = message;
  // A byte is always withheld, so end(false) cannot commit a rejected image.
  if (Update.isRunning()) Update.end(false);
  otaStarted = false;
}

void handleOtaUpload() {
  HTTPUpload& up = server.upload();
  if (up.status == UPLOAD_FILE_START) {
    if (!requestAuthorized()) { failOta("Sign in first"); return; }
    if (++otaFiles != 1 || otaFailed) { failOta("Upload exactly one firmware image"); return; }
    firmware.reset();
    otaError = "";
  } else if (up.status == UPLOAD_FILE_WRITE && !otaFailed) {
    const uint8_t* data = up.buf;
    size_t size = up.currentSize;
    if (!otaStarted) {
      size_t take = std::min(size, FirmwareValidation::prefixSize - firmware.received);
      firmware.consume(data, take);
      data += take; size -= take;
      if (firmware.received < FirmwareValidation::prefixSize) return;
      uint32_t freeSpace = ESP.getFreeSketchSpace();
      size_t capacity = freeSpace > 0x1000 ? (freeSpace - 0x1000) & 0xfffff000 : 0;
      if (const char* why = firmware.check(capacity, FS_PHYS_ADDR)) { failOta(why); return; }
      if (!Update.begin(firmware.expectedSize, U_FLASH)) { failOta("Cannot stage firmware"); return; }
      otaStarted = true;
      if (Update.write(firmware.prefix, FirmwareValidation::prefixSize - 1) != FirmwareValidation::prefixSize - 1) {
        failOta("Firmware staging failed"); return;
      }
    }
    if (size) {
      if (size > firmware.expectedSize - firmware.received) { failOta("Image exceeds declared length"); return; }
      uint8_t previous = firmware.lastByte;
      firmware.consume(data, size);
      if (Update.write(&previous, 1) != 1 ||
          (size > 1 && Update.write(const_cast<uint8_t*>(data), size - 1) != size - 1)) {
        failOta("Firmware staging failed"); return;
      }
    }
  } else if (up.status == UPLOAD_FILE_END && !otaFailed) {
    if (!otaStarted || !firmware.complete()) { failOta("Incomplete image or CRC mismatch"); return; }
    otaReady = true; // Commit only after the complete HTTP request has been parsed.
  } else if (up.status == UPLOAD_FILE_ABORTED) {
    failOta("Upload aborted");
    // The parser may not call the response handler for a disconnected client.
    otaFiles = 0;
    otaFailed = false;
  }
  yield();
}

void finishOta() {
  bool success = otaFiles == 1 && otaReady && !otaFailed;
  if (success) {
    success = Update.write(&firmware.lastByte, 1) == 1 && Update.end(false);
    if (!success) failOta("Firmware commit failed");
  }
  server.sendHeader("Connection", "close");
  server.send(success ? 200 : 400, "text/plain",
              success ? "OK" : (otaError.length() ? otaError : String("No complete firmware uploaded")));
  if (success) safeRestart();
  otaStarted = otaReady = otaFailed = false;
  otaFiles = 0;
  otaError = "";
}

void handleStatus();
void setupRoutes() {
  server.on("/",         sendIndex);
  server.on("/index.html", sendIndex);
  server.on("/config",   []{ server.send(200, "application/json", configJson()); });
  server.on("/api/set",  handleApiSet);
  server.on("/scanwifi", handleScanWifi);
  server.on("/connect",  handleConnect);
  server.on("/restart",  []{ if (!requireAuth()) return; server.send(200, "text/plain", "Restarting"); safeRestart(); });
  server.on("/version",  handleVersion);
  server.on("/update_ota", HTTP_POST, finishOta, handleOtaUpload);
  server.on("/format_storage", HTTP_POST, handleFormatStorage);
  server.on("/api/photos", handlePhotoList);
  server.on("/api/weather/sync", HTTP_POST, handleWeatherSync);
  server.on("/api/photos/upload", HTTP_POST, finishPhotoUpload, handlePhotoUpload);
  server.on("/api/photos/delete", HTTP_POST, handlePhotoDelete);
  server.on("/status",   handleStatus);
  server.on("/api/auth/login", HTTP_POST, handleLogin);
  server.on("/api/auth/logout", HTTP_POST, handleLogout);
  server.on("/api/auth/reveal", HTTP_POST, handleReveal);
  server.on("/api/auth/password", HTTP_POST, handlePasswordChange);
  server.on("/api/settings/export", handleSettingsExport);
  server.on("/api/settings/import", HTTP_POST, handleSettingsImport);
  server.on("/api/settings/reset", HTTP_POST, handleSettingsReset);
  // Crash log and crash-loop protection (crash.cpp): a signed-in session or the API key.
  auto crashAuth = [] {
    if (requestAuthorized() || integrationKeyOk()) return true;
    server.send(401, "text/plain", "Sign in or send the API key (X-Api-Key)");
    return false;
  };
  server.on("/api/crashes", HTTP_GET, [crashAuth] {   // the log file as it is (crash.cpp's layout)
    if (!crashAuth()) return;
    const char* path = crashLogPath();
    if (!path) { server.send(204); return; }
    File f = LittleFS.open(path, "r");
    server.streamFile(f, "application/octet-stream");
    f.close();
  });
  server.on("/api/crashes/resume", HTTP_POST, [crashAuth] {
    if (!crashAuth()) return;
    crashResume(); faceInvalidate(); safeShown = false;
    server.send(200, "text/plain", "OK");
  });
  server.on("/api/crashes/clear", HTTP_POST, [crashAuth] {
    if (!crashAuth()) return;
    crashClear(); faceInvalidate(); safeShown = false;
    server.send(200, "text/plain", "OK");
  });
  server.on("/api/wifi/forget", HTTP_POST, handleWifiForget);
  server.collectHeaders("Cookie", "X-Api-Key");   // sessions; the faces API key
  integrationsRoutes();
  server.onNotFound([]{
    if (servePhoto()) return;
    if (mode == MODE_AP) { // captive portal: send everything to setup
      server.sendHeader("Location", "/", true);
      server.send(302, "text/plain", "");
    } else server.send(404, "text/plain", "Not found");
  });
}

// ---------------------------- WiFi ------------------------------------------
// The router's (or SDK's) reason for the last disconnect during a try, e.g. 15 = the
// 4-way handshake timed out. 0 = none. The settings page explains the codes.
volatile int staReason = 0;
int staFailReason = 0;
WiFiEventHandler staDisconnected;

void startAP() {
  mode = MODE_AP;
  WiFi.persistent(false);
  WiFi.setAutoReconnect(false); // serviceSta() owns retries (see below).
  WiFi.mode(WIFI_AP_STA); // Keep recovery accessible even when the router disappears.
  WiFi.hostname(deviceHost());   // DHCP hostname: lets the router name the clock
  staDisconnected = WiFi.onStationModeDisconnected([](const WiFiEventStationModeDisconnected& e) { staReason = e.reason; });
  WiFi.softAPConfig(IPAddress(192,168,4,1), IPAddress(192,168,4,1), IPAddress(255,255,255,0));
  restartAP();
  dns.start(53, "*", WiFi.softAPIP());
}

// WPA2 with the device password; open in recovery, so recovery is always reachable.
// "AI Agent Monitor 1a2b3c": the hostname's suffix, so clocks side by side differ.
String setupSsid() { String h = deviceHost(); return "AI Agent Monitor " + h.substring(h.lastIndexOf('-') + 1); }
void restartAP() {
  if (recoveryMode) WiFi.softAP(setupSsid().c_str());
  else WiFi.softAP(setupSsid().c_str(), devicePassword.c_str());
}

// While the STA side searches for a missing router the ESP8266 hops channels and AP
// clients drop. Search in short windows so the setup/recovery AP stays usable.
String staSsid, staPass;
bool staTrying = false, staRetryNow = true;
uint32_t staTimer = 0;
int staFail = -1, staFailShown = -1;   // WiFi.status() when the last try gave up; -1 = none
constexpr uint32_t STA_TRY_MS = 30000, STA_RETRY_MS = 5UL * 60 * 1000;

void staConfigure(const String& ssid, const String& pass) {
  staSsid = ssid; staPass = pass;
  staTrying = false; staRetryNow = true; staFail = -1;
}

// Why the last try failed, for the setup screen and the settings page.
String staFailText() {
  switch (staFail) {
    case -1:                return "";
    case WL_NO_SSID_AVAIL:  return F("network not found");
    case WL_WRONG_PASSWORD: return F("wrong password");
    case WL_CONNECT_FAILED: return F("connection refused");
    default:                return F("no answer");
  }
}

void serviceSta() {
  if (staSsid.isEmpty()) return;
  uint32_t now = millis();
  if (WiFi.status() == WL_CONNECTED) {
    staTrying = false;
    staRetryNow = true;              // a dropped link retries immediately
    staFail = -1;
  } else if (staTrying) {
    if (now - staTimer >= STA_TRY_MS) {
      staFail = WiFi.status(); staFailReason = staReason;
      WiFi.disconnect(false); staTrying = false; staTimer = now;
    }
  } else if (staRetryNow || now - staTimer >= STA_RETRY_MS) {
    staRetryNow = false;
    staReason = 0;
    WiFi.begin(staSsid.c_str(), staPass.c_str());
    staTrying = true; staTimer = now;
  }
}

void drawSetupScreen() {
  clockDisplay().fillScreen(TFT_BLACK);
  centerText("Setup WiFi", 50, 4, TFT_WHITE);
  centerText("Join WiFi:", 88, 2, TFT_WHITE);
  centerText(setupSsid(), 110, 2, TFT_CYAN);
  centerText("Password:", 140, 2, TFT_WHITE);
  centerText(devicePassword, 166, 4, TFT_YELLOW);
  centerText("then open " + WiFi.softAPIP().toString(), 206, 2, TFT_WHITE);
  staFailShown = staFail;
  if (staFail >= 0) centerText("Can't join: " + staFailText() + (staFailReason ? " (" + String(staFailReason) + ")" : String()), 228, 2, TFT_ORANGE);
}

void drawPasswordScreen() {
  clockDisplay().fillScreen(TFT_BLACK);
  centerText("Clock password", 80, 4, TFT_WHITE);
  centerText(devicePassword, 125, 4, TFT_YELLOW);
  centerText("Settings page and setup WiFi", 170, 2, TFT_CYAN);
}

void drawRecoveryScreen() {
  clockDisplay().fillScreen(TFT_BLACK);
  centerText("Recovery", 60, 4, TFT_ORANGE);
  centerText("Join WiFi:", 105, 2, TFT_WHITE);
  centerText(setupSsid(), 130, 2, TFT_CYAN);
  centerText(WiFi.softAPIP().toString(), 160, 4, TFT_YELLOW);
  centerText("Password: " + devicePassword, 190, 2, TFT_WHITE);
  if (WiFi.status() == WL_CONNECTED) centerText(WiFi.localIP().toString(), 210, 2, TFT_GREEN);
  if (storageMountFailed) centerText("Storage not mounted", 228, 2, TFT_RED);
}

// Safe mode (crash.cpp, after 5 crashes in a row): no face, so the clock stays reachable.
void drawSafeScreen() {
  clockDisplay().fillScreen(TFT_BLACK);
  centerText(F("Safe mode"), 50, 4, TFT_ORANGE);
  centerText(String(crashLoopCount()) + F(" crashes in a row"), 82, 2, TFT_WHITE);
  centerText(WiFi.localIP().toString(), 120, 4, TFT_YELLOW);
  centerText(deviceHost() + (SMARTCLOCK_MDNS ? ".local" : ""), 152, 2, TFT_CYAN);
  centerText(F("v" FW_VERSION), 174, 2, TFT_LIGHTGREY);
  centerText(F("Settings > System > Crashes: Resume"), 208, 2, TFT_WHITE);
}

void startOptionalServices() {
  optionalStarted = true;
  markBoot(BOOT_STARTING); // A reset/crash during optional startup forces the next boot into recovery.
  LittleFS.setConfig(LittleFSConfig(false));
  storageReady = LittleFS.begin();
  if (!storageReady) {
    // Preserve unknown data; recovery serves OTA (and an explicit format) from PROGMEM.
    storageMountFailed = recoveryMode = true;
    staConfigure(DEFAULT_WIFI_SSID, DEFAULT_WIFI_PASS);
    markBoot(BOOT_CLEAN);
    return;
  }
  cfg.load();
  crashBoot(FW_VERSION);   // a crash record from RTC memory into the log; crash-loop level
  displayBegin();
  drawSetupScreen();
  // The unit may have no button fitted; GPIO4 is never used to choose recovery.
  pinMode(PIN_BUTTON, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(PIN_BUTTON), onButton, FALLING);
  if (!cfg.ssid.isEmpty()) staConfigure(cfg.ssid, cfg.password);
  else staConfigure(DEFAULT_WIFI_SSID, DEFAULT_WIFI_PASS);
}

// Live device state for the settings page. Never includes secrets.
String mqttStatus();   // integrations.cpp

// {net.*}, {sys.*}, {theme.night}, {time.zone} for custom faces (faces.cpp).
String deviceValue(const String& key) {
  bool linked = WiFi.status() == WL_CONNECTED;
  if (key == "net.ip") return linked ? WiFi.localIP().toString() : WiFi.softAPIP().toString();
  if (key == "net.host") return deviceHost();
  if (key == "net.ssid") return linked ? WiFi.SSID() : setupSsid();
  if (key == "net.rssi") return linked ? String(WiFi.RSSI()) : "";
  if (key == "net.mqtt") return mqttStatus() == "connected" ? "1" : "0";
  if (key == "sys.version") return FW_VERSION;
  if (key == "sys.uptime") return String(millis() / 1000);
  if (key == "sys.heap") return String(ESP.getFreeHeap());
  if (key == "sys.lang") return cfg.lang;
  if (key == "theme.night") return nightNow ? "1" : "0";
  if (key == "time.zone") {
    time_t now = time(nullptr);
    struct tm t; localtime_r(&now, &t);
    return _tzname[t.tm_isdst > 0 ? 1 : 0];
  }
  return "";
}

void handleStatus() {
  JsonDocument doc;
  doc["recovery"] = recoveryMode; doc["storageReady"] = storageReady;
  doc["storageMountFailed"] = storageMountFailed;
  doc["version"] = FW_VERSION; doc["build"] = FW_BUILD; doc["uptime"] = millis() / 1000; doc["heap"] = ESP.getFreeHeap();
  doc["heapLow"] = heapLow;   // (getMaxFreeBlockSize() would add 1.6 KB of umm stats)
  doc["stackFree"] = ESP.getFreeContStack();   // the least free stack since boot (of 4 KB)
  doc["resetReason"] = ESP.getResetReason();
  doc["cpuMHz"] = ESP.getCpuFreqMHz();
  JsonObject draws = doc["drawMs"].to<JsonObject>();   // {"<face id>": [last, slowest]}
  for (int i = 0; i <= FACE_MESSAGE; ++i) {
    if (!drawMax[i]) continue;
    JsonArray a = draws[String(i)].to<JsonArray>();
    a.add(drawLast[i]); a.add(drawMax[i]);
  }
  bool linked = WiFi.status() == WL_CONNECTED;
  doc["connected"] = linked;
  doc["ssid"] = linked ? WiFi.SSID() : staSsid;
  if (!linked && staFail >= 0) { doc["wifiError"] = staFailText(); doc["wifiCode"] = staFailReason; }
  if (linked) { doc["ip"] = WiFi.localIP().toString(); doc["rssi"] = WiFi.RSSI(); }
  doc["apIp"] = WiFi.softAPIP().toString(); doc["apSsid"] = setupSsid();
  doc["host"] = deviceHost();   // the DHCP hostname, as the router lists it
  if (SMARTCLOCK_MDNS) doc["mdns"] = deviceHost() + ".local";
  time_t now = time(nullptr);
  if (now > 1600000000) {            // only once NTP has set the clock
    struct tm t; localtime_r(&now, &t);
    // Not strftime(): it links 3.3 KB for this one string. _tzname holds the zone
    // abbreviations of the POSIX TZ rule set by configTime().
    char buf[40];
    snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d:%02d %s", t.tm_year + 1900, t.tm_mon + 1,
             t.tm_mday, t.tm_hour, t.tm_min, t.tm_sec, _tzname[t.tm_isdst > 0 ? 1 : 0]);
    doc["time"] = buf;
  }
  if (wx.error.length()) doc["weatherError"] = wx.error;
  if (wx.lastSuccess) doc["weatherAge"] = (millis() - wx.lastSuccess) / 1000;
  if (wxHeapUse) doc["weatherHeap"] = wxHeapUse;
  if (cfg.geoFor == cfg.city && cfg.geoName.length()) doc["location"] = cfg.geoName;
  if (wx.valid) {
    JsonObject w = doc["weather"].to<JsonObject>();
    w["temp"] = wx.temp; w["feels"] = wx.feels; w["humidity"] = wx.humidity; w["wind"] = wx.wind;
    w["main"] = wx.main; w["desc"] = wx.desc; w["icon"] = wx.icon; w["city"] = wx.cityName;
  }
  doc["forecastDays"] = fc.valid ? fc.count : 0;
  doc["photos"] = storageReady ? photoCount() : 0;
  doc["face"] = cfg.theme;
  doc["signedIn"] = requestAuthorized();
  extern String mqttStatus();
  doc["mqtt"] = mqttStatus();
  if (cfg.theme == FACE_CUSTOM) doc["customFace"] = cfg.customFace;
  doc["nightAuto"] = cfg.nightAuto;
  if (wx.sunrise >= 0 && wx.sunset >= 0) {
    char sun[12];
    unsigned rise = wx.sunrise % 1440, set = wx.sunset % 1440;
    snprintf(sun, sizeof(sun), "%02u:%02u", rise / 60, rise % 60); doc["sunrise"] = sun;
    snprintf(sun, sizeof(sun), "%02u:%02u", set / 60, set % 60); doc["sunset"] = sun;
  }
  crashStatus(doc);
  String out; serializeJson(doc, out);
  server.send(200, "application/json", out);
}

// Recovery: AP + OTA + optional build-time WiFi only. The panel is shown so the
// device does not look dead, unless the panel itself reset the previous boot.
void recoveryLoop() {
  serviceSta();
  if (!displayReady && recoveryDisplay && millis() - bootStarted >= 3000) {
    markBoot(BOOT_RECOVERY_DISPLAY);
    displayBegin();
    drawRecoveryScreen();
    markBoot(BOOT_CLEAN);
    recoveryLinkShown = WiFi.status() == WL_CONNECTED;
  }
  if (revealUntil && (int32_t)(millis() - revealUntil) >= 0) { revealUntil = 0; if (displayReady) drawRecoveryScreen(); }
  if (displayReady && recoveryLinkShown != (WiFi.status() == WL_CONNECTED)) {
    recoveryLinkShown = !recoveryLinkShown;
    drawRecoveryScreen();
  }
  delay(1);
}

// mDNS waits for the station link and 28 KB of free heap (its responder allocates
// on start), then advertises the web UI with a few identifying TXT records.
// Built with SMARTCLOCK_MDNS (the full image; the no-MQTT plain image leaves it out
// for size). The DHCP hostname also names the clock in the router.
void serviceMdns() {
#if SMARTCLOCK_MDNS
  static bool started = false;
  if (started) { MDNS.update(); return; }
  if (WiFi.status() != WL_CONNECTED || millis() - bootStarted < 1500) return;
  if (ESP.getFreeHeap() < MDNS_MIN_HEAP) return;
  String host = deviceHost();
  if (!MDNS.begin(host.c_str())) return;
  MDNS.addService("http", "tcp", 80);
  MDNS.addServiceTxt("http", "tcp", "model", "AI Agent Monitor");
  MDNS.addServiceTxt("http", "tcp", "version", FW_VERSION);
  MDNS.addServiceTxt("http", "tcp", "board", "SD Pro (GeekMagic-style)");
  MDNS.addServiceTxt("http", "tcp", "api", "ai-agent-monitor");
  MDNS.addServiceTxt("http", "tcp", "path", "/");
  started = true;
#endif
}

// ---------------------------- setup / loop ----------------------------------
void setup() {
  // No Serial.begin(): nothing is printed, and leaving it out drops the UART driver.
  BootMarker marker{};
  BootState previous = BOOT_CLEAN;
  if (ESP.rtcUserMemoryRead(32, reinterpret_cast<uint32_t*>(&marker), sizeof(marker)) &&
      marker.magic == BOOT_MAGIC &&
      (marker.state == BOOT_STARTING || marker.state == BOOT_RECOVERY_DISPLAY))
    previous = BootState(marker.state);
  markBoot(BOOT_CLEAN);
  // Only a power-on or external reset is part of the power-cycle gesture. Crashes,
  // watchdog resets and our own restarts clear the count; the RTC marker above is
  // what catches a crash during startup.
  const rst_info* reset = ESP.getResetInfoPtr();
  bool powerOn = reset && (reset->reason == REASON_DEFAULT_RST || reset->reason == REASON_EXT_SYS_RST);
  bool powerCycled = false;
  if (powerOn) powerCycled = countPowerCycle();
  else clearPowerCycles();
  recoveryMode = powerCycled || previous != BOOT_CLEAN;
  recoveryDisplay = previous != BOOT_RECOVERY_DISPLAY;
  loadPassword();   // before startAP(): it is the setup network's WPA2 key
  startAP();
  setupRoutes();
  server.begin();
  if (recoveryMode) staConfigure(DEFAULT_WIFI_SSID, DEFAULT_WIFI_PASS);
  bootStarted = millis();
}

void loop() {
  dns.processNextRequest();
  server.handleClient();
  serviceMdns();
  uint32_t heap = ESP.getFreeHeap();
  if (heap < heapLow) heapLow = heap;
  // OTA must be the only activity while staging; no settings writes or network work.
  if (otaFiles || otaStarted) { delay(1); return; }
  if (!powerCyclesCleared && millis() - bootStarted >= CYCLE_WINDOW_MS) {
    clearPowerCycles();               // this boot outlived the power-cycle window
    powerCyclesCleared = true;
  }
  if (recoveryMode) { recoveryLoop(); return; }
  if (!optionalStarted) {
    // Allow a recovery connection before any storage/display initialization.
    if (millis() - bootStarted >= 3000) startOptionalServices();
    return;
  }
  if (!bootHealthy && millis() - bootStarted >= 30000) {
    markBoot(BOOT_CLEAN);
    bootHealthy = true;
  }
  crashTick();
  serviceSta();
  if (WiFi.status() == WL_CONNECTED && mode != MODE_RUN) {
    mode = MODE_RUN;
    linkUpAt = millis();
    // Joined with the build-time network from secrets.ini: keep it, or a release
    // image (built without it) would have no network to join.
    if (cfg.ssid.isEmpty() && staSsid.length()) { cfg.ssid = staSsid; cfg.password = staPass; cfg.save(); }
    applyTime();
    clockDisplay().fillScreen(TFT_BLACK);
    centerText(WiFi.localIP().toString(), 100, 4, TFT_YELLOW);
    centerText(deviceHost() + (SMARTCLOCK_MDNS ? ".local" : ""), 140, 2, TFT_CYAN);
    centerText(F("v" FW_VERSION), 165, 2, TFT_LIGHTGREY);
    if (crashLevel()) centerText(crashLevel() > 1 ? F("Safe mode: crashed") : F("Custom faces off: crashed"), 195, 2, TFT_ORANGE);
    faceHoldUntil = millis() + 3000;   // let the address be read
    safeShown = false;
    faceInvalidate();
  } else if (WiFi.status() != WL_CONNECTED && mode == MODE_RUN) {
    mode = MODE_AP;
    drawSetupScreen();
    faceInvalidate();
  } else if (mode == MODE_AP && revealUntil && (int32_t)(millis() - revealUntil) >= 0) {
    revealUntil = 0;
    drawSetupScreen();   // back from "Show on clock" while not connected
  } else if (mode == MODE_AP && !revealUntil && staFail != staFailShown) {
    drawSetupScreen();   // a try failed: say why
  }

  static uint32_t lastButton = 0;
  if (buttonPressed) {                 // button (if fitted) cycles faces
    buttonPressed = false;
    // Debounced and not persisted: a noisy or unfitted GPIO4 must not wear flash.
    if (millis() - lastButton > 300) { lastButton = millis(); advanceFace(false); faceSince = millis(); }
  }
  uint32_t turn = faceTurnSec ? faceTurnSec : cfg.themeInterval;
  if (cfg.themeInterval > 0 && !nightTile() && millis() - faceSince > turn * 1000) {
    faceSince = millis();
    advanceFace(true);   // in RAM only; the saved face stays the user's choice
  }
  if (cfg.theme == FACE_MESSAGE && !messageActive(messageShowing())) {   // expired or cleared
    faceSince = millis();
    advanceFace(true);
    if (cfg.theme == FACE_MESSAGE && !messageActive(messageShowing())) cfg.theme = FACE_CLASSIC;
  }
  // A warning or error message on screen lights the panel fully, even at night.
  static bool boosted = false;
  bool boost = cfg.theme == FACE_MESSAGE && messageUrgent(messageShowing());
  if (boost != boosted) { boosted = boost; brightnessDirty = true; }

  if (mode == MODE_RUN) {
    serviceWeather();
    integrationsLoop();
    time_t now = time(nullptr);
    bool timeValid = now > 1600000000;
    struct tm t; localtime_r(&now, &t);

    static int lastBrightMin = -1;     // apply night dimming once per minute
    if (timeValid && (t.tm_min != lastBrightMin || brightnessDirty)) {
      brightnessDirty = false;
      lastBrightMin = t.tm_min;
      nightNow = isNight(t);
      bool off = nightNow && cfg.nightOff && !boosted;   // an urgent message wakes the panel
      setPanelOn(!off);
      setGamma(nightNow && cfg.nightGamma >= 0 ? cfg.nightGamma : cfg.gamma);
      setBacklight(backlightOff || off ? 0 : boosted ? 100 : nightNow ? cfg.nightBrightness : cfg.brightness);
      // Night colours too; an urgent message keeps its own.
      uint16_t tint = nightNow && !boosted && cfg.nightTint ? std::max<uint16_t>(cfg.nightColor, 1) : 0;
      if (tint != nightPanel().tint) { nightPanel().tint = tint; faceInvalidate(); }
    }
    if (revealUntil && (int32_t)(millis() - revealUntil) >= 0) { revealUntil = 0; faceInvalidate(); }
    // At night, one face of the user's choice instead of the rotation; a message shown
    // on purpose (show) still comes through.
    int shown = cfg.theme;
    const char* custom = nullptr;
    if (nightTile() && cfg.theme != FACE_MESSAGE) {
      shown = entryFace(cfg.nightFace);
      if (shown == FACE_CUSTOM) custom = cfg.nightFace.c_str();
    }
    if (shown == FACE_CUSTOM && crashLevel()) { shown = FACE_CLASSIC; custom = nullptr; }   // off after a crash loop
    if (crashLevel() > 1) {   // safe mode: no face, the address screen stays for troubleshooting
      if (!safeShown && (int32_t)(millis() - faceHoldUntil) >= 0) { drawSafeScreen(); safeShown = true; }
    } else if (!revealUntil && !panelOff && (int32_t)(millis() - faceHoldUntil) >= 0) {
      crashFace = shown;
      crashCustom = shown != FACE_CUSTOM ? nullptr : custom ? custom : cfg.customFace.c_str();
      uint32_t start = micros();
      faceDraw(shown, t, timeValid, custom);
      crashFace = -1; crashCustom = nullptr;
      uint32_t ms = (micros() - start) / 1000;
      if (ms > 1 && shown >= 0 && shown <= FACE_MESSAGE) {
        drawLast[shown] = std::min<uint32_t>(ms, 65535);
        drawMax[shown] = std::max(drawMax[shown], drawLast[shown]);
      }
    }
  }
  delay(20);   // faces redraw only what changed, so a short tick is cheap
}

// Clock faces for the 240x240 ST7789. There is no RAM for a full-screen buffer, so
// every face redraws only the regions whose content changed, and text is drawn with
// a background colour (or padding) so it overwrites what was there.
#include "faces.h"
#include <Arduino.h>
#include <algorithm>
#include <new>
#include <LittleFS.h>
#include <TFT_eSPI.h>
#include <tjpgd.h>   // the decoder core only: the TJpg_Decoder wrapper links SPIFFS (~32 KB)
#include "config.h"
#include "weather.h"
#include "weather_icons.h"
#include "integrations.h"
#include <ArduinoJson.h>

TFT_eSPI& clockDisplay();   // main.cpp
static TFT_eSPI& D() { return clockDisplay(); }
// For the emulator's drawing speed: the path loops below are float maths, software on
// the ESP8266 and hardware on the PC, so they count their work in estimated ESP8266
// cycles instead of being timed (emulator/panel.cpp). Nothing on the clock.
#ifdef HOST_MOCK
void emuWork(uint32_t cycles);   // emulator/panel.cpp
#define EMU_WORK(cycles) emuWork(cycles)
#else
#define EMU_WORK(cycles)
#endif

namespace {

constexpr int SCR_W = 240, SCR_H = 240;   // the panel

// ---- Palette (RGB565). Accents come from the user's color1..3.
constexpr uint16_t BG = 0x0000, PANEL = 0x18E3, PANEL_HI = 0x2945, DIM = 0x528A,
                   MUTED = 0x9D14, WHITE = 0xFFFF, CLOUD = 0xE75D, RAIN = 0x5D9F;

const char* const DAY_LONG[] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};
const char* const DAY_SHORT[] = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};
const char* const MONTH_LONG[] = {"January", "February", "March", "April", "May", "June", "July",
                                  "August", "September", "October", "November", "December"};
const char* const MONTH_SHORT[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
// French, without accents: the fonts are ASCII.
const char* const DAY_LONG_FR[] = {"dimanche", "lundi", "mardi", "mercredi", "jeudi", "vendredi", "samedi"};
const char* const DAY_SHORT_FR[] = {"DIM", "LUN", "MAR", "MER", "JEU", "VEN", "SAM"};
const char* const MONTH_LONG_FR[] = {"janvier", "fevrier", "mars", "avril", "mai", "juin", "juillet",
                                     "aout", "septembre", "octobre", "novembre", "decembre"};
const char* const MONTH_SHORT_FR[] = {"janv", "fevr", "mars", "avr", "mai", "juin", "juil", "aout", "sept", "oct", "nov", "dec"};

bool fr() { return cfg.lang == "fr"; }
const char* tr(const char* en, const char* french) { return fr() ? french : en; }
const char* dayLong(int i) { return (fr() ? DAY_LONG_FR : DAY_LONG)[i]; }
const char* dayShort(int i) { return (fr() ? DAY_SHORT_FR : DAY_SHORT)[i]; }
const char* monthLong(int i) { return (fr() ? MONTH_LONG_FR : MONTH_LONG)[i]; }
const char* monthShort(int i) { return (fr() ? MONTH_SHORT_FR : MONTH_SHORT)[i]; }
// Weather words in the clock's language; wx.main / wx.desc stay English for the API.
String wxMain() { if (!fr()) return wx.main; String m, d; wmoText(wx.code, true, m, d); return m; }
String wxDesc() { if (!fr()) return wx.desc; String m, d; wmoText(wx.code, true, m, d); return d; }

int drawnFace = -1;      // -2 = "setting the time" screen
bool dirty = true;
int photosCached = -1;   // -1 = recount

// ---- Text and number helpers --------------------------------------------------
void text(const char* s, int x, int y, uint8_t font, uint16_t fg, uint16_t bg, uint8_t datum, int pad = 0) {
  TFT_eSPI& d = D();
  d.setTextColor(fg, bg);
  d.setTextDatum(datum);
  d.setTextPadding(pad);
  d.drawString(s, x, y, font);
  d.setTextPadding(0);
}

void textClear(const char* s, int x, int y, uint8_t font, uint16_t fg, uint8_t datum) {   // transparent
  TFT_eSPI& d = D();
  d.setTextColor(fg);
  d.setTextDatum(datum);
  d.drawString(s, x, y, font);
}

int displayTemp(float c) { return lroundf(cfg.celsius ? c : c * 9 / 5 + 32); }

int clockHour(const struct tm& t) {
  int h = t.tm_hour;
  if (cfg.hour12) { h %= 12; if (!h) h = 12; }
  return h;
}

void ring(int x, int y, int r, uint16_t fg, uint16_t bg) {   // the degree sign; no font has one
  D().fillSmoothCircle(x, y, r, fg, bg);
  D().fillCircle(x, y, r > 3 ? r - 2 : r - 1, bg);
}

// A temperature with a drawn degree ring, centred on (cx, cy).
void temp(int cx, int cy, int value, uint8_t font, uint16_t fg, uint16_t bg) {
  char buf[8]; snprintf(buf, sizeof(buf), "%d", value);
  int r = font >= 6 ? 6 : font == 4 ? 4 : 3;
  int w = D().textWidth(buf, font), total = w + 2 * r + 3;
  int x0 = cx - total / 2, top = cy - D().fontHeight(font) / 2;
  text(buf, x0, cy, font, fg, bg, ML_DATUM);
  ring(x0 + w + r + 2, top + r + (font >= 6 ? 4 : 2), r, fg, bg);
}

void colon(int cx, int cy, uint16_t c, uint16_t bg) {
  D().fillSmoothCircle(cx, cy - 13, 4, c, bg);
  D().fillSmoothCircle(cx, cy + 13, 4, c, bg);
}

// Classic's and Simple's HH:MM, in the Big tiles' line font (defined with them).
int bigTimeColonX(int cx, const struct tm& t);
void bigTime(int cx, int cy, const struct tm& t, uint16_t fg, uint16_t bg, bool force);

void smallTime(char* out, size_t n, const struct tm& t) {
  snprintf(out, n, cfg.hour12 ? "%d:%02d" : "%02d:%02d", clockHour(t), t.tm_min);
}

const char* ampm(const struct tm& t) { return cfg.hour12 ? (t.tm_hour < 12 ? "AM" : "PM") : ""; }

// The placeholders' cloud, drawn from primitives. Anti-aliased shapes blend their edges
// into a single background colour, so where circles overlap the seams are painted over
// with a plain fill afterwards.
void cloud(int cx, int cy, int s, uint16_t c, uint16_t bg) {
  struct { float x, y, r; } puffs[] = {{-0.22f, 0.04f, 0.22f}, {0.02f, -0.08f, 0.30f}, {0.26f, 0.06f, 0.20f}};
  int bx = cx - s * 0.44f, by = cy + s * 0.02f, bw = s * 0.88f, bh = s * 0.24f;
  for (auto& p : puffs) D().fillSmoothCircle(cx + p.x * s, cy + p.y * s, p.r * s, c, bg);
  D().fillSmoothRoundRect(bx, by, bw, bh, bh / 2, c, bg);
  for (auto& p : puffs) D().fillCircle(cx + p.x * s, cy + p.y * s, p.r * s - 1, c);
  D().fillRoundRect(bx + 1, by + 1, bw - 2, bh - 2, bh / 2 - 1, c);
}

// A weather icon (s px square, centred), defined with the path filler below.
void weatherIcon(int cx, int cy, int s, const String& icon, uint16_t bg, uint16_t color);

void placeholder(int iconY, const char* title, const char* line1, const char* line2) {
  text(title, 120, iconY + 62, 4, WHITE, BG, MC_DATUM);
  text(line1, 120, iconY + 90, 2, MUTED, BG, MC_DATUM);
  if (line2) text(line2, 120, iconY + 108, 2, MUTED, BG, MC_DATUM);
}

// What to say while there is no data: the last error, or waiting.
void weatherPlaceholder(const char* title) {
  if (wx.error == "City not found" || wx.error == "Set a city") {
    placeholder(92, title, wx.error.c_str(), tr("Check the city in settings", "Verifiez la ville"));
    return;
  }
  placeholder(92, title, wx.error.length() ? wx.error.c_str() : tr("Waiting for the first update", "En attente des donnees"), nullptr);
}

// ---- Setting the time -----------------------------------------------------------
void syncScreen() {
  D().fillScreen(BG);
  D().fillSmoothCircle(120, 88, 34, PANEL_HI, BG);
  D().drawWedgeLine(120, 88, 120, 66, 3, 2, WHITE, PANEL_HI);
  D().drawWedgeLine(120, 88, 136, 98, 3, 2, cfg.color1, PANEL_HI);
  D().fillSmoothCircle(120, 88, 4, WHITE, PANEL_HI);
  text(tr("Setting the time", "Reglage de l'heure"), 120, 150, 4, WHITE, BG, MC_DATUM);
  text(tr("Waiting for a time server", "Attente du serveur de temps"), 120, 178, 2, MUTED, BG, MC_DATUM);
}

// ---- 0 Classic: date, big time, seconds bar, weather line ------------------------
void classic(const struct tm& t, bool force) {
  static int day = -1, min = -1, sec = -1;
  static uint32_t wxv = ~0u;
  if (force) { day = min = sec = -1; wxv = ~0u; }
  const int barX = 30, barY = 166, barW = cfg.hour12 ? 150 : 180, barH = 4;
  if (t.tm_yday != day) {
    char date[24];
    snprintf(date, sizeof(date), "%d %s", t.tm_mday, monthLong(t.tm_mon));
    String wd = dayLong(t.tm_wday); wd.toUpperCase();
    text(wd.c_str(), 120, 28, 2, cfg.color2, BG, MC_DATUM, 200);
    text(date, 120, 52, 4, WHITE, BG, MC_DATUM, 230);
    day = t.tm_yday;
  }
  if (t.tm_min != min) {
    bigTime(120, 112, t, cfg.color1, BG, force);
    D().fillRect(0, barY - 8, 240, 20, BG);
    D().fillRoundRect(barX, barY, barW, barH, 2, PANEL_HI);
    if (cfg.hour12) text(ampm(t), 210, barY + 2, 2, MUTED, BG, MC_DATUM);
    min = t.tm_min; sec = -1;
  }
  if (t.tm_sec != sec) {
    D().fillRoundRect(barX, barY, std::max(barH, barW * (t.tm_sec + 1) / 60), barH, 2, cfg.color2);
    sec = t.tm_sec;
  }
  if (wx.version != wxv) {
    D().fillRect(0, 184, 240, 56, BG);
    if (wx.valid) {
      char tb[8]; snprintf(tb, sizeof(tb), "%d", displayTemp(wx.temp));
      int tw = D().textWidth(tb, 4) + 12, mw = D().textWidth(wxMain(), 2);
      int x = 120 - (36 + 8 + tw + 8 + mw) / 2;
      weatherIcon(x + 18, 206, 36, wx.icon, BG, cfg.color1);
      temp(x + 44 + tw / 2, 207, displayTemp(wx.temp), 4, cfg.color3, BG);
      text(wxMain().c_str(), x + 44 + tw + 8, 208, 2, MUTED, BG, ML_DATUM);
    }
    wxv = wx.version;
  }
}

// ---- 1 Weather: big icon and temperature, details below -------------------------
void weather(const struct tm& t, bool force) {
  static int min = -1;
  static uint32_t wxv = ~0u;
  if (force) { min = -1; wxv = ~0u; }
  if (t.tm_min != min) {
    char tb[8]; smallTime(tb, sizeof(tb), t);
    char db[16]; snprintf(db, sizeof(db), "%s %d %s", dayShort(t.tm_wday), t.tm_mday, monthShort(t.tm_mon));
    text(tb, 16, 24, 4, WHITE, BG, ML_DATUM, 90);
    text(db, 224, 24, 2, MUTED, BG, MR_DATUM, 110);
    min = t.tm_min;
  }
  if (wx.version == wxv) return;
  wxv = wx.version;
  D().fillRect(0, 44, 240, 196, BG);
  if (!wx.valid) {
    cloud(120, 92, 84, DIM, BG);
    weatherPlaceholder(tr("No weather yet", "Pas encore de meteo"));
    return;
  }
  weatherIcon(64, 104, 100, wx.icon, BG, cfg.color1);
  char tb[8]; snprintf(tb, sizeof(tb), "%d", displayTemp(wx.temp));
  int w = D().textWidth(tb, 6);
  text(tb, 126, 96, 6, WHITE, BG, ML_DATUM);
  ring(126 + w + 8, 80, 6, WHITE, BG);
  text(cfg.celsius ? "C" : "F", 126 + w + 18, 80, 2, MUTED, BG, ML_DATUM);
  char hl[24]; snprintf(hl, sizeof(hl), "H %d   L %d", displayTemp(wx.tempMax), displayTemp(wx.tempMin));
  text(hl, 128, 132, 2, MUTED, BG, ML_DATUM);
  String desc = wxDesc(); if (desc.length()) desc.setCharAt(0, toupper(desc[0]));
  text(desc.c_str(), 120, 170, D().textWidth(desc, 4) <= 228 ? 4 : 2, cfg.color2, BG, MC_DATUM);
  const char* labels[] = {tr("FEELS", "RESSENTI"), tr("HUMIDITY", "HUMIDITE"), tr("WIND", "VENT")};
  for (int i = 0; i < 3; ++i) text(labels[i], 42 + i * 78, 198, 2, MUTED, BG, MC_DATUM);
  temp(42, 222, displayTemp(wx.feels), 4, WHITE, BG);
  char hu[8]; snprintf(hu, sizeof(hu), "%d%%", wx.humidity);
  text(hu, 120, 222, 4, WHITE, BG, MC_DATUM);
  char wd[8]; snprintf(wd, sizeof(wd), "%d", (int)lroundf(cfg.mile ? wx.wind * 2.237f : wx.wind * 3.6f));
  int ww = D().textWidth(wd, 4), uw = D().textWidth(cfg.mile ? "mph" : "km/h", 2);
  int x0 = 198 - (ww + 3 + uw) / 2;
  text(wd, x0, 222, 4, WHITE, BG, ML_DATUM);
  text(cfg.mile ? "mph" : "km/h", x0 + ww + 3, 226, 2, MUTED, BG, ML_DATUM);
}

// ---- 5 Forecast: four day cards ---------------------------------------------------
void forecast(const struct tm& t, bool force) {
  static int min = -1;
  static uint32_t fcv = ~0u;
  if (force) { min = -1; fcv = ~0u; }
  if (t.tm_min != min) {
    String city = wx.cityName.length() ? wx.cityName : cfg.city;
    while (city.length() > 3 && D().textWidth(city, 4) > 150) city.remove(city.length() - 1);
    char tb[8]; smallTime(tb, sizeof(tb), t);
    text(city.c_str(), 12, 24, 4, WHITE, BG, ML_DATUM, 150);
    text(tb, 228, 24, 4, MUTED, BG, MR_DATUM, 70);
    min = t.tm_min;
  }
  if (fc.version == fcv && !force) return;
  fcv = fc.version;
  D().fillRect(0, 44, 240, 196, BG);
  if (!fc.valid) {
    cloud(120, 92, 84, DIM, BG);
    weatherPlaceholder(tr("No forecast yet", "Pas encore de previsions"));
    return;
  }
  for (int i = 0; i < FORECAST_DAYS; ++i) {
    int x = 6 + i * 58, cx = x + 27;
    D().fillSmoothRoundRect(x, 50, 54, 182, 12, PANEL, BG);
    if (i >= fc.count) continue;
    const ForecastDay& d = fc.days[i];
    text(d.today ? tr("TODAY", "AUJ.") : dayShort(d.wday), cx, 70, 2, d.today ? cfg.color2 : MUTED, PANEL, MC_DATUM);
    weatherIcon(cx, 110, 44, d.icon, PANEL, cfg.color1);
    temp(cx, 152, displayTemp(d.hi), 4, WHITE, PANEL);
    temp(cx, 178, displayTemp(d.lo), 2, MUTED, PANEL);
    if (d.pop >= 10) {
      char p[12]; snprintf(p, sizeof(p), "%d%%", std::min(d.pop, 100));
      D().fillSmoothCircle(cx - 14, 210, 3, RAIN, PANEL);
      D().fillTriangle(cx - 17, 209, cx - 11, 209, cx - 14, 203, RAIN);
      text(p, cx - 8, 208, 2, RAIN, PANEL, ML_DATUM);
    }
  }
}

// ---- 4 Simple: the time and the date, nothing else -------------------------------
void simple(const struct tm& t, bool force) {
  static int min = -1, sec = -1, day = -1;
  if (force) { min = sec = day = -1; }
  if (t.tm_min != min) { bigTime(120, 104, t, WHITE, BG, force); min = t.tm_min; }
  if (t.tm_sec != sec) { colon(bigTimeColonX(120, t), 104, t.tm_sec % 2 ? DIM : WHITE, BG); sec = t.tm_sec; }
  if (t.tm_yday != day) {
    char db[32];
    snprintf(db, sizeof(db), "%s  %d %s", dayLong(t.tm_wday), t.tm_mday, monthLong(t.tm_mon));
    String s = db; s.toUpperCase();
    text(s.c_str(), 120, 168, 2, MUTED, BG, MC_DATUM, 236);
    text(ampm(t), 120, 190, 2, cfg.color1, BG, MC_DATUM, 40);
    day = t.tm_yday;
  }
}

// ---- 2 Photo: JPEGs from /photo, with the time on top unless turned off ----------
struct JpgIo { File* file; int ox, oy; };

size_t jpgIn(JDEC* jd, uint8_t* buf, size_t n) {
  File* f = static_cast<JpgIo*>(jd->device)->file;
  if (buf) return f->read(buf, n);
  return f->seek(n, SeekCur) ? n : 0;
}

int jpgOut(JDEC* jd, void* bmp, JRECT* r) {
  JpgIo* io = static_cast<JpgIo*>(jd->device);
  int y = io->oy + r->top;
  if (y >= 240) return 0;
  D().pushImage(io->ox + r->left, y, r->right - r->left + 1, r->bottom - r->top + 1, static_cast<uint16_t*>(bmp));
  return 1;
}

// Decodes a baseline JPEG straight to the panel, scaled by 1/2..1/8 to fit 240x240,
// centred and on a cleared screen unless a position is given (custom faces).
// The 3.6 KB workspace is only allocated while decoding.
bool drawJpeg(const String& path, int x = -1, int y = -1) {
  constexpr size_t POOL = 3600;
  File f = LittleFS.open(path, "r");
  if (!f) return false;
  void* pool = malloc(POOL);
  bool ok = false;
  JDEC jd{};
  jd.swap = 0;   // this tjpgd keeps a byte-swap flag across jd_prepare(); emit native RGB565
  JpgIo io{&f, 0, 0};
  if (pool && jd_prepare(&jd, jpgIn, pool, POOL, &io) == JDR_OK) {
    uint8_t scale = 0;
    while (((jd.width >> scale) > 240 || (jd.height >> scale) > 240) && scale < 3) ++scale;
    bool centred = x < 0 || y < 0;
    io.ox = centred ? (240 - (jd.width >> scale)) / 2 : x;
    io.oy = centred ? (240 - (jd.height >> scale)) / 2 : y;
    // Clear only around a photo smaller than the screen; a full-screen photo
    // overwrites every pixel, and clearing first would flash black.
    if (centred && ((jd.width >> scale) < 240 || (jd.height >> scale) < 240)) D().fillScreen(BG);
    D().setSwapBytes(true);   // tjpgd emits native-endian RGB565
    ok = jd_decomp(&jd, jpgOut, scale) == JDR_OK;
    D().setSwapBytes(false);
  }
  free(pool);
  f.close();
  return ok;
}

bool isPhoto(const String& name) {
  String n = name; n.toLowerCase();
  return n.endsWith(".jpg") || n.endsWith(".jpeg");
}

String photoAt(int index) {   // index modulo the number of photos
  Dir dir = LittleFS.openDir("/photo");
  int n = 0;
  String first;
  while (dir.next()) {
    if (!dir.isFile() || !isPhoto(dir.fileName()) || !photoEnabled(dir.fileName())) continue;
    if (n == 0) first = dir.fileName();
    if (n++ == index) return "/photo/" + dir.fileName();
  }
  return first.length() ? "/photo/" + first : String();
}

void photo(const struct tm& t, bool force) {
  static int min = -1, index = -1;
  static uint32_t shown = 0;
  static bool have = false;
  if (force || millis() - shown > (uint32_t)std::max(1, cfg.photoInterval) * 1000) {
    shown = millis();
    int count = photoCount();
    have = count > 0;
    if (!have) {
      if (!force) return;
      D().fillScreen(BG);
      D().fillSmoothRoundRect(84, 62, 72, 58, 10, PANEL_HI, BG);
      D().fillSmoothCircle(104, 82, 7, MUTED, PANEL_HI);
      D().fillTriangle(92, 112, 116, 88, 140, 112, DIM);
      placeholder(92, tr("No photos yet", "Aucune photo"), tr("Upload photos in the", "Ajoutez des photos dans"), tr("clock's settings", "les reglages"));
      return;
    }
    int next = (index + 1) % count;
    // The same single photo again (geekmagic-hacs sets a 1 s interval): nothing to
    // redraw. A new upload still shows at once, because it forces a redraw.
    if (force || next != index) {
      index = next;
      min = -1;   // the photo covers the time pill; draw it again
      if (!drawJpeg(photoAt(index))) {
        D().fillScreen(BG);
        placeholder(92, tr("Photo unreadable", "Photo illisible"), tr("Use a baseline JPEG", "Utilisez un JPEG de base"), nullptr);
      }
    }
  }
  if (have && cfg.photoClock && t.tm_min != min) {
    char tb[8]; smallTime(tb, sizeof(tb), t);
    D().fillSmoothRoundRect(66, 196, 108, 34, 17, BG, BG);
    text(tb, 120, 214, 4, WHITE, BG, MC_DATUM);
    min = t.tm_min;
  }
}

// ---- Custom faces (integrations): a JSON list of items --------------------------
// Items draw in order. Any string field may hold placeholders ({ss}, {temp}, pushed
// variables). Number and colour fields may also hold an expression, evaluated after
// the placeholders: "120+60*sin({ss}*6)", "{state}==warn?#ff9f0a:#30d158".
// Only items whose placeholder-dependent content changed are redrawn: an opaque item
// (text or icon with "bg", ring, bar) whose content changed is redrawn alone; a change
// to anything else (position, size, visibility, a transparent item) redraws the face.
JsonDocument customDoc;
String customName;
uint32_t customVer = 0, customEvalAt = 0;
bool customLoaded = false;
constexpr size_t CUSTOM_ITEMS = 32;
uint32_t customHash[CUSTOM_ITEMS], customGeo[CUSTOM_ITEMS];

// Field access through a few out-of-line helpers: inlined ArduinoJson lookups at
// every use cost several KB of flash.
__attribute__((noinline)) const char* js(JsonObjectConst o, const char* k, const char* d) {
  const char* v = o[k];
  return v ? v : d;
}
__attribute__((noinline)) bool jhas(JsonObjectConst o, const char* k) { return !o[k].isNull(); }

// "#rgb" / "#rrggbb" -> RGB565.
uint16_t hex565(const char* s, uint16_t fallback) {
  if (!s || *s != '#') return fallback;
  uint32_t n = strtoul(s + 1, nullptr, 16);
  size_t len = strlen(s + 1);
  if (len == 3) n = ((n & 0xf00) * 0x1100) | ((n & 0xf0) * 0x110) | ((n & 0xf) * 0x11);
  else if (len != 6) return fallback;
  return ((n >> 8) & 0xF800) | ((n >> 5) & 0x07E0) | ((n >> 3) & 0x001F);
}

String messageValue(const String& key);

// {name} -> its current value: clock and weather fields, else a pushed variable.
String fieldValue(const String& key, const struct tm& t) {
  char b[24];
  auto num = [&](int v) { snprintf(b, sizeof(b), "%d", v); return String(b); };
  auto two = [&](int v) { snprintf(b, sizeof(b), "%02d", v); return String(b); };
  if (key == "time") { smallTime(b, sizeof(b), t); return b; }
  if (key == "hh") { snprintf(b, sizeof(b), cfg.hour12 ? "%d" : "%02d", clockHour(t)); return b; }
  if (key == "mm") return two(t.tm_min);
  if (key == "ss") return two(t.tm_sec);
  if (key == "ms") return num(millis() % 1000);
  if (key == "ampm") return ampm(t);
  if (key == "day") return num(t.tm_mday);
  if (key == "dow") return dayLong(t.tm_wday);
  if (key == "dow3") return dayShort(t.tm_wday);
  if (key == "month") return monthLong(t.tm_mon);
  if (key == "mon3") return monthShort(t.tm_mon);
  if (key == "year") return num(t.tm_year + 1900);
  if (key == "date") { snprintf(b, sizeof(b), "%d %s", t.tm_mday, monthLong(t.tm_mon)); return b; }
  if (key == "unit") return cfg.celsius ? "C" : "F";
  if (key == "wunit") return cfg.mile ? "mph" : "km/h";
  bool w = wx.valid;
  if (key == "temp") return w ? num(displayTemp(wx.temp)) : "--";
  if (key == "feels") return w ? num(displayTemp(wx.feels)) : "--";
  if (key == "hi") return w ? num(displayTemp(wx.tempMax)) : "--";
  if (key == "lo") return w ? num(displayTemp(wx.tempMin)) : "--";
  if (key == "hum") return w ? num(wx.humidity) : "--";
  if (key == "wind") return w ? num((int)lroundf(cfg.mile ? wx.wind * 2.237f : wx.wind * 3.6f)) : "--";
  if (key == "desc") return wxDesc();
  if (key == "main") return wxMain();
  if (key == "city") return wx.cityName;
  if (key == "icon") return wx.icon;
  if (key == "sunrise" || key == "sunset") {
    int m = key == "sunrise" ? wx.sunrise : wx.sunset;
    if (m < 0) return "--";
    snprintf(b, sizeof(b), "%02d:%02d", (m / 60) % 24, m % 60);
    return b;
  }
  if (key.startsWith("msg.")) return messageValue(key.substring(4));
  return faceVar(key);
}

// ---- The Message face's icons: Lucide-style outlines on a 24x24 grid, in flash.
#define ICON_CIRCLE "M2 12a10 10 0 1 0 20 0a10 10 0 1 0-20 0"
const char ICON_INFO[] PROGMEM = ICON_CIRCLE "M12 16v-4M12 8h.01";
const char ICON_SUCCESS[] PROGMEM = ICON_CIRCLE "M9 12l2 2 4-4";
const char ICON_WARNING[] PROGMEM = "M21.73 18l-8-14a2 2 0 0 0-3.48 0l-8 14A2 2 0 0 0 4 21h16a2 2 0 0 0 1.73-3M12 9v4M12 17h.01";
const char ICON_ERROR[] PROGMEM = ICON_CIRCLE "M15 9l-6 6M9 9l6 6";
const char ICON_WAITING[] PROGMEM = ICON_CIRCLE "M9.09 9a3 3 0 0 1 5.83 1c0 2-3 3-3 3M12 17h.01";
const char ICON_RUNNING[] PROGMEM = "M21 12a9 9 0 1 1-6.219-8.56";
const char ICON_BELL[] PROGMEM = "M6 8a6 6 0 0 1 12 0c0 7 3 9 3 9H3s3-2 3-9M10.3 21a1.94 1.94 0 0 0 3.4 0";
const char ICON_DONE[] PROGMEM = "M4 15s1-1 4-1 5 2 8 2 4-1 4-1V3s-1 1-4 1-5-2-8-2-4 1-4 1zM4 22v-7";
struct MessageIcon { const char* name; const char* color; const char* path; };
const MessageIcon MESSAGE_ICON_SET[] = {
    {"info", "#0a84ff", ICON_INFO},       {"success", "#30d158", ICON_SUCCESS}, {"warning", "#ff9f0a", ICON_WARNING},
    {"error", "#ff453a", ICON_ERROR},     {"waiting", "#ffd60a", ICON_WAITING}, {"running", "#64d2ff", ICON_RUNNING},
    {"bell", "#ff9f0a", ICON_BELL},       {"done", "#30d158", ICON_DONE}};

String pgmString(const char* p) {
  String s;
  for (char c; (c = (char)pgm_read_byte(p)); ++p) s += c;
  return s;
}

// A message field for the Message face's template: the icon's path and colours come
// from the icon set, the rest from the message.
String messageValue(const String& key) {
  int slot = messageShowing();
  String icon = messageField(slot, "icon");
  const MessageIcon* def = &MESSAGE_ICON_SET[0];
  for (const MessageIcon& m : MESSAGE_ICON_SET) if (icon == m.name) def = &m;
  String color = messageField(slot, "color");
  if (color.isEmpty()) color = def->color;
  if (key == "icon") return pgmString(def->path);
  if (key == "color") return color;
  if (key == "bg") {   // the colour at a quarter strength: a solid background in its hue
    uint16_t c = hex565(color.c_str(), WHITE);
    char b[8];
    snprintf(b, sizeof(b), "#%02x%02x%02x", ((c >> 11) * 255 / 31) / 4, (((c >> 5) & 63) * 255 / 63) / 4, ((c & 31) * 255 / 31) / 4);
    return b;
  }
  return messageField(slot, key);
}

// {name} is replaced by its value; {name|fallback} uses the fallback while the value
// is empty (a variable not pushed yet, for instance after a restart).
String resolve(const char* tpl, const struct tm& t) {
  String out;
  for (const char* p = tpl; *p; ++p) {
    const char* end = *p == '{' ? strchr(p, '}') : nullptr;
    const char* bar = end ? (const char*)memchr(p, '|', end - p) : nullptr;
    const char* keyEnd = bar ? bar : end;
    if (!end || keyEnd - p > 16 || end - p > 96) { out += *p; continue; }
    String value = fieldValue(String(p + 1).substring(0, keyEnd - p - 1), t);
    if (value.isEmpty() && bar) value = String(bar + 1).substring(0, end - bar - 1);
    out += value;
    p = end;
  }
  return out;
}

// ---- Expressions: numbers, words (strings), + - * / %, comparisons, && || !, ?:,
// and sin cos (degrees), min max abs round. A word compares as text; in arithmetic
// it reads as its leading number. Errors give 0 / "". Values are slices of the
// expression text, so evaluating allocates nothing.
// Numbers are parsed here rather than with strtof/atof, which link ~9 KB of libc.
float parseNum(const char*& s, bool* ok = nullptr) {
  const char* p = s;
  while (*p == ' ') ++p;
  bool neg = *p == '-';
  if (*p == '-' || *p == '+') ++p;
  float v = 0, f = 0.1f;
  bool digits = false;
  while (isdigit((unsigned char)*p)) { v = v * 10 + (*p++ - '0'); digits = true; }
  if (*p == '.') for (++p; isdigit((unsigned char)*p); f *= 0.1f) { v += (*p++ - '0') * f; digits = true; }
  if (digits && (*p == 'e' || *p == 'E') && (isdigit((unsigned char)p[1]) || (strchr("+-", p[1]) && isdigit((unsigned char)p[2])))) {
    bool en = p[1] == '-';
    p += isdigit((unsigned char)p[1]) ? 1 : 2;
    int e = 0;
    while (isdigit((unsigned char)*p)) e = e * 10 + (*p++ - '0');
    while (e-- > 0) v = en ? v / 10 : v * 10;
  }
  if (ok) *ok = digits;
  if (!digits) return 0;
  s = p;
  return neg ? -v : v;
}
float toNum(const char* s) { return parseNum(s); }

String numText(float n) {
  char b[16];
  long m = lroundf(n * 1000);
  if (m % 1000 == 0) snprintf(b, sizeof(b), "%ld", m / 1000);
  else snprintf(b, sizeof(b), "%s%ld.%03ld", m < 0 ? "-" : "", labs(m) / 1000, labs(m) % 1000);
  return b;
}

struct Val { float n = 0; const char* s = nullptr; uint8_t len = 0; bool num = true; };

bool sliceIs(const Val& v, const char* w) { return !v.num && strlen(w) == v.len && !strncmp(v.s, w, v.len); }
bool truth(const Val& v) {
  if (v.num) return v.n != 0;
  return v.len && !sliceIs(v, "0") && !sliceIs(v, "false") && !sliceIs(v, "off");
}
float numOf(const Val& v) { const char* s = v.s; return v.num ? v.n : parseNum(s); }
Val number(float n) { Val v; v.n = n; return v; }

class Expr {
 public:
  explicit Expr(const char* src) : p(src) {}
  Val parse() { return ternary(); }

 private:
  const char* p;
  void ws() { while (*p == ' ') ++p; }
  bool eat(const char* tok) {
    ws();
    size_t n = strlen(tok);
    if (strncmp(p, tok, n)) return false;
    p += n;
    return true;
  }
  Val ternary() {
    Val c = orOp();
    if (!eat("?")) return c;
    Val a = ternary(), b;
    if (eat(":")) b = ternary();
    return truth(c) ? a : b;
  }
  Val orOp() {
    Val a = andOp();
    while (eat("||")) { bool l = truth(a); a = number(truth(andOp()) || l); }
    return a;
  }
  Val andOp() {
    Val a = cmp();
    while (eat("&&")) { bool l = truth(a); a = number(truth(cmp()) && l); }
    return a;
  }
  Val cmp() {
    Val a = add();
    static const char* const ops[] = {"==", "!=", "<=", ">=", "<", ">"};
    for (int i = 0; i < 6; ++i) {
      if (!eat(ops[i])) continue;
      Val b = add();
      if (i < 2 && (!a.num || !b.num)) {   // text: equal only if both are the same word
        bool same = !a.num && !b.num && a.len == b.len && !strncmp(a.s, b.s, a.len);
        return number(same == (i == 0));
      }
      float x = numOf(a), y = numOf(b);
      bool r[] = {x == y, x != y, x <= y, x >= y, x < y, x > y};
      return number(r[i]);
    }
    return a;
  }
  Val add() {
    Val a = mul();
    for (;;) {
      if (eat("+")) { float l = numOf(a); a = number(l + numOf(mul())); }
      else if (eat("-")) { float l = numOf(a); a = number(l - numOf(mul())); }
      else return a;
    }
  }
  Val mul() {
    Val a = unary();
    for (;;) {
      ws();
      char op = *p;
      if (op != '*' && op != '/' && op != '%') return a;
      ++p;
      float l = numOf(a), r = numOf(unary());
      a = number(op == '*' ? l * r : !r ? 0 : op == '/' ? l / r : l - r * (long)(l / r));
    }
  }
  Val unary() {
    if (eat("-")) return number(-numOf(unary()));
    if (eat("!")) return number(!truth(unary()));
    return primary();
  }
  Val primary() {
    if (eat("(")) { Val v = ternary(); eat(")"); return v; }
    ws();
    const char* start = p;
    while (*p && !strchr("+-*/%()?:<>=!&|,", *p)) ++p;
    const char* end = p;
    while (end > start && end[-1] == ' ') --end;
    Val v;
    v.s = start; v.len = std::min(255, (int)(end - start)); v.num = false;
    ws();
    if (*p == '(' && v.len) {   // a function call
      ++p;
      float a = numOf(ternary()), b = eat(",") ? numOf(ternary()) : 0;
      eat(")");
      if (sliceIs(v, "sin")) return number(sinf(a * DEG_TO_RAD));
      if (sliceIs(v, "cos")) return number(cosf(a * DEG_TO_RAD));
      if (sliceIs(v, "min")) return number(std::min(a, b));
      if (sliceIs(v, "max")) return number(std::max(a, b));
      if (sliceIs(v, "abs")) return number(fabsf(a));
      if (sliceIs(v, "round")) return number(lroundf(a));
      return number(0);
    }
    const char* s = start;
    bool ok;
    float n = parseNum(s, &ok);
    if (ok && s == end) return number(n);   // the whole word is a number
    return v;
  }
};

bool hasOperator(const String& s) {
  for (unsigned i = 0; i < s.length(); ++i) if (strchr("+-*/%()?:<>=!&|", s[i]) && !(i == 0 && s[i] == '-')) return true;
  return false;
}
float evalNum(const String& s) { return hasOperator(s) ? numOf(Expr(s.c_str()).parse()) : toNum(s.c_str()); }
String evalStr(const String& s) {
  if (!hasOperator(s)) return s;
  Val v = Expr(s.c_str()).parse();
  if (v.num) return numText(v.n);
  String out;
  out.concat(v.s, v.len);
  return out;
}

// A number field: a JSON number, or a string of placeholders and an expression.
__attribute__((noinline)) float num(JsonObjectConst o, const char* k, float d, const struct tm& t) {
  JsonVariantConst v = o[k];
  if (v.is<float>()) return v.as<float>();
  if (v.is<const char*>()) return evalNum(resolve(v.as<const char*>(), t));
  return d;
}
int inum(JsonObjectConst o, const char* k, int d, const struct tm& t) { return (int)lroundf(num(o, k, d, t)); }

__attribute__((noinline)) bool visible(JsonObjectConst it, const struct tm& t) {
  JsonVariantConst v = it["show"];
  if (v.is<bool>()) return v.as<bool>();
  const char* s = v;
  if (!s) return true;
  String r = resolve(s, t);
  if (hasOperator(r)) return truth(Expr(r.c_str()).parse());
  return r.length() && r != "0" && r != "false" && r != "off";
}

// ---- Paint: a solid colour or a linear gradient ({"stops":[..],"angle":deg} or
// {"from":..,"to":..}; angle as in CSS: 180 = top to bottom, 90 = left to right).
struct Paint {
  uint16_t c[4] = {WHITE};
  uint8_t n = 1;
  float x0 = 0, y0 = 0, dx = 0, dy = 0;   // t = (x - x0) * dx + (y - y0) * dy, 0..1
  uint16_t first() const { return c[0]; }
};

uint16_t colorOf(JsonVariantConst v, uint16_t fallback, const struct tm& t) {
  if (v.is<int>()) return (uint16_t)v.as<int>();   // already RGB565
  const char* s = v;
  if (!s) return fallback;
  if (strchr(s, '{') || hasOperator(s)) return hex565(evalStr(resolve(s, t)).c_str(), fallback);
  return hex565(s, fallback);
}

// The gradient spans the box (x, y, w, h) the paint is used for.
Paint paintOf(JsonVariantConst v, uint16_t fallback, int x, int y, int w, int h, const struct tm& t) {
  Paint p;
  if (!v.is<JsonObjectConst>()) { p.c[0] = colorOf(v, fallback, t); return p; }
  JsonObjectConst g = v;
  if (g["stops"].is<JsonArrayConst>()) {
    p.n = 0;
    for (JsonVariantConst s : g["stops"].as<JsonArrayConst>()) {
      if (p.n == 4) break;
      p.c[p.n++] = colorOf(s, fallback, t);
    }
  } else {
    p.n = 2;
    p.c[0] = colorOf(g["from"], fallback, t);
    p.c[1] = colorOf(g["to"], fallback, t);
  }
  if (p.n < 2) { if (!p.n) p.c[0] = fallback; p.n = 1; return p; }
  float a = num(g, "angle", 180, t) * DEG_TO_RAD, sx = sinf(a), sy = -cosf(a);
  float len = fabsf(w * sx) + fabsf(h * sy);   // the CSS gradient line
  if (len < 1) len = 1;
  p.dx = sx / len; p.dy = sy / len;
  p.x0 = x + w / 2.0f;   // t = 0.5 at the box's centre
  p.y0 = y + h / 2.0f;
  return p;
}

uint16_t lerp565(uint16_t a, uint16_t b, int f) {   // f: 0..256
  int r = ((a >> 11) * (256 - f) + (b >> 11) * f) >> 8;
  int g = (((a >> 5) & 63) * (256 - f) + ((b >> 5) & 63) * f) >> 8;
  int bl = ((a & 31) * (256 - f) + (b & 31) * f) >> 8;
  return (r << 11) | (g << 5) | bl;
}

uint16_t paintAt(const Paint& p, float x, float y) {
  if (p.n < 2) return p.c[0];
  float t = (x - p.x0) * p.dx + (y - p.y0) * p.dy + 0.5f;
  t = std::min(1.0f, std::max(0.0f, t)) * (p.n - 1);
  int seg = std::min((int)t, p.n - 2);
  return lerp565(p.c[seg], p.c[seg + 1], (int)((t - seg) * 256));
}

// One horizontal span [x0, x1] of row y, in runs of equal colour. The gradient
// position steps in 16.16 fixed point: the ESP8266 has no FPU.
void span(const Paint& p, int x0, int x1, int y) {
  x0 = std::max(x0, 0); x1 = std::min(x1, SCR_W - 1);
  if (y < 0 || y >= SCR_H || x1 < x0) return;
  if (p.n < 2) { D().drawFastHLine(x0, y, x1 - x0 + 1, p.c[0]); return; }
  int32_t top = (p.n - 1) << 16;
  int32_t t = (int32_t)(((x0 + 0.5f - p.x0) * p.dx + (y + 0.5f - p.y0) * p.dy + 0.5f) * top);
  int32_t dt = (int32_t)(p.dx * top);
  int runStart = x0;
  uint16_t run = 0;
  for (int x = x0; x <= x1 + 1; ++x, t += dt) {
    uint16_t c = run;
    if (x <= x1) {
      int32_t tc = std::min(top, std::max((int32_t)0, t));
      int seg = std::min(tc >> 16, (int32_t)p.n - 2);
      c = lerp565(p.c[seg], p.c[seg + 1], (tc - (seg << 16)) >> 8);
    }
    if (x == x0) run = c;
    else if (c != run || x > x1) { D().drawFastHLine(runStart, y, x - runStart, run); runStart = x; run = c; }
  }
}

// Writes one row of coverage (0..255): full runs as spans, edge pixels blended into
// `under` (the face background: the panel cannot be read back).
uint8_t cov[SCR_W];
void emitRow(int lo, int hi, int y, const Paint& paint, uint16_t under) {
  for (int x = lo; x <= hi;) {
    if (cov[x] >= 250) {
      int e = x;
      while (e + 1 <= hi && cov[e + 1] >= 250) ++e;
      span(paint, x, e, y);
      x = e + 1;
    } else {
      if (cov[x]) D().drawPixel(x, y, lerp565(under, paintAt(paint, x + 0.5f, y + 0.5f), cov[x] + 1));
      ++x;
    }
  }
}

// Rounded rectangle (r >= 0) filled with a paint, row by row.
void paintRect(const Paint& p, int x, int y, int w, int h, int r) {
  r = std::min(r, std::min(w, h) / 2);
  for (int j = 0; j < h; ++j) {
    int inset = 0;
    int dy = j < r ? r - j : j >= h - r ? j - (h - 1 - r) : 0;
    if (dy) inset = r - (int)lroundf(sqrtf(std::max(0, r * r - dy * dy)));
    span(p, x + inset, x + w - 1 - inset, y + j);
  }
}

// ---- Paths: SVG path data (M L H V C S Q T A Z, absolute and relative) scaled from a
// viewBox, filled (non-zero rule, anti-aliased edges) and/or stroked.
struct Poly {
  static constexpr int MAX_PTS = 400, MAX_RINGS = 32;   // 3.2 KB of heap while drawing
  float* x = new (std::nothrow) float[MAX_PTS];
  float* y = new (std::nothrow) float[MAX_PTS];
  int n = 0, rings = 0, ringStart[MAX_RINGS + 1] = {0};
  float sx = 1, sy = 1, ox = 0, oy = 0, cosr = 1, sinr = 0, cx = 0, cy = 0;
  ~Poly() { delete[] x; delete[] y; }
  void map(float vx, float vy, float& X, float& Y) const {   // viewBox -> screen
    float px = vx * sx + ox - cx, py = vy * sy + oy - cy;
    X = px * cosr - py * sinr + cx;
    Y = px * sinr + py * cosr + cy;
  }
  void move() { if (rings < MAX_RINGS && n > ringStart[rings]) ringStart[++rings] = n; }
  bool ok() const { return x && y; }
  void add(float vx, float vy) {
    if (n >= MAX_PTS || !ok()) return;
    map(vx, vy, x[n], y[n]);
    ++n;
  }
  void close() { move(); }
  int ringCount() const { return n > ringStart[rings] ? rings + 1 : rings; }
  int ringEnd(int r) const { return r < rings ? ringStart[r + 1] : n; }
};

int curveSteps(float len) { return std::max(2, std::min(24, (int)(len / 3))); }

// atan2 to ~1e-5 rad, without libm's atan2f/atanf.
float fastAtan2(float y, float x) {
  float ax = fabsf(x), ay = fabsf(y);
  if (ax < 1e-9f && ay < 1e-9f) return 0;
  float a = std::min(ax, ay) / std::max(ax, ay), s = a * a;
  float r = ((-0.0464964749f * s + 0.15931422f) * s - 0.327622764f) * s * a + a;
  if (ay > ax) r = 1.57079637f - r;
  if (x < 0) r = 3.14159274f - r;
  return y < 0 ? -r : r;
}

void arcTo(Poly& P, float x1, float y1, float rx, float ry, float rotDeg, bool large, bool sweep, float x2, float y2) {
  if (rx == 0 || ry == 0) { P.add(x2, y2); return; }
  rx = fabsf(rx); ry = fabsf(ry);
  float phi = rotDeg * DEG_TO_RAD, cp = cosf(phi), sp = sinf(phi);
  float dx = (x1 - x2) / 2, dy = (y1 - y2) / 2;
  float x1p = cp * dx + sp * dy, y1p = -sp * dx + cp * dy;
  float lam = (x1p * x1p) / (rx * rx) + (y1p * y1p) / (ry * ry);
  if (lam > 1) { float s = sqrtf(lam); rx *= s; ry *= s; }
  float top = rx * rx * ry * ry - rx * rx * y1p * y1p - ry * ry * x1p * x1p;
  float den = rx * rx * y1p * y1p + ry * ry * x1p * x1p;
  float co = den ? sqrtf(std::max(0.0f, top / den)) : 0;
  if (large == sweep) co = -co;
  float cxp = co * rx * y1p / ry, cyp = -co * ry * x1p / rx;
  float ccx = cp * cxp - sp * cyp + (x1 + x2) / 2, ccy = sp * cxp + cp * cyp + (y1 + y2) / 2;
  float a1 = fastAtan2((y1p - cyp) / ry, (x1p - cxp) / rx);
  float a2 = fastAtan2((-y1p - cyp) / ry, (-x1p - cxp) / rx);
  float da = a2 - a1;
  if (sweep && da < 0) da += 2 * PI;
  if (!sweep && da > 0) da -= 2 * PI;
  int steps = curveSteps(fabsf(da) * std::max(rx, ry) * std::max(fabsf(P.sx), fabsf(P.sy)));
  for (int i = 1; i <= steps; ++i) {
    float a = a1 + da * i / steps, ex = rx * cosf(a), ey = ry * sinf(a);
    P.add(cp * ex - sp * ey + ccx, sp * ex + cp * ey + ccy);
  }
}

// A cubic Bezier from (x0, y0); quadratics are passed in as cubics.
__attribute__((noinline)) void cubicTo(Poly& P, float x0, float y0, float x1, float y1, float x2, float y2, float x3, float y3) {
  float len = (fabsf(x1 - x0) + fabsf(y1 - y0) + fabsf(x2 - x1) + fabsf(y2 - y1) + fabsf(x3 - x2) + fabsf(y3 - y2)) *
              std::max(fabsf(P.sx), fabsf(P.sy));
  int steps = curveSteps(len);
  for (int i = 1; i <= steps; ++i) {
    float u = (float)i / steps, v = 1 - u, a = v * v * v, b = 3 * v * v * u, c = 3 * v * u * u, d = u * u * u;
    P.add(a * x0 + b * x1 + c * x2 + d * x3, a * y0 + b * y1 + c * y2 + d * y3);
  }
}

struct PathReader {
  const char* d;
  void skip() { while (*d == ' ' || *d == ',' || *d == '\n' || *d == '\t' || *d == '\r') ++d; }
  __attribute__((noinline)) bool num(float& out) {
    skip();
    bool ok;
    out = parseNum(d, &ok);
    return ok;
  }
  bool nums(float* out, int n) { for (int i = 0; i < n; ++i) if (!num(out[i])) return false; return true; }
  bool flag(bool& out) {   // arc flags may be packed: "a1 1 0 01 2 3"
    skip();
    if (*d != '0' && *d != '1') return false;
    out = *d++ == '1';
    return true;
  }
};

void parsePath(Poly& P, const char* src) {
  PathReader R{src};
  char cmd = 0, last = 0;
  float cx = 0, cy = 0, sx = 0, sy = 0, qx = 0, qy = 0;   // current, subpath start, last control
  float a[7];
  for (;;) {
    R.skip();
    if (!*R.d) break;
    if (isalpha((unsigned char)*R.d)) cmd = *R.d++;
    else if (!cmd) break;
    bool rel = islower((unsigned char)cmd);
    float ox = rel ? cx : 0, oy = rel ? cy : 0;
    char c = tolower(cmd);
    bool smooth = (c == 's' && strchr("CcSs", last)) || (c == 't' && strchr("QqTt", last));
    float rx = smooth ? 2 * cx - qx : cx, ry = smooth ? 2 * cy - qy : cy;   // reflected control
    if (c == 'm') {
      if (!R.nums(a, 2)) return;
      P.move(); cx = sx = a[0] + ox; cy = sy = a[1] + oy; P.add(cx, cy);
      cmd = rel ? 'l' : 'L';   // further pairs are line-tos
    } else if (c == 'l' || c == 'h' || c == 'v') {
      if (!R.nums(a, c == 'l' ? 2 : 1)) return;
      if (c == 'l') { cx = a[0] + ox; cy = a[1] + oy; }
      else if (c == 'h') cx = a[0] + ox;
      else cy = a[0] + oy;
      P.add(cx, cy);
    } else if (c == 'c' || c == 's') {
      float* q = c == 'c' ? a : a + 2;
      if (c == 's') { a[0] = rx - ox; a[1] = ry - oy; }
      if (!R.nums(q, c == 'c' ? 6 : 4)) return;
      cubicTo(P, cx, cy, a[0] + ox, a[1] + oy, a[2] + ox, a[3] + oy, a[4] + ox, a[5] + oy);
      qx = a[2] + ox; qy = a[3] + oy; cx = a[4] + ox; cy = a[5] + oy;
    } else if (c == 'q' || c == 't') {
      float* q = c == 'q' ? a : a + 2;
      if (c == 't') { a[0] = rx - ox; a[1] = ry - oy; }
      if (!R.nums(q, c == 'q' ? 4 : 2)) return;
      float kx = a[0] + ox, ky = a[1] + oy, ex = a[2] + ox, ey = a[3] + oy;
      cubicTo(P, cx, cy, cx + (kx - cx) * 2 / 3, cy + (ky - cy) * 2 / 3, ex + (kx - ex) * 2 / 3, ey + (ky - ey) * 2 / 3, ex, ey);
      qx = kx; qy = ky; cx = ex; cy = ey;
    } else if (c == 'a') {
      bool large, sweep;
      if (!R.nums(a, 3) || !R.flag(large) || !R.flag(sweep) || !R.nums(a + 3, 2)) return;
      arcTo(P, cx, cy, a[0], a[1], a[2], large, sweep, a[3] + ox, a[4] + oy);
      cx = a[3] + ox; cy = a[4] + oy;
    } else if (c == 'z') {
      if (P.n) P.add(sx, sy);
      P.close(); cx = sx; cy = sy;
    } else {
      return;
    }
    last = cmd;
  }
}

// Non-zero (or even-odd) fill with 4 sub-scanlines per row and fractional span ends.
void fillPoly(const Poly& P, const Paint& paint, uint16_t under, bool evenOdd) {
  if (P.n < 3) return;
  float minY = 1e9f, maxY = -1e9f;
  for (int i = 0; i < P.n; ++i) { minY = std::min(minY, P.y[i]); maxY = std::max(maxY, P.y[i]); }
  int y0 = std::max(0, (int)floorf(minY)), y1 = std::min(SCR_H - 1, (int)ceilf(maxY));
  constexpr int SUB = 4, MAX_X = 64;
  float xs[MAX_X];
  int8_t dir[MAX_X];
  for (int y = y0; y <= y1; ++y) {
    yield();   // see strokePoly
    EMU_WORK(150);
    memset(cov, 0, sizeof(cov));
    int lo = SCR_W, hi = -1;
    for (int s = 0; s < SUB; ++s) {
      float sy = y + (s + 0.5f) / SUB;
      int k = 0;
      for (int r = 0, rings = P.ringCount(); r < rings; ++r) {
        int a0 = P.ringStart[r], a1 = P.ringEnd(r);
        for (int i = a0; i < a1; ++i) {
          int j = i + 1 < a1 ? i + 1 : a0;   // rings close implicitly
          float ya = P.y[i], yb = P.y[j];
          EMU_WORK(150);
          if ((ya <= sy) == (yb <= sy) || k >= MAX_X) continue;
          EMU_WORK(800);
          float xa = P.x[i] + (sy - ya) * (P.x[j] - P.x[i]) / (yb - ya);
          int m = k++;   // insertion sort by x
          while (m > 0 && xs[m - 1] > xa) { xs[m] = xs[m - 1]; dir[m] = dir[m - 1]; --m; }
          xs[m] = xa; dir[m] = yb > ya ? 1 : -1;
        }
      }
      int wind = 0;
      for (int i = 0; i + 1 < k; ++i) {
        wind = evenOdd ? (wind ^ 1) : wind + dir[i];
        if (!wind) continue;
        float xa = std::max(0.0f, xs[i]), xb = std::min((float)SCR_W, xs[i + 1]);
        if (xb <= xa) continue;
        int ia = (int)xa, ib = (int)xb;
        lo = std::min(lo, ia); hi = std::max(hi, std::min(ib, SCR_W - 1));
        auto addCov = [&](int x, float amount) { EMU_WORK(400); if (x >= 0 && x < SCR_W) cov[x] = std::min(255, cov[x] + (int)(amount * 255 / SUB + 0.5f)); };
        if (ia == ib) { addCov(ia, xb - xa); continue; }
        addCov(ia, 1 - (xa - ia));
        for (int x = ia + 1; x < ib; ++x) addCov(x, 1);
        addCov(ib, xb - ib);
      }
    }
    emitRow(lo, hi, y, paint, under);
  }
}

// A stroke as one shape: each pixel's coverage comes from its distance to the nearest
// segment, so joints overlap without seams and ends and corners come out round.
void strokePoly(const Poly& P, float width, const Paint& paint, uint16_t under) {
  float r = std::max(0.5f, width / 2);
  // Coverage is r + 0.5 - distance: none beyond rOut, full within rIn. Comparing squared
  // distances leaves the square root to the edge pixels (floats are software here).
  float rOut = r + 0.5f, rIn = r - 0.5f, out2 = rOut * rOut, in2 = rIn * rIn;
  float minY = 1e9f, maxY = -1e9f;
  for (int i = 0; i < P.n; ++i) { minY = std::min(minY, P.y[i]); maxY = std::max(maxY, P.y[i]); }
  int y0 = std::max(0, (int)floorf(minY - r - 1)), y1 = std::min(SCR_H - 1, (int)ceilf(maxY + r + 1));
  for (int y = y0; y <= y1; ++y) {
    // A large glyph takes over a second without an FPU; the software watchdog resets
    // the chip after about 3 s without a yield.
    yield();
    EMU_WORK(150);
    memset(cov, 0, sizeof(cov));
    int lo = SCR_W, hi = -1;
    float py = y + 0.5f;
    for (int ring = 0, rings = P.ringCount(); ring < rings; ++ring) {
      int a0 = P.ringStart[ring], a1 = P.ringEnd(ring);
      for (int i = a0; i < a1; ++i) {
        int j = i + 1 < a1 ? i + 1 : i;   // a lone point still draws a dot
        float ax = P.x[i], ay = P.y[i], dx = P.x[j] - ax, dy = P.y[j] - ay;
        EMU_WORK(300);
        if (py < std::min(ay, ay + dy) - r - 1 || py > std::max(ay, ay + dy) + r + 1) continue;
        EMU_WORK(2000);
        float len2 = dx * dx + dy * dy, inv = len2 ? 1 / len2 : 0;
        float left = std::min(ax, ax + dx) - r - 1, right = std::max(ax, ax + dx) + r + 1;
        if (dy != 0) {   // only where this row is within rOut of the segment's line
          float mid = ax + (py - ay) * dx / dy, half = (rOut + 1) * sqrtf(len2) / fabsf(dy);
          left = std::max(left, mid - half); right = std::min(right, mid + half);
        }
        int xa = std::max(0, (int)floorf(left)), xb = std::min(SCR_W - 1, (int)ceilf(right));
        for (int x = xa; x <= xb; ++x) {
          EMU_WORK(900);
          float px = x + 0.5f, u = ((px - ax) * dx + (py - ay) * dy) * inv;
          u = std::min(1.0f, std::max(0.0f, u));
          float ex = px - ax - u * dx, ey = py - ay - u * dy, d2 = ex * ex + ey * ey;
          if (d2 >= out2) continue;
          if (!(rIn >= 0 && d2 <= in2)) EMU_WORK(600);
          int c = rIn >= 0 && d2 <= in2 ? 255 : (int)((rOut - sqrtf(d2)) * 255);
          if (c <= 0) continue;
          cov[x] = std::max<int>(cov[x], std::min(c, 255));
          lo = std::min(lo, x); hi = std::max(hi, x);
        }
        if (j == i) break;
      }
    }
    emitRow(lo, hi, y, paint, under);
  }
}

// {"type":"path","d":"M..." or ["M...","M..."],"x","y","size" (or "w","h"),
//  "vb":"0 0 24 24","align","rotate","fill","stroke","sw","rule":"evenodd"}
void drawPath(JsonObjectConst it, const struct tm& t, uint16_t under) {
  float vb[4] = {0, 0, 24, 24};
  if (const char* s = it["vb"]) { PathReader R{s}; R.nums(vb, 4); }

  if (vb[2] <= 0 || vb[3] <= 0) return;
  float size = num(it, "size", 48, t), w = num(it, "w", size, t), h = num(it, "h", size, t);
  float x = num(it, "x", 0, t), y = num(it, "y", 0, t);
  uint8_t datum = 0;
  if (const char* a = it["align"]) {
    static const char* const names[] = {"tl", "tc", "tr", "ml", "mc", "mr", "bl", "bc", "br"};
    for (uint8_t i = 0; i < 9; ++i) if (!strcmp(a, names[i])) datum = i;
  }
  x -= (datum % 3) * w / 2;
  y -= (datum / 3) * h / 2;
  Poly P;
  if (!P.ok()) return;
  P.sx = w / vb[2]; P.sy = h / vb[3];
  P.ox = x - vb[0] * P.sx; P.oy = y - vb[1] * P.sy;
  P.cx = x + w / 2; P.cy = y + h / 2;
  float rot = num(it, "rotate", 0, t) * DEG_TO_RAD;
  P.cosr = cosf(rot); P.sinr = sinf(rot);
  JsonVariantConst d = it["d"];
  auto parse = [&](const char* data) {
    if (!strchr(data, '{')) { parsePath(P, data); return; }
    String r = resolve(data, t);
    parsePath(P, r.c_str());
  };
  if (d.is<JsonArrayConst>()) for (JsonVariantConst part : d.as<JsonArrayConst>()) parse(part | "");
  else parse(d | "");
  P.close();
  bool hasStroke = jhas(it, "stroke");
  JsonVariantConst fillV = it["fill"];
  bool fill = !(fillV.is<bool>() && !fillV.as<bool>()) && !(fillV.is<const char*>() && !strcmp(fillV.as<const char*>(), "none"));
  if (hasStroke && fillV.isNull()) fill = false;   // a stroke alone: an outline icon
  if (fill) {
    JsonVariantConst src = fillV.isNull() || fillV.is<bool>() ? it["color"] : fillV;
    Paint p = paintOf(src, WHITE, (int)x, (int)y, (int)w, (int)h, t);
    const char* rule = it["rule"];
    fillPoly(P, p, under, rule && !strcmp(rule, "evenodd"));
  }
  if (hasStroke) {
    float sw = num(it, "sw", 2, t) * std::max(P.sx, P.sy);
    strokePoly(P, sw, paintOf(it["stroke"], WHITE, (int)x, (int)y, (int)w, (int)h, t), under);
  }
}

// ---- Text, possibly wrapped to "w" pixels over at most "lines" lines.
void drawText(JsonObjectConst it, const struct tm& t, int x, int y, uint16_t c, uint16_t bg) {
  String s = resolve(js(it, "text", ""), t);
  int font = inum(it, "font", 4, t);
  if (font == 8) font = 6;   // font 8 left the build (flash); 6 is the next digit font
  if (font != 2 && font != 4 && font != 6 && font != 8) font = 4;
  static const char* const names[] = {"tl", "tc", "tr", "ml", "mc", "mr", "bl", "bc", "br"};
  uint8_t datum = TL_DATUM;
  const char* a = js(it, "align", "tl");
  for (uint8_t i = 0; i < 9; ++i) if (!strcmp(a, names[i])) datum = i;
  bool opaque = jhas(it, "bg");
  int w = inum(it, "w", 0, t);
  if (w <= 0) {
    if (!opaque) textClear(s.c_str(), x, y, font, c, datum);
    else text(s.c_str(), x, y, font, c, bg, datum, inum(it, "pad", 0, t));
    return;
  }
  String lines[8];
  int count = 0, maxLines = std::max(1, std::min(8, inum(it, "lines", 8, t)));
  String line;
  for (int i = 0; i <= (int)s.length() && count < maxLines; ++i) {
    bool end = i == (int)s.length();
    if (!end && s[i] != ' ' && s[i] != '\n') { line += s[i]; continue; }
    // A word ended: does it still fit on this line?
    int cut = line.lastIndexOf(' ');
    if (D().textWidth(line, font) > w && cut > 0) {
      lines[count++] = line.substring(0, cut);
      line = line.substring(cut + 1);
      if (count == maxLines) break;
    }
    if (end || s[i] == '\n') { lines[count++] = line; line = ""; }
    else line += ' ';
  }
  int lh = D().fontHeight(font) + inum(it, "lh", 2, t), total = count * lh - (count ? inum(it, "lh", 2, t) : 0);
  int top = y - (datum / 3) * total / 2;
  uint8_t lineDatum = datum % 3;   // TL, TC or TR
  for (int i = 0; i < count; ++i) {
    if (!opaque) textClear(lines[i].c_str(), x, top + i * lh, font, c, lineDatum);
    else text(lines[i].c_str(), x, top + i * lh, font, c, bg, lineDatum, w);
  }
}

bool itemOpaque(JsonObjectConst it) {
  const char* type = js(it, "type", "");
  if (!strcmp(type, "ring") || !strcmp(type, "bar")) return true;
  return jhas(it, "bg") && (!strcmp(type, "text") || !strcmp(type, "icon"));
}

// FNV-1a over the resolved values of every placeholder-bearing string in an item.
// "content" keys (what an opaque item redraws in place) and the others (position,
// size, visibility: these need the whole face redrawn) hash separately.
void mixValues(JsonVariantConst v, const char* key, const struct tm& t, uint32_t& content, uint32_t& geo) {
  if (v.is<JsonObjectConst>()) { for (JsonPairConst kv : v.as<JsonObjectConst>()) mixValues(kv.value(), key, t, content, geo); return; }
  if (v.is<JsonArrayConst>()) { for (JsonVariantConst e : v.as<JsonArrayConst>()) mixValues(e, key, t, content, geo); return; }
  const char* s = v;
  if (!s || !strchr(s, '{')) return;
  static const char* const contentKeys[] = {"text", "value", "max", "icon", "color", "track", "fill", "stroke"};
  bool isContent = false;
  for (const char* k : contentKeys) if (!strcmp(key, k)) isContent = true;
  uint32_t& h = isContent ? content : geo;
  String r = resolve(s, t);
  for (unsigned i = 0; i < r.length(); ++i) h = (h ^ (uint8_t)r[i]) * 16777619u;
  h = (h ^ '|') * 16777619u;
}

void ring(int x, int y, int r, int w, int value, int max, uint16_t c, uint16_t track, uint16_t bg) {
  int ir = std::max(0, r - w);
  D().drawSmoothArc(x, y, r, ir, 0, 360, track, bg);
  if (max <= 0 || value <= 0) return;
  int deg = std::min(360, value * 360 / max);
  // TFT_eSPI arcs start at 6 o'clock; progress starts at 12, so 180 + deg, split at 360.
  if (deg >= 360) { D().drawSmoothArc(x, y, r, ir, 0, 360, c, bg); return; }
  int end = 180 + deg;
  if (end <= 360) D().drawSmoothArc(x, y, r, ir, 180, end, c, bg);
  else { D().drawSmoothArc(x, y, r, ir, 180, 360, c, bg); D().drawSmoothArc(x, y, r, ir, 0, end - 360, c, bg); }
}

void drawItem(JsonObjectConst it, const struct tm& t, uint16_t faceBg) {
  if (!visible(it, t)) return;
  const char* type = js(it, "type", "");
  int x = inum(it, "x", 0, t), y = inum(it, "y", 0, t);
  uint16_t bg = colorOf(it["bg"], faceBg, t);
  if (!strcmp(type, "text")) {
    drawText(it, t, x, y, colorOf(it["color"], WHITE, t), bg);
  } else if (!strcmp(type, "rect")) {
    int w = inum(it, "w", 0, t), h = inum(it, "h", 0, t), r = inum(it, "r", 0, t);
    Paint p = paintOf(it["color"], WHITE, x, y, w, h, t);
    bool fill = !it["fill"].is<bool>() || it["fill"].as<bool>();
    if (fill && p.n == 1) { if (r) D().fillSmoothRoundRect(x, y, w, h, r, p.first(), faceBg); else D().fillRect(x, y, w, h, p.first()); }
    else if (fill) paintRect(p, x, y, w, h, r);
    else if (r) D().drawRoundRect(x, y, w, h, r, p.first());
    else D().drawRect(x, y, w, h, p.first());
  } else if (!strcmp(type, "circle")) {
    int r = inum(it, "r", 10, t);
    Paint p = paintOf(it["color"], WHITE, x - r, y - r, 2 * r, 2 * r, t);
    bool fill = !it["fill"].is<bool>() || it["fill"].as<bool>();
    if (!fill) D().drawSmoothCircle(x, y, r, p.first(), faceBg);
    else if (p.n == 1) D().fillSmoothCircle(x, y, r, p.first(), faceBg);
    else paintRect(p, x - r, y - r, 2 * r + 1, 2 * r + 1, r);
  } else if (!strcmp(type, "line")) {
    float w = num(it, "w", 2, t) / 2.0f;
    D().drawWedgeLine(x, y, num(it, "x2", x, t), num(it, "y2", y, t), w, w, colorOf(it["color"], WHITE, t), faceBg);
  } else if (!strcmp(type, "ring")) {
    ring(x, y, inum(it, "r", 50, t), inum(it, "w", 8, t), inum(it, "value", 0, t), inum(it, "max", 100, t),
         colorOf(it["color"], WHITE, t), colorOf(it["track"], PANEL_HI, t), faceBg);
  } else if (!strcmp(type, "bar")) {
    int w = inum(it, "w", 100, t), h = inum(it, "h", 8, t), r = std::min(h / 2, inum(it, "r", h / 2, t));
    int value = inum(it, "value", 0, t), max = std::max(1, inum(it, "max", 100, t));
    uint16_t track = colorOf(it["track"], PANEL_HI, t);
    D().fillSmoothRoundRect(x, y, w, h, r, track, faceBg);
    int fw = std::min(w, std::max(0, value) * w / max);
    if (fw > 0) D().fillSmoothRoundRect(x, y, std::max(fw, 2 * r), h, r, colorOf(it["color"], WHITE, t), track);
  } else if (!strcmp(type, "path")) {
    drawPath(it, t, faceBg);
  } else if (!strcmp(type, "icon")) {
    int size = inum(it, "size", 48, t);
    if (jhas(it, "bg")) D().fillRect(x - size / 2 - 2, y - size / 2 - 2, size + 4, size + 4, bg);
    weatherIcon(x, y, size, resolve(js(it, "icon", "{icon}"), t), bg, colorOf(it["color"], cfg.color1, t));
  } else if (!strcmp(type, "image")) {
    drawJpeg(String("/photo/") + resolve(js(it, "src", ""), t), x, y);
  }
}

void renderItems(const struct tm& t, bool force);

void custom(const struct tm& t, bool force) {
  if (customName != cfg.customFace || customVer != customFacesVersion()) {
    customName = cfg.customFace;
    customVer = customFacesVersion();
    customDoc.clear();
    File f = LittleFS.open("/faces/" + customName + ".json", "r");
    customLoaded = f && !deserializeJson(customDoc, f) && customDoc["items"].is<JsonArray>();
    if (f) f.close();
    force = true;
  }
  if (!customLoaded) {
    if (force) { D().fillScreen(BG); placeholder(92, tr("Face not found", "Cadran introuvable"), customName.c_str(), nullptr); }
    return;
  }
  renderItems(t, force);
}

// The face in customDoc, item by item.
void renderItems(const struct tm& t, bool force) {
  // A face asks for faster updates with "fps" (default 4, at most 20).
  int fps = std::max(1, std::min(20, (int)(customDoc["fps"] | 4)));
  if (!force && millis() - customEvalAt < 1000u / fps) return;
  customEvalAt = millis();
  JsonVariantConst bgV = customDoc["bg"];
  Paint bgPaint = paintOf(bgV, BG, 0, 0, SCR_W, SCR_H, t);
  uint16_t bg = bgPaint.first();
  JsonArrayConst items = customDoc["items"];
  size_t n = std::min(items.size(), CUSTOM_ITEMS);
  uint32_t h[CUSTOM_ITEMS], g[CUSTOM_ITEMS];
  bool full = force;
  for (size_t i = 0; i < n; ++i) {
    h[i] = g[i] = 2166136261u;
    for (JsonPairConst kv : items[i].as<JsonObjectConst>()) mixValues(kv.value(), kv.key().c_str(), t, h[i], g[i]);
    if (g[i] != customGeo[i] || (h[i] != customHash[i] && !itemOpaque(items[i]))) full = true;
  }
  if (full) {
    if (bgPaint.n > 1) for (int y = 0; y < SCR_H; ++y) span(bgPaint, 0, SCR_W - 1, y);
    else D().fillScreen(bg);
  }
  for (size_t i = 0; i < n; ++i) {
    if (full || h[i] != customHash[i]) drawItem(items[i], t, bg);
    customHash[i] = h[i];
    customGeo[i] = g[i];
  }
}

// ---- The Message face: the message messageShowing() points at, drawn from this
// template by the custom-face renderer ({msg.*} placeholders, see messageValue).
const char MESSAGE_FACE[] PROGMEM = R"({"bg":"{msg.bg}","items":[
{"type":"path","x":120,"y":56,"size":76,"align":"mc","stroke":"{msg.color}","sw":2,"d":"{msg.icon}"},
{"type":"text","x":120,"y":108,"w":228,"lines":1,"text":"{msg.title}","font":4,"align":"tc","color":"{msg.color}"},
{"type":"text","x":120,"y":142,"w":220,"lines":"{msg.pct}>=0?3:4","text":"{msg.text}","font":2,"align":"tc","color":"#f2f2f7"},
{"type":"bar","x":40,"y":204,"w":160,"h":8,"value":"{msg.pct}","color":"{msg.color}","track":"#2c2c2e","show":"{msg.pct}>=0"},
{"type":"text","x":120,"y":228,"text":"{time}","font":2,"align":"mc","color":"#8e8e93"}]})";
const char* const MESSAGE_DOC = "\x01message";   // customName while customDoc holds the template

void message(const struct tm& t, bool force) {
  static uint32_t seen = 0;
  static int seenSlot = -1;
  if (customName != MESSAGE_DOC) {
    customDoc.clear();
    String json = pgmString(MESSAGE_FACE);
    customLoaded = !deserializeJson(customDoc, json.c_str());   // copied into the document
    customName = MESSAGE_DOC;   // custom() reloads its own face when it is next shown
    force = true;
  }
  if (seen != messageVersion() || seenSlot != messageShowing()) {
    seen = messageVersion();
    seenSlot = messageShowing();
    force = true;
  }
  if (customLoaded) renderItems(t, force);
}

// ---- The Big tiles (Big date, Big time, Big weather): one thing each, as large as the
// screen allows, in one style. The fonts stop at 26 px for letters, so the tiles use
// a monoline sans: each glyph a few round-capped strokes on a 20-unit-high grid,
// drawn by the path renderer (the same outline style as the icons). The shapes and
// spacing follow Roboto Mono's centre lines: every glyph sits in a 16.9-unit cell.
const char MG_0[] PROGMEM = "M8.45 0C12 0 14.2 3.7 14.2 8.6v2.8c0 4.9-2.2 8.6-5.75 8.6S2.7 16.3 2.7 11.4V8.6C2.7 3.7 4.9 0 8.45 0";
const char MG_1[] PROGMEM = "M3.4 3.6L9.4 0v20";
const char MG_2[] PROGMEM = "M1.9 5.5C1.9 2.5 4.4 0 7.4 0s5.1 2.4 5.1 5.4c0 1.8-1.2 3.2-2.5 4.6L2.2 20H14";
const char MG_3[] PROGMEM = "M2.1 5.1C2.1 2.2 4.6 0 7.8 0s5.1 2.2 5.1 5c0 2.9-2.3 4.9-5.3 4.9H5.6M7.6 9.9c3.4 0 5.8 2.1 5.8 5.1S11 20 7.9 20s-6.2-2.2-6.2-5.4";
const char MG_4[] PROGMEM = "M11.3 20V0L1.2 14.2H15.1";
const char MG_5[] PROGMEM = "M14.5 0H5.1L4.2 9.4C5.4 8.3 7.2 7.8 9.3 7.8c2.9 0 4.9 2.6 4.9 6s-2 6.2-5 6.2-5.6-2.1-5.6-5.3";
const char MG_6[] PROGMEM = "M11.9 0C6.4 0 2.5 4.4 2.5 12v1.5a5.75 6.3 0 1 0 11.5 0a5.75 6.3 0 1 0-11.5 0";
const char MG_7[] PROGMEM = "M1.8 0H14.4L5.4 20";
const char MG_8[] PROGMEM = "M8.7 0a5.4 4.85 0 1 0 0 9.7a5.4 4.85 0 1 0 0-9.7M8.7 9.7a5.95 5.15 0 1 0 0 10.3a5.95 5.15 0 1 0 0-10.3";
const char MG_9[] PROGMEM = "M2.5 6.55a5.75 6.55 0 1 0 11.5 0a5.75 6.55 0 1 0-11.5 0M14 6.55V8.5C14 15.6 10.2 20 4.9 20";
const char MG_MINUS[] PROGMEM = "M4.5 10.8H12.4";
const char MG_DEG[] PROGMEM = "M5.55 3a2.85 2.85 0 1 0 5.7 0a2.85 2.85 0 1 0-5.7 0";
const char MG_SPACE[] PROGMEM = "";
const char MG_A[] PROGMEM = "M2 20L8.6 0 15.2 20M4.1 13.6h9";
const char MG_B[] PROGMEM = "M3.3 0V20H9.3c3 0 5.3-2.3 5.3-5.15S12.3 9.7 9.3 9.7H3.3M3.3 0h5.1c3 0 5.3 2 5.3 4.85S11.4 9.7 8.4 9.7";
const char MG_C[] PROGMEM = "M14.4 6.1C14 2.4 11.6 0 8.6 0 5 0 2.3 3.5 2.3 8.5v3C2.3 16.5 5 20 8.6 20c3 0 5.4-2.4 5.8-6.1";
const char MG_D[] PROGMEM = "M3.1 0V20H7.8C11.8 20 14.8 16 14.8 11.5v-3C14.8 4 11.8 0 7.8 0z";
const char MG_E[] PROGMEM = "M14.2 0H3.3V20H14.2M3.3 9.7H13";
const char MG_F[] PROGMEM = "M14.5 0H3.5V20M3.5 9.7H13.2";
const char MG_G[] PROGMEM = "M14.4 5.3C13.8 2.2 11.6 0 8.6 0 5 0 2.2 3.5 2.2 8.5v3C2.2 16.5 5 20 8.8 20c2.3 0 4.5-.7 5.7-2.1v-6.6H8.6";
const char MG_H[] PROGMEM = "M2.7 0v20M14 0v20M2.7 9.7H14";
const char MG_I[] PROGMEM = "M2.9 0h11M8.4 0v20M2.9 20h11";
const char MG_J[] PROGMEM = "M13.3 0V14C13.3 17.4 10.8 20 7.6 20S2 17.6 2 14.5";
const char MG_K[] PROGMEM = "M3.5 0v20M15 0L3.5 11.9M7 9.3L15.7 20";
const char MG_L[] PROGMEM = "M3.7 0V20H14.6";
const char MG_M[] PROGMEM = "M2.9 20V0l5.5 13.1L14.2 0v20";
const char MG_N[] PROGMEM = "M2.8 20V0L14 20V0";
const char MG_O[] PROGMEM = "M8.45 0C12.3 0 14.6 3.6 14.6 8.5v3c0 4.9-2.3 8.5-6.15 8.5S2.3 16.4 2.3 11.5v-3C2.3 3.6 4.6 0 8.45 0";
const char MG_P[] PROGMEM = "M3.7 20V0H10c2.9 0 5 2.4 5 5.6s-2.1 5.6-5 5.6H3.7";
const char MG_Q[] PROGMEM = "M8.45 0C12.3 0 14.6 3.6 14.6 8.5v3c0 4.9-2.3 8.5-6.15 8.5S2.3 16.4 2.3 11.5v-3C2.3 3.6 4.6 0 8.45 0M10.4 16.4L14.2 20";
const char MG_R[] PROGMEM = "M3.4 20V0H9c2.9 0 5.1 2.3 5.1 5.5s-2.2 5.6-5.1 5.6H3.4M9.9 11.1L14.9 20";
const char MG_S[] PROGMEM = "M14.5 5.3C14 2.2 11.6 0 8.8 0 5.5 0 3 2.1 3 4.9c0 3 2.6 4.1 5.7 5 3.5 1 6 2.5 6 5.3 0 2.8-2.5 4.8-5.8 4.8S2.8 17.7 2.5 14.6";
const char MG_T[] PROGMEM = "M1.5 0H15.3M8.4 0V20";
const char MG_U[] PROGMEM = "M2.8 0V14c0 3.5 2.5 6 5.65 6s5.65-2.5 5.65-6V0";
const char MG_V[] PROGMEM = "M1.8 0L8.4 20 15 0";
const char MG_W[] PROGMEM = "M1.6 0L4.8 20 8.7 1 12.5 20 15.8 0";
const char MG_X[] PROGMEM = "M2.2 0L15.1 20M14.9 0L2.1 20";
const char MG_Y[] PROGMEM = "M1.6 0L8.2 12.2 14.6 0M8.2 12.2V20";
const char MG_Z[] PROGMEM = "M2.4 0H13.6L2.4 20H14.3";
struct MonoGlyph { char c; const char* path; };
const MonoGlyph MONO[] = {
    {'0', MG_0}, {'1', MG_1}, {'2', MG_2}, {'3', MG_3}, {'4', MG_4}, {'5', MG_5}, {'6', MG_6},
    {'7', MG_7}, {'8', MG_8}, {'9', MG_9}, {'-', MG_MINUS}, {'*', MG_DEG}, {' ', MG_SPACE},
    {'A', MG_A}, {'B', MG_B}, {'C', MG_C}, {'D', MG_D}, {'E', MG_E}, {'F', MG_F}, {'G', MG_G},
    {'H', MG_H}, {'I', MG_I}, {'J', MG_J}, {'K', MG_K}, {'L', MG_L}, {'M', MG_M}, {'N', MG_N},
    {'O', MG_O}, {'P', MG_P}, {'Q', MG_Q}, {'R', MG_R}, {'S', MG_S}, {'T', MG_T}, {'U', MG_U},
    {'V', MG_V}, {'W', MG_W}, {'X', MG_X}, {'Y', MG_Y}, {'Z', MG_Z}};
// Grid units ('*' is the degree sign). MONO_SB: the side bearing left out when centring.
constexpr float MONO_H = 20, MONO_ADV = 16.9f, MONO_SB = 1.5f, MONO_STROKE = 2.3f;

const MonoGlyph* monoGlyph(char c) {
  c = toupper((unsigned char)c);
  for (const MonoGlyph& g : MONO) if (g.c == c) return &g;
  return nullptr;
}

float monoWidth(const char* s) {
  int n = 0;
  for (; *s; ++s) if (monoGlyph(*s)) ++n;
  return n ? n * MONO_ADV - 2 * MONO_SB : 0;
}

// Draws s centred on cx in the box from y to y+maxH, as large as maxW and maxH allow.
void monoText(const char* s, int cx, int y, int maxW, int maxH, uint16_t color) {
  float k = std::min(maxH / (MONO_H + MONO_STROKE), maxW / (monoWidth(s) + MONO_STROKE));
  float x = cx - (monoWidth(s) / 2 + MONO_SB) * k, top = y + (maxH - MONO_H * k) / 2;
  Paint paint;
  paint.c[0] = color;
  for (; *s; ++s) {
    const MonoGlyph* g = monoGlyph(*s);
    if (!g) continue;
    String d = pgmString(g->path);
    if (d.length()) {
      Poly P;
      if (!P.ok()) return;
      P.sx = P.sy = k; P.ox = x; P.oy = top;
      parsePath(P, d.c_str());
      P.close();
      strokePoly(P, MONO_STROKE * k, paint, BG);
    }
    x += MONO_ADV * k;
  }
}

// ---- Weather icons: Erik Flowers' Weather Icons (weather_icons.h), filled as paths.
// A name is "wi-day-<base>", "wi-night-<base>" or "wi-<base>"; custom faces may still
// give OpenWeatherMap-style codes ("10d"), read as the WMO code they came from.
String wiBase(int b) {   // the b-th name in WI_BASE_NAMES
  String s;
  for (const char* p = WI_BASE_NAMES; ; ++p) {
    char c = pgm_read_byte(p);
    if (!c) { if (!b--) return s; s = ""; continue; }
    s += c;
  }
}

int wiIndex(const String& name) {
  bool night = name.endsWith("n");
  int base = -1;
  if (name.length() >= 2 && isdigit((unsigned char)name[0])) {
    static const uint8_t legacy[][2] = {{1, 0}, {2, 2}, {3, 3}, {4, 3}, {9, 61}, {10, 63}, {11, 95}, {13, 71}, {50, 4}};
    int c = name.substring(0, 2).toInt();
    for (auto& l : legacy) if (l[0] == c) base = pgm_read_byte(&WI_WMO[l[1]]);
  } else if (name.startsWith("wi-")) {
    String b = name.substring(3);
    night = b.startsWith("night-");
    if (night) b.remove(0, 6);
    else if (b.startsWith("day-")) b.remove(0, 4);
    for (int i = 0; i < WI_BASES && base < 0; ++i) if (b == wiBase(i)) base = i;
  }
  if (base < 0) return -1;
  return pgm_read_byte(&WI_FIRST[base]) + (night && base < WI_BASES - WI_AGNOSTIC ? 1 : 0);
}

void weatherIcon(int cx, int cy, int s, const String& icon, uint16_t bg, uint16_t color) {
  int k = wiIndex(icon);
  Poly P;
  if (k < 0 || !P.ok()) return;
  P.sx = P.sy = s / (30.0f * WI_UNIT); P.ox = cx - s / 2.0f; P.oy = cy - s / 2.0f;
  for (int i = pgm_read_word(&WI_START[k]), e = pgm_read_word(&WI_START[k + 1]); i < e;) {
    uint8_t x = pgm_read_byte(&WI_DATA[i++]);
    if (x == 0xFF) P.close();
    else P.add(x, pgm_read_byte(&WI_DATA[i++]));
  }
  Paint paint;
  paint.c[0] = color;
  fillPoly(P, paint, bg, false);
}

// ---- Classic's and Simple's HH:MM: digits as tall as font 8's were (55 px), the colon
// as two dots between them. The group is centred, so "9:41" sits in the middle too.
// Only the half that changed is redrawn: a line-font digit takes about 0.1 s.
constexpr int TIME_H = 61;   // the digits' box: 55 px plus the stroke
constexpr float TIME_K = TIME_H / (MONO_H + MONO_STROKE);   // monoText's scale for it

void clockDigits(char* h, char* m, const struct tm& t) {
  snprintf(h, 4, cfg.hour12 ? "%d" : "%02d", clockHour(t));
  snprintf(m, 4, "%02d", t.tm_min);
}

int bigTimeColonX(int cx, const struct tm& t) {
  char h[4], m[4];
  clockDigits(h, m, t);
  return cx + int((monoWidth(h) - monoWidth(m)) * TIME_K) / 2;
}

void bigTime(int cx, int cy, const struct tm& t, uint16_t fg, uint16_t bg, bool force) {
  static char lastH[4], lastM[4];
  static int lastColon = -1;
  char h[4], m[4];
  clockDigits(h, m, t);
  int c = bigTimeColonX(cx, t), top = cy - TIME_H / 2;
  bool all = force || c != lastColon;
  if (all) { D().fillRect(0, cy - 38, SCR_W, 76, bg); colon(c, cy, fg, bg); lastColon = c; }
  if (all || strcmp(h, lastH)) {
    if (!all) D().fillRect(0, cy - 38, c - 6, 76, bg);
    monoText(h, c - 12 - int(monoWidth(h) * TIME_K) / 2, top, SCR_W, TIME_H, fg);
    strcpy(lastH, h);
  }
  if (all || strcmp(m, lastM)) {
    if (!all) D().fillRect(c + 6, cy - 38, SCR_W - c - 6, 76, bg);
    monoText(m, c + 12 + int(monoWidth(m) * TIME_K) / 2, top, SCR_W, TIME_H, fg);
    strcpy(lastM, m);
  }
}

// The tiles' layout: the main element white, the others in the accent colour.
constexpr int TILE_W = 226;

// ---- 3 Big date: the day of the week, the day, the month
void bigDate(const struct tm& t, bool force) {
  static int day = -1;
  if (!force && t.tm_yday == day) return;
  if (!force) D().fillScreen(BG);
  day = t.tm_yday;
  monoText(dayLong(t.tm_wday), 120, 6, TILE_W, 44, cfg.color1);
  char n[4]; snprintf(n, sizeof(n), "%d", t.tm_mday);
  monoText(n, 120, 56, TILE_W, 128, WHITE);
  monoText(monthLong(t.tm_mon), 120, 190, TILE_W, 44, cfg.color1);
}

// ---- 7 Big time: the hours, the minutes below
void bigTimeFace(const struct tm& t, bool force) {
  static int hour = -1, minute = -1;
  if (force) hour = minute = -1;
  int h = clockHour(t);
  char b[4];
  if (h != hour) {
    D().fillRect(0, 0, SCR_W, 120, BG);
    snprintf(b, sizeof(b), cfg.hour12 ? "%d" : "%02d", h);
    monoText(b, 120, 8, TILE_W, 106, WHITE);
    hour = h;
  }
  if (t.tm_min != minute) {
    D().fillRect(0, 120, SCR_W, 120, BG);
    snprintf(b, sizeof(b), "%02d", t.tm_min);
    monoText(b, 120, 126, TILE_W, 106, cfg.color1);
    minute = t.tm_min;
  }
}

// ---- 8 Big weather: the conditions' icon, the temperature below
void bigWeather(const struct tm& t, bool force) {
  static uint32_t seen = 0;
  if (!wx.valid) {
    if (force) weatherPlaceholder(tr("No weather yet", "Pas encore de meteo"));
    return;
  }
  if (!force && seen == wx.version) return;
  if (!force) D().fillScreen(BG);
  seen = wx.version;
  weatherIcon(120, 62, 124, wx.icon, BG, cfg.color1);   // the icon fills the top half
  char b[8]; snprintf(b, sizeof(b), "%d*", displayTemp(wx.temp));   // '*' draws the degree sign
  monoText(b, 120, 126, TILE_W, 106, WHITE);
}

// ---- 10-13 Day tiles: one forecast day each, today first ----------------------------
// The forecast days show the day's name, its icon and its high, then the feels-like
// high, the low and the chance of precipitation. Today is not a forecast: it shows the
// current conditions' icon and temperature, then the day's low, high and precipitation.
void dayTile(int k, const struct tm& t, bool force) {
  static uint32_t seen = 0, seenWx = 0;
  bool ok = fc.valid && k < fc.count;
  if (!ok) {
    if (force) weatherPlaceholder(tr("No forecast yet", "Pas encore de previsions"));
    return;
  }
  bool now = k == 0 && wx.valid;   // today, from the current conditions
  if (!force && seen == fc.version && (!now || seenWx == wx.version)) return;
  if (!force) D().fillScreen(BG);
  seen = fc.version; seenWx = wx.version;
  const ForecastDay& d = fc.days[k];
  monoText(dayLong(d.wday), 120, 6, TILE_W, 40, cfg.color1);
  weatherIcon(54, 104, 96, now ? wx.icon : d.icon, BG, cfg.color1);   // a 96 px icon on the left
  char b[8];
  snprintf(b, sizeof(b), "%d*", displayTemp(now ? wx.temp : d.hi));   // '*' draws the degree sign
  monoText(b, 170, 58, 134, 72, WHITE);
  text(now ? tr("NOW", "ACTUEL") : tr("HIGH", "MAX"), 170, 146, 2, MUTED, BG, MC_DATUM);
  D().drawFastHLine(16, 166, 208, PANEL_HI);
  const char* labels[] = {now ? tr("LOW", "MIN") : tr("FEELS", "RESSENTI"),
                          now ? tr("HIGH", "MAX") : tr("LOW", "MIN"), tr("PRECIP", "PRECIP.")};
  for (int i = 0; i < 3; ++i) text(labels[i], 44 + 76 * i, 184, 2, MUTED, BG, MC_DATUM);
  temp(44, 214, displayTemp(now ? d.lo : d.feels), 4, WHITE, BG);
  temp(120, 214, displayTemp(now ? d.hi : d.lo), 4, WHITE, BG);
  snprintf(b, sizeof(b), "%d%%", std::min(d.pop, 100));
  text(b, 196, 214, 4, WHITE, BG, MC_DATUM);
}

// ---- 9 Agents: every active agent message on one screen --------------------------
// Text cut to a width, with "..." when it does not fit.
String fit(String s, int width, uint8_t font) {
  if (D().textWidth(s, font) <= width) return s;
  while (s.length() > 1 && D().textWidth(s + "...", font) > width) s.remove(s.length() - 1);
  return s + "...";
}

void agents(const struct tm& t, bool force) {
  static uint32_t seen = 0;
  static int min = -1;
  if (!force && seen == messageVersion() && min == t.tm_min) return;   // minutes left change each minute
  seen = messageVersion(); min = t.tm_min;
  D().fillScreen(BG);
  char tb[12]; smallTime(tb, sizeof(tb), t);
  text(tb, 16, 20, 4, WHITE, BG, ML_DATUM);
  text("AGENTS", 224, 20, 2, MUTED, BG, MR_DATUM);
  int row = 0;
  for (int slot = 0; slot < MESSAGE_MAX; ++slot) {
    if (!messageActive(slot)) continue;
    int y = 44 + row * 49;
    D().fillSmoothRoundRect(6, y, 228, 45, 8, PANEL, BG);
    String icon = messageField(slot, "icon"), color = messageField(slot, "color");
    const MessageIcon* def = &MESSAGE_ICON_SET[0];
    for (const MessageIcon& m : MESSAGE_ICON_SET) if (icon == m.name) def = &m;
    if (color.isEmpty()) color = def->color;
    uint16_t c = hex565(color.c_str(), WHITE);
    JsonDocument item;   // the icon, drawn as a path item
    item["x"] = 26; item["y"] = y + 22; item["size"] = 28; item["align"] = "mc";
    item["stroke"] = color; item["sw"] = 2; item["d"] = pgmString(def->path);
    drawPath(item.as<JsonObjectConst>(), t, PANEL);
    long left = messageField(slot, "ttl").toInt();
    char tl[8];
    snprintf(tl, sizeof(tl), left >= 3600 ? "%ldh" : "%ldm", left >= 3600 ? left / 3600 : (left + 59) / 60);
    text(tl, 226, y + 13, 2, MUTED, PANEL, MR_DATUM);
    text(fit(messageField(slot, "title"), 150, 2).c_str(), 48, y + 13, 2, c, PANEL, ML_DATUM);
    text(fit(messageField(slot, "text"), 176, 2).c_str(), 48, y + 32, 2, CLOUD, PANEL, ML_DATUM);
    ++row;
  }
  if (!row) {
    text(tr("All quiet", "Tout est calme"), 120, 120, 4, MUTED, BG, MC_DATUM);
    text(tr("No agent messages", "Aucun message"), 120, 150, 2, DIM, BG, MC_DATUM);
  }
}

}  // namespace

String weatherIconName(int wmo, bool day) {
  int base = pgm_read_byte(&WI_WMO[wmo >= 0 && wmo < 100 ? wmo : 0]);
  return String(base >= WI_BASES - WI_AGNOSTIC ? "wi-" : day ? "wi-day-" : "wi-night-") + wiBase(base);
}

// ---- Public -----------------------------------------------------------------------
void faceInvalidate() { dirty = true; }
void facePhotosChanged() { photosCached = -1; dirty = true; }

int photoCount() {
  if (photosCached < 0) {
    photosCached = 0;
    Dir dir = LittleFS.openDir("/photo");
    while (dir.next()) if (dir.isFile() && isPhoto(dir.fileName()) && photoEnabled(dir.fileName())) photosCached++;
  }
  return photosCached;
}

bool faceAvailable(int face) {
  switch (face) {
    case FACE_WEATHER:  return wx.valid;
    case FACE_FORECAST: return fc.valid;
    case FACE_PHOTO:    return photoCount() > 0;
    case FACE_BIGWEATHER: return wx.valid;
    case FACE_AGENTS: for (int s = 0; s < MESSAGE_MAX; ++s) if (messageActive(s)) return true; return false;
    case FACE_FLIP:     return false;   // retired; its id stays reserved
    case FACE_DAY0: case FACE_DAY0 + 1: case FACE_DAY0 + 2: case FACE_DAY3:
      return fc.valid && face - FACE_DAY0 < fc.count;
    case FACE_CUSTOM:   return cfg.facesApi && customFaceExists(cfg.customFace);
    case FACE_MESSAGE:  return messageActive(messageShowing());
    default:            return face >= 0 && face < FACE_COUNT;
  }
}

void faceDraw(int face, const struct tm& t, bool timeValid) {
  if (!timeValid) {
    if (drawnFace != -2 || dirty) syncScreen();
    drawnFace = -2; dirty = false;
    return;
  }
  bool force = dirty || face != drawnFace;
  if (face == FACE_CUSTOM && !cfg.facesApi) face = FACE_CLASSIC;   // integration turned off
  if (force && face != FACE_PHOTO && face != FACE_CUSTOM && face != FACE_MESSAGE) D().fillScreen(BG);
  switch (face) {
    case FACE_WEATHER:  weather(t, force);  break;
    case FACE_PHOTO:    photo(t, force);    break;
    case FACE_SIMPLE:   simple(t, force);   break;
    case FACE_FORECAST: forecast(t, force); break;
    case FACE_DATE:     bigDate(t, force);  break;
    case FACE_BIGTIME:  bigTimeFace(t, force); break;
    case FACE_BIGWEATHER: bigWeather(t, force); break;
    case FACE_AGENTS:   agents(t, force);   break;
    case FACE_DAY0: case FACE_DAY0 + 1: case FACE_DAY0 + 2: case FACE_DAY3:
      dayTile(face - FACE_DAY0, t, force); break;
    case FACE_CUSTOM:   custom(t, force);   break;
    case FACE_MESSAGE:  message(t, force);  break;
    default:            classic(t, force);  break;
  }
  drawnFace = face; dirty = false;
}

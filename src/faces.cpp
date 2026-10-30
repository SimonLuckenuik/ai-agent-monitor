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
#include "ascii.h"
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

// ---- Colours (RGB565): each part of a face is a role, bound to a colour of the palette
// in use (docs/specifications/settings.md). computeColors() fills R[] about once a
// second; the built-in faces draw with it, and custom faces reach it as @name.
enum Role { R_BG, R_TEXT, R_MUTED, R_PANEL, R_TIME, R_SECONDS, R_DATE, R_HIGHLIGHT, R_ICON, R_WEATHER,
            R_HIGH, R_LOW, R_RAIN, R_SUN, R_OK, R_WARN, R_ERROR, R_INFO, ROLE_COUNT };
const char* const ROLE_NAMES[] = {"bg", "text", "muted", "panel", "time", "seconds", "date", "highlight", "icon",
                                  "weather", "high", "low", "rain", "sun", "ok", "warn", "error", "info", nullptr};
const char* const ROLE_DEFAULTS[] = {"black", "white", "", "", "accent1", "accent2", "accent1", "accent2", "accent1",
                                     "accent3", "@text", "@muted", "rain", "@text", "green", "orange", "red", "blue"};
uint16_t R[ROLE_COUNT];
uint16_t panelHi, dim, cloudGrey;   // mixes of bg and text
#define BG R[R_BG]
#define WHITE R[R_TEXT]
#define MUTED R[R_MUTED]
#define PANEL R[R_PANEL]
#define RAIN R[R_RAIN]
#define PANEL_HI panelHi
#define DIM dim
#define CLOUD cloudGrey

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

uint16_t hex565(const char* s, uint16_t fallback);
String resolve(const char* tpl, const struct tm& t);
String evalStr(const String& s);
int pick(const String& s, const char* const* names);

// a + (b - a) * f, channel by channel.
uint16_t mix565(uint16_t a, uint16_t b, float f) {
  auto ch = [&](int sh, int m) { int x = (a >> sh) & m, y = (b >> sh) & m; return ((int)lroundf(x + (y - x) * f) & m) << sh; };
  return ch(11, 31) | ch(5, 63) | ch(0, 31);
}

JsonDocument palDoc, roleDoc;   // cfg.palettes and cfg.roles, parsed when they change
uint16_t parsedRev = 0xFFFF;
String activePal;               // the palette in use: cfg.palette, or cfg.nightPalette at night
String facePal;                 // a clock palette the custom face on screen names ("" none)
uint8_t roleState[ROLE_COUNT];  // this pass: 0 to do, 1 in progress, 2 done

// A colour of a palette; a palette inherits the colours it lacks from "day".
bool swatch(const char* pal, const char* name, uint16_t* out) {
  JsonVariantConst v = palDoc[pal][name];
  if (v.isNull()) v = palDoc["day"][name];
  const char* s = v;
  if (!s) return false;
  *out = hex565(s, 0);
  return true;
}

uint16_t roleColor(int i, const struct tm& t, int depth);

// A role's binding: "#hex", "@role", a palette colour's name, or an expression that
// gives one of those. Chains stop after 4 steps.
bool colourSpec(const char* s, const struct tm& t, int depth, uint16_t* out) {
  if (!s || !*s || depth > 4) return false;
  if (*s == '#') { *out = hex565(s, 0); return true; }
  if (strchr(s, '{') || strchr(s, '?')) {
    String r = evalStr(resolve(s, t));
    return !strchr(r.c_str(), '{') && !strchr(r.c_str(), '?') && colourSpec(r.c_str(), t, depth + 1, out);
  }
  const char* name = *s == '@' ? s + 1 : s;
  int j = pick(name, ROLE_NAMES);
  if (*s == '@' && j >= 0) { *out = roleColor(j, t, depth + 1); return true; }
  if (swatch(activePal.c_str(), name, out)) return true;
  if (j >= 0) { *out = roleColor(j, t, depth + 1); return true; }
  return false;
}

uint16_t roleColor(int i, const struct tm& t, int depth) {
  if (roleState[i]) return R[i];   // done, or a loop back to it
  roleState[i] = 1;
  const char* spec = roleDoc[ROLE_NAMES[i]] | ROLE_DEFAULTS[i];
  uint16_t c;
  bool ok = colourSpec(spec, t, depth, &c) || colourSpec(ROLE_DEFAULTS[i], t, depth, &c);
  if (!ok) c = i == R_MUTED || i == R_PANEL ? mix565(roleColor(R_BG, t, depth + 1), roleColor(R_TEXT, t, depth + 1), i == R_MUTED ? 0.6f : 0.1f)
               : i == R_BG ? 0 : 0xFFFF;
  R[i] = c;
  roleState[i] = 2;
  return c;
}

// The roles' colours for now; true if any changed (the face is then redrawn).
bool computeColors(const struct tm& t) {
  if (parsedRev != cfg.colorsRev) {
    parsedRev = cfg.colorsRev;
    palDoc.clear(); deserializeJson(palDoc, cfg.palettes.c_str());
    roleDoc.clear(); deserializeJson(roleDoc, cfg.roles.c_str());
  }
  activePal = nightActive() && cfg.nightPalette.length() ? cfg.nightPalette : cfg.palette;
  uint16_t before[ROLE_COUNT];
  memcpy(before, R, sizeof(R));
  memset(roleState, 0, sizeof(roleState));
  for (int i = 0; i < ROLE_COUNT; ++i) roleColor(i, t, 0);
  panelHi = mix565(BG, WHITE, 0.16f); dim = mix565(BG, WHITE, 0.32f); cloudGrey = mix565(BG, WHITE, 0.9f);
  return memcmp(before, R, sizeof(R)) != 0;
}

// @name in a custom face, after its own palette: the clock palette the face names, a
// role, a colour of the palette in use.
bool clockColor(const char* name, uint16_t* out) {
  if (facePal.length() && swatch(facePal.c_str(), name, out)) return true;
  int j = pick(name, ROLE_NAMES);
  if (j >= 0) { *out = R[j]; return true; }
  return swatch(activePal.c_str(), name, out);
}

// A custom face's "invert": the panel flips every colour itself, with no redraw.
bool panelInverted = false;
void invertPanel(bool on) {
  if (on == panelInverted) return;
  panelInverted = on;
  D().invertDisplay(!on);   // the IPS panel shows true colours with INVON
}

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
  D().drawWedgeLine(120, 88, 136, 98, 3, 2, R[R_TIME], PANEL_HI);
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
    text(wd.c_str(), 120, 28, 2, R[R_DATE], BG, MC_DATUM, 200);
    text(date, 120, 52, 4, WHITE, BG, MC_DATUM, 230);
    day = t.tm_yday;
  }
  if (t.tm_min != min) {
    bigTime(120, 112, t, R[R_TIME], BG, force);
    D().fillRect(0, barY - 8, 240, 20, BG);
    D().fillRoundRect(barX, barY, barW, barH, 2, PANEL_HI);
    if (cfg.hour12) text(ampm(t), 210, barY + 2, 2, MUTED, BG, MC_DATUM);
    min = t.tm_min; sec = -1;
  }
  if (t.tm_sec != sec) {
    D().fillRoundRect(barX, barY, std::max(barH, barW * (t.tm_sec + 1) / 60), barH, 2, R[R_SECONDS]);
    sec = t.tm_sec;
  }
  if (wx.version != wxv) {
    D().fillRect(0, 184, 240, 56, BG);
    if (wx.valid) {
      char tb[8]; snprintf(tb, sizeof(tb), "%d", displayTemp(wx.temp));
      int tw = D().textWidth(tb, 4) + 12, mw = D().textWidth(wxMain(), 2);
      int x = 120 - (36 + 8 + tw + 8 + mw) / 2;
      weatherIcon(x + 18, 206, 36, wx.icon, BG, R[R_ICON]);
      temp(x + 44 + tw / 2, 207, displayTemp(wx.temp), 4, R[R_WEATHER], BG);
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
  weatherIcon(64, 104, 100, wx.icon, BG, R[R_ICON]);
  char tb[8]; snprintf(tb, sizeof(tb), "%d", displayTemp(wx.temp));
  int w = D().textWidth(tb, 6);
  text(tb, 126, 96, 6, WHITE, BG, ML_DATUM);
  ring(126 + w + 8, 80, 6, WHITE, BG);
  text(cfg.celsius ? "C" : "F", 126 + w + 18, 80, 2, MUTED, BG, ML_DATUM);
  char hl[24]; snprintf(hl, sizeof(hl), "H %d   L %d", displayTemp(wx.tempMax), displayTemp(wx.tempMin));
  text(hl, 128, 132, 2, MUTED, BG, ML_DATUM);
  String desc = wxDesc(); if (desc.length()) desc.setCharAt(0, toupper(desc[0]));
  text(desc.c_str(), 120, 170, D().textWidth(desc, 4) <= 228 ? 4 : 2, R[R_HIGHLIGHT], BG, MC_DATUM);
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
    text(d.today ? tr("TODAY", "AUJ.") : dayShort(d.wday), cx, 70, 2, d.today ? R[R_HIGHLIGHT] : MUTED, PANEL, MC_DATUM);
    weatherIcon(cx, 110, 44, d.icon, PANEL, R[R_ICON]);
    temp(cx, 152, displayTemp(d.hi), 4, R[R_HIGH], PANEL);
    temp(cx, 178, displayTemp(d.lo), 2, R[R_LOW], PANEL);
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
    text(ampm(t), 120, 190, 2, R[R_TIME], BG, MC_DATUM, 40);
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
// Items draw in order. Any string field may hold placeholders ({time.sec}, {now.temp}, pushed
// variables). Number and colour fields may also hold an expression, evaluated after
// the placeholders: "120+60*sin({time.sec}*6)", "{state}==warn?#ff9f0a:#30d158".
// Only items whose placeholder-dependent content changed are redrawn: an opaque item
// (text or icon with "bg", ring, bar) whose content changed is redrawn alone; a change
// to anything else (position, size, visibility, a transparent item) redraws the face.
JsonDocument customDoc;
String customName;
String customWanted;   // the custom face to draw: cfg.customFace, or the night face
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

String messageValueAt(int slot, const String& key);

// The entry a repeat item is drawing, for {it.*}: of 'd' days, 'm' messages, 'n' numbers.
struct Repeat { char of = 0; int index = 0, count = 0, slot = 0; } rep;

String hex565Text(uint16_t c, int div = 1) {   // RGB565 -> "#rrggbb" (div 4: a quarter strength)
  char b[8];
  snprintf(b, sizeof(b), "#%02x%02x%02x", ((c >> 11) * 255 / 31) / div, (((c >> 5) & 63) * 255 / 63) / div, ((c & 31) * 255 / 31) / div);
  return b;
}

// Minutes after midnight -> "7:21" (12 h) or "07:21" (24 h), as the clock is set.
String clockText(int m) {
  if (m < 0) return "--";
  char b[8];
  int h = (m / 60) % 24;
  if (cfg.hour12) { h %= 12; if (!h) h = 12; }
  snprintf(b, sizeof(b), cfg.hour12 ? "%d:%02d" : "%02d:%02d", h, m % 60);
  return b;
}

// The index of s in a null-terminated list of names, or -1. Placeholder names are looked
// up through these tables: a chain of comparisons costs more flash.
int pick(const String& s, const char* const* names) {
  for (int i = 0; names[i]; ++i) if (s == names[i]) return i;
  return -1;
}
const char* const DAY_F[] = {"wday", "wday3", "low", "high", "feels", "rain", "wind", "hum", "rise", "set", "icon", nullptr};

String numText(long v) { char b[12]; snprintf(b, sizeof(b), "%ld", v); return b; }
int windSpeed(float ms) { return lroundf(cfg.mile ? ms * 2.237f : ms * 3.6f); }

// One forecast day's field: {day1.high}, or {it.high} in a repeat over "days".
String dayValue(int k, const String& f) {
  if (!fc.valid || k < 0 || k >= fc.count) return "--";
  const ForecastDay& d = fc.days[k];
  switch (pick(f, DAY_F)) {
    case 0: return dayLong(d.wday);
    case 1: return dayShort(d.wday);
    case 2: return numText(displayTemp(d.lo));
    case 3: return numText(displayTemp(d.hi));
    case 4: return numText(displayTemp(d.feels));
    case 5: return numText(d.pop);
    case 6: return numText(windSpeed(d.wind));
    case 7: return numText(d.humidity);
    case 8: return clockText(d.sunrise);
    case 9: return clockText(d.sunset);
    case 10: return d.icon;
  }
  return "";
}

// The moon's age in days since a new moon (2000-01-06 18:14 UTC), 0 to 29.53.
float moonAge() {
  float d = (time(nullptr) - 947182440L) / 86400.0f;
  return d - 29.530589f * floorf(d / 29.530589f);
}

// ISO 8601 week number.
int isoWeek(const struct tm& t) {
  auto weeks = [](int y) {   // 52 or 53
    auto p = [](int y) { return (y + y / 4 - y / 100 + y / 400) % 7; };
    return 52 + (p(y) == 4 || p(y - 1) == 3);
  };
  int y = t.tm_year + 1900, w = (t.tm_yday - (t.tm_wday + 6) % 7 + 10) / 7;
  if (w < 1) return weeks(y - 1);
  return w > weeks(y) ? 1 : w;
}

const char* const GROUPS[] = {"time", "date", "now", "it", "sun", "moon", "units", "loc", "theme", "msg", "age", nullptr};
const char* const TIME_F[] = {"hm", "hour", "min", "sec", "ms", "ampm", "epoch", "daymin", nullptr};
const char* const DATE_F[] = {"text", "day", "wday", "wday3", "month", "month3", "monthnum", "year", "yday", "week", "wdaynum", "mdays", nullptr};
const char* const NOW_F[] = {"text", "kind", "icon", "err", "temp", "feels", "hum", "wind", "press", "clouds", "code", "age", nullptr};
const char* const SUN_F[] = {"up", "next", "rise", "set", "until_rise", "until_set", "daylen", nullptr};
const char* const MOON_F[] = {"phase", "lit", "age", nullptr};

// {group.name} -> its current value; a name without a dot is a pushed variable.
String fieldValue(const String& key, const struct tm& t) {
  char b[24];
  int dot = key.indexOf('.');
  if (dot < 0) return faceVar(key);
  String g = key.substring(0, dot), f = key.substring(dot + 1);
  int mod = t.tm_hour * 60 + t.tm_min, k;
  if (g.length() == 4 && g.startsWith("day") && isdigit((unsigned char)g[3])) return dayValue(g[3] - '0', f);
  switch (pick(g, GROUPS)) {
    case 0:   // time
      switch (pick(f, TIME_F)) {
        case 0: smallTime(b, sizeof(b), t); return b;
        case 1: snprintf(b, sizeof(b), cfg.hour12 ? "%d" : "%02d", clockHour(t)); return b;
        case 2: snprintf(b, sizeof(b), "%02d", t.tm_min); return b;
        case 3: snprintf(b, sizeof(b), "%02d", t.tm_sec); return b;
        case 4: return numText(millis() % 1000);
        case 5: return ampm(t);
        case 6: return numText(time(nullptr));
        case 7: return numText(mod);
      }
      break;
    case 1:   // date
      switch (pick(f, DATE_F)) {
        case 0: snprintf(b, sizeof(b), "%d %s", t.tm_mday, monthLong(t.tm_mon)); return b;
        case 1: return numText(t.tm_mday);
        case 2: return dayLong(t.tm_wday);
        case 3: return dayShort(t.tm_wday);
        case 4: return monthLong(t.tm_mon);
        case 5: return monthShort(t.tm_mon);
        case 6: return numText(t.tm_mon + 1);
        case 7: return numText(t.tm_year + 1900);
        case 8: return numText(t.tm_yday + 1);
        case 9: return numText(isoWeek(t));
        case 10: return numText(t.tm_wday ? t.tm_wday : 7);   // ISO: 1 Monday .. 7 Sunday
        case 11: {   // days in the month
          int m = t.tm_mon, y = t.tm_year + 1900;
          return numText(m == 1 ? 28 + (y % 4 == 0 && (y % 100 || y % 400 == 0)) : 30 + ((m + (m > 6)) % 2 == 0));
        }
      }
      break;
    case 2:   // now: the current weather
      k = pick(f, NOW_F);
      if (k == 0) return wxDesc();
      if (k == 1) return wxMain();
      if (k == 2) return wx.icon;
      if (k == 3) return wx.error;
      if (k < 0) break;
      if (!wx.valid) return "--";
      switch (k) {
        case 4: return numText(displayTemp(wx.temp));
        case 5: return numText(displayTemp(wx.feels));
        case 6: return numText(wx.humidity);
        case 7: return numText(windSpeed(wx.wind));
        case 8: return numText(wx.pressure);
        case 9: return numText(wx.clouds);
        case 10: return numText(wx.code);
        case 11: return numText((millis() - wx.lastSuccess) / 60000);
      }
      break;
    case 3:   // it: the repeat entry being drawn
      if (f == "index") return numText(rep.index);
      if (f == "count") return numText(rep.count);
      if (rep.of == 'd') return dayValue(rep.slot, f);
      if (rep.of == 'm') return messageValueAt(rep.slot, f);
      break;
    case 4: {   // sun
      int rise = wx.sunrise, set = wx.sunset;
      const ForecastDay* next = fc.valid && fc.count > 1 ? &fc.days[1] : nullptr;
      int rise2 = next && next->sunrise >= 0 ? next->sunrise : rise, set2 = next && next->sunset >= 0 ? next->sunset : set;
      bool up = rise >= 0 && set >= 0 ? mod >= rise && mod < set : t.tm_hour >= 6 && t.tm_hour < 18;
      k = pick(f, SUN_F);
      if (k == 0) return up ? "1" : "0";
      if (k == 1) return up ? "set" : "rise";
      if (k == 2) return clockText(rise);
      if (k == 3) return clockText(set);
      if (k < 0) break;
      if (rise < 0 || set < 0) return "--";
      if (k == 4) return numText(mod < rise ? rise - mod : rise2 + 1440 - mod);
      if (k == 5) return numText(mod < set ? set - mod : set2 + 1440 - mod);
      return numText(set - rise);
    }
    case 5: {   // moon
      float a = moonAge();
      k = pick(f, MOON_F);
      if (k == 0) return numText((int)(a / 29.530589f * 8 + 0.5f) % 8);
      if (k == 1) return numText(lroundf((1 - cosf(a / 29.530589f * 2 * PI)) * 50));
      if (k == 2) return numText((int)a);
      break;
    }
    case 6:   // units
      if (f == "temp") return cfg.celsius ? "C" : "F";
      if (f == "wind") return cfg.mile ? "mph" : "km/h";
      break;
    case 7:   // loc
      if (f == "city") return wx.cityName;
      break;
    case 8:   // theme: the roles' colours ({theme.night} is main.cpp's)
      if (f == "palette") return activePal;
      k = f == "color1" ? R_TIME : f == "color2" ? R_HIGHLIGHT : f == "color3" ? R_WEATHER : pick(f, ROLE_NAMES);
      if (k >= 0) return hex565Text(R[k]);
      break;
    case 9:   // msg: the message on screen
      if (f == "count") { int n = 0; for (int i = 0; i < MESSAGE_MAX; ++i) n += messageActive(i); return numText(n); }
      return messageValueAt(messageShowing(), f);
    case 10: {   // age: seconds since a variable was pushed
      long a = faceVarAge(f);
      return a < 0 ? "" : numText(a);
    }
  }
  return deviceValue(key);
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
struct MessageIcon { const char* name; uint8_t role; const char* path; };   // role: its colour
const MessageIcon MESSAGE_ICON_SET[] = {
    {"info", R_INFO, ICON_INFO},       {"success", R_OK, ICON_SUCCESS}, {"warning", R_WARN, ICON_WARNING},
    {"error", R_ERROR, ICON_ERROR},    {"waiting", R_WARN, ICON_WAITING}, {"running", R_INFO, ICON_RUNNING},
    {"bell", R_WARN, ICON_BELL},       {"done", R_OK, ICON_DONE}};

String pgmString(const char* p) {
  String s;
  for (char c; (c = (char)pgm_read_byte(p)); ++p) s += c;
  return s;
}

// A message field for the Message face's template: the icon's path and colours come
// from the icon set, the rest from the message.
String messageValueAt(int slot, const String& key) {
  String icon = messageField(slot, "icon");
  const MessageIcon* def = &MESSAGE_ICON_SET[0];
  for (const MessageIcon& m : MESSAGE_ICON_SET) if (icon == m.name) def = &m;
  String color = messageField(slot, "color");
  if (color.isEmpty()) color = hex565Text(R[def->role]);
  if (key == "icon") return pgmString(def->path);
  if (key == "color") return color;
  if (key == "bg") return hex565Text(hex565(color.c_str(), WHITE), 4);   // a solid background in its hue
  return messageField(slot, key);
}

String resolve(const char* tpl, const struct tm& t);
String evalStr(const String& s);
float parseNum(const char*& s, bool* ok = nullptr);
double parseNumD(const char*& s, bool* ok = nullptr);

// {key:mod:mod}: u upper case, l lower case, + a sign on positive numbers, 0N zero-padded
// to N, N cut to N characters, .N N decimals; any other word names a map in the face's
// "maps" ({"state": {"ok": "All good", "*": "Unknown"}}).
String floatText(double n);
String applyMod(String v, const String& m) {
  if (m == "u") { v.toUpperCase(); return v; }
  if (m == "l") { v.toLowerCase(); return v; }
  const char* s = v.c_str();
  bool isNum;
  double n = parseNumD(s, &isNum);
  isNum = isNum && !*s;
  if (m == "+") return isNum && n > 0 ? "+" + v : v;
  if (m[0] == '.' && isNum) {
    int d = std::min(3, std::max(0, (int)m.substring(1).toInt())), p = d == 3 ? 1000 : d == 2 ? 100 : d == 1 ? 10 : 1;
    if (fabs(n) * p > 2e9) return v;
    long q = (long)(fabs(n) * p + 0.5);
    char b[20];
    if (d) snprintf(b, sizeof(b), "%s%ld.%0*ld", n < 0 && q ? "-" : "", q / p, d, q % p);
    else snprintf(b, sizeof(b), "%s%ld", n < 0 && q ? "-" : "", q);
    return b;
  }
  if (isdigit((unsigned char)m[0])) {
    unsigned width = std::min(64, (int)m.toInt());
    if (m[0] != '0') return v.length() > width ? v.substring(0, width) : v;
    bool neg = v[0] == '-';
    String digits = neg ? v.substring(1) : v;
    while (digits.length() + neg < width) digits = "0" + digits;
    return neg ? "-" + digits : digits;
  }
  JsonVariantConst map = customDoc["maps"][m];
  if (!map.is<JsonObjectConst>()) return v;
  JsonVariantConst e = map[v];
  if (e.isNull()) e = map["*"];
  if (e.isNull()) return v;
  return e.is<const char*>() ? String(e.as<const char*>()) : floatText(e.as<float>());
}

bool hasOperator(const String& s);

// The inside of one {...}: a key (letters, digits, _ and .), with :modifiers and a
// |fallback for an empty value; an expression (an operator, or {...} inside, resolved
// first); anything else is left as it is, braces included.
String placeholder(const String& in, const struct tm& t) {
  unsigned k = 0;
  while (k < in.length() && (isalnum((unsigned char)in[k]) || in[k] == '_' || in[k] == '.')) ++k;
  if (k && (k == in.length() || in[k] == ':' || in[k] == '|')) {
    int bar = in.indexOf('|', k);
    String mods = in.substring(k, bar < 0 ? in.length() : bar), v = fieldValue(in.substring(0, k), t);
    for (int a = 0; a >= 0 && a < (int)mods.length();) {
      int next = mods.indexOf(':', a + 1);
      v = applyMod(v, mods.substring(a + 1, next < 0 ? mods.length() : next));
      a = next;
    }
    if (v.isEmpty() && bar >= 0) v = in.substring(bar + 1);
    return v;
  }
  if (in.indexOf('{') < 0 && !hasOperator(in)) return "{" + in + "}";
  return evalStr(resolve(in.c_str(), t));
}

// Every {...} in a string replaced by its value.
String resolve(const char* tpl, const struct tm& t) {
  String out;
  for (const char* p = tpl; *p; ++p) {
    const char* end = nullptr;
    if (*p == '{') {
      int depth = 0;
      for (const char* q = p; *q && q - p < 240; ++q) {
        if (*q == '{') ++depth;
        else if (*q == '}' && !--depth) { end = q; break; }
      }
    }
    if (!end) { out += *p; continue; }
    String inner;
    inner.concat(p + 1, end - p - 1);
    out += placeholder(inner, t);
    p = end;
  }
  return out;
}

// ---- Expressions: numbers, words (strings), + - * / %, comparisons, && || !, ?:,
// and sin cos (degrees), min max abs round. A word compares as text; in arithmetic
// it reads as its leading number. Errors give 0 / "". Values are slices of the
// expression text, so evaluating allocates nothing.
// Numbers are parsed here rather than with strtof/atof, which link ~9 KB of libc.
// Expressions compute in double: a float cannot hold a Unix time to the second
// ({until}-{time.epoch}); paths stay in float, which is faster.
template <typename T> T parseNumT(const char*& s, bool* ok) {
  const char* p = s;
  while (*p == ' ') ++p;
  bool neg = *p == '-';
  if (*p == '-' || *p == '+') ++p;
  T v = 0, f = T(0.1);
  bool digits = false;
  while (isdigit((unsigned char)*p)) { v = v * 10 + (*p++ - '0'); digits = true; }
  if (*p == '.') for (++p; isdigit((unsigned char)*p); f *= T(0.1)) { v += (*p++ - '0') * f; digits = true; }
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
float parseNum(const char*& s, bool* ok) { return parseNumT<float>(s, ok); }
double parseNumD(const char*& s, bool* ok) { return parseNumT<double>(s, ok); }
float toNum(const char* s) { return (float)parseNumD(s); }

// A number as text: up to 3 decimals; large ones (Unix times) whole, without printf's %f.
String floatText(double n) {
  char b[24];
  if (fabs(n) >= 1e6 && fabs(n) < 9e15) {
    long long m = (long long)(n + (n < 0 ? -0.5 : 0.5));
    unsigned long long u = m < 0 ? -m : m;
    char* e = b + sizeof(b);
    *--e = 0;
    do { *--e = '0' + u % 10; u /= 10; } while (u);
    if (m < 0) *--e = '-';
    return e;
  }
  long m = (long)floor(n * 1000 + 0.5);
  if (m % 1000 == 0) snprintf(b, sizeof(b), "%ld", m / 1000);
  else snprintf(b, sizeof(b), "%s%ld.%03ld", m < 0 ? "-" : "", labs(m) / 1000, labs(m) % 1000);
  return b;
}

struct Val { double n = 0; const char* s = nullptr; uint8_t len = 0; bool num = true; };

bool sliceIs(const Val& v, const char* w) { return !v.num && strlen(w) == v.len && !strncmp(v.s, w, v.len); }
bool truth(const Val& v) {
  if (v.num) return v.n != 0;
  return v.len && !sliceIs(v, "0") && !sliceIs(v, "false") && !sliceIs(v, "off");
}
double numOf(const Val& v) { const char* s = v.s; return v.num ? v.n : parseNumD(s); }
Val number(double n) { Val v; v.n = n; return v; }

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
      double x = numOf(a), y = numOf(b);
      bool r[] = {x == y, x != y, x <= y, x >= y, x < y, x > y};
      return number(r[i]);
    }
    return a;
  }
  Val add() {
    Val a = mul();
    for (;;) {
      if (eat("+")) { double l = numOf(a); a = number(l + numOf(mul())); }
      else if (eat("-")) { double l = numOf(a); a = number(l - numOf(mul())); }
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
      double l = numOf(a), r = numOf(unary());
      a = number(op == '*' ? l * r : !r ? 0 : op == '/' ? l / r : l - r * (long long)(l / r));
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
      double a = numOf(ternary()), b = eat(",") ? numOf(ternary()) : 0;
      eat(")");
      if (sliceIs(v, "sin")) return number(sinf((float)(a * DEG_TO_RAD)));
      if (sliceIs(v, "cos")) return number(cosf((float)(a * DEG_TO_RAD)));
      if (sliceIs(v, "min")) return number(std::min(a, b));
      if (sliceIs(v, "max")) return number(std::max(a, b));
      if (sliceIs(v, "abs")) return number(fabs(a));
      if (sliceIs(v, "round")) return number(floor(a + 0.5));
      if (sliceIs(v, "rand")) return number(a >= 1 ? random((long)a) : 0);
      return number(0);
    }
    const char* s = start;
    bool ok;
    double n = parseNumD(s, &ok);
    if (ok && s == end) return number(n);   // the whole word is a number
    return v;
  }
};

bool hasOperator(const String& s) {
  for (unsigned i = 0; i < s.length(); ++i) if (strchr("+-*/%()?:<>=!&|", s[i]) && !(i == 0 && s[i] == '-')) return true;
  return false;
}
float evalNum(const String& s) { return hasOperator(s) ? (float)numOf(Expr(s.c_str()).parse()) : toNum(s.c_str()); }
String evalStr(const String& s) {
  if (!hasOperator(s)) return s;
  Val v = Expr(s.c_str()).parse();
  if (v.num) return floatText(v.n);
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

bool visibleField(JsonVariantConst v, const struct tm& t) {   // "show", "when": true when absent
  if (v.is<bool>()) return v.as<bool>();
  const char* s = v;
  if (!s) return true;
  String r = resolve(s, t);
  if (hasOperator(r)) return truth(Expr(r.c_str()).parse());
  return r.length() && r != "0" && r != "false" && r != "off";
}
__attribute__((noinline)) bool visible(JsonObjectConst it, const struct tm& t) { return visibleField(it["show"], t); }

// ---- Paint: a solid colour or a linear gradient ({"stops":[..],"angle":deg} or
// {"from":..,"to":..}; angle as in CSS: 180 = top to bottom, 90 = left to right).
struct Paint {
  uint16_t c[4] = {WHITE};
  uint8_t n = 1;
  float x0 = 0, y0 = 0, dx = 0, dy = 0;   // t = (x - x0) * dx + (y - y0) * dy, 0..1
  uint16_t first() const { return c[0]; }
};

// The face's palette: "palette" as an object, or a name (placeholders and an expression
// allowed: "{sun.up}?day:night") from "palettes". renderItems() picks it once per pass.
String paletteName;
uint16_t colorOf(JsonVariantConst v, uint16_t fallback, const struct tm& t);
uint16_t paletteColor(const char* name, uint16_t fallback, const struct tm& t) {
  JsonVariantConst p = customDoc["palette"];
  JsonVariantConst e = p[name];
  if (!p.is<JsonObjectConst>()) e = customDoc["palettes"][paletteName][name];
  const char* s = e;
  if (s && *s == '@') return fallback;   // no palette entry points at another
  if (!e.isNull()) return colorOf(e, fallback, t);
  uint16_t c;
  return clockColor(name, &c) ? c : fallback;
}

uint16_t colorOf(JsonVariantConst v, uint16_t fallback, const struct tm& t) {
  if (v.is<int>()) return (uint16_t)v.as<int>();   // already RGB565
  const char* s = v;
  if (!s) return fallback;
  if (*s == '@' && !strchr(s, '{')) return paletteColor(s + 1, fallback, t);
  if (!strchr(s, '{') && !hasOperator(s)) return hex565(s, fallback);
  String r = evalStr(resolve(s, t));
  return r[0] == '@' ? paletteColor(r.c_str() + 1, fallback, t) : hex565(r.c_str(), fallback);
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

int offX = 0, offY = 0;   // where the repeat entry being drawn starts

uint8_t alignOf(JsonObjectConst it) {   // "align": tl tc tr ml mc mr bl bc br -> 0..8
  static const char* const names[] = {"tl", "tc", "tr", "ml", "mc", "mr", "bl", "bc", "br"};
  const char* a = it["align"];
  if (a) for (uint8_t i = 0; i < 9; ++i) if (!strcmp(a, names[i])) return i;
  return 0;
}

// {"type":"path","d":"M..." or ["M...","M..."],"x","y","size" (or "w","h"),
//  "vb":"0 0 24 24","align","rotate","fill","stroke","sw","rule":"evenodd"}
void drawPath(JsonObjectConst it, const struct tm& t, uint16_t under) {
  float vb[4] = {0, 0, 24, 24};
  if (const char* s = it["vb"]) { PathReader R{s}; R.nums(vb, 4); }

  if (vb[2] <= 0 || vb[3] <= 0) return;
  float size = num(it, "size", 48, t), w = num(it, "w", size, t), h = num(it, "h", size, t);
  float x = num(it, "x", 0, t) + offX, y = num(it, "y", 0, t) + offY;
  uint8_t datum = alignOf(it);
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
void drawBig(JsonObjectConst it, const struct tm& t, int x, int y, uint16_t c, uint16_t bg);

void drawText(JsonObjectConst it, const struct tm& t, int x, int y, uint16_t c, uint16_t bg) {
  if (!strcmp(js(it, "font", ""), "big")) { drawBig(it, t, x, y, c, bg); return; }
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
  return jhas(it, "bg") && (!strcmp(type, "text") || !strcmp(type, "icon") || !strcmp(type, "sprite") || !strcmp(type, "spark"));
}

// FNV-1a over the resolved values of every placeholder-bearing string in an item.
// "content" keys (what an opaque item redraws in place) and the others (position,
// size, visibility: these need the whole face redrawn) hash separately.
void mixValues(JsonVariantConst v, const char* key, const struct tm& t, uint32_t& content, uint32_t& geo) {
  if (v.is<JsonObjectConst>()) { for (JsonPairConst kv : v.as<JsonObjectConst>()) mixValues(kv.value(), key, t, content, geo); return; }
  if (v.is<JsonArrayConst>()) { for (JsonVariantConst e : v.as<JsonArrayConst>()) mixValues(e, key, t, content, geo); return; }
  const char* s = v;
  if (!s || !strchr(s, '{')) return;
  static const char* const contentKeys[] = {"text", "value", "max", "icon", "color", "track", "fill", "stroke", "frame"};
  bool isContent = false;
  for (const char* k : contentKeys) if (!strcmp(key, k)) isContent = true;
  uint32_t& h = isContent ? content : geo;
  // "show" counts by its outcome: "{at}-{time.epoch}>0" resolves to new text every second.
  String r = strcmp(key, "show") ? resolve(s, t) : String(visibleField(v, t) ? "1" : "0");
  for (unsigned i = 0; i < r.length(); ++i) h = (h ^ (uint8_t)r[i]) * 16777619u;
  h = (h ^ '|') * 16777619u;
}

int repeatCount(JsonObjectConst it, const struct tm& t);
void repeatAt(int i);

// One item's two hashes. A repeat hashes every entry (any change redraws the face); a
// spark hashes its variable's history.
void mixItem(JsonObjectConst it, const struct tm& t, uint32_t& h, uint32_t& g) {
  const char* type = js(it, "type", "");
  for (JsonPairConst kv : it) if (strcmp(kv.key().c_str(), "items")) mixValues(kv.value(), kv.key().c_str(), t, h, g);
  if (!strcmp(type, "spark")) {
    uint32_t pushes = 0;
    faceHistory(resolve(js(it, "var", ""), t), nullptr, 0, &pushes);
    h = (h ^ pushes) * 16777619u;
  }
  if (strcmp(type, "repeat") || rep.of) return;
  int n = repeatCount(it, t);
  g = (g ^ n) * 16777619u;
  for (int i = 0; i < n; ++i) {
    repeatAt(i);
    for (JsonObjectConst c : it["items"].as<JsonArrayConst>())
      for (JsonPairConst kv : c) mixValues(kv.value(), kv.key().c_str(), t, g, g);
  }
  rep = Repeat();
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

// {"type":"sprite","x","y","scale","align","palette":{"r":"#ff453a"},"rows":["..r.."]}:
// pixel art, one character per pixel; characters not in the palette are transparent.
// "frames": [[rows], [rows]] and "frame": an expression pick one for animation.
void drawSprite(JsonObjectConst it, const struct tm& t, int x, int y, uint16_t bg, bool opaque) {
  JsonArrayConst rows = it["rows"];
  if (it["frames"].is<JsonArrayConst>()) {
    JsonArrayConst frames = it["frames"];
    int n = frames.size();
    if (!n) return;
    int k = inum(it, "frame", 0, t) % n;
    rows = frames[k < 0 ? k + n : k];
  }
  int s = std::max(1, std::min(16, inum(it, "scale", 1, t))), h = std::min<int>(rows.size(), 64), w = 0;
  for (JsonVariantConst r : rows) w = std::max<int>(w, strlen(r | ""));
  w = std::min(w, 64);
  uint8_t datum = alignOf(it);
  x -= (datum % 3) * w * s / 2;
  y -= (datum / 3) * h * s / 2;
  char keys[16];
  uint16_t cols[16];
  int nk = 0;
  for (JsonPairConst kv : it["palette"].as<JsonObjectConst>()) {
    const char* v = kv.value();
    if (nk == 16 || !kv.key().c_str()[0] || (v && (!*v || !strcmp(v, "none")))) continue;
    if (kv.value().isNull()) continue;
    keys[nk] = kv.key().c_str()[0];
    cols[nk++] = colorOf(kv.value(), WHITE, t);
  }
  // Opaque: transparent pixels are painted with bg in the same pass, so each pixel is
  // written once (clearing the box first made a changing frame blink).
  for (int r = 0; r < h; ++r) {
    const char* row = rows[r] | "";
    int len = std::min<int>(strlen(row), w);
    for (int c = 0; c < (opaque ? w : len);) {
      char ch = c < len ? row[c] : 0;
      int k = 0;
      while (k < nk && keys[k] != ch) ++k;
      int run = 1;
      while (c + run < w && (c + run < len ? row[c + run] : 0) == ch) ++run;
      if (k < nk) D().fillRect(x + c * s, y + r * s, run * s, s, cols[k]);
      else if (opaque) D().fillRect(x + c * s, y + r * s, run * s, s, bg);
      c += run;
    }
  }
}

// {"type":"spark","x","y","w","h","var","min","max","color","fill","bg"}: the values
// pushed to a variable with "$history", oldest on the left.
void drawSpark(JsonObjectConst it, const struct tm& t, int x, int y, uint16_t bg, bool opaque) {
  int w = inum(it, "w", 100, t), h = std::max(2, inum(it, "h", 30, t));
  float v[HISTORY_LEN];
  int n = faceHistory(resolve(js(it, "var", ""), t), v, HISTORY_LEN, nullptr);
  if (opaque) D().fillRect(x, y, w, h, bg);
  if (n < 2) return;
  float lo = v[0], hi = v[0];
  for (int i = 1; i < n; ++i) { lo = std::min(lo, v[i]); hi = std::max(hi, v[i]); }
  if (jhas(it, "min")) lo = num(it, "min", lo, t);
  if (jhas(it, "max")) hi = num(it, "max", hi, t);
  if (hi - lo < 1e-6f) { hi = lo + 1; lo -= 1; }
  uint16_t c = colorOf(it["color"], WHITE, t);
  bool fill = jhas(it, "fill");
  uint16_t fc2 = colorOf(it["fill"], c, t);
  float px = 0, py = 0;
  for (int i = 0; i < n; ++i) {
    // One pixel inside the box: the line's anti-aliased edge stays within what bg clears.
    float cx = x + 1 + (float)i * (w - 3) / (n - 1);
    float cy = y + 1 + (h - 3) * (1 - std::min(1.0f, std::max(0.0f, (v[i] - lo) / (hi - lo))));
    if (fill && i) for (int col = (int)px + 1; col <= (int)cx; ++col) {
      int top = (int)(py + (cy - py) * (col - px) / std::max(1.0f, cx - px));
      D().drawFastVLine(col, top + 1, y + h - top - 1, fc2);
    }
    if (i) D().drawWedgeLine(px, py, cx, cy, 0.75f, 0.75f, c, bg);
    px = cx; py = cy;
  }
}

// {"type":"repeat","of":"days" | "msgs" | a number,"x","y","dx","dy","items":[...]}:
// its items once per entry, offset by dx, dy each time; {it.*} reads the entry.
int repeatCount(JsonObjectConst it, const struct tm& t) {
  const char* of = it["of"];
  int n = 0;
  if (of && !strcmp(of, "days")) { rep.of = 'd'; n = fc.valid ? fc.count : 0; }
  else if (of && !strcmp(of, "msgs")) { rep.of = 'm'; for (int i = 0; i < MESSAGE_MAX; ++i) n += messageActive(i); }
  else { rep.of = 'n'; n = std::max(0, std::min(60, inum(it, "of", 0, t))); }
  rep.count = n;
  return n;
}
void repeatAt(int i) {
  rep.index = rep.slot = i;
  if (rep.of == 'm') for (int s = 0, k = 0; s < MESSAGE_MAX; ++s) if (messageActive(s) && k++ == i) rep.slot = s;
}

void drawItem(JsonObjectConst it, const struct tm& t, uint16_t faceBg);
void drawRepeat(JsonObjectConst it, const struct tm& t, int x, int y, uint16_t faceBg) {
  if (rep.of) return;   // not inside another repeat
  int n = repeatCount(it, t), dx = inum(it, "dx", 0, t), dy = inum(it, "dy", 0, t);
  for (int i = 0; i < n; ++i) {
    repeatAt(i);
    offX = x + i * dx; offY = y + i * dy;
    for (JsonObjectConst c : it["items"].as<JsonArrayConst>()) drawItem(c, t, faceBg);
  }
  offX = offY = 0;
  rep = Repeat();
}

void drawItem(JsonObjectConst it, const struct tm& t, uint16_t faceBg) {
  if (!visible(it, t)) return;
  const char* type = js(it, "type", "");
  int x = inum(it, "x", 0, t) + offX, y = inum(it, "y", 0, t) + offY;
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
    if (jhas(it, "border")) {
      int bw = std::max(1, inum(it, "bw", 2, t));
      uint16_t c = colorOf(it["border"], WHITE, t);
      if (r > 0) D().drawSmoothRoundRect(x, y, std::max(r, bw), std::max(r, bw) - bw, w - 1, h - 1, c, faceBg);
      else { D().fillRect(x, y, w, bw, c); D().fillRect(x, y + h - bw, w, bw, c); D().fillRect(x, y, bw, h, c); D().fillRect(x + w - bw, y, bw, h, c); }
    }
  } else if (!strcmp(type, "circle")) {
    int r = inum(it, "r", 10, t);
    Paint p = paintOf(it["color"], WHITE, x - r, y - r, 2 * r, 2 * r, t);
    bool fill = !it["fill"].is<bool>() || it["fill"].as<bool>();
    if (!fill) D().drawSmoothCircle(x, y, r, p.first(), faceBg);
    else if (p.n == 1) D().fillSmoothCircle(x, y, r, p.first(), faceBg);
    else paintRect(p, x - r, y - r, 2 * r + 1, 2 * r + 1, r);
    if (jhas(it, "border")) D().drawSmoothArc(x, y, r, std::max(0, r - std::max(1, inum(it, "bw", 2, t))), 0, 360, colorOf(it["border"], WHITE, t), faceBg);
  } else if (!strcmp(type, "line")) {
    float w = num(it, "w", 2, t) / 2.0f;
    D().drawWedgeLine(x, y, num(it, "x2", x - offX, t) + offX, num(it, "y2", y - offY, t) + offY, w, w, colorOf(it["color"], WHITE, t), faceBg);
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
    weatherIcon(x, y, size, resolve(js(it, "icon", "{now.icon}"), t), bg, colorOf(it["color"], R[R_ICON], t));
  } else if (!strcmp(type, "image")) {
    drawJpeg(String("/photo/") + resolve(js(it, "src", ""), t), x, y);
  } else if (!strcmp(type, "sprite")) {
    drawSprite(it, t, x, y, bg, jhas(it, "bg"));
  } else if (!strcmp(type, "spark")) {
    drawSpark(it, t, x, y, bg, jhas(it, "bg"));
  } else if (!strcmp(type, "repeat")) {
    drawRepeat(it, t, x, y, faceBg);
  }
}

void renderItems(const struct tm& t, bool force);

// The screen box an item covers (x0 > x1 when hidden), so that an item that moves
// repaints only around its old and new places. False for types with no simple box
// (text, sprite, image, repeat): those still repaint the whole face.
struct Box { int16_t x0, y0, x1, y1; };
Box customBox[CUSTOM_ITEMS];
bool customBoxOk[CUSTOM_ITEMS];

bool itemBox(JsonObjectConst it, const struct tm& t, Box& b) {
  b = {1, 1, 0, 0};
  if (!visible(it, t)) return true;
  const char* type = js(it, "type", "");
  int x = inum(it, "x", 0, t), y = inum(it, "y", 0, t), x0 = x, y0 = y, x1 = x, y1 = y, m = 2;
  bool rect = !strcmp(type, "rect"), bar = !strcmp(type, "bar");
  if (rect || bar || !strcmp(type, "spark")) {
    x1 = x + inum(it, "w", rect ? 0 : 100, t);
    y1 = y + inum(it, "h", rect ? 0 : bar ? 8 : 30, t);
  } else if (!strcmp(type, "circle") || !strcmp(type, "ring") || !strcmp(type, "icon")) {
    int r = !strcmp(type, "icon") ? inum(it, "size", 48, t) / 2 + 2 : inum(it, "r", !strcmp(type, "ring") ? 50 : 10, t);
    x0 -= r; y0 -= r; x1 += r; y1 += r;
  } else if (!strcmp(type, "line")) {
    int x2 = (int)num(it, "x2", x, t), y2 = (int)num(it, "y2", y, t);
    x0 = std::min(x, x2); x1 = std::max(x, x2); y0 = std::min(y, y2); y1 = std::max(y, y2);
    m += (int)num(it, "w", 2, t) / 2;
  } else if (!strcmp(type, "path")) {
    float size = num(it, "size", 48, t), w = num(it, "w", size, t), h = num(it, "h", size, t);
    uint8_t datum = alignOf(it);
    x0 = x - (datum % 3) * w / 2; y0 = y - (datum / 3) * h / 2; x1 = x0 + w; y1 = y0 + h;
    if (jhas(it, "rotate")) { int e = (int)std::max(w, h) / 4 + 1; x0 -= e; y0 -= e; x1 += e; y1 += e; }
    m += (int)(num(it, "sw", 2, t) * size / 24) + 1;   // a stroke is scaled with the path
  } else {
    return false;
  }
  b = {(int16_t)(x0 - m), (int16_t)(y0 - m), (int16_t)(x1 + m), (int16_t)(y1 + m)};
  return true;
}

void growBox(Box& d, const Box& b) {
  if (b.x0 > b.x1) return;
  d.x0 = std::min(d.x0, b.x0); d.y0 = std::min(d.y0, b.y0);
  d.x1 = std::max(d.x1, b.x1); d.y1 = std::max(d.y1, b.y1);
}

void custom(const struct tm& t, bool force) {
  if (customName != customWanted || customVer != customFacesVersion()) {
    customName = customWanted;
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
  static String drawnPalette;
  const char* pal = customDoc["palette"];
  paletteName = pal ? evalStr(resolve(pal, t)) : String();
  invertPanel(!customDoc["invert"].isNull() && visibleField(customDoc["invert"], t));
  // A name the face's own palettes lack may be one of the clock's.
  facePal = paletteName.length() && customDoc["palettes"][paletteName].isNull() && !palDoc[paletteName].isNull() ? paletteName : String();
  bool full = force || paletteName != drawnPalette;
  drawnPalette = paletteName;
  JsonVariantConst bgV = customDoc["bg"];
  Paint bgPaint = paintOf(bgV, BG, 0, 0, SCR_W, SCR_H, t);
  uint16_t bg = bgPaint.first();
  JsonArrayConst items = customDoc["items"];
  size_t n = std::min(items.size(), CUSTOM_ITEMS);
  uint32_t h[CUSTOM_ITEMS], g[CUSTOM_ITEMS];
  bool moved[CUSTOM_ITEMS];
  for (size_t i = 0; i < n; ++i) {
    h[i] = g[i] = 2166136261u;
    mixItem(items[i].as<JsonObjectConst>(), t, h[i], g[i]);
    moved[i] = g[i] != customGeo[i] || (h[i] != customHash[i] && !itemOpaque(items[i]));
  }
  static uint16_t drawnBg = 0;
  if (bg != drawnBg) { full = true; drawnBg = bg; }
  // Items that moved: repaint the box around their old and new places, clipped to it.
  Box dirty = {SCR_W, SCR_H, -1, -1};
  for (size_t i = 0; i < n && !full; ++i) {
    if (!moved[i]) continue;
    Box nb;
    if (!customBoxOk[i] || !itemBox(items[i], t, nb)) { full = true; break; }
    growBox(dirty, customBox[i]);
    growBox(dirty, nb);
    customBox[i] = nb;
  }
  int x0 = std::max<int>(0, dirty.x0), y0 = std::max<int>(0, dirty.y0);
  int x1 = std::min<int>(SCR_W - 1, dirty.x1), y1 = std::min<int>(SCR_H - 1, dirty.y1);
  bool clip = !full && x0 <= x1 && y0 <= y1;
  if (full) {
    x0 = y0 = 0; x1 = SCR_W - 1; y1 = SCR_H - 1;
    for (size_t i = 0; i < n; ++i) customBoxOk[i] = itemBox(items[i], t, customBox[i]);
  } else if (clip) {
    D().setViewport(x0, y0, x1 - x0 + 1, y1 - y0 + 1, false);
  }
  if (full || clip) {
    if (bgPaint.n > 1) for (int y = y0; y <= y1; ++y) span(bgPaint, x0, x1, y);
    else D().fillRect(x0, y0, x1 - x0 + 1, y1 - y0 + 1, bg);
  }
  for (size_t i = 0; i < n && clip; ++i) {
    const Box& b = customBox[i];
    if (!customBoxOk[i] || (b.x0 <= x1 && b.x1 >= x0 && b.y0 <= y1 && b.y1 >= y0)) drawItem(items[i], t, bg);
  }
  if (clip) D().resetViewport();
  for (size_t i = 0; i < n; ++i) {
    if (full || (h[i] != customHash[i] && !moved[i])) drawItem(items[i], t, bg);
    customHash[i] = h[i];
    customGeo[i] = g[i];
  }
}

// A custom face's turn in the rotation: "when" (in the rotation only while true) and
// "duration" (seconds on screen; 0 keeps the rotation's). Reads just those two fields.
bool faceTurn(const String& name, uint32_t* seconds, bool* hasWhen = nullptr) {
  File file = LittleFS.open("/faces/" + name + ".json", "r");
  if (!file) return false;
  JsonDocument filter, doc;
  filter["when"] = true; filter["duration"] = true;
  bool ok = !deserializeJson(doc, file, DeserializationOption::Filter(filter));
  file.close();
  if (!ok) return true;
  time_t now = time(nullptr);
  struct tm t; localtime_r(&now, &t);
  if (seconds) *seconds = std::max(0, std::min(3600, (int)lroundf(num(doc.as<JsonObjectConst>(), "duration", 0, t))));
  if (hasWhen) { *hasWhen = !doc["when"].isNull(); return true; }
  return visibleField(doc["when"], t);
}

// ---- The Message face: the message messageShowing() points at, drawn from this
// template by the custom-face renderer ({msg.*} placeholders, see messageValue).
const char MESSAGE_FACE[] PROGMEM = R"({"bg":"{msg.bg}","items":[
{"type":"path","x":120,"y":56,"size":76,"align":"mc","stroke":"{msg.color}","sw":2,"d":"{msg.icon}"},
{"type":"text","x":120,"y":108,"w":228,"lines":1,"text":"{msg.title}","font":4,"align":"tc","color":"{msg.color}"},
{"type":"text","x":120,"y":142,"w":220,"lines":"{msg.pct}>=0?3:4","text":"{msg.text}","font":2,"align":"tc","color":"#f2f2f7"},
{"type":"bar","x":40,"y":204,"w":160,"h":8,"value":"{msg.pct}","color":"{msg.color}","track":"#2c2c2e","show":"{msg.pct}>=0"},
{"type":"text","x":120,"y":228,"text":"{time.hm}","font":2,"align":"mc","color":"#8e8e93"}]})";
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
// Punctuation, and symbols that no other font has (custom faces' "big" text; monoPrep()
// turns their UTF-8 into the codes 1 to 9).
const char MG_COLON[] PROGMEM = "M8.45 5.6h.01M8.45 14.4h.01";
const char MG_DOT[] PROGMEM = "M8.45 19h.01";
const char MG_COMMA[] PROGMEM = "M9.3 16.6L7.7 20";
const char MG_PCT[] PROGMEM = "M2 4.4a2.6 2.6 0 1 0 5.2 0a2.6 2.6 0 1 0-5.2 0M9.7 15.6a2.6 2.6 0 1 0 5.2 0a2.6 2.6 0 1 0-5.2 0M13.8 1.2L3.1 18.8";
const char MG_SLASH[] PROGMEM = "M13.4 0L3.5 20";
const char MG_PLUS[] PROGMEM = "M8.45 4.5v11M2.95 10h11";
const char MG_BANG[] PROGMEM = "M8.45 0v13M8.45 19h.01";
const char MG_ASK[] PROGMEM = "M3.3 5.3C3.6 2.2 5.8 0 8.6 0c3 0 5.2 2.1 5.2 4.9 0 2.4-1.6 3.6-3.4 4.8-1.2.8-1.9 1.8-1.9 3.4v.6M8.5 19h.01";
const char MG_QUOTE[] PROGMEM = "M8.45 0v5";
const char MG_UP[] PROGMEM = "M8.45 20V1M2.5 7l5.95-6 5.95 6";
const char MG_DOWN[] PROGMEM = "M8.45 0v19M2.5 13l5.95 6 5.95-6";
const char MG_LEFT[] PROGMEM = "M15 10H2M8 4l-6 6 6 6";
const char MG_RIGHT[] PROGMEM = "M2 10h13M9 4l6 6-6 6";
const char MG_CHECK[] PROGMEM = "M2 11l4.5 5L15 4";
const char MG_CROSS[] PROGMEM = "M3 4l11 12M14 4L3 16";
const char MG_BULLET[] PROGMEM = "M6.45 10a2 2 0 1 0 4 0a2 2 0 1 0-4 0";
const char MG_HEART[] PROGMEM = "M8.45 17.5C4 14 1.7 11.3 1.7 7.9 1.7 5.2 3.7 3.4 6 3.4c1.2 0 2.2.6 2.45 1.6.25-1 1.25-1.6 2.45-1.6 2.3 0 4.3 1.8 4.3 4.5 0 3.4-2.3 6.1-6.75 9.6z";
const char MG_BOLT[] PROGMEM = "M10 0L3.5 11.5h5L7 20l6.5-11.5h-5z";
struct MonoGlyph { char c; const char* path; };
const MonoGlyph MONO[] = {
    {'0', MG_0}, {'1', MG_1}, {'2', MG_2}, {'3', MG_3}, {'4', MG_4}, {'5', MG_5}, {'6', MG_6},
    {'7', MG_7}, {'8', MG_8}, {'9', MG_9}, {'-', MG_MINUS}, {'*', MG_DEG}, {' ', MG_SPACE},
    {'A', MG_A}, {'B', MG_B}, {'C', MG_C}, {'D', MG_D}, {'E', MG_E}, {'F', MG_F}, {'G', MG_G},
    {'H', MG_H}, {'I', MG_I}, {'J', MG_J}, {'K', MG_K}, {'L', MG_L}, {'M', MG_M}, {'N', MG_N},
    {'O', MG_O}, {'P', MG_P}, {'Q', MG_Q}, {'R', MG_R}, {'S', MG_S}, {'T', MG_T}, {'U', MG_U},
    {'V', MG_V}, {'W', MG_W}, {'X', MG_X}, {'Y', MG_Y}, {'Z', MG_Z},
    {':', MG_COLON}, {'.', MG_DOT}, {',', MG_COMMA}, {'%', MG_PCT}, {'/', MG_SLASH}, {'+', MG_PLUS},
    {'!', MG_BANG}, {'?', MG_ASK}, {'\'', MG_QUOTE}, {'\x01', MG_UP}, {'\x02', MG_DOWN}, {'\x03', MG_LEFT},
    {'\x04', MG_RIGHT}, {'\x05', MG_CHECK}, {'\x06', MG_CROSS}, {'\x07', MG_BULLET}, {'\x08', MG_HEART},
    {'\x09', MG_BOLT}};
// Grid units ('*' is the degree sign). MONO_SB: the side bearing left out when centring.
constexpr float MONO_H = 20, MONO_ADV = 16.9f, MONO_SB = 1.5f, MONO_STROKE = 2.3f;

const MonoGlyph* monoGlyph(char c) {
  c = toupper((unsigned char)c);
  for (const MonoGlyph& g : MONO) if (g.c == c) return &g;
  return nullptr;
}

// : . , ! ' take half a cell, so "10:09" stays compact.
bool monoNarrow(char c) { return strchr(":.,!'", c) != nullptr; }

float monoWidth(const char* s) {
  float w = 0;
  for (; *s; ++s) if (monoGlyph(*s)) w += monoNarrow(*s) ? MONO_ADV / 2 : MONO_ADV;
  return w ? w - 2 * MONO_SB : 0;
}

// Draws s from x (its first cell's left edge) with the glyphs' tops at top, k pixels
// per grid unit; the edges blend into under.
void monoDraw(const char* s, float x, float top, float k, uint16_t color, uint16_t under) {
  Paint paint;
  paint.c[0] = color;
  for (; *s; ++s) {
    const MonoGlyph* g = monoGlyph(*s);
    if (!g) continue;
    String d = pgmString(g->path);
    if (d.length()) {
      Poly P;
      if (!P.ok()) return;
      P.sx = P.sy = k; P.ox = x - (monoNarrow(*s) ? MONO_ADV / 4 * k : 0); P.oy = top;
      parsePath(P, d.c_str());
      P.close();
      strokePoly(P, MONO_STROKE * k, paint, under);
    }
    x += (monoNarrow(*s) ? MONO_ADV / 2 : MONO_ADV) * k;
  }
}

// Draws s centred on cx in the box from y to y+maxH, as large as maxW and maxH allow.
void monoText(const char* s, int cx, int y, int maxW, int maxH, uint16_t color) {
  float k = std::min(maxH / (MONO_H + MONO_STROKE), maxW / (monoWidth(s) + MONO_STROKE));
  monoDraw(s, cx - (monoWidth(s) / 2 + MONO_SB) * k, y + (maxH - MONO_H * k) / 2, k, color, BG);
}

// Text for the line font: the symbols it has (arrows, check, cross, bullet, heart,
// bolt, degree) as their codes, everything else folded to ASCII.
String monoPrep(const String& in) {
  static const char* const SYMBOLS[] = {"\u2191", "\u2193", "\u2190", "\u2192", "\u2713", "\u2717", "\u2022", "\u2665", "\u26a1",
                                        "\u2714", "\u2715", "\u2764", "\u00b0", nullptr};
  static const char CODES[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 5, 6, 8, '*'};
  String s = in;
  for (int i = 0; SYMBOLS[i]; ++i) { char code[2] = {CODES[i], 0}; s.replace(SYMBOLS[i], code); }
  return asciiFold(s);
}

// A text item with "font": "big": the line font, "size" pixels tall (40), narrowed to
// at most "w" pixels; "align", "bg" and "pad" as for other text.
void drawBig(JsonObjectConst it, const struct tm& t, int x, int y, uint16_t c, uint16_t bg) {
  String s = monoPrep(resolve(js(it, "text", ""), t));
  float k = std::max(6.0f, std::min(240.0f, num(it, "size", 40, t))) / MONO_H;
  float tw = (monoWidth(s.c_str()) + MONO_STROKE) * k;
  int maxW = inum(it, "w", 0, t);
  if (maxW > 0 && tw > maxW) { k *= maxW / tw; tw = maxW; }
  float th = (MONO_H + MONO_STROKE) * k;
  uint8_t datum = alignOf(it);
  float left = x - (datum % 3) * tw / 2, top = y - (datum / 3) * th / 2;
  if (jhas(it, "bg")) {
    int pad = inum(it, "pad", 0, t);
    D().fillRect(left - pad, top, tw + 2 * pad + 1, th + 1, bg);
  }
  monoDraw(s.c_str(), left + (MONO_STROKE / 2 - MONO_SB) * k, top + MONO_STROKE / 2 * k, k, c, bg);
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
  monoText(dayLong(t.tm_wday), 120, 6, TILE_W, 44, R[R_DATE]);
  char n[4]; snprintf(n, sizeof(n), "%d", t.tm_mday);
  monoText(n, 120, 56, TILE_W, 128, WHITE);
  monoText(monthLong(t.tm_mon), 120, 190, TILE_W, 44, R[R_DATE]);
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
    monoText(b, 120, 126, TILE_W, 106, R[R_TIME]);
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
  weatherIcon(120, 62, 124, wx.icon, BG, R[R_ICON]);   // the icon fills the top half
  char b[8]; snprintf(b, sizeof(b), "%d*", displayTemp(wx.temp));   // '*' draws the degree sign
  monoText(b, 120, 126, TILE_W, 106, WHITE);
}

// ---- 10-13 Day tiles: Weather today, then one forecast day each ---------------------
// The big number is the feels-like temperature, unlabelled: what it is like to go out.
// Today adds the actual temperature under it. Then a line of small facts split by drawn rules (a space in font 2 is 6 px; typed " | "
// separators do not fit), then the day's low, high and chance of precipitation.

// One item of a fact line: its text, with a sun arrow before it or a degree ring after.
struct Fact { char s[16]; int8_t arrow; bool deg; };   // arrow: 1 sunrise, -1 sunset
constexpr int ARROW_W = 11;   // the arrow and the space after it

int factWidth(const Fact* f, int n, int gap) {   // gap: either side of each rule
  int w = (n - 1) * (2 * gap + 1);
  for (int i = 0; i < n; ++i) w += D().textWidth(f[i].s, 2) + (f[i].arrow ? ARROW_W : 0) + (f[i].deg ? 9 : 0);
  return w;
}

// The facts in font 2, centred on cx but kept on the screen.
void factLine(int cx, int cy, const Fact* f, int n, int gap, uint16_t fg) {
  int w = factWidth(f, n, gap), x = std::max(4, std::min(cx - w / 2, SCR_W - 4 - w));
  for (int i = 0; i < n; ++i) {
    uint16_t c = f[i].arrow ? R[R_SUN] : fg;
    if (f[i].arrow) {   // a small arrow: up before a sunrise, down before a sunset
      int tip = cy - 5 * f[i].arrow;
      D().drawWedgeLine(x + 3.5f, cy - 5, x + 3.5f, cy + 5, 0.75f, 0.75f, c, BG);
      D().drawWedgeLine(x + 0.5f, tip + 3 * f[i].arrow, x + 3.5f, tip, 0.75f, 0.75f, c, BG);
      D().drawWedgeLine(x + 6.5f, tip + 3 * f[i].arrow, x + 3.5f, tip, 0.75f, 0.75f, c, BG);
      x += ARROW_W;
    }
    text(f[i].s, x, cy, 2, c, BG, ML_DATUM);
    x += D().textWidth(f[i].s, 2);
    if (f[i].deg) { ring(x + 5, cy - D().fontHeight(2) / 2 + 5, 3, c, BG); x += 9; }
    if (i < n - 1) { D().drawFastVLine(x + gap, cy - 7, 14, DIM); x += 2 * gap + 1; }
  }
}

void sunFact(Fact& f, int minutes, bool rise) {
  f.arrow = rise ? 1 : -1; f.deg = false;
  if (minutes < 0) { strcpy(f.s, "--"); return; }
  int h = minutes / 60 % 24;
  if (cfg.hour12) h = h % 12 ? h % 12 : 12;
  snprintf(f.s, sizeof(f.s), "%d:%02d", h, minutes % 60);
}

// Wind, humidity and two sun times. When the line is too wide the rules move closer,
// then the wind loses its unit.
void weatherFacts(int cy, float wind, int humidity, const Fact& sun1, const Fact& sun2) {
  Fact f[4] = {};
  int speed = lroundf(cfg.mile ? wind * 2.237f : wind * 3.6f);
  snprintf(f[0].s, sizeof(f[0].s), "%s %d %s", tr("W", "V"), speed, cfg.mile ? "mph" : "km/h");
  snprintf(f[1].s, sizeof(f[1].s), "H %d%%", humidity);
  f[2] = sun1; f[3] = sun2;
  int gap = factWidth(f, 4, 6) <= SCR_W - 8 ? 6 : 4;
  if (factWidth(f, 4, gap) > SCR_W - 8) snprintf(f[0].s, sizeof(f[0].s), "%s %d", tr("W", "V"), speed);
  factLine(120, cy, f, 4, gap, WHITE);
}

void dayTile(int k, const struct tm& t, bool force) {
  static uint32_t seen = 0, seenWx = 0;
  static int seenPhase = -1;
  bool ok = fc.valid && k < fc.count;
  if (!ok) {
    if (force) weatherPlaceholder(tr("No forecast yet", "Pas encore de previsions"));
    return;
  }
  const ForecastDay& d = fc.days[k];
  bool now = k == 0 && wx.valid;   // today, from the current conditions
  // Today shows the next two sun events: before sunrise, today's sunrise and sunset;
  // after it, the sunset and tomorrow's sunrise; after sunset, tomorrow's two.
  int mins = t.tm_hour * 60 + t.tm_min;
  int phase = !now ? 0 : d.sunset >= 0 && mins >= d.sunset ? 2 : d.sunrise >= 0 && mins >= d.sunrise ? 1 : 0;
  if (!force && seen == fc.version && (!now || (seenWx == wx.version && seenPhase == phase))) return;
  if (!force) D().fillScreen(BG);
  seen = fc.version; seenWx = wx.version; seenPhase = phase;
  const ForecastDay& next = fc.days[fc.count > 1 ? 1 : 0];
  monoText(dayLong(d.wday), 120, 6, TILE_W, 40, R[R_DATE]);
  weatherIcon(54, 104, 96, now ? wx.icon : d.icon, BG, R[R_ICON]);   // a 96 px icon on the left
  char b[8];
  snprintf(b, sizeof(b), "%d*", displayTemp(now ? wx.feels : d.feels));   // '*' draws the degree sign
  monoText(b, 170, 58, 134, 72, WHITE);
  if (now) {   // the actual temperature under the feels-like one: "NOW -1", with a ring
    Fact f = {};
    snprintf(f.s, sizeof(f.s), "%s %d", tr("NOW", "ACTUEL"), displayTemp(wx.temp));
    f.deg = true;
    factLine(170, 140, &f, 1, 6, MUTED);
  }
  Fact sun1, sun2;
  if (phase == 0) { sunFact(sun1, d.sunrise, true); sunFact(sun2, d.sunset, false); }
  else if (phase == 1) { sunFact(sun1, d.sunset, false); sunFact(sun2, next.sunrise, true); }
  else { sunFact(sun1, next.sunrise, true); sunFact(sun2, next.sunset, false); }
  weatherFacts(166, now ? wx.wind : d.wind, now ? wx.humidity : d.humidity, sun1, sun2);
  D().drawFastHLine(0, 179, SCR_W, PANEL_HI);
  const char* labels[] = {tr("LOW", "MIN"), tr("HIGH", "MAX"), tr("PRECIP", "PRECIP.")};
  for (int i = 0; i < 3; ++i) text(labels[i], 44 + 76 * i, 194, 2, MUTED, BG, MC_DATUM);
  temp(44, 222, displayTemp(d.lo), 4, R[R_LOW], BG);
  temp(120, 222, displayTemp(d.hi), 4, R[R_HIGH], BG);
  snprintf(b, sizeof(b), "%d%%", std::min(d.pop, 100));
  text(b, 196, 222, 4, RAIN, BG, MC_DATUM);
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
    if (color.isEmpty()) color = hex565Text(R[def->role]);
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
void faceInvalidate() { dirty = true; invertPanel(false); }
void facePhotosChanged() { photosCached = -1; dirty = true; }

int photoCount() {
  if (photosCached < 0) {
    photosCached = 0;
    Dir dir = LittleFS.openDir("/photo");
    while (dir.next()) if (dir.isFile() && isPhoto(dir.fileName()) && photoEnabled(dir.fileName())) photosCached++;
  }
  return photosCached;
}

bool customFaceDue(const String& name, uint32_t* seconds) { return faceTurn(name, seconds); }
void customFaceRule(const String& name, bool* hasWhen, uint32_t* seconds) { *hasWhen = false; faceTurn(name, seconds, hasWhen); }
bool roleKnown(const char* name) { return pick(name, ROLE_NAMES) >= 0; }

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

void faceDraw(int face, const struct tm& t, bool timeValid, const char* customFace) {
  customWanted = customFace ? customFace : cfg.customFace.c_str();
  // The roles' colours: once a second (a role may follow the time or the weather),
  // and at once when the settings or the night palette change.
  static int colorSec = -1;
  static uint16_t colorRev = 0xFFFF;
  static bool colorNight = false;
  if (t.tm_sec != colorSec || colorRev != cfg.colorsRev || colorNight != nightActive()) {
    colorSec = t.tm_sec; colorRev = cfg.colorsRev; colorNight = nightActive();
    if (computeColors(t)) dirty = true;
  }
  if (!timeValid) {
    if (drawnFace != -2 || dirty) syncScreen();
    drawnFace = -2; dirty = false;
    return;
  }
  bool force = dirty || face != drawnFace;
  if (face == FACE_CUSTOM && !cfg.facesApi) face = FACE_CLASSIC;   // integration turned off
  if (face != FACE_CUSTOM && face != FACE_MESSAGE) invertPanel(false);
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

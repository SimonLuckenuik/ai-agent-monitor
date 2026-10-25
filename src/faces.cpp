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
#include "integrations.h"
#include <ArduinoJson.h>

TFT_eSPI& clockDisplay();   // main.cpp
static TFT_eSPI& D() { return clockDisplay(); }

namespace {

constexpr int SCR_W = 240, SCR_H = 240;   // the panel

// ---- Palette (RGB565). Accents come from the user's color1..3.
constexpr uint16_t BG = 0x0000, PANEL = 0x18E3, PANEL_HI = 0x2945, DIM = 0x528A,
                   MUTED = 0x9D14, WHITE = 0xFFFF, CLOUD = 0xE75D, CLOUD_DARK = 0xA536,
                   RAIN = 0x5D9F, SUN = 0xFD84, BOLT = 0xFEA1, MOON = 0xF738;

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
  D().fillSmoothCircle(cx, cy - 15, 5, c, bg);
  D().fillSmoothCircle(cx, cy + 15, 5, c, bg);
}

// HH:MM in font 8 (55 px digits). "12:34" is 249 px wide in that font, so the colon
// is drawn as two dots to fit the 240 px panel. The group is centred, so "9:41"
// sits in the middle too; the band is cleared only when the width changes.
int bigTimeColonX(int cx, const struct tm& t) {
  char h[4]; snprintf(h, sizeof(h), cfg.hour12 ? "%d" : "%02d", clockHour(t));
  return cx + (D().textWidth(h, 8) - 110) / 2;   // 110 = two digits
}

void bigTime(int cx, int cy, const struct tm& t, uint16_t fg, uint16_t bg) {
  static int lastColon = -1;
  char h[4], m[4];
  snprintf(h, sizeof(h), cfg.hour12 ? "%d" : "%02d", clockHour(t));
  snprintf(m, sizeof(m), "%02d", t.tm_min);
  int c = bigTimeColonX(cx, t);
  if (c != lastColon) { D().fillRect(0, cy - 38, 240, 76, bg); lastColon = c; }
  text(h, c - 10, cy, 8, fg, bg, MR_DATUM);
  text(m, c + 10, cy, 8, fg, bg, ML_DATUM);
  colon(c, cy, fg, bg);
}

void smallTime(char* out, size_t n, const struct tm& t) {
  snprintf(out, n, cfg.hour12 ? "%d:%02d" : "%02d:%02d", clockHour(t), t.tm_min);
}

const char* ampm(const struct tm& t) { return cfg.hour12 ? (t.tm_hour < 12 ? "AM" : "PM") : ""; }

// ---- Weather icons, drawn from primitives at any size ----------------------------
// Anti-aliased shapes blend their edges into a single background colour, so where
// circles overlap the seams are painted over with a plain fill afterwards.
void cloud(int cx, int cy, int s, uint16_t c, uint16_t bg) {
  struct { float x, y, r; } puffs[] = {{-0.22f, 0.04f, 0.22f}, {0.02f, -0.08f, 0.30f}, {0.26f, 0.06f, 0.20f}};
  int bx = cx - s * 0.44f, by = cy + s * 0.02f, bw = s * 0.88f, bh = s * 0.24f;
  for (auto& p : puffs) D().fillSmoothCircle(cx + p.x * s, cy + p.y * s, p.r * s, c, bg);
  D().fillSmoothRoundRect(bx, by, bw, bh, bh / 2, c, bg);
  for (auto& p : puffs) D().fillCircle(cx + p.x * s, cy + p.y * s, p.r * s - 1, c);
  D().fillRoundRect(bx + 1, by + 1, bw - 2, bh - 2, bh / 2 - 1, c);
}

void sun(int cx, int cy, int R, uint16_t bg) {
  D().fillSmoothCircle(cx, cy, R * 0.55f, SUN, bg);
  float w = R > 30 ? 2.2f : R > 14 ? 1.5f : 1.0f;
  for (int i = 0; i < 8; ++i) {
    float a = i * PI / 4;
    D().drawWedgeLine(cx + sinf(a) * R * 0.72f, cy - cosf(a) * R * 0.72f,
                      cx + sinf(a) * R, cy - cosf(a) * R, w, w, SUN, bg);
  }
}

void moon(int cx, int cy, int r, uint16_t bg) {
  D().fillSmoothCircle(cx, cy, r, MOON, bg);
  D().fillCircle(cx + r * 0.45f, cy - r * 0.35f, r * 0.8f, bg);   // the cut-out
}

void weatherIcon(int cx, int cy, int s, const String& icon, uint16_t bg) {
  int code = icon.length() >= 2 ? icon.substring(0, 2).toInt() : 0;
  bool night = icon.endsWith("n");
  auto drops = [&](uint16_t c, bool snow) {
    float pos[][2] = {{-0.22f, 0.20f}, {0.0f, 0.26f}, {0.22f, 0.20f}};
    for (auto& p : pos) {
      int x = cx + p[0] * s, y = cy + p[1] * s;
      if (snow) D().fillSmoothCircle(x, y + s * 0.06f, std::max(2, s / 22), c, bg);
      else D().drawWedgeLine(x, y, x - s * 0.06f, y + s * 0.16f, std::max(1.0f, s / 44.0f), std::max(1.0f, s / 44.0f), c, bg);
    }
  };
  switch (code) {
    case 1: if (night) moon(cx, cy, s * 0.30f, bg); else sun(cx, cy, s * 0.42f, bg); break;
    case 2:
      if (night) moon(cx - s * 0.14f, cy - s * 0.14f, s * 0.22f, bg); else sun(cx - s * 0.14f, cy - s * 0.16f, s * 0.30f, bg);
      cloud(cx + s * 0.08f, cy + s * 0.06f, s * 0.78f, CLOUD, bg);
      break;
    case 3: cloud(cx, cy, s * 0.9f, CLOUD, bg); break;
    case 4:
      cloud(cx + s * 0.14f, cy - s * 0.12f, s * 0.68f, CLOUD_DARK, bg);
      cloud(cx - s * 0.06f, cy + s * 0.06f, s * 0.80f, CLOUD, bg);
      break;
    case 9: case 10:
      if (code == 10 && !night) sun(cx + s * 0.18f, cy - s * 0.24f, s * 0.24f, bg);
      cloud(cx, cy - s * 0.12f, s * 0.86f, CLOUD, bg);
      drops(RAIN, false);
      break;
    case 11: {
      cloud(cx, cy - s * 0.12f, s * 0.86f, CLOUD_DARK, bg);
      D().fillTriangle(cx + s * 0.06f, cy + s * 0.08f, cx - s * 0.12f, cy + s * 0.30f, cx + s * 0.02f, cy + s * 0.30f, BOLT);
      D().fillTriangle(cx + s * 0.08f, cy + s * 0.24f, cx - s * 0.06f, cy + s * 0.46f, cx - s * 0.04f, cy + s * 0.24f, BOLT);
      break;
    }
    case 13: cloud(cx, cy - s * 0.12f, s * 0.86f, CLOUD, bg); drops(WHITE, true); break;
    case 50:
      for (int i = 0; i < 3; ++i) {
        int w = s * (i == 1 ? 0.86f : 0.66f), h = std::max(3, s / 12);
        D().fillSmoothRoundRect(cx - w / 2 + (i == 2 ? s * 0.08f : 0), cy + (i - 1) * s * 0.2f - h / 2, w, h, h / 2, CLOUD_DARK, bg);
      }
      break;
    default: cloud(cx, cy, s * 0.9f, CLOUD_DARK, bg); break;
  }
}

String dayIcon(const String& icon) {   // forecast days always use the day variant
  return icon.length() >= 2 ? icon.substring(0, 2) + "d" : icon;
}

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
    bigTime(120, 112, t, cfg.color1, BG);
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
      weatherIcon(x + 18, 206, 36, wx.icon, BG);
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
  weatherIcon(64, 104, 100, wx.icon, BG);
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
    weatherIcon(cx, 110, 44, dayIcon(d.icon), PANEL);
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

// ---- 6 Flip: split-flap cards with a fold animation ---------------------------------
constexpr int FLIP_Y = 34, FLIP_H = 136, FLIP_W = 114, FLIP_MID = FLIP_Y + FLIP_H / 2;

void flipCard(int x, const char* digits, const char* tag) {
  D().fillSmoothRoundRect(x, FLIP_Y, FLIP_W, FLIP_H, 12, PANEL, BG);
  D().fillSmoothRoundRect(x, FLIP_Y, FLIP_W, FLIP_H / 2 + 12, 12, PANEL_HI, BG);
  D().fillRect(x, FLIP_MID, FLIP_W, 13, PANEL);
  textClear(digits, x + FLIP_W / 2, FLIP_MID + 3, 8, WHITE, MC_DATUM);
  if (tag && *tag) textClear(tag, x + 12, FLIP_Y + 10, 2, MUTED, TL_DATUM);
  D().fillRect(x, FLIP_MID - 1, FLIP_W, 3, BG);   // the hinge
  D().fillCircle(x, FLIP_MID, 4, BG);
  D().fillCircle(x + FLIP_W - 1, FLIP_MID, 4, BG);
}

// The top flap falls: the new top half is revealed from the top edge down, then the
// flap's back shows the new bottom half growing from the hinge.
void flipAnimate(int x, const char* digits, const char* tag) {
  const int steps = 6, half = FLIP_H / 2;
  for (int k = 1; k <= steps; ++k) {
    D().setViewport(x, FLIP_Y, FLIP_W, half * k / steps, false);
    flipCard(x, digits, tag);
    D().resetViewport();
    delay(10);
  }
  for (int k = 1; k <= steps; ++k) {
    D().setViewport(x, FLIP_MID, FLIP_W, (half + 1) * k / steps, false);
    flipCard(x, digits, tag);
    D().resetViewport();
    delay(10);
  }
}

void flip(const struct tm& t, bool force) {
  static char ph[4] = "", pm[4] = "";
  static int day = -1;
  static uint32_t wxv = ~0u;
  if (force) { ph[0] = pm[0] = 0; day = -1; wxv = ~0u; }
  char h[4], m[4];
  snprintf(h, sizeof(h), cfg.hour12 ? "%d" : "%02d", clockHour(t));
  snprintf(m, sizeof(m), "%02d", t.tm_min);
  const char* tag = ampm(t);
  if (strcmp(h, ph)) { if (force || !ph[0]) flipCard(4, h, tag); else flipAnimate(4, h, tag); strcpy(ph, h); }
  if (strcmp(m, pm)) { if (force || !pm[0]) flipCard(122, m, nullptr); else flipAnimate(122, m, nullptr); strcpy(pm, m); }
  if (t.tm_yday != day) {
    char db[28];
    snprintf(db, sizeof(db), "%s %d %s", dayLong(t.tm_wday), t.tm_mday, monthLong(t.tm_mon));
    if (D().textWidth(db, 4) > 228) snprintf(db, sizeof(db), "%.3s %d %s", dayLong(t.tm_wday), t.tm_mday, monthShort(t.tm_mon));
    text(db, 120, 194, 4, cfg.color2, BG, MC_DATUM, 236);
    day = t.tm_yday;
  }
  if (wx.version != wxv) {
    D().fillRect(0, 210, 240, 30, BG);
    if (wx.valid) {
      weatherIcon(96, 224, 24, wx.icon, BG);
      temp(130, 225, displayTemp(wx.temp), 2, MUTED, BG);
    }
    wxv = wx.version;
  }
}

// ---- 4 Simple: the time and the date, nothing else -------------------------------
void simple(const struct tm& t, bool force) {
  static int min = -1, sec = -1, day = -1;
  if (force) { min = sec = day = -1; }
  if (t.tm_min != min) { bigTime(120, 104, t, WHITE, BG); min = t.tm_min; }
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
  if (key == "bg") {   // the colour at a quarter strength, for the top of the background
    uint16_t c = hex565(color.c_str(), WHITE);
    char b[8];
    snprintf(b, sizeof(b), "#%02x%02x%02x", ((c >> 11) * 255 / 31) / 4, (((c >> 5) & 63) * 255 / 63) / 4, ((c & 31) * 255 / 31) / 4);
    return b;
  }
  return messageField(slot, key);
}

String resolve(const char* tpl, const struct tm& t) {
  String out;
  for (const char* p = tpl; *p; ++p) {
    const char* end = *p == '{' ? strchr(p, '}') : nullptr;
    if (!end || end - p > 16) { out += *p; continue; }
    out += fieldValue(String(p + 1).substring(0, end - p - 1), t);
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
          if ((ya <= sy) == (yb <= sy) || k >= MAX_X) continue;
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
        auto addCov = [&](int x, float amount) { if (x >= 0 && x < SCR_W) cov[x] = std::min(255, cov[x] + (int)(amount * 255 / SUB + 0.5f)); };
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
  float minY = 1e9f, maxY = -1e9f;
  for (int i = 0; i < P.n; ++i) { minY = std::min(minY, P.y[i]); maxY = std::max(maxY, P.y[i]); }
  int y0 = std::max(0, (int)floorf(minY - r - 1)), y1 = std::min(SCR_H - 1, (int)ceilf(maxY + r + 1));
  for (int y = y0; y <= y1; ++y) {
    memset(cov, 0, sizeof(cov));
    int lo = SCR_W, hi = -1;
    float py = y + 0.5f;
    for (int ring = 0, rings = P.ringCount(); ring < rings; ++ring) {
      int a0 = P.ringStart[ring], a1 = P.ringEnd(ring);
      for (int i = a0; i < a1; ++i) {
        int j = i + 1 < a1 ? i + 1 : i;   // a lone point still draws a dot
        float ax = P.x[i], ay = P.y[i], dx = P.x[j] - ax, dy = P.y[j] - ay;
        if (py < std::min(ay, ay + dy) - r - 1 || py > std::max(ay, ay + dy) + r + 1) continue;
        float len2 = dx * dx + dy * dy;
        int xa = std::max(0, (int)floorf(std::min(ax, ax + dx) - r - 1));
        int xb = std::min(SCR_W - 1, (int)ceilf(std::max(ax, ax + dx) + r + 1));
        for (int x = xa; x <= xb; ++x) {
          float px = x + 0.5f, u = len2 ? ((px - ax) * dx + (py - ay) * dy) / len2 : 0;
          u = std::min(1.0f, std::max(0.0f, u));
          float ex = px - ax - u * dx, ey = py - ay - u * dy;
          float c = r + 0.5f - sqrtf(ex * ex + ey * ey);
          if (c <= 0) continue;
          cov[x] = std::max<int>(cov[x], c >= 1 ? 255 : (int)(c * 255));
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
    weatherIcon(x, y, size, resolve(js(it, "icon", "{icon}"), t), bg);
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
const char MESSAGE_FACE[] PROGMEM = R"({"bg":{"stops":["{msg.bg}","#000000"],"angle":180},"items":[
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

}  // namespace

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
    case FACE_DIAL:     return false;   // removed; the id stays reserved
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
    case FACE_FLIP:     flip(t, force);     break;
    case FACE_CUSTOM:   custom(t, force);   break;
    case FACE_MESSAGE:  message(t, force);  break;
    default:            classic(t, force);  break;
  }
  drawnFace = face; dirty = false;
}

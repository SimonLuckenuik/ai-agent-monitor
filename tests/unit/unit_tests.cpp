// Unit tests for the custom-face engine (expressions, numbers, colours, gradients,
// SVG paths, the Message face's icons and template) and for asciiFold().
// src/faces.cpp is included so its internal functions are reachable here.
// run_unit_tests.py builds and runs this.
#include <Arduino.h>
#include <LittleFS.h>
#include <TFT_eSPI.h>
#include <map>
#include <vector>
#include <string>
#include "faces.cpp"

// ---- What faces.cpp needs from the rest of the firmware.
Config cfg;
Weather wx;
Forecast fc;
LittleFSClass LittleFS;
static std::map<std::string, std::string> vars;
uint32_t customFacesVersion() { return 1; }
bool customFaceExists(const String&) { return false; }
String faceVar(const String& key) { auto it = vars.find(key.c_str()); return it == vars.end() ? String() : String(it->second); }
bool photoEnabled(const String&) { return true; }
bool messageActive(int) { return false; }
int messageShowing() { return 0; }
uint32_t messageVersion() { return 1; }
String messageField(int, const String&) { return String(); }
static std::map<std::string, std::string> device;   // net.*, sys.*, ...
String deviceValue(const String& key) { auto it = device.find(key.c_str()); return it == device.end() ? String() : String(it->second); }
static bool night = false;   // night palette
bool nightActive() { return night; }
long faceVarAge(const String& key) { return vars.count(key.c_str()) ? 30 : -1; }
static std::vector<float> history;   // the "load" variable's
int faceHistory(const String& key, float* out, int max, uint32_t* pushes) {
  if (key != "load") return 0;
  if (pushes) *pushes = history.size();
  int n = std::min<int>(history.size(), max);
  for (int i = 0; i < n && out; ++i) out[i] = history[history.size() - n + i];
  return n;
}
void wmoText(int, bool fr, String& main, String& desc) { main = fr ? "Nuageux" : "Clouds"; desc = main; }
uint32_t millis() { return 0; }
void delay(unsigned long) {}
TFT_eSPI& clockDisplay() { static TFT_eSPI panel; return panel; }

// ---- A tiny harness.
static int checks = 0, failures = 0;
#define CHECK(cond) do { ++checks; if (!(cond)) { ++failures; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)
#define CHECK_NUM(expr, want) do { float got_ = (expr); ++checks; \
  if (fabsf(got_ - (float)(want)) > 0.01f) { ++failures; printf("FAIL %s:%d: %s = %g, want %g\n", __FILE__, __LINE__, #expr, got_, (double)(want)); } } while (0)
// Curves are flattened to a few segments: their extremes are sampled, within ~0.2 px.
#define CHECK_CURVE(expr, want) do { float got_ = (expr); ++checks; \
  if (fabsf(got_ - (float)(want)) > 0.2f) { ++failures; printf("FAIL %s:%d: %s = %g, want %g\n", __FILE__, __LINE__, #expr, got_, (double)(want)); } } while (0)
#define CHECK_STR(expr, want) do { std::string got_ = (expr).c_str(); ++checks; \
  if (got_ != (want)) { ++failures; printf("FAIL %s:%d: %s = \"%s\", want \"%s\"\n", __FILE__, __LINE__, #expr, got_.c_str(), want); } } while (0)

static struct tm now() { struct tm t{}; t.tm_year = 126; t.tm_mon = 9; t.tm_mday = 26; t.tm_hour = 9; t.tm_min = 5; t.tm_sec = 7; return t; }

// An item or value parsed from JSON text; the document outlives the returned view.
static JsonDocument doc;
static JsonObjectConst item(const char* json) { doc.clear(); deserializeJson(doc, json); return doc.as<JsonObjectConst>(); }

static void parse(Poly& P, const char* d, float scale = 1) {
  P.sx = P.sy = scale;
  parsePath(P, d);
  P.close();
}
static float minY(const Poly& P) { float m = 1e9f; for (int i = 0; i < P.n; ++i) m = std::min(m, P.y[i]); return m; }
static float maxY(const Poly& P) { float m = -1e9f; for (int i = 0; i < P.n; ++i) m = std::max(m, P.y[i]); return m; }
static bool last(const Poly& P, float x, float y) { return P.n && fabsf(P.x[P.n - 1] - x) < 0.01f && fabsf(P.y[P.n - 1] - y) < 0.01f; }

static void testNumbers() {
  const char* s = "1.5.5";
  CHECK_NUM(parseNum(s), 1.5f);
  CHECK_NUM(parseNum(s), 0.5f);   // "1.5.5" is two numbers, as in SVG path data
  s = "-.5"; CHECK_NUM(parseNum(s), -0.5f);
  s = "1e2"; CHECK_NUM(parseNum(s), 100);
  s = "2E-1"; CHECK_NUM(parseNum(s), 0.2f);
  bool ok = true;
  s = "abc"; parseNum(s, &ok); CHECK(!ok);
  s = "-"; parseNum(s, &ok); CHECK(!ok);
}

static void testExpressions() {
  CHECK_NUM(evalNum("1+2*3"), 7);
  CHECK_NUM(evalNum("(1+2)*3"), 9);
  CHECK_NUM(evalNum("10-4-3"), 3);          // left to right
  CHECK_NUM(evalNum("2*3%4"), 2);
  CHECK_NUM(evalNum("10%3"), 1);
  CHECK_NUM(evalNum("7/2"), 3.5f);
  CHECK_NUM(evalNum("1/0"), 0);             // no division by zero
  CHECK_NUM(evalNum("-5+2"), -3);
  CHECK_NUM(evalNum("2*-3"), -6);
  CHECK_NUM(evalNum(" 1 + 2 "), 3);
  CHECK_NUM(evalNum("42"), 42);
  CHECK_NUM(evalNum("-7"), -7);
  CHECK_NUM(evalNum("sin(90)"), 1);         // degrees
  CHECK_NUM(evalNum("cos(180)"), -1);
  CHECK_NUM(evalNum("120+90*sin(30)"), 165);
  CHECK_NUM(evalNum("min(3,4)"), 3);
  CHECK_NUM(evalNum("max(3,4)"), 4);
  CHECK_NUM(evalNum("abs(-2.5)"), 2.5f);
  CHECK_NUM(evalNum("round(2.6)"), 3);
  CHECK_NUM(evalNum("nope(2)"), 0);         // unknown function
  CHECK_NUM(evalNum("3>2"), 1);
  CHECK_NUM(evalNum("2>=3"), 0);
  CHECK_NUM(evalNum("1==1&&2==2"), 1);
  CHECK_NUM(evalNum("1==2||2==2"), 1);
  CHECK_NUM(evalNum("!0"), 1);
  CHECK_NUM(evalNum("1?5:6"), 5);
  CHECK_NUM(evalNum("0?5:6"), 6);
  CHECK_NUM(evalNum("0?1:0?2:3"), 3);       // nested, right to left
  CHECK_NUM(evalNum("--"), 0);              // a weather placeholder with no data yet

  CHECK_STR(evalStr("warn==warn"), "1");    // words compare as text
  CHECK_STR(evalStr("warn==ok"), "0");
  CHECK_STR(evalStr("warn!=ok"), "1");
  CHECK_STR(evalStr("light rain==light rain"), "1");
  CHECK_STR(evalStr("1==1.0"), "1");        // numbers compare as numbers
  CHECK_STR(evalStr("1?#ff0000:#00ff00"), "#ff0000");
  CHECK_STR(evalStr("0?#ff0000:#00ff00"), "#00ff00");
  CHECK_STR(evalStr("1?light rain:clear"), "light rain");
  CHECK_STR(evalStr("abc"), "abc");         // no operator: left as it is
  CHECK_STR(evalStr("2.5*2"), "5");
  CHECK_STR(evalStr("1/3"), "0.333");
  CHECK_STR(evalStr("0-1.25"), "-1.250");
}

static void testFields() {
  struct tm t = now();
  vars = {{"state", "warn"}, {"n", "42"}};
  CHECK_STR(resolve("{state}-{n}", t), "warn-42");
  CHECK_STR(resolve("{nope}!", t), "!");
  CHECK_STR(resolve("{nope|waiting}", t), "waiting");   // the fallback while empty
  CHECK_STR(resolve("{state|none}", t), "warn");
  CHECK_STR(resolve("{time.sec}", t), "07");
  CHECK_STR(resolve("{not a placeholder because it is too long}", t), "{not a placeholder because it is too long}");

  CHECK_NUM(num(item(R"({"x":"120+{n}"})"), "x", 0, t), 162);
  CHECK_NUM(num(item(R"({"x":5})"), "x", 0, t), 5);
  CHECK_NUM(num(item(R"({})"), "x", 9, t), 9);
  CHECK_NUM(inum(item(R"({"x":"10/4"})"), "x", 0, t), 3);   // rounded

  CHECK(visible(item(R"({})"), t));
  CHECK(visible(item(R"({"show":"{state}==warn"})"), t));
  CHECK(!visible(item(R"({"show":"{state}==ok"})"), t));
  CHECK(visible(item(R"({"show":"{n}>40"})"), t));
  CHECK(!visible(item(R"({"show":false})"), t));
  CHECK(!visible(item(R"({"show":"0"})"), t));
  CHECK(!visible(item(R"({"show":"off"})"), t));
  CHECK(!visible(item(R"({"show":"{nope}"})"), t));
}

static void testColours() {
  struct tm t = now();
  CHECK(hex565("#ffffff", 1) == 0xFFFF);
  CHECK(hex565("#fff", 1) == 0xFFFF);
  CHECK(hex565("#ff0000", 1) == 0xF800);
  CHECK(hex565("#00ff00", 1) == 0x07E0);
  CHECK(hex565("#0000ff", 1) == 0x001F);
  CHECK(hex565("red", 7) == 7);
  CHECK(hex565("#12345", 7) == 7);
  CHECK(colorOf(item(R"({"c":63488})")["c"], 0, t) == 63488);
  CHECK(colorOf(item(R"({"c":"{n}>40?#00ff00:#ff0000"})")["c"], 0, t) == 0x07E0);
  CHECK(colorOf(item(R"({})")["c"], 5, t) == 5);
  CHECK(lerp565(0x0000, 0xFFFF, 0) == 0x0000);
  CHECK(lerp565(0x0000, 0xFFFF, 256) == 0xFFFF);

  auto red = [](uint16_t c) { return c >> 11; };
  auto green = [](uint16_t c) { return (c >> 5) & 63; };
  auto blue = [](uint16_t c) { return c & 31; };
  Paint p = paintOf(item(R"({"g":{"stops":["#000000","#ffffff"],"angle":180}})")["g"], 0, 0, 0, 10, 100, t);
  CHECK(p.n == 2);
  CHECK(red(paintAt(p, 5, 0.5f)) <= 1);      // top: black
  CHECK(red(paintAt(p, 5, 99.5f)) >= 30);    // bottom: white
  CHECK(abs((int)red(paintAt(p, 5, 50)) - 15) <= 2);
  p = paintOf(item(R"({"g":{"from":"#000000","to":"#ffffff","angle":90}})")["g"], 0, 0, 0, 100, 10, t);
  CHECK(red(paintAt(p, 0.5f, 5)) <= 1 && red(paintAt(p, 99.5f, 5)) >= 30);   // left to right
  p = paintOf(item(R"({"g":{"stops":["#ff0000","#00ff00","#0000ff"]}})")["g"], 0, 0, 0, 10, 100, t);
  CHECK(p.n == 3);
  CHECK(red(paintAt(p, 5, 0.5f)) >= 29 && green(paintAt(p, 5, 50)) >= 60 && blue(paintAt(p, 5, 99.5f)) >= 29);
  p = paintOf(item(R"({"g":"#00ff00"})")["g"], 0, 0, 0, 10, 10, t);
  CHECK(p.n == 1 && p.first() == 0x07E0);
}

static void testPaths() {
  { Poly P; parse(P, "M0 0L10 0L10 10Z");
    CHECK(P.n == 4 && P.ringCount() == 1 && last(P, 0, 0)); }
  { Poly P; parse(P, "M0 0 10 0 10 10");           // pairs after M are line-tos
    CHECK(P.n == 3 && last(P, 10, 10)); }
  { Poly P; parse(P, "m1 1l2 0l0 2z");             // relative
    CHECK(P.n == 4 && P.x[2] == 3 && P.y[2] == 3 && last(P, 1, 1)); }
  { Poly P; parse(P, "M0 0h5v5H0z");
    CHECK(P.n == 5 && P.x[2] == 5 && P.y[2] == 5 && P.x[3] == 0); }
  { Poly P; parse(P, "M0 0L1 0M5 5L6 5");          // two subpaths
    CHECK(P.ringCount() == 2); }
  { Poly P; parse(P, "M0 0L1e1 0");
    CHECK(last(P, 10, 0)); }
  { Poly P; parse(P, "M1 1", 2);                   // the viewBox scale
    CHECK(P.n == 1 && P.x[0] == 2 && P.y[0] == 2); }
  { Poly P; parse(P, "M0 0C0 10 10 10 10 0");
    CHECK(last(P, 10, 0)); CHECK_CURVE(maxY(P), 7.5f); }
  { Poly P; parse(P, "M0 0C0 10 10 10 10 0S20 -10 20 0");   // reflected control point
    CHECK(last(P, 20, 0)); CHECK_CURVE(minY(P), -7.5f); }
  { Poly P; parse(P, "M0 0Q5 10 10 0");
    CHECK(last(P, 10, 0)); CHECK_CURVE(maxY(P), 5); }
  { Poly P; parse(P, "M0 0Q5 10 10 0T20 0");
    CHECK(last(P, 20, 0)); CHECK_CURVE(minY(P), -5); }
  { Poly P; parse(P, "M2 12a10 10 0 1 0 20 0a10 10 0 1 0-20 0");   // a circle of two arcs
    bool round = P.n >= 8;
    float lo = 1e9f, hi = -1e9f;
    for (int i = 0; i < P.n; ++i) {
      float d = sqrtf((P.x[i] - 12) * (P.x[i] - 12) + (P.y[i] - 12) * (P.y[i] - 12));
      round = round && fabsf(d - 10) < 0.05f;
      lo = std::min(lo, P.x[i]); hi = std::max(hi, P.x[i]);
    }
    CHECK(round); CHECK_NUM(lo, 2); CHECK_NUM(hi, 22); CHECK(last(P, 2, 12)); }
  { Poly P; parse(P, "M0 0a5 5 0 0110 0");          // packed arc flags
    CHECK(last(P, 10, 0) && P.n > 2); }
  { Poly P; P.cosr = 0; P.sinr = 1; parse(P, "M1 0");   // a quarter turn about (0, 0)
    CHECK(fabsf(P.x[0]) < 0.01f && fabsf(P.y[0] - 1) < 0.01f); }
  { Poly P; parse(P, "M0 0L5 5X9 9");               // an unknown command stops the path
    CHECK(P.n == 2); }
}

static void testMessageFace() {
  for (const MessageIcon& icon : MESSAGE_ICON_SET) {
    Poly P;
    parse(P, pgmString(icon.path).c_str());
    float x0 = 1e9f, x1 = -1e9f, y0 = 1e9f, y1 = -1e9f;
    for (int i = 0; i < P.n; ++i) {
      x0 = std::min(x0, P.x[i]); x1 = std::max(x1, P.x[i]);
      y0 = std::min(y0, P.y[i]); y1 = std::max(y1, P.y[i]);
    }
    bool inside = P.n >= 4 && x0 >= -0.5f && y0 >= -0.5f && x1 <= 24.5f && y1 <= 24.5f && x1 - x0 >= 12 && y1 - y0 >= 12;
    if (!inside) printf("  icon %s: %d points, x %.1f..%.1f y %.1f..%.1f\n", icon.name, P.n, x0, x1, y0, y1);
    CHECK(inside);
    CHECK(icon.role >= R_OK && icon.role <= R_INFO);
  }
  JsonDocument face;
  CHECK(!deserializeJson(face, pgmString(MESSAGE_FACE).c_str()));
  CHECK(face["items"].size() == 5);
}

// The Big tiles' font: every glyph inside its slot, digits the full cap height, so
// numbers line up.
static void testMonoFont() {
  for (const MonoGlyph& g : MONO) {
    String d = pgmString(g.path);
    if (d.isEmpty()) continue;
    Poly P;
    parse(P, d.c_str());
    float x0 = 1e9f, x1 = -1e9f, y0 = 1e9f, y1 = -1e9f;
    for (int i = 0; i < P.n; ++i) {
      x0 = std::min(x0, P.x[i]); x1 = std::max(x1, P.x[i]);
      y0 = std::min(y0, P.y[i]); y1 = std::max(y1, P.y[i]);
    }
    bool digit = g.c >= '0' && g.c <= '9';
    bool inside = x0 >= 0 && x1 <= MONO_ADV && y0 >= -0.05f && y1 <= MONO_H + 0.05f;
    bool full = !digit || (y0 <= 0.5f && y1 >= MONO_H - 0.5f);
    if (!inside || !full) printf("  glyph %c: x %.2f..%.2f y %.2f..%.2f\n", g.c, x0, x1, y0, y1);
    CHECK(inside && full);
  }
}

static void testAsciiFold() {
  CHECK_STR(asciiFold("plain ASCII 123"), "plain ASCII 123");
  CHECK_STR(asciiFold("d\xC3\xA9j\xC3\xA0 vu"), "deja vu");
  CHECK_STR(asciiFold("na\xC3\xAFve caf\xC3\xA9"), "naive cafe");
  CHECK_STR(asciiFold("Stra\xC3\x9F" "e"), "Strasse");
  CHECK_STR(asciiFold("\xC3\x86sir \xC3\xA6"), "AEsir ae");
  CHECK_STR(asciiFold("\xC5\x92uvre \xC5\x93uf"), "OEuvre oeuf");
  CHECK_STR(asciiFold("\xE2\x80\x9Chi\xE2\x80\x9D \xE2\x80\x93 it\xE2\x80\x99s ok\xE2\x80\xA6"), "\"hi\" - it's ok...");
  CHECK_STR(asciiFold("\xE2\x82\xAC" "5"), "?5");   // the euro sign: one '?'
  CHECK_STR(asciiFold("\xE6\x97\xA5\xE6\x9C\xAC"), "??");
}

// The older rotation order (setting "faceorder") as face entries.
static void testFaceOrder() {
  CHECK_STR(Config::entriesOf("7801234569"), "#7,#8,#0,#1,#2,#3,#4,#5,#6,#9");
  CHECK_STR(Config::entriesOf("73xd"), "#7,#3,#13");   // x is no face id
  CHECK_STR(Config::entriesOf(""), "");
}

// Placeholder names, modifiers, maps, expressions in braces, palettes, repeats.
static void testPlaceholders() {
  struct tm t = now();   // Monday 26 October 2026, 09:05:07
  t.tm_wday = 1; t.tm_yday = 298;
  cfg.hour12 = false; cfg.celsius = true; cfg.mile = false;
  computeColors(t);   // the default palette and roles
  vars = {{"state", "warn"}, {"n", "42"}, {"pi", "3.14159"}, {"neg", "-3"}};
  device = {{"net.ip", "10.0.0.2"}};
  wx.valid = true; wx.temp = 21.4f; wx.feels = 19.6f; wx.humidity = 64; wx.wind = 4.2f;
  wx.sunrise = 7 * 60; wx.sunset = 18 * 60; wx.pressure = 1013; wx.icon = "wi-day-rain";
  fc.valid = true; fc.count = 2;
  fc.days[0].hi = 24.6f; fc.days[0].lo = 15.2f; fc.days[0].pop = 40; fc.days[0].wday = 1;
  fc.days[1].hi = 12; fc.days[1].lo = 3; fc.days[1].wday = 2; fc.days[1].sunrise = 7 * 60 + 2; fc.days[1].icon = "wi-day-snow";

  // Names.
  CHECK_STR(resolve("{time.hm}", t), "09:05");
  CHECK_STR(resolve("{time.hour}:{time.min}:{time.sec}", t), "09:05:07");
  CHECK_STR(resolve("{time.daymin}", t), "545");
  CHECK_STR(resolve("{date.wday} {date.wday3} {date.day} {date.month} {date.month3}", t), "Monday MON 26 October Oct");
  CHECK_STR(resolve("{date.monthnum}/{date.year} {date.yday} w{date.week}", t), "10/2026 299 w44");
  CHECK_STR(resolve("{date.text}", t), "26 October");
  CHECK_STR(resolve("{date.wdaynum} {date.mdays}", t), "1 31");
  struct tm feb = t; feb.tm_mon = 1; feb.tm_wday = 0;
  CHECK_STR(resolve("{date.wdaynum} {date.mdays}", feb), "7 28");
  feb.tm_year = 128;   // 2028: a leap year
  CHECK_STR(resolve("{date.mdays}", feb), "29");
  struct tm months = t;
  std::string lengths;
  for (int m = 0; m < 12; ++m) { months.tm_mon = m; lengths += resolve("{date.mdays}", months).c_str(); lengths += " "; }
  CHECK_STR(String(lengths), "31 28 31 30 31 30 31 31 30 31 30 31 ");
  CHECK_STR(resolve("{now.temp} {now.feels} {now.hum} {now.wind} {now.press}", t), "21 20 64 15 1013");
  CHECK_STR(resolve("{now.icon}", t), "wi-day-rain");
  CHECK_STR(resolve("{units.temp} {units.wind}", t), "C km/h");
  CHECK_STR(resolve("{day0.high}/{day0.low} {day0.rain}% {day1.wday3} {day1.icon}", t), "25/15 40% TUE wi-day-snow");
  CHECK_STR(resolve("{day3.high}", t), "--");    // no such day
  CHECK_STR(resolve("{sun.up} {sun.next} {sun.rise} {sun.set}", t), "1 set 07:00 18:00");
  CHECK_STR(resolve("{sun.until_set} {sun.until_rise} {sun.daylen}", t), "535 1317 660");   // tomorrow's 7:02
  CHECK_STR(resolve("{theme.color1}", t), "#ffaa20");
  CHECK_STR(resolve("{net.ip}", t), "10.0.0.2");
  CHECK_STR(resolve("{age.state}|{age.nope}", t), "30|");
  CHECK_STR(resolve("{msg.count}", t), "0");
  CHECK_STR(resolve("{nope.name}", t), "");
  int phase = resolve("{moon.phase}", t).toInt(), lit = resolve("{moon.lit}", t).toInt();
  CHECK(phase >= 0 && phase <= 7);
  CHECK(lit >= 0 && lit <= 100);
  cfg.hour12 = true;
  CHECK_STR(resolve("{time.hm} {time.ampm} {sun.set}", t), "9:05 AM 6:00");
  cfg.hour12 = false;
  struct tm ny = t; ny.tm_year = 127; ny.tm_mon = 0; ny.tm_mday = 1; ny.tm_wday = 5; ny.tm_yday = 0;
  CHECK_STR(resolve("{date.week}", ny), "53");   // 1 January 2027 is in 2026's week 53
  struct tm dec = t; dec.tm_mon = 11; dec.tm_mday = 28; dec.tm_wday = 1; dec.tm_yday = 361;
  CHECK_STR(resolve("{date.week}", dec), "53");

  // Modifiers, then the fallback.
  CHECK_STR(resolve("{state:u} {state:2} {n:+} {n:05} {neg:03} {pi:.2} {pi:.0}", t), "WARN wa +42 00042 -03 3.14 3");
  CHECK_STR(resolve("{date.wday:u:3}", t), "MON");
  CHECK_STR(resolve("{nope:u|none}", t), "none");
  CHECK_STR(resolve("{neg:+}", t), "-3");

  // Maps, from the face.
  customDoc.clear();
  deserializeJson(customDoc, R"({"maps":{"lbl":{"warn":"Careful","*":"?"},"num":{"42":7}},
    "palette":"{n}>40?day:night","palettes":{"day":{"fg":"#00ff00","ref":"@fg"},"night":{"fg":"#ff0000"}}})");
  CHECK_STR(resolve("{state:lbl} {nope:lbl} {n:num} {state:nomap}", t), "Careful ? 7 warn");

  // Expressions in braces; plain words in braces stay as they are.
  CHECK_STR(resolve("{{n}>40?big:small}", t), "big");
  CHECK_STR(resolve("Build {{state}==warn?FAILED:OK}!", t), "Build FAILED!");
  CHECK_STR(resolve("{{n}*2+1}", t), "85");
  CHECK_STR(resolve("{rand(1)}", t), "0");
  vars["until"] = "1791223740";   // Unix times: a float would be up to a minute out
  CHECK_STR(evalStr("1791223740-1791222630"), "1110");
  CHECK_STR(evalStr("1791222630+0"), "1791222630");
  CHECK_STR(evalStr("1791223740%60"), "0");
  CHECK_STR(resolve("{{until}-1791222630}", t), "1110");
  CHECK_STR(resolve("{a b}", t), "{a b}");
  CHECK_STR(resolve("{x", t), "{x");
  CHECK_NUM(evalNum("rand(5)") < 5, 1);

  // Palettes: "@name", picked by an expression.
  paletteName = evalStr(resolve(customDoc["palette"], t));
  CHECK_STR(paletteName, "day");
  CHECK(colorOf(item(R"({"c":"@fg"})")["c"], 0, t) == 0x07E0);
  CHECK(colorOf(item(R"({"c":"{n}>40?@fg:#000000"})")["c"], 0, t) == 0x07E0);
  CHECK(colorOf(item(R"({"c":"@nope"})")["c"], 5, t) == 5);
  CHECK(colorOf(item(R"({"c":"@ref"})")["c"], 5, t) == 5);   // an entry naming another is refused
  paletteName = "night";
  CHECK(colorOf(item(R"({"c":"@fg"})")["c"], 0, t) == 0xF800);

  // Repeat entries.
  CHECK(repeatCount(item(R"({"of":3})"), t) == 3);
  repeatAt(2);
  CHECK_STR(resolve("{it.index}/{it.count}", t), "2/3");
  CHECK(repeatCount(item(R"({"of":"days"})"), t) == 2);
  repeatAt(1);
  CHECK_STR(resolve("{it.wday3} {it.high}", t), "TUE 12");
  CHECK(repeatCount(item(R"({"of":"msgs"})"), t) == 0);
  CHECK(repeatCount(item(R"({"of":"500"})"), t) == 60);   // at most 60
  rep = Repeat();

  // "when" follows "show".
  CHECK(visibleField(item(R"({"w":"{sun.up}"})")["w"], t));
  CHECK(!visibleField(item(R"({"w":"{state}==ok"})")["w"], t));
  customDoc.clear();
  paletteName = "";
  fc = Forecast(); wx = Weather();
}

// Palettes and roles (docs/specifications/settings.md).
static String hexOf(uint16_t c) { return hex565Text(c); }
static void testColors() {
  struct tm t = now();
  cfg.palettes = DEFAULT_PALETTES; cfg.palette = "day"; cfg.nightPalette = "night"; cfg.roles = "{}"; cfg.colorsRev++;
  night = false;
  computeColors(t);
  CHECK_STR(hexOf(R[R_BG]), "#000000");
  CHECK_STR(hexOf(R[R_TIME]), "#ffaa20");   // accent1, #ffa826 in RGB565
  CHECK(R[R_HIGH] == R[R_TEXT]);   // @text
  CHECK(R[R_MUTED] == mix565(0, 0xFFFF, 0.6f));   // automatic
  CHECK_STR(resolve("{theme.time} {theme.color2} {theme.palette}", t), "#ffaa20 #6ab6ff day");
  {   // the default night palette has every colour of the day palette
    JsonDocument p;
    deserializeJson(p, DEFAULT_PALETTES);
    bool all = p["night"].size() == p["day"].size();
    for (JsonPairConst c : p["day"].as<JsonObjectConst>()) all = all && p["night"][c.key().c_str()].is<const char*>();
    CHECK(all);
  }
  CHECK_STR(resolve("[{nope:03|000}] [{n:03}]", t), "[000] [042]");
  // A night palette with one colour of its own; the rest inherited from day.
  cfg.palettes = "{\"day\":{\"accent1\":\"#ff0000\",\"black\":\"#000000\",\"white\":\"#ffffff\",\"blue\":\"#0000ff\"},\"night\":{\"accent1\":\"#00ff00\"}}";
  cfg.colorsRev++;
  computeColors(t);
  CHECK_STR(hexOf(R[R_TIME]), "#ff0000");
  night = true;
  CHECK(computeColors(t));   // changed: the face is redrawn
  CHECK_STR(hexOf(R[R_TIME]), "#00ff00");
  CHECK_STR(hexOf(R[R_BG]), "#000000");   // inherited
  night = false;
  // Bindings: a colour, another role, an expression, a loop, an unknown name.
  cfg.roles = "{\"bg\":\"#336699\",\"text\":\"@time\",\"date\":\"{now.temp}<0?@blue:#ffffff\",\"low\":\"@high\",\"high\":\"@low\",\"icon\":\"nope\"}";
  cfg.colorsRev++;
  computeColors(t);
  CHECK(R[R_BG] == hex565("#336699", 0));
  CHECK_STR(hexOf(R[R_TEXT]), "#ff0000");
  CHECK_STR(hexOf(R[R_ICON]), "#ff0000");   // unknown: the default, accent1
  CHECK(R[R_HIGH] == R[R_HIGH]);   // a loop ends without hanging
  wx.temp = -5; wx.valid = true;
  computeColors(t);
  CHECK_STR(hexOf(R[R_DATE]), "#0000ff");
  wx.temp = 21;
  computeColors(t);
  CHECK_STR(hexOf(R[R_DATE]), "#ffffff");
  // Custom faces: own palette, then roles, then the palette in use.
  customDoc.clear();
  deserializeJson(customDoc, "{\"palette\":{\"time\":\"#123456\"},\"items\":[]}");
  JsonDocument v;
  v["c"] = "@bg";
  CHECK(colorOf(v["c"], 1, t) == R[R_BG]);
  v["c"] = "@blue";
  CHECK_STR(hexOf(colorOf(v["c"], 1, t)), "#0000ff");
  v["c"] = "@time";
  CHECK(colorOf(v["c"], 1, t) == hex565("#123456", 0));   // the face's own palette first
  v["c"] = "@nothing";
  CHECK(colorOf(v["c"], 1, t) == 1);
  customDoc.clear();
  // The big font's text: symbols to codes, accents folded, lower case drawn as capitals.
  CHECK_STR(monoPrep("\u2191 7:02 \u2713 \u00e9t\u00e9 21\u00b0"), "\x01 7:02 \x05 ete 21*");
  CHECK(monoGlyph('a') == monoGlyph('A') && monoGlyph(':') && monoGlyph('\x09') && !monoGlyph('#'));
  cfg.palettes = DEFAULT_PALETTES; cfg.roles = "{}"; cfg.colorsRev++;
  computeColors(t);
}

int main() {
  testNumbers();
  testExpressions();
  testFields();
  testColours();
  testPaths();
  testMessageFace();
  testMonoFont();
  testAsciiFold();
  testFaceOrder();
  testPlaceholders();
  testColors();
  printf(failures ? "FAIL: %d of %d checks\n" : "PASS: %d checks\n", failures ? failures : checks, checks);
  return failures ? 1 : 0;
}

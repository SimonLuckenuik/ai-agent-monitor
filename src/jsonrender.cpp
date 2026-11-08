// json-render specs compiled into custom faces, on the clock (spec 047).
//
// A spec is a flat map of elements ({"root": id, "elements": {id: {type, props,
// children, visible}}}) drawn from a catalog. This file lays the tree out (a flex
// subset: direction, gap, padding, margin, grow, px and %, align, justify, wrap,
// absolute) and writes the result as a face in the clock's own format, so the existing
// renderer draws it: placeholders, expressions, flicker-free updates, the night tint.
// Live values ($state, $template, $cond, visible) become placeholders and expressions.
// Layout is fixed when the spec is saved: a hidden element leaves its space.
#include "jsonrender.h"
#if SMARTCLOCK_JSONRENDER
#include <ArduinoJson.h>
#include <TFT_eSPI.h>
#include <ESP8266WebServer.h>
#include "json_parse.h"
#include <LittleFS.h>

TFT_eSPI& clockDisplay();
float jrMonoWidth(const char* s);
String jrResolve(const char* tpl, const struct tm& t);
struct tm* clockLocal(time_t t, struct tm* out, const char** abbr);
float readNumber(const char*& s, bool* ok);
extern ESP8266WebServer server;

namespace {

// --- Catalog ----------------------------------------------------------------------------
enum Type : uint8_t {
  T_FRAME, T_BOX, T_ROW, T_COLUMN, T_STACK, T_CARD, T_GRID,   // containers
  T_HEADING, T_TEXT, T_CLOCK, T_BADGE, T_BUTTON, T_ALERT, T_PROGRESS, T_RING, T_SPARK,
  T_WICON, T_ICON, T_IMAGE, T_DIVIDER, T_SEPARATOR, T_SPACER, T_AVATAR, T_SKELETON,
  T_SPINNER, T_TABLE,
  // Ink's display components
  T_NEWLINE, T_PROGRESSBAR, T_BARCHART, T_LIST, T_LISTITEM, T_KEYVALUE, T_LINK, T_STATUSLINE,
  T_METRIC, T_CALLOUT, T_TIMELINE, T_MARKDOWN,
  T_UNKNOWN };
const char* const TYPE_NAMES[] = {
  "Frame", "Box", "Row", "Column", "Stack", "Card", "Grid",
  "Heading", "Text", "Clock", "Badge", "Button", "Alert", "Progress", "Ring", "Sparkline",
  "WeatherIcon", "Icon", "Image", "Divider", "Separator", "Spacer", "Avatar", "Skeleton",
  "Spinner", "Table",
  "Newline", "ProgressBar", "BarChart", "List", "ListItem", "KeyValue", "Link", "StatusLine",
  "Metric", "Callout", "Timeline", "Markdown" };
bool isContainer(uint8_t t) { return t <= T_GRID; }

struct Node {
  JsonObjectConst e, p;   // the element and its props
  const char* id;
  uint8_t type;
  int16_t first = -1, next = -1;
};

Node* nodes = nullptr;
int nodeCount = 0;
String warn, items, vis, bgS;   // warnings, the items so far, the visibility condition, the background under
int itemCount = 0;
float scale = 1;                // the Frame's size to the screen's
bool ink = false;               // a spec rooted at an Ink Box: lengths in character cells, Box is a row
uint8_t textV = 1;              // how a line of text sits in its box: 0 top, 1 middle, 2 on the bottom (its row's alignItems)
uint32_t bgHex = 0xffffff;      // the background colour as a number, when it is one (for rgba)
bool bgKnown = true;
bool overflowItems = false;

void warnOnce(const String& w) {
  if (warn.indexOf(w) >= 0 || warn.length() > 600) return;
  if (warn.length()) warn += '\n';
  warn += w;
}

// --- JSON text out ----------------------------------------------------------------------
void quote(String& o, const String& s) {
  o += '"';
  for (unsigned i = 0; i < s.length(); ++i) {
    char c = s[i];
    if (c == '"' || c == '\\') { o += '\\'; o += c; }
    else if ((uint8_t)c < 0x20) o += ' ';
    else o += c;
  }
  o += '"';
}
void appendJson(JsonVariantConst v, String& o) { String t; serializeJson(v, t); o += t; }   // serializeJson replaces a String
void itemStart(const char* type) {
  if (itemCount >= JR_ITEMS_MAX) { overflowItems = true; }
  if (itemCount++) items += ',';
  items += F("{\"type\":\""); items += type; items += '"';
}
void fNum(const char* k, long v) { items += ",\""; items += k; items += "\":"; items += v; }
void fStr(const char* k, const String& v) { items += ",\""; items += k; items += "\":"; quote(items, v); }
void itemEnd() { if (vis.length()) fStr("show", vis); items += '}'; }

// --- Values -----------------------------------------------------------------------------
const char* const CLOCK_GROUPS[] = {"time", "date", "now", "day0", "day1", "day2", "day3", "day4",
  "sun", "moon", "units", "loc", "theme", "msg", "net", "sys", "age"};

// A state path as a placeholder name: /vars/build and /build -> build; /now/temp -> now.temp;
// /days/2/high -> day2.high.
String placeholder(const char* path) {
  String p = path ? path : "";
  if (p.startsWith("/")) p.remove(0, 1);
  if (p.startsWith("vars/")) p.remove(0, 5);
  else if (p.startsWith("days/") && p.length() > 6) p = "day" + p.substring(5, 6) + p.substring(6);
  int slash = p.indexOf('/');
  String head = slash < 0 ? p : p.substring(0, slash);
  for (const char* g : CLOCK_GROUPS) if (head == g) { p.replace('/', '.'); return p; }
  if (slash >= 0) { warnOnce("state path /" + p + ": variables are one level (/vars/name)"); p = p.substring(slash + 1); p.replace("/", ""); }
  return p;
}

enum Kind { K_TEXT, K_NUM, K_COLOUR };
String value(JsonVariantConst v, Kind k);

String condition(JsonVariantConst c) {
  if (c.is<bool>()) return c.as<bool>() ? "1" : "0";
  if (c.is<JsonArrayConst>()) {
    String s;
    for (JsonVariantConst x : c.as<JsonArrayConst>()) { if (s.length()) s += "&&"; s += condition(x); }
    return s;
  }
  JsonObjectConst o = c.as<JsonObjectConst>();
  if (o.isNull()) return "1";
  for (const char* op : {"$and", "$or"}) {
    if (!o[op].is<JsonArrayConst>()) continue;
    String s;
    for (JsonVariantConst x : o[op].as<JsonArrayConst>()) { if (s.length()) s += op[2] == 'a' ? "&&" : "||"; s += "(" + condition(x) + ")"; }
    return s;
  }
  if (o["$item"] || o["$index"]) { warnOnce("$item/$index: repeat is not supported yet"); return "1"; }
  String v = "{" + placeholder(o["$state"]) + "}";
  bool no = o["not"] | false;
  if (!o["eq"].isNull()) return v + (no ? "!=" : "==") + o["eq"].as<String>();
  if (!o["neq"].isNull()) return v + (no ? "==" : "!=") + o["neq"].as<String>();
  if (!o["gt"].isNull()) return v + ">" + o["gt"].as<String>();
  if (!o["gte"].isNull()) return v + ">=" + o["gte"].as<String>();
  if (!o["lt"].isNull()) return v + "<" + o["lt"].as<String>();
  if (!o["lte"].isNull()) return v + "<=" + o["lte"].as<String>();
  return no ? "!" + v : v;
}

// CSS colours.
struct Named { const char* n; uint32_t c; };
const Named NAMED[] = {
  {"black", 0x000000}, {"white", 0xffffff}, {"red", 0xff0000}, {"green", 0x008000}, {"blue", 0x0000ff},
  {"yellow", 0xffff00}, {"orange", 0xffa500}, {"purple", 0x800080}, {"pink", 0xffc0cb}, {"gray", 0x808080},
  {"grey", 0x808080}, {"silver", 0xc0c0c0}, {"maroon", 0x800000}, {"navy", 0x000080}, {"teal", 0x008080},
  {"olive", 0x808000}, {"lime", 0x00ff00}, {"aqua", 0x00ffff}, {"cyan", 0x00ffff}, {"magenta", 0xff00ff},
  {"fuchsia", 0xff00ff}, {"brown", 0xa52a2a}, {"gold", 0xffd700}, {"indigo", 0x4b0082}, {"violet", 0xee82ee},
  {"coral", 0xff7f50}, {"salmon", 0xfa8072}, {"tomato", 0xff6347}, {"crimson", 0xdc143c},
  {"darkgray", 0xa9a9a9}, {"darkgrey", 0xa9a9a9}, {"lightgray", 0xd3d3d3}, {"lightgrey", 0xd3d3d3},
  {"gainsboro", 0xdcdcdc}, {"whitesmoke", 0xf5f5f5}, {"slategray", 0x708090}, {"skyblue", 0x87ceeb},
  {"steelblue", 0x4682b4}, {"royalblue", 0x4169e1}, {"dodgerblue", 0x1e90ff}, {"darkblue", 0x00008b},
  {"darkgreen", 0x006400}, {"limegreen", 0x32cd32}, {"seagreen", 0x2e8b57}, {"turquoise", 0x40e0d0},
  {"orangered", 0xff4500}, {"darkorange", 0xff8c00}, {"khaki", 0xf0e68c}, {"beige", 0xf5f5dc},
  {"ivory", 0xfffff0}, {"lavender", 0xe6e6fa}, {"plum", 0xdda0dd}, {"orchid", 0xda70d6}, {"tan", 0xd2b48c},
  {"chocolate", 0xd2691e}, {"firebrick", 0xb22222}, {"midnightblue", 0x191970}};

bool noPaint(JsonVariantConst v) {
  if (v.is<bool>()) return !v.as<bool>();
  const char* s = v.as<const char*>();
  return s && (!strcmp(s, "none") || !strcmp(s, "transparent"));
}
String hex(uint32_t c) { char b[8]; snprintf(b, sizeof b, "#%06x", (unsigned)(c & 0xffffff)); return b; }
uint32_t blend(uint32_t fg, uint32_t bg, float a) {
  uint32_t r = 0;
  for (int s = 0; s <= 16; s += 8) {
    int f = (fg >> s) & 255, b = (bg >> s) & 255;
    r |= (uint32_t)lroundf(f * a + b * (1 - a)) << s;
  }
  return r;
}
bool hexOf(const String& s, uint32_t* out) {   // "#rgb", "#rrggbb", "#rrggbbaa" -> number (alpha blended)
  if (!s.startsWith("#")) return false;
  String h = s.substring(1);
  for (unsigned i = 0; i < h.length(); ++i) if (!isxdigit((unsigned char)h[i])) return false;
  if (h.length() == 3 || h.length() == 4) { String x; for (unsigned i = 0; i < h.length(); ++i) { x += h[i]; x += h[i]; } h = x; }
  if (h.length() != 6 && h.length() != 8) return false;
  uint32_t c = strtoul(h.substring(0, 6).c_str(), nullptr, 16);
  if (h.length() == 8) c = blend(c, bgHex, strtoul(h.substring(6).c_str(), nullptr, 16) / 255.0f);
  *out = c;
  return true;
}
String colourLiteral(String s) {
  s.trim();
  String l = s; l.toLowerCase();
  uint32_t c;
  if (hexOf(l, &c)) return hex(c);
  if (l == "transparent") return bgS.length() ? bgS : hex(bgHex);
  if (l.startsWith("rgb")) {   // rgb(r, g, b) / rgba(r, g, b, a)
    const char* p = strchr(l.c_str(), '(');
    float v[4] = {0, 0, 0, 1};
    for (int i = 0; p && i < 4; ++i) {
      ++p;
      while (*p == ' ') ++p;
      bool ok;
      v[i] = readNumber(p, &ok);
      if (!ok) break;
      if (*p == '%') { v[i] = i < 3 ? v[i] * 2.55f : v[i] / 100; ++p; }
      while (*p == ' ') ++p;
      if (*p != ',' && *p != '/') break;
    }
    c = ((uint32_t)constrain((int)v[0], 0, 255) << 16) | ((uint32_t)constrain((int)v[1], 0, 255) << 8) | constrain((int)v[2], 0, 255);
    if (v[3] < 1) { if (!bgKnown) warnOnce("rgba() over a changing background: blended with " + hex(bgHex)); c = blend(c, bgHex, v[3]); }
    return hex(c);
  }
  if (ink) {   // the terminal's colours, in the clock's own shades
    bool bright = l.endsWith("bright");
    String b = bright ? l.substring(0, l.length() - 6) : l;
    static const Named ANSI[] = {{"black", 0x000000}, {"red", 0xff453a}, {"green", 0x30d158}, {"yellow", 0xffd60a},
      {"blue", 0x0a84ff}, {"magenta", 0xbf5af2}, {"cyan", 0x64d2ff}, {"white", 0xe5e5ea}, {"gray", 0x8e8e93}, {"grey", 0x8e8e93}};
    for (const Named& n : ANSI) if (b == n.n) return hex(bright ? blend(n.c, 0xffffff, 0.7f) : n.c);
  }
  for (const Named& n : NAMED) if (l == n.n) return hex(n.c);
  return s;   // "@role", a placeholder, or something the clock reads itself
}

String value(JsonVariantConst v, Kind k) {
  if (v.isNull()) return "";
  if (v.is<JsonObjectConst>()) {
    JsonObjectConst o = v.as<JsonObjectConst>();
    if (!o["$state"].isNull()) {
      String s = "{" + placeholder(o["$state"]) + (o["fallback"].isNull() ? "" : "|" + o["fallback"].as<String>()) + "}";
      return s;
    }
    if (!o["$template"].isNull()) {   // "Hi ${/user/name}" -> "Hi {user}"
      String t = o["$template"].as<String>(), r;
      int i = 0;
      while (true) {
        int a = t.indexOf("${", i);
        if (a < 0) { r += t.substring(i); break; }
        int b = t.indexOf('}', a);
        if (b < 0) { r += t.substring(i); break; }
        r += t.substring(i, a);
        String in = t.substring(a + 2, b), fb;
        int bar = in.indexOf('|');
        if (bar >= 0) { fb = in.substring(bar); in = in.substring(0, bar); }
        r += "{" + placeholder(in.c_str()) + fb + "}";
        i = b + 1;
      }
      return r;
    }
    if (o.containsKey("$cond")) {
      String e = condition(o["$cond"]) + "?" + value(o["$then"], k) + ":" + value(o["$else"], k);
      return k == K_TEXT ? "{" + e + "}" : e;
    }
    if (!o["$computed"].isNull()) { warnOnce("$computed is not supported: " + o["$computed"].as<String>()); return ""; }
    if (!o["$item"].isNull() || !o["$index"].isNull()) { warnOnce("$item/$index: repeat is not supported yet"); return ""; }
    if (!o["$bindState"].isNull()) return "{" + placeholder(o["$bindState"]) + "}";   // shown, never written
    warnOnce("unsupported dynamic value");
    return "";
  }
  String s = v.as<String>();
  return k == K_COLOUR ? colourLiteral(s) : s;
}
bool live(const String& s) { return s.indexOf('{') >= 0; }

// --- Measuring text ---------------------------------------------------------------------
struct Font { String name; int font = 2, size = 0, h = 16; float weight = 1; };

Font fontFor(float css, const char* family, bool bold) {
  Font f;
  float px = css * scale;
  String fam = family ? family : "";
  if (fam.length() && fam != "2" && fam != "4") {
    f.name = fam; f.font = 0; f.size = max(8, (int)lroundf(px * 0.72f)); f.h = (int)lroundf(f.size * 1.15f);
  } else if (fam == "2" || (fam.isEmpty() && px <= 19)) { f.font = 2; f.h = 16; }
  else if (fam == "4" || px <= 31) { f.font = 4; f.h = 26; }
  else { f.name = "big"; f.font = 0; f.size = max(8, (int)lroundf(px * 0.72f)); f.h = (int)lroundf(f.size * 1.15f); }
  if (bold && f.name == "big") f.weight = 1.7f;

  return f;
}
int widthOf(const String& s, const Font& f) {
  if (f.font) return clockDisplay().textWidth(s.c_str(), f.font);
  if (f.name == "big") return (int)lroundf((jrMonoWidth(s.c_str()) + 2.3f) * f.size / 20 * (f.weight > 1 ? 1.04f : 1));
  float k = f.name == "bebas" ? 0.47f : f.name == "pixel" ? 0.86f : 0.66f;
  return (int)lroundf(s.length() * f.size * k);
}
String sample(const String& tpl) {   // what a template shows now
  time_t now = time(nullptr);
  struct tm t;
  clockLocal(now, &t, nullptr);
  String s = jrResolve(tpl.c_str(), t);
  if (s.isEmpty() && live(tpl)) s = "0000";
  return s;
}
// Lines of text in width w (greedy, by words); fonts 2 and 4 only.
int lineCount(const String& s, const Font& f, int w) {
  if (!f.font || w <= 0 || widthOf(s, f) <= w) return 1;
  int lines = 1, start = 0, last = -1;
  for (int i = 0; i <= (int)s.length(); ++i) {
    if (i < (int)s.length() && s[i] != ' ') continue;
    if (widthOf(s.substring(start, i), f) > w && last > start) { lines++; start = last + 1; }
    last = i;
  }
  if (widthOf(s.substring(start), f) > w) lines++;
  return min(lines, 8);
}

// --- Props ------------------------------------------------------------------------------
float num(JsonObjectConst p, const char* k, float d) { JsonVariantConst v = p[k]; return v.is<float>() ? v.as<float>() : d; }
// Ink measures in character cells: a cell is 6 px across (40 columns fill the screen), a row 16 px.
constexpr int INK_COL = 6, INK_ROW = 16;
// A length from the spec (scaled, in cells for Ink); d, in pixels, when it is not given.
int px(JsonObjectConst p, const char* k, float d) {
  JsonVariantConst v = p[k];
  return v.is<float>() ? (int)lroundf(v.as<float>() * scale * (ink ? INK_COL : 1)) : (int)lroundf(d);
}
// A vertical length (Ink: rows of 16 px).
int pxRow(JsonObjectConst p, const char* k, float d) {
  JsonVariantConst v = p[k];
  return v.is<float>() ? (int)lroundf(v.as<float>() * scale * (ink ? INK_ROW : 1)) : (int)lroundf(d);
}
// A size: px, or "50%" of base; -1 when not given.
int size(JsonVariantConst v, int base, int cell = INK_COL) {
  if (v.is<float>()) return (int)lroundf(v.as<float>() * scale * (ink ? cell : 1));
  if (v.is<const char*>()) {
    String s = v.as<const char*>();
    if (s.endsWith("%")) return base * s.toInt() / 100;
    if (s.endsWith("px")) return (int)lroundf(s.toInt() * scale);
    if (s.toInt() > 0) return (int)lroundf(s.toInt() * scale);
  }
  return -1;
}
const char* str(JsonObjectConst p, const char* k, const char* d = "") { const char* s = p[k]; return s ? s : d; }
bool is(JsonObjectConst p, const char* k, const char* v) { const char* s = p[k]; return s && !strcmp(s, v); }

enum Align : uint8_t { A_START, A_CENTER, A_END, A_STRETCH, A_BETWEEN, A_AROUND };
uint8_t alignOf(const char* s, uint8_t d) {
  if (!s) return d;
  String a = s;
  if (a.endsWith("start") || a == "left" || a == "top") return A_START;
  if (a == "center") return A_CENTER;
  if (a.endsWith("end") || a == "right" || a == "bottom") return A_END;
  if (a == "stretch") return A_STRETCH;
  if (a.endsWith("between")) return A_BETWEEN;
  if (a.endsWith("around") || a.endsWith("evenly")) return A_AROUND;
  return d;
}

int stackGap(const char* g) {   // shadcn's sizes
  if (!g) return 8;
  String s = g;
  return s == "none" ? 0 : s == "sm" ? 4 : s == "md" ? 8 : s == "lg" ? 12 : s == "xl" ? 16 : 8;
}

struct Lay {
  bool row = false, wrap = false, abs = false;
  uint8_t align = A_STRETCH, justify = A_START, cols = 0;   // cols: Grid
  int16_t gap = 0, pad[4] = {0, 0, 0, 0}, mar[4] = {0, 0, 0, 0};   // top, right, bottom, left
  float grow = 0;
};
Lay layOf(const Node& n) {
  Lay l;
  JsonObjectConst p = n.p;
  int P = px(p, "padding", 0);
  for (int16_t& v : l.pad) v = P;
  if (!p["paddingTop"].isNull()) l.pad[0] = px(p, "paddingTop", 0);
  if (!p["paddingRight"].isNull()) l.pad[1] = px(p, "paddingRight", 0);
  if (!p["paddingBottom"].isNull()) l.pad[2] = px(p, "paddingBottom", 0);
  if (!p["paddingLeft"].isNull()) l.pad[3] = px(p, "paddingLeft", 0);
  int M = px(p, "margin", 0);
  for (int16_t& v : l.mar) v = M;
  if (!p["marginTop"].isNull()) l.mar[0] = px(p, "marginTop", 0);
  if (!p["marginBottom"].isNull()) l.mar[2] = px(p, "marginBottom", 0);
  l.grow = num(p, "flex", num(p, "grow", 0));
  l.gap = px(p, "gap", 0);
  l.abs = is(p, "position", "absolute");
  l.align = alignOf(p["alignItems"] | p["align"].as<const char*>(), A_STRETCH);
  l.justify = alignOf(p["justifyContent"] | p["justify"].as<const char*>(), A_START);
  switch (n.type) {
    case T_FRAME: l.row = is(p, "flexDirection", "row"); break;
    case T_BOX: {   // Ink's Box is a row by default, the image renderer's a column
      const char* d = p["flexDirection"];
      l.row = d ? !strncmp(d, "row", 3) : ink;
      if (ink) { l.wrap = is(p, "flexWrap", "wrap"); if (p["gap"].isNull()) l.gap = px(p, l.row ? "columnGap" : "rowGap", 0); }
      if (!p["flexGrow"].isNull()) l.grow = num(p, "flexGrow", 0);
      break;
    }
    case T_ROW: l.row = true; l.wrap = p["wrap"] | false; break;
    case T_COLUMN: break;
    case T_STACK: l.row = is(p, "direction", "horizontal"); l.gap = stackGap(p["gap"]); break;
    case T_CARD:
      if (p["padding"].isNull()) for (int16_t& v : l.pad) v = 10;
      else if (ink) for (int16_t& v : l.pad) v = max<int16_t>(v, 6);
      if (p["gap"].isNull()) l.gap = 6;
      break;
    case T_GRID: l.row = true; l.wrap = true; l.cols = max(1, (int)num(p, "columns", 2)); l.gap = p["gap"].is<const char*>() ? stackGap(p["gap"]) : px(p, "gap", 8); break;
    case T_DIVIDER:
      if (!ink) { l.mar[0] = px(p, "marginTop", 8); l.mar[2] = px(p, "marginBottom", 8); }
      else { l.mar[0] = l.mar[2] = 3; }
      break;
    case T_SPACER: if (ink && p.size() == 0) l.grow = 1; break;   // Ink's Spacer fills the line
    default: break;
  }
  return l;
}

// --- Leaves: size -----------------------------------------------------------------------
struct Sz { int w, h; };

bool fills(const Node& n) {
  switch (n.type) {
    case T_ALERT: case T_PROGRESS: case T_SPARK: case T_TABLE: case T_PROGRESSBAR: case T_BARCHART:
    case T_LIST: case T_LISTITEM: case T_KEYVALUE: case T_STATUSLINE: case T_METRIC: case T_CALLOUT:
    case T_TIMELINE: case T_MARKDOWN: case T_DIVIDER: case T_SEPARATOR: case T_SKELETON:
      return n.p["width"].isNull();
    default: return false;
  }
}
// Whether it gives up room when its line overflows.
bool shrinks(const Node& n, bool row) {
  switch (n.type) {
    case T_ICON: case T_WICON: case T_RING: case T_AVATAR: case T_SPINNER: case T_BADGE: case T_BUTTON: return false;
    default: return (row ? n.p["width"] : n.p["height"]).isNull() && !(n.type == T_SPACER && !row);
  }
}
// Its flex grow in a row: its own, or 1 for a leaf that fills its line.
float growIn(const Node& n, bool row) { float g = layOf(n).grow; return g > 0 ? g : row && fills(n) ? 1 : 0; }

String textOf(const Node& n) {
  JsonObjectConst p = n.p;
  if (n.type == T_CLOCK) {
    String f = str(p, "format", "hm");
    if (f == "hm") return "{time.hm}";
    if (f == "hms") return "{time.hm}:{time.sec}";
    if (f == "date") return "{date.text}";
    if (f == "wday") return "{date.wday}";
    if (f == "wday3") return "{date.wday3}";
    if (f == "dmy") return "{date.day} {date.month3} {date.year}";
    if (f == "ampm") return "{time.hm} {time.ampm}";
    return value(p["format"], K_TEXT);   // a template of its own ("{date.wday:u}")
  }
  return value(p["text"].isNull() ? p["label"] : p["text"], K_TEXT);
}
float cssSize(const Node& n) {
  JsonObjectConst p = n.p;
  if (n.type == T_HEADING) {
    String l = str(p, "level", "h2");
    if (ink) return l == "h1" ? 34 : l == "h2" ? 26 : l == "h3" ? 22 : 16;
    return l == "h1" ? 48 : l == "h3" ? 28 : l == "h4" ? 22 : 36;
  }
  if (n.type == T_CLOCK) return num(p, "fontSize", 64);
  if (n.type == T_TEXT) {
    String v = str(p, "variant", "");
    if (v == "lead") return num(p, "fontSize", 20);
    if (v == "caption" || v == "muted") return num(p, "fontSize", 14);
  }
  return num(p, "fontSize", 16);
}
Font textFont(const Node& n) {
  bool asked = is(n.p, "fontWeight", "bold") || (n.p["bold"] | false);
  bool bold = asked || (n.type == T_HEADING) || (n.type == T_CLOCK);
  if (asked && cssSize(n) * scale <= 31 && !n.p["font"].is<const char*>()) warnOnce("bold: only the large (line font) sizes have a bold");
  return fontFor(cssSize(n), n.p["font"].is<int>() ? (n.p["font"].as<int>() == 4 ? "4" : "2") : n.p["font"].as<const char*>(), bold);
}
float lineHeight(const Node& n) { return num(n.p, "lineHeight", n.type == T_HEADING || n.type == T_CLOCK ? 1.2f : 1.4f); }
int lineH(const Node& n, const Font& f) {
  float css = (f.font ? f.h : cssSize(n) * scale);
  return max(f.h, (int)lroundf(css * lineHeight(n) * (f.font ? 1.0f : 1.0f)) - (f.font ? (int)lroundf(f.h * (lineHeight(n) - 1) / 2) : 0));
}

int textW(const Node& n, const Font& f, const String& t) {
  int w = widthOf(sample(t), f);
  int mc = n.p["maxChars"] | 0;
  if (mc) w = max(w, widthOf(String("00000000000000000000000000000000").substring(0, min(mc, 32)), f));
  return w;
}

Sz leafSize(const Node& n, int avail) {
  JsonObjectConst p = n.p;
  switch (n.type) {
    case T_HEADING: case T_TEXT: case T_CLOCK: {
      Font f = textFont(n);
      String t = textOf(n);
      int w = textW(n, f, t), lh = lineH(n, f);
      int lines = live(t) ? 1 : lineCount(sample(t), f, avail);
      return {min(w, avail), lh * lines};
    }
    case T_BADGE: case T_BUTTON: {
      Font f = fontFor(n.type == T_BUTTON ? 16 : 13, nullptr, false);
      return {textW(n, f, textOf(n)) + 16, n.type == T_BUTTON ? 30 : 22};
    }
    case T_ALERT: {
      Font f = fontFor(14, nullptr, false);
      String m = value(p["message"], K_TEXT);
      int lines = m.length() ? (live(m) ? 1 : lineCount(sample(m), f, avail - 20)) : 0;
      return {avail, 12 + 18 + lines * 18 + (lines ? 2 : 0)};
    }
    case T_PROGRESS: return {size(p["width"], avail) > 0 ? size(p["width"], avail) : avail, px(p, "height", 8) + (p["label"].isNull() ? 0 : 20)};
    case T_RING: { int d = px(p, "size", 64); return {d, d}; }
    case T_WICON: case T_ICON: { int d = px(p, "size", n.type == T_ICON ? 24 : 48); return {d, d}; }
    case T_IMAGE: return {size(p["width"], avail) > 0 ? size(p["width"], avail) : 80, size(p["height"], 240, INK_ROW) > 0 ? size(p["height"], 240, INK_ROW) : 60};
    case T_DIVIDER: case T_SEPARATOR: {
      int t = px(p, "thickness", 1);
      if (is(p, "orientation", "vertical")) return {t, 0};
      return {avail, n.type == T_SEPARATOR ? t + 8 : p["title"].isNull() ? t : 16};
    }
    case T_SPACER: { int h = px(p, "height", 20); int w = px(p, "width", 0); return {w, h}; }
    case T_AVATAR: { String s = str(p, "size", "md"); int d = s == "sm" ? 24 : s == "lg" ? 48 : 36; return {d, d}; }
    case T_SKELETON: return {size(p["width"], avail) > 0 ? size(p["width"], avail) : avail, size(p["height"], 240, INK_ROW) > 0 ? size(p["height"], 240, INK_ROW) : 16};
    case T_SPINNER: { String s = str(p, "size", "md"); int d = s == "sm" ? 16 : s == "lg" ? 40 : 24; return {d + (p["label"].isNull() ? 0 : 60), d}; }
    case T_TABLE: {
      JsonArrayConst rows = p["rows"];
      return {avail, 20 * ((int)rows.size() + 1) + 2};
    }
    case T_NEWLINE: return {0, 16 * max(1, (int)num(p, "count", 1))};
    case T_PROGRESSBAR: return {size(p["width"], avail) > 0 ? min(avail, size(p["width"], avail)) : avail, 8 + (p["label"].isNull() ? 0 : 20)};
    case T_BARCHART: return {avail, 20 * (int)p["data"].as<JsonArrayConst>().size()};
    case T_LIST: { int sp = px(p, "spacing", 0); int k = p["items"].as<JsonArrayConst>().size(); return {avail, k * 18 + max(0, k - 1) * sp}; }
    case T_LISTITEM: return {avail, p["subtitle"].isNull() ? 22 : 38};
    case T_KEYVALUE: case T_LINK: case T_STATUSLINE: return {avail, 20};
    case T_METRIC: return {avail, 18 + 40 + (p["detail"].isNull() ? 0 : 18)};
    case T_CALLOUT: {
      Font f = fontFor(14, nullptr, false);
      String c = value(p["content"], K_TEXT);
      int lines = c.length() ? (live(c) ? 1 : lineCount(sample(c), f, avail - 14)) : 0;
      return {avail, 8 + (p["title"].isNull() ? 0 : 18) + lines * 18 + 4};
    }
    case T_TIMELINE: {
      int h = 0;
      for (JsonVariantConst it : p["items"].as<JsonArrayConst>()) h += 22 + (it["description"].isNull() ? 0 : 18);
      return {avail, h};
    }
    case T_MARKDOWN: {
      Font f = fontFor(14, nullptr, false);
      String t = value(p["text"], K_TEXT);
      int h = 0, a = 0;
      while (a <= (int)t.length()) {
        int b = t.indexOf('\n', a); if (b < 0) b = t.length();
        String line = t.substring(a, b); line.trim();
        if (line.startsWith("#")) h += 28;
        else if (line.length()) h += 18 * lineCount(line, f, avail - 10);
        else h += 8;
        a = b + 1;
      }
      return {avail, h};
    }
    case T_SPARK:
      if (p["data"].is<JsonArrayConst>()) return {size(p["width"], avail) > 0 ? min(avail, size(p["width"], avail)) : avail, 32 + (p["label"].isNull() ? 0 : 18)};
      return {size(p["width"], avail) > 0 ? size(p["width"], avail) : avail, size(p["height"], 240, INK_ROW) > 0 ? size(p["height"], 240, INK_ROW) : 40};
    default: return {min(avail, 100), 30};
  }
}

// --- Layout -----------------------------------------------------------------------------
int fixedW(const Node& n, int base) { return size(n.p["width"], base); }
int fixedH(const Node& n, int base) { return isContainer(n.type) || n.type == T_SPACER ? size(n.p["height"], base, INK_ROW) : -1; }
int headerH(const Node& n) {
  if (n.type != T_CARD) return 0;
  int h = 0;
  if (!n.p["title"].isNull()) h += 26;
  if (!n.p["description"].isNull()) h += 18;
  return h ? h + 4 : 0;
}

Sz measure(int i, int avail, int depth = 0);

// Sizes of the flow children along the main axis, given the container's content box.
__attribute__((noinline)) Sz measure(int i, int avail, int depth) {
  const Node& n = nodes[i];
  if (depth > JR_DEPTH_MAX) return {0, 0};
  Lay l = layOf(n);
  int fw = fixedW(n, avail);
  if (!isContainer(n.type)) {
    Sz s = leafSize(n, fw > 0 ? fw : avail);
    if (fw > 0) s.w = fw;
    return s;
  }
  int cw = (fw > 0 ? fw : avail) - l.pad[1] - l.pad[3];
  int mainSum = 0, cross = 0, count = 0, lineMain = 0, lineCross = 0, total = 0;
  for (int c = n.first; c >= 0; c = nodes[c].next) {
    Lay cl = layOf(nodes[c]);
    if (cl.abs) continue;
    int childAvail = cw - (l.row ? 0 : cl.mar[1] + cl.mar[3]);
    if (l.cols) childAvail = (cw - l.gap * (l.cols - 1)) / l.cols;
    Sz s = measure(c, childAvail, depth + 1);
    if (l.cols) s.w = childAvail;
    else if (l.row && fills(nodes[c])) s.w = 0;   // shares the row (see growIn)
    int m = l.row ? s.w + cl.mar[1] + cl.mar[3] : s.h + cl.mar[0] + cl.mar[2];
    int x = l.row ? s.h + cl.mar[0] + cl.mar[2] : s.w + cl.mar[1] + cl.mar[3];
    if (l.row && l.wrap && count && lineMain + l.gap + m > cw) {   // a new line
      total += lineCross + l.gap; mainSum = max(mainSum, lineMain); lineMain = 0; lineCross = 0; count = 0;
    }
    lineMain += (count ? l.gap : 0) + m;
    lineCross = max(lineCross, x);
    cross = max(cross, x);
    count++;
  }
  mainSum = max(mainSum, lineMain);
  total += lineCross;
  int w, h;
  if (l.row) { w = mainSum; h = l.wrap ? total : cross; }
  else { w = cross; h = mainSum; }
  w += l.pad[1] + l.pad[3];
  h += l.pad[0] + l.pad[2] + headerH(n);
  if (fw > 0) w = fw;
  int fh = fixedH(n, 240);
  if (fh > 0) h = fh;
  return {w, h};
}

void place(int i, int x, int y, int w, int h, int depth);

// --- Emitting ---------------------------------------------------------------------------
uint32_t luminanceOk(uint32_t c) { return ((c >> 16 & 255) * 3 + (c >> 8 & 255) * 6 + (c & 255)) / 10; }
String inkOn() { return bgKnown && luminanceOk(bgHex) < 128 ? "#ffffff" : "#000000"; }
String mutedOn() { return bgKnown && luminanceOk(bgHex) < 128 ? "#8e8e93" : "#6b7280"; }
String panelOn() { return bgKnown && luminanceOk(bgHex) < 128 ? "#2c2c2e" : "#e5e7eb"; }

void emitText(const String& t, const Font& f, const String& colour, int x, int y, int w, int h, uint8_t al, int lh, int lines, const Node* n) {
  itemStart("text");
  int ax = al == A_CENTER ? x + w / 2 : al == A_END ? x + w : x;
  const char* hor = al == A_CENTER ? "c" : al == A_END ? "r" : "l";
  if (lines > 1) {
    fNum("x", ax); fNum("y", y + max(0, (lh - f.h) / 2));
    fStr("align", String("t") + hor); fNum("w", w); fNum("lines", lines); fNum("lh", max(0, lh - f.h));
  } else {
    // In a row aligned to the end, texts share a baseline at the bottom of their boxes (fonts 2
    // and 4 have a descender below it: 3 and 6 px); aligned to the start, a top line.
    if (n && textV == 2) { fNum("x", ax); fNum("y", y + h + (f.font == 2 ? 3 : f.font == 4 ? 6 : 0)); fStr("align", String("b") + hor); }
    else if (n && textV == 0) { fNum("x", ax); fNum("y", y); fStr("align", String("t") + hor); }
    else { fNum("x", ax); fNum("y", y + h / 2); fStr("align", String("m") + hor); }
  }
  fStr("text", t);
  if (f.font) fNum("font", f.font);
  else { fStr("font", f.name); fNum("size", f.size); if (f.weight > 1) items += ",\"weight\":1.7"; }
  if (!f.font && widthOf(sample(t), f) > w) fNum("w", w);   // the line font shrinks to fit
  if (colour.length()) fStr("color", colour);
  // No "bg" on text: it is drawn over whatever is under it (a ring, a box, an icon). When
  // a live value changes, the renderer repaints the area around the old and new text with
  // the items under and over it, off screen (spec 016), so nothing smears and nothing
  // under the text is wiped by a background box.
  itemEnd();
  if (n && lines <= 1 && !live(t)) {
    const char* deco = n->p["textDecoration"];
    if (n->p["underline"] | false) deco = "underline";
    if (n->p["strikethrough"] | false) deco = "line-through";
    if (deco && strcmp(deco, "none")) {
      int tw = widthOf(t, f), lx = ax - (al == A_CENTER ? tw / 2 : al == A_END ? tw : 0);
      int ly = !strcmp(deco, "underline") ? y + h / 2 + f.h / 2 - 1 : y + h / 2;
      itemStart("line"); fNum("x", lx); fNum("y", ly); fNum("x2", lx + tw); fNum("y2", ly); fNum("w", 1);
      if (colour.length()) fStr("color", colour);
      itemEnd();
    }
  }
}

void emitRect(int x, int y, int w, int h, int r, const String& fill, const String& border, int bw) {
  itemStart("rect");
  fNum("x", x); fNum("y", y); fNum("w", w); fNum("h", h);
  if (r) fNum("r", min(r, min(w, h) / 2));
  if (fill.length()) fStr("color", fill); else items += ",\"fill\":false";
  if (border.length() && bw > 0) { fStr("border", border); fNum("bw", bw); }
  if (!fill.length() && border.length()) fStr("color", border);
  itemEnd();
}

__attribute__((noinline)) void leafInk(const Node& n, int x, int y, int w, int h) {
  JsonObjectConst p = n.p;
  String colour = value(p["color"], K_COLOUR);
  switch (n.type) {
    case T_NEWLINE: return;
    case T_PROGRESSBAR: {
      int by = y;
      if (!p["label"].isNull()) { emitText(value(p["label"], K_TEXT), fontFor(14, nullptr, false), mutedOn(), x, y, w, 18, A_START, 18, 1, nullptr); by += 20; }
      String v = value(p["progress"], K_NUM);
      itemStart("bar"); fNum("x", x); fNum("y", by); fNum("w", w); fNum("h", 8);
      fStr("value", p["progress"].is<float>() ? String((int)lroundf(p["progress"].as<float>() * 100)) : "(" + v + ")*100");
      fStr("color", colour.length() ? colour : String("#30d158")); fStr("track", panelOn()); itemEnd();
      return;
    }
    case T_BARCHART: {
      JsonArrayConst d = p["data"];
      float hi = 0, total = 0;
      for (JsonVariantConst r : d) { hi = max(hi, r["value"].as<float>()); total += r["value"].as<float>(); }
      if (hi <= 0) hi = 1;
      Font f = fontFor(14, nullptr, false);
      int lw = 0;
      for (JsonVariantConst r : d) lw = max(lw, widthOf(r["label"].as<String>(), f));
      lw = min(lw + 6, w / 3);
      bool showV = p["showValues"] | true, showP = p["showPercentage"] | false;
      int vw = showV || showP ? 40 : 0, row = 0;
      static const uint32_t PAL[] = {0x0a84ff, 0x30d158, 0xff9f0a, 0xbf5af2, 0x64d2ff, 0xff453a};
      for (JsonVariantConst r : d) {
        int ry = y + row * 20;
        emitText(r["label"].as<String>(), f, mutedOn(), x, ry, lw - 4, 20, A_START, 20, 1, nullptr);
        float v = r["value"].as<float>();
        int bw = max(1, (int)((w - lw - vw) * v / hi));
        String c = value(r["color"], K_COLOUR);
        emitRect(x + lw, ry + 5, bw, 10, 2, c.length() ? c : hex(PAL[row % 6]), "", 0);
        if (vw) {
          String t = showP && total > 0 ? String((int)lroundf(v * 100 / total)) + "%" : String((int)lroundf(v));
          emitText(t, f, inkOn(), x + lw + bw + 4, ry, vw, 20, A_START, 20, 1, nullptr);
        }
        row++;
      }
      return;
    }
    case T_LIST: {
      Font f = fontFor(14, nullptr, false);
      int sp = px(p, "spacing", 0), i = 0;
      bool ordered = p["ordered"] | false;
      for (JsonVariantConst it : p["items"].as<JsonArrayConst>()) {
        int ly = y + i * (18 + sp);
        if (ordered) emitText(String(i + 1) + ".", f, mutedOn(), x, ly, 18, 18, A_END, 18, 1, nullptr);
        else { itemStart("circle"); fNum("x", x + 8); fNum("y", ly + 9); fNum("r", 2); fStr("color", mutedOn()); itemEnd(); }
        emitText(value(it, K_TEXT), f, colour.length() ? colour : inkOn(), x + 22, ly, w - 22, 18, A_START, 18, 1, nullptr);
        i++;
      }
      return;
    }
    case T_LISTITEM: {
      Font f = fontFor(14, nullptr, false);
      int lx = x;
      if (!p["leading"].isNull()) { emitText(value(p["leading"], K_TEXT), f, mutedOn(), x, y, 18, 20, A_START, 20, 1, nullptr); lx += 22; }
      String tr = value(p["trailing"], K_TEXT);
      int tw = tr.length() ? min(w / 2, widthOf(sample(tr), f) + 4) : 0;
      emitText(value(p["title"], K_TEXT), f, colour.length() ? colour : inkOn(), lx, y, w - (lx - x) - tw, 20, A_START, 20, 1, nullptr);
      if (tw) emitText(tr, f, mutedOn(), x + w - tw, y, tw, 20, A_END, 20, 1, nullptr);
      if (!p["subtitle"].isNull()) emitText(value(p["subtitle"], K_TEXT), f, mutedOn(), lx, y + 18, w - (lx - x), 18, A_START, 18, 1, nullptr);
      return;
    }
    case T_KEYVALUE: {
      Font f = fontFor(14, nullptr, false);
      String lc = value(p["labelColor"], K_COLOUR);
      String label = value(p["label"], K_TEXT);
      String v;
      if (p["value"].is<JsonArrayConst>()) { for (JsonVariantConst q : p["value"].as<JsonArrayConst>()) { if (v.length()) v += ", "; v += q.as<String>(); } }
      else v = value(p["value"], K_TEXT);
      int lw = min(w / 2, widthOf(sample(label), f) + 8);
      emitText(label, f, lc.length() ? lc : mutedOn(), x, y, lw, h, A_START, h, 1, nullptr);
      emitText(v, f, colour.length() ? colour : inkOn(), x + lw, y, w - lw, h, A_END, h, 1, nullptr);
      return;
    }
    case T_LINK: {
      Font f = fontFor(14, nullptr, false);
      String t = value(p["label"].isNull() ? p["url"] : p["label"], K_TEXT);
      String c = colour.length() ? colour : String("#64d2ff");
      emitText(t, f, c, x, y, w, h, A_START, h, 1, nullptr);
      int tw = min(w, widthOf(sample(t), f));
      itemStart("line"); fNum("x", x); fNum("y", y + h / 2 + 7); fNum("x2", x + tw); fNum("y2", y + h / 2 + 7); fNum("w", 1); fStr("color", c); itemEnd();
      return;
    }
    case T_STATUSLINE: case T_CALLOUT: {
      String st = str(p, n.type == T_STATUSLINE ? "status" : "type", "info");
      uint32_t c = st == "success" || st == "tip" ? 0x30d158 : st == "warning" ? 0xff9f0a : st == "error" || st == "important" ? 0xff453a : 0x0a84ff;
      Font f = fontFor(14, nullptr, false);
      if (n.type == T_STATUSLINE) {
        static const char* const GLYPH[] = {"M20 6 9 17l-5-5", "M18 6 6 18M6 6l12 12", "M12 3 2 21h20zM12 10v4M12 17.5v.5", "M2 12a10 10 0 1 0 20 0a10 10 0 1 0 -20 0M12 11v6M12 7.5v.5"};
        int g = st == "success" ? 0 : st == "error" ? 1 : st == "warning" ? 2 : 3;
        itemStart("path"); fNum("x", x); fNum("y", y + (h - 16) / 2); fNum("size", 16); fStr("d", GLYPH[g]); fStr("stroke", hex(c)); items += ",\"sw\":2.5"; itemEnd();
        emitText(value(p["text"], K_TEXT), f, colour.length() ? colour : inkOn(), x + 22, y, w - 22, h, A_START, h, 1, nullptr);
        return;
      }
      emitRect(x, y, 3, h, 0, hex(c), "", 0);
      int ty = y + 4;
      if (!p["title"].isNull()) { emitText(value(p["title"], K_TEXT), f, hex(c), x + 10, ty, w - 12, 18, A_START, 18, 1, nullptr); ty += 18; }
      String m = value(p["content"], K_TEXT);
      if (m.length()) {
        int lines = live(m) ? 1 : lineCount(sample(m), f, w - 14);
        emitText(m, f, inkOn(), x + 10, ty, w - 14, 18 * lines, A_START, 18, lines, nullptr);
      }
      return;
    }
    case T_METRIC: {
      Font f = fontFor(14, nullptr, false);
      emitText(value(p["label"], K_TEXT), f, mutedOn(), x, y, w, 18, A_START, 18, 1, nullptr);
      String tr = str(p, "trend", "");
      int aw = tr == "up" || tr == "down" ? 30 : 0;
      Font big = fontFor(48, nullptr, true);
      emitText(value(p["value"], K_TEXT), big, colour.length() ? colour : inkOn(), x, y + 18, w - aw, 40, A_START, 40, 1, nullptr);
      if (aw) emitText(tr == "up" ? "↑" : "↓", fontFor(36, "big", true), tr == "up" ? "#30d158" : "#ff453a", x + w - aw, y + 18, aw, 40, A_END, 40, 1, nullptr);
      if (!p["detail"].isNull()) emitText(value(p["detail"], K_TEXT), f, mutedOn(), x, y + 58, w, 18, A_START, 18, 1, nullptr);
      return;
    }
    case T_TIMELINE: {
      Font f = fontFor(14, nullptr, false);
      JsonArrayConst its = p["items"];
      int ty = y, k = 0, count = its.size();
      for (JsonVariantConst it : its) {
        String st = it["status"] | "upcoming";
        String c = st == "completed" ? "#30d158" : st == "current" ? "#0a84ff" : mutedOn();
        int ih = 22 + (it["description"].isNull() ? 0 : 18);
        if (k < count - 1) emitRect(x + 6, ty + 14, 2, ih - 6, 0, panelOn(), "", 0);
        itemStart("circle"); fNum("x", x + 7); fNum("y", ty + 9); fNum("r", 5); fStr("color", c); if (bgS.length()) fStr("under", bgS); itemEnd();
        String date = value(it["date"], K_TEXT);
        int dw = date.length() ? min(w / 3, widthOf(date, f) + 4) : 0;
        emitText(value(it["title"], K_TEXT), f, st == "upcoming" ? mutedOn() : inkOn(), x + 20, ty, w - 20 - dw, 18, A_START, 18, 1, nullptr);
        if (dw) emitText(date, f, mutedOn(), x + w - dw, ty, dw, 18, A_END, 18, 1, nullptr);
        if (!it["description"].isNull()) emitText(value(it["description"], K_TEXT), f, mutedOn(), x + 20, ty + 18, w - 20, 18, A_START, 18, 1, nullptr);
        ty += ih; k++;
      }
      return;
    }
    case T_MARKDOWN: {   // plain text with headings and bullets; inline markup removed
      Font f = fontFor(14, nullptr, false);
      String t = value(p["text"], K_TEXT);
      t.replace("**", ""); t.replace("__", ""); t.replace("`", "");
      int ty = y, a = 0;
      while (a <= (int)t.length()) {
        int b = t.indexOf('\n', a); if (b < 0) b = t.length();
        String line = t.substring(a, b); line.trim();
        if (line.startsWith("#")) { while (line.startsWith("#")) line.remove(0, 1); line.trim(); emitText(line, fontFor(22, nullptr, false), inkOn(), x, ty, w, 28, A_START, 28, 1, nullptr); ty += 28; }
        else if (line.length()) {
          bool bullet = line.startsWith("- ") || line.startsWith("* ");
          if (bullet) { line.remove(0, 2); itemStart("circle"); fNum("x", x + 4); fNum("y", ty + 9); fNum("r", 2); fStr("color", mutedOn()); itemEnd(); }
          int lines = lineCount(line, f, w - 10);
          emitText(line, f, inkOn(), x + (bullet ? 12 : 0), ty, w - (bullet ? 12 : 0), 18 * lines, A_START, 18, lines, nullptr);
          ty += 18 * lines;
        } else ty += 8;
        a = b + 1;
      }
      return;
    }
    default: return;
  }
}

__attribute__((noinline)) void leaf(const Node& n, int x, int y, int w, int h) {
  JsonObjectConst p = n.p;
  String colour = value(p["color"], K_COLOUR);
  switch (n.type) {
    case T_HEADING: case T_TEXT: case T_CLOCK: {
      Font f = textFont(n);
      String t = textOf(n);
      int lh = lineH(n, f);
      int lines = live(t) ? 1 : lineCount(sample(t), f, w);
      if (!colour.length()) colour = is(p, "variant", "muted") || is(p, "variant", "caption") ? mutedOn() : inkOn();
      if (p["dimColor"] | false) { uint32_t c; if (hexOf(colour, &c)) colour = hex(blend(c, bgHex, 0.55f)); }
      String tbg = value(p["backgroundColor"], K_COLOUR);
      if (p["inverse"] | false) { tbg = colour; colour = bgS.length() ? bgS : hex(bgHex); }
      if (tbg.length()) {   // a highlight behind the text (Ink)
        int tw = min(w, textW(n, f, t) + 6), al = alignOf(p["align"], A_START);
        int bx = al == A_CENTER ? x + (w - tw) / 2 : al == A_END ? x + w - tw : x;
        emitRect(bx, y, tw, h, 2, tbg, "", 0);
        String save = bgS; bgS = tbg;
        emitText(t, f, colour, bx + 3, y, tw - 6, h, A_START, lh, 1, &n);
        bgS = save;
        return;
      }
      if (p["wrap"].is<const char*>() && strncmp(p["wrap"], "truncate", 8) == 0) lines = 1;
      if ((p["italic"] | false) || p["fontStyle"] == "italic") warnOnce("italic is not drawn");
      if (!p["letterSpacing"].isNull()) warnOnce("letterSpacing is not drawn");
      emitText(t, f, colour, x, y, w, h, alignOf(p["align"], A_START), lh, lines, &n);
      return;
    }
    case T_BADGE: case T_BUTTON: {
      String v = str(p, "variant", "default");
      String fill = value(p["background"], K_COLOUR), border;
      if (!fill.length()) {
        if (v == "destructive" || v == "danger" || v == "error") fill = "#ff453a";
        else if (v == "success") fill = "#30d158";
        else if (v == "warning") fill = "#ff9f0a";
        else if (v == "info") fill = "#0a84ff";
        else if (v == "primary") fill = "#0a84ff";
        else if (v == "outline") border = inkOn();
        else if (v == "secondary") fill = panelOn();
        else fill = n.type == T_BUTTON ? "#0a84ff" : inkOn();
      }
      uint32_t fh = 0;
      bool dark = hexOf(fill, &fh) && luminanceOk(fh) < 140;
      if (!colour.length()) colour = border.length() ? inkOn() : dark ? "#ffffff" : "#000000";
      emitRect(x, y, w, h, h / 2, border.length() ? "" : fill, border, 1);
      Font f = fontFor(n.type == T_BUTTON ? 16 : 13, nullptr, false);
      String saveBg = bgS; bgS = border.length() ? bgS : fill;
      emitText(textOf(n), f, colour, x, y, w, h, A_CENTER, h, 1, nullptr);
      bgS = saveBg;
      return;
    }
    case T_ALERT: {
      String t = str(p, "type", "info");
      uint32_t c = t == "success" ? 0x30d158 : t == "warning" ? 0xff9f0a : t == "error" ? 0xff453a : 0x0a84ff;
      String fill = hex(blend(c, bgHex, 0.18f));
      emitRect(x, y, w, h, 8, fill, hex(c), 1);
      String save = bgS; bgS = fill;
      Font f = fontFor(14, nullptr, false);
      emitText(value(p["title"], K_TEXT), f, hex(c), x + 10, y + 6, w - 20, 18, A_START, 18, 1, nullptr);
      String m = value(p["message"], K_TEXT);
      if (m.length()) {
        int lines = live(m) ? 1 : lineCount(sample(m), f, w - 20);
        emitText(m, f, luminanceOk(blend(c, bgHex, 0.18f)) < 128 ? "#ffffff" : "#000000", x + 10, y + 26, w - 20, 18 * lines, A_START, 18, lines, nullptr);
      }
      bgS = save;
      return;
    }
    case T_PROGRESS: {
      int bh = px(p, "height", 8), by = y;
      if (!p["label"].isNull()) {
        emitText(value(p["label"], K_TEXT), fontFor(14, nullptr, false), mutedOn(), x, y, w, 18, A_START, 18, 1, nullptr);
        by += 20;
      }
      itemStart("bar");
      fNum("x", x); fNum("y", by); fNum("w", w); fNum("h", bh);
      fStr("value", value(p["value"], K_NUM));
      if (!p["max"].isNull()) fStr("max", value(p["max"], K_NUM));
      fStr("color", colour.length() ? colour : String("#0a84ff"));
      String tr = value(p["track"], K_COLOUR);
      fStr("track", tr.length() ? tr : panelOn());
      itemEnd();
      return;
    }
    case T_RING: {
      int d = min(w, h);
      itemStart("ring");
      fNum("x", x + w / 2); fNum("y", y + h / 2); fNum("r", d / 2); fNum("w", px(p, "thickness", 6));
      fStr("value", value(p["value"], K_NUM));
      if (!p["max"].isNull()) fStr("max", value(p["max"], K_NUM));
      fStr("color", colour.length() ? colour : String("#0a84ff"));
      String tr = value(p["track"], K_COLOUR);
      fStr("track", tr.length() ? tr : panelOn());
      itemEnd();
      if (!p["label"].isNull()) emitText(value(p["label"], K_TEXT), fontFor(num(p, "labelSize", 16), p["labelFont"], true), inkOn(), x, y, w, h, A_CENTER, h, 1, nullptr);
      return;
    }
    case T_SPARK: {
      if (p["data"].is<JsonArrayConst>()) {   // Ink: the values themselves, drawn as a path
        int top = y;
        if (!p["label"].isNull()) { emitText(value(p["label"], K_TEXT), fontFor(14, nullptr, false), mutedOn(), x, y, w, 18, A_START, 18, 1, nullptr); top += 18; }
        JsonArrayConst d = p["data"];
        float lo = 1e9f, hi = -1e9f;
        for (JsonVariantConst v : d) { float f = v.as<float>(); lo = min(lo, f); hi = max(hi, f); }
        if (!p["min"].isNull()) lo = num(p, "min", lo);
        if (!p["max"].isNull()) hi = num(p, "max", hi);
        if (hi <= lo) hi = lo + 1;
        int k = d.size(), ph = y + h - top - 2;
        String path;
        for (int i = 0; i < k; ++i) {
          int px1 = k > 1 ? i * (w - 2) / (k - 1) + 1 : w / 2;
          int py1 = top - y + 1 + (int)lroundf((hi - d[i].as<float>()) * ph / (hi - lo));
          path += (i ? "L" : "M"); path += px1; path += ' '; path += py1;
        }
        itemStart("path");
        fNum("x", x); fNum("y", y); fNum("w", w); fNum("h", h);
        String vb = "0 0 "; vb += w; vb += ' '; vb += h; fStr("vb", vb);
        fStr("d", path); fStr("stroke", colour.length() ? colour : String("#64d2ff"));
        items += ",\"sw\":2"; itemEnd();
        return;
      }
      itemStart("spark");
      fNum("x", x); fNum("y", y); fNum("w", w); fNum("h", h);
      const char* st = p["state"] | p["var"].as<const char*>();
      fStr("var", placeholder(st));
      fStr("color", colour.length() ? colour : String("#0a84ff"));
      String fill = value(p["fill"], K_COLOUR);
      if (fill.length()) fStr("fill", fill);
      if (!p["min"].isNull()) fNum("min", (long)num(p, "min", 0));
      if (!p["max"].isNull()) fNum("max", (long)num(p, "max", 0));
      if (bgS.length()) fStr("bg", bgS);
      itemEnd();
      return;
    }
    case T_WICON: {
      itemStart("icon");
      fNum("x", x + w / 2); fNum("y", y + h / 2); fNum("size", min(w, h));
      String nm = value(p["name"], K_TEXT);
      fStr("icon", nm.length() ? nm : String("{now.icon}"));
      if (colour.length()) fStr("color", colour);
      if (bgS.length()) fStr("bg", bgS);
      itemEnd();
      return;
    }
    case T_ICON: {
      if (p["paths"].is<JsonArrayConst>()) {   // several paths, each with its own colours
        for (JsonVariantConst q : p["paths"].as<JsonArrayConst>()) {
          itemStart("path");
          fNum("x", x); fNum("y", y); fNum("size", min(w, h));
          if (q["d"].is<JsonArrayConst>()) { items += ",\"d\":"; appendJson(q["d"], items); } else fStr("d", q["d"] | "");
          if (!p["viewBox"].isNull()) fStr("vb", str(p, "viewBox"));
          String fill = noPaint(q["fill"]) ? String() : value(q["fill"], K_COLOUR), stroke = value(q["stroke"], K_COLOUR);
          if (!fill.length() && !stroke.length()) stroke = colour.length() ? colour : inkOn();
          if (fill.length()) fStr("fill", fill);
          if (stroke.length()) fStr("stroke", stroke);
          if (!q["strokeWidth"].isNull() || !p["strokeWidth"].isNull()) fNum("sw", (long)(q["strokeWidth"] | p["strokeWidth"] | 2));
          if (q["rule"].is<const char*>()) fStr("rule", q["rule"].as<const char*>());
          itemEnd();
        }
        return;
      }
      itemStart("path");
      fNum("x", x); fNum("y", y); fNum("size", min(w, h));
      if (p["d"].is<JsonArrayConst>()) { items += ",\"d\":"; appendJson(p["d"], items); }
      else fStr("d", str(p, "d"));
      if (!p["viewBox"].isNull()) fStr("vb", str(p, "viewBox"));
      String fill = noPaint(p["fill"]) ? String() : value(p["fill"], K_COLOUR), stroke = value(p["stroke"], K_COLOUR);
      if (!fill.length() && !stroke.length()) stroke = colour.length() ? colour : inkOn();
      if (fill.length()) fStr("fill", fill);
      if (stroke.length()) fStr("stroke", stroke);
      if (!p["strokeWidth"].isNull()) fNum("sw", (long)num(p, "strokeWidth", 2));
      itemEnd();
      return;
    }
    case T_IMAGE: {
      String src = str(p, "src");
      if (src.startsWith("http") || src.isEmpty()) {
        warnOnce("Image: the clock cannot fetch URLs; send the photo to the clock and give its file name");
        emitRect(x, y, w, h, px(p, "borderRadius", 0), panelOn(), "", 0);
        emitText(str(p, "alt", "image"), fontFor(14, nullptr, false), mutedOn(), x, y, w, h, A_CENTER, h, 1, nullptr);
        return;
      }
      itemStart("image"); fNum("x", x); fNum("y", y); fStr("src", src); itemEnd();
      return;
    }
    case T_DIVIDER: case T_SEPARATOR: {
      int t = max(1, px(p, "thickness", 1));
      String c = colour.length() ? colour : String(n.type == T_DIVIDER ? "#e5e7eb" : "") ;
      if (!c.length()) c = panelOn();
      if (is(p, "orientation", "vertical")) { emitRect(x, y, t, h, 0, c, "", 0); return; }
      if (p["dimColor"] | false) { uint32_t cc; if (hexOf(c, &cc)) c = hex(blend(cc, bgHex, 0.5f)); }
      if (!p["title"].isNull()) {
        Font f = fontFor(14, nullptr, false);
        String tt = value(p["title"], K_TEXT);
        int tw = widthOf(sample(tt), f) + 12, side = max(0, (w - tw) / 2);
        emitRect(x, y + (h - t) / 2, side, t, 0, c, "", 0);
        emitRect(x + w - side, y + (h - t) / 2, side, t, 0, c, "", 0);
        emitText(tt, f, mutedOn(), x + side, y, tw, h, A_CENTER, h, 1, nullptr);
        return;
      }
      emitRect(x, y + (h - t) / 2, w, t, 0, c, "", 0);
      return;
    }
    case T_SPACER: return;
    case T_AVATAR: {
      int d = min(w, h);
      String fill = value(p["background"], K_COLOUR);
      itemStart("circle"); fNum("x", x + d / 2); fNum("y", y + d / 2); fNum("r", d / 2);
      fStr("color", fill.length() ? fill : String("#5e5ce6")); if (bgS.length()) fStr("under", bgS); itemEnd();
      String nm = str(p, "name"), ini;
      for (unsigned k = 0; k < nm.length() && ini.length() < 2; ++k) if (k == 0 || nm[k - 1] == ' ') ini += (char)toupper(nm[k]);
      String save = bgS; bgS = "";
      emitText(ini, fontFor(d >= 40 ? 22 : 14, nullptr, false), "#ffffff", x, y, d, d, A_CENTER, d, 1, nullptr);
      bgS = save;
      return;
    }
    case T_SKELETON: emitRect(x, y, w, h, (p["rounded"] | false) ? h / 2 : 4, panelOn(), "", 0); return;
    case T_SPINNER: {
      String s = str(p, "size", "md");
      int d = s == "sm" ? 16 : s == "lg" ? 40 : 24;
      itemStart("ring"); fNum("x", x + d / 2); fNum("y", y + h / 2); fNum("r", d / 2); fNum("w", max(2, d / 8));
      fStr("value", "{time.sec}%4*25+25"); fStr("color", colour.length() ? colour : String("#0a84ff")); fStr("track", panelOn()); itemEnd();
      if (!p["label"].isNull()) emitText(value(p["label"], K_TEXT), fontFor(14, nullptr, false), mutedOn(), x + d + 6, y, w - d - 6, h, A_START, h, 1, nullptr);
      return;
    }
    case T_TABLE: {
      JsonArrayConst cols = p["columns"], rows = p["rows"];
      int nc = max(1, (int)cols.size()), cw = w / nc;
      Font f = fontFor(14, nullptr, false);
      int c = 0;
      bool objs = cols[0].is<JsonObjectConst>();
      String hc = value(p["headerColor"], K_COLOUR);
      for (JsonVariantConst h1 : cols) {
        emitText(objs ? h1["header"].as<String>() : h1.as<String>(), f, hc.length() ? hc : mutedOn(), x + c * cw + 2, y, cw - 4, 20, objs ? alignOf(h1["align"], A_START) : A_START, 20, 1, nullptr);
        c++;
      }
      emitRect(x, y + 20, w, 1, 0, panelOn(), "", 0);
      int r = 0;
      for (JsonVariantConst row : rows) {
        c = 0;
        for (int ci = 0; ci < nc; ++ci) {
          JsonVariantConst cell = objs ? row[cols[ci]["key"] | ""] : row[ci];
          if (!objs && cell.isNull()) break;
          String s = value(cell, K_TEXT);
          while (s.length() > 1 && !live(s) && widthOf(s, f) > cw - 4) s.remove(s.length() - 1);
          emitText(s, f, inkOn(), x + c * cw + 2, y + 22 + r * 20, cw - 4, 20, objs ? alignOf(cols[ci]["align"], A_START) : A_START, 20, 1, nullptr);
          c++;
        }
        r++;
      }
      return;
    }
    case T_NEWLINE: case T_PROGRESSBAR: case T_BARCHART: case T_LIST: case T_LISTITEM: case T_KEYVALUE:
    case T_LINK: case T_STATUSLINE: case T_CALLOUT: case T_METRIC: case T_TIMELINE: case T_MARKDOWN:
      leafInk(n, x, y, w, h);
      return;
    default: {
      warnOnce(String("unknown component ") + str(n.e, "type") + ": drawn as a box");
      emitRect(x, y, w, h, 4, "", "#8e8e93", 1);
      emitText(str(n.e, "type"), fontFor(14, nullptr, false), "#8e8e93", x, y, w, h, A_CENTER, h, 1, nullptr);
    }
  }
}

__attribute__((noinline)) void place(int i, int x, int y, int w, int h, int depth) {
  const Node& n = nodes[i];
  if (depth > JR_DEPTH_MAX) return;
  String saveVis = vis, saveBg = bgS;
  uint32_t saveHex = bgHex;
  bool saveKnown = bgKnown;
  if (!n.e["visible"].isNull()) {
    String c = condition(n.e["visible"]);
    vis = vis.length() ? "(" + vis + ")&&(" + c + ")" : c;
  }
  for (const char* k : {"on", "watch"}) if (!n.e[k].isNull()) warnOnce(String(k) + ": the clock has no input; ignored");
  if (!n.e["repeat"].isNull()) warnOnce("repeat is not supported yet: drawn once");
  if (!isContainer(n.type)) { leaf(n, x, y, w, h); vis = saveVis; return; }

  JsonObjectConst p = n.p;
  Lay l = layOf(n);
  // The box itself.
  String fill = value(p["backgroundColor"].isNull() ? p["background"] : p["backgroundColor"], K_COLOUR);
  String border = value(p["borderColor"].isNull() ? p["border"] : p["borderColor"], K_COLOUR);
  int bw = px(p, "borderWidth", border.length() ? 1 : 0), radius = px(p, "borderRadius", px(p, "radius", 0));
  if (n.type == T_CARD) {
    if (!fill.length()) fill = bgKnown && luminanceOk(bgHex) < 128 ? "#1c1c1e" : "#ffffff";
    if (!border.length()) { border = panelOn(); bw = max(bw, 1); }
    if (p["borderRadius"].isNull() && p["radius"].isNull()) radius = 10;
  }
  if (n.type != T_FRAME && (fill.length() || (border.length() && bw > 0))) emitRect(x, y, w, h, radius, fill, border, bw);
  if (fill.length()) {
    bgS = fill;
    uint32_t c;
    bgKnown = hexOf(fill, &c);
    if (bgKnown) bgHex = c;
  }
  if (!is(p, "overflow", "visible") && !p["overflow"].isNull()) warnOnce("overflow: hidden cannot clip on the clock");

  int ix = x + l.pad[3], iy = y + l.pad[0], iw = w - l.pad[1] - l.pad[3], ih = h - l.pad[0] - l.pad[2];
  if (n.type == T_CARD) {   // title and description
    if (!p["title"].isNull()) { emitText(value(p["title"], K_TEXT), fontFor(22, nullptr, false), inkOn(), ix, iy, iw, 26, A_START, 26, 1, nullptr); iy += 26; ih -= 26; }
    if (!p["description"].isNull()) { emitText(value(p["description"], K_TEXT), fontFor(14, nullptr, false), mutedOn(), ix, iy, iw, 18, A_START, 18, 1, nullptr); iy += 18; ih -= 18; }
    if (headerH(n)) { iy += 4; ih -= 4; }
  }

  // Flow children, in lines (one line unless wrapping).
  // On the heap: the chip has a 4 KB stack, and containers nest.
  int MAXK = 0;
  for (int c = n.first; c >= 0; c = nodes[c].next) MAXK++;
  int* kid = new (std::nothrow) int[3 * max(1, MAXK)];
  if (!kid) { warnOnce("not enough memory for a container"); vis = saveVis; bgS = saveBg; bgHex = saveHex; bgKnown = saveKnown; return; }
  int* mainS = kid + MAXK;
  int* crossS = kid + 2 * MAXK;
  int k = 0;
  for (int c = n.first; c >= 0 && k < MAXK; c = nodes[c].next) {
    Lay cl = layOf(nodes[c]);
    if (cl.abs) {   // absolutely placed, inside the padding box
      Sz s = measure(c, iw);
      int fwc = fixedW(nodes[c], iw), fhc = size(nodes[c].p["height"], ih, INK_ROW);
      if (fwc > 0) s.w = fwc;
      if (fhc > 0) s.h = fhc;
      JsonObjectConst cp = nodes[c].p;
      // Both sides given (and no size): it stretches between them, as in CSS.
      if (fwc <= 0 && !cp["left"].isNull() && !cp["right"].isNull()) s.w = w - px(cp, "left", 0) - px(cp, "right", 0);
      if (fhc <= 0 && !cp["top"].isNull() && !cp["bottom"].isNull()) s.h = h - pxRow(cp, "top", 0) - pxRow(cp, "bottom", 0);
      int cx = !cp["left"].isNull() ? x + px(cp, "left", 0) : !cp["right"].isNull() ? x + w - px(cp, "right", 0) - s.w : ix;
      int cy = !cp["top"].isNull() ? y + pxRow(cp, "top", 0) : !cp["bottom"].isNull() ? y + h - pxRow(cp, "bottom", 0) - s.h : iy;
      place(c, cx, cy, s.w, s.h, depth + 1);
      continue;
    }
    kid[k++] = c;
  }
  int start = 0, lineTop = l.row ? iy : ix;
  while (start < k) {
    // Measure this line's children.
    int end = start, used = 0;
    float grow = 0;
    int lineCross = 0;
    for (; end < k; ++end) {
      const Node& cn = nodes[kid[end]];
      Lay cl = layOf(cn);
      int availCross = l.row ? ih : iw - cl.mar[1] - cl.mar[3];
      int m, cr;
      if (l.row) {
        int avail = l.cols ? (iw - l.gap * (l.cols - 1)) / l.cols : iw;
        Sz s = measure(kid[end], avail);
        m = l.cols ? avail : fills(cn) ? 0 : s.w; cr = s.h;
        int fh2 = size(cn.p["height"], ih, INK_ROW); if (fh2 > 0) cr = fh2;
        m += cl.mar[1] + cl.mar[3];
        cr += cl.mar[0] + cl.mar[2];
      } else {
        Sz s = measure(kid[end], availCross);
        int fw2 = fixedW(cn, iw);
        cr = (fw2 > 0 ? fw2 : s.w) + cl.mar[1] + cl.mar[3];
        int fh2 = size(cn.p["height"], ih, INK_ROW);
        m = (fh2 > 0 ? fh2 : s.h) + cl.mar[0] + cl.mar[2];
      }
      if (l.wrap && end > start && used + l.gap + m > iw) break;
      used += (end > start ? l.gap : 0) + m;
      mainS[end] = m; crossS[end] = cr;
      grow += growIn(cn, l.row);
      lineCross = max(lineCross, cr);
      if (l.cols && end - start + 1 >= l.cols) { ++end; break; }
    }
    int count = end - start, room = (l.row ? iw : ih) - used;
    if (room < 0 && !l.wrap) {   // shrink, as CSS's flex-shrink: 1 (not icons, rings, fixed widths)
      int can = 0;
      for (int j = start; j < end; ++j) if (shrinks(nodes[kid[j]], l.row)) can += mainS[j];
      if (can > 0) {
        int take = min(-room, can / 2), left = take;
        for (int j = start; j < end; ++j) {
          if (!shrinks(nodes[kid[j]], l.row)) continue;
          int t = min(left, (int)((long)take * mainS[j] / can) + 1);
          mainS[j] -= t; left -= t;
        }
        room += take - left;
      }
      if (room < 0) warnOnce(String("a ") + TYPE_NAMES[n.type] + " overflows its box by " + (-room) + " px");
    }
    if (grow > 0 && room > 0) {
      for (int j = start; j < end; ++j) { float g = growIn(nodes[kid[j]], l.row); if (g > 0) mainS[j] += (int)(room * g / grow); }
      room = 0;
    }
    int lead = 0, gap = l.gap;
    if (room > 0) {
      if (l.justify == A_CENTER) lead = room / 2;
      else if (l.justify == A_END) lead = room;
      else if (l.justify == A_BETWEEN && count > 1) gap += room / (count - 1);
      else if (l.justify == A_AROUND) { gap += room / count; lead = room / count / 2; }
    }
    int lineSpan = l.row ? (l.wrap ? lineCross : ih) : iw;
    int pos = (l.row ? ix : iy) + lead;
    for (int j = start; j < end; ++j) {
      const Node& cn = nodes[kid[j]];
      Lay cl = layOf(cn);
      uint8_t al = alignOf(cn.p["alignSelf"], l.align);
      int crossIn = crossS[j], fixedCross = l.row ? size(cn.p["height"], ih, INK_ROW) : fixedW(cn, iw);
      bool stretch = al == A_STRETCH && fixedCross < 0 && !(cn.type == T_RING || cn.type == T_WICON || cn.type == T_ICON || cn.type == T_AVATAR || cn.type == T_BADGE || cn.type == T_BUTTON || cn.type == T_SPINNER || cn.type == T_IMAGE);
      if (stretch) crossIn = lineSpan;
      int off = al == A_CENTER ? (lineSpan - crossIn) / 2 : al == A_END ? lineSpan - crossIn : 0;
      int bx, by, bwid, bhgt;
      if (l.row) {
        bx = pos + cl.mar[3]; bwid = mainS[j] - cl.mar[1] - cl.mar[3];
        by = lineTop + off + cl.mar[0]; bhgt = crossIn - cl.mar[0] - cl.mar[2];
      } else {
        by = pos + cl.mar[0]; bhgt = mainS[j] - cl.mar[0] - cl.mar[2];
        bx = lineTop + off + cl.mar[3]; bwid = crossIn - cl.mar[1] - cl.mar[3];
      }
      uint8_t saveV = textV;
      textV = !l.row ? 1 : al == A_END ? 2 : al == A_START ? 0 : 1;
      place(kid[j], bx, by, max(0, bwid), max(0, bhgt), depth + 1);
      textV = saveV;
      pos += mainS[j] + gap;
    }
    lineTop += lineCross + l.gap;
    start = end;
    if (!l.wrap) break;
  }
  delete[] kid;
  vis = saveVis; bgS = saveBg; bgHex = saveHex; bgKnown = saveKnown;
}

int indexOf(JsonObjectConst elements, const char* id) {
  for (int i = 0; i < nodeCount; ++i) if (!strcmp(nodes[i].id, id)) return i;
  return -1;
}

}  // namespace

bool jrIsSpec(const String& body) {
  return body.indexOf("\"elements\"") >= 0 && body.indexOf("\"root\"") >= 0;
}

namespace {
String jrCompileHere(const char* path, String& out, String& warnings, String& seedOut);
// The arguments for the run on the stack of our own (it takes no arguments).
const char* thPath; String* thOut; String* thWarn; String* thSeed; String thErr;
}  // namespace
// Called by name from the stack switch's assembly: C linkage, not in the anonymous namespace.
extern "C" __attribute__((used)) void jrCompileThunked() { thErr = jrCompileHere(thPath, *thOut, *thWarn, *thSeed); }

// Layout recurses as deep as the spec nests (at most JR_DEPTH_MAX): it runs on a stack of
// its own taken from the heap while compiling, not on the 4 KB one. The core's stack thunk
// (BearSSL's) is 6.2 KB; the deepest sample used 3.7 KB, so ours is 4.5 KB: the same switch
// as the core's make_stack_thunk(), with our own stack. It is painted so the deepest use is
// known (jrStackUsed(): /status jrStack).
constexpr size_t JR_STACK = 4608;
uint32_t jrStackDeepest = 0;
uint32_t jrStackUsed() { return jrStackDeepest; }

#if SMARTCLOCK_EMULATOR
extern "C" bool emuStackThunk(void (*f)(), size_t size);
#else
extern "C" {
uint32_t* jr_stack_ptr = nullptr;   // the stack's lowest word (the canary)
uint32_t* jr_stack_top = nullptr;   // where it starts (it grows down)
uint32_t* jr_stack_save = nullptr;  // the caller's stack pointer while it runs
void jr_stack_smashed() { panic(); }   // overflowed: the heap below it is damaged
}
__asm__ (
  ".text\n.literal_position\n.literal .LC_JR_STACK_VALUE, 0xdeadbeef\n.text\n"
  ".global thunk_jrCompileThunked\n.type thunk_jrCompileThunked, @function\n.align 4\n"
  "thunk_jrCompileThunked:\n"
  "  addi a1, a1, -16\n  s32i a0, a1, 12\n  s32i a15, a1, 8\n"
  "  movi a15, jr_stack_save\n  s32i a1, a15, 0\n"
  "  movi a15, jr_stack_top\n  l32i.n a1, a15, 0\n"
  "  call0 jrCompileThunked\n"
  "  movi a15, jr_stack_ptr\n  l32i.n a15, a15, 0\n  l32i.n a15, a15, 0\n"
  "  l32r a0, .LC_JR_STACK_VALUE\n  beq a0, a15, .L1_JR_STACK\n  call0 jr_stack_smashed\n"
  ".L1_JR_STACK:\n"
  "  movi a15, jr_stack_save\n  l32i.n a1, a15, 0\n"
  "  l32i.n a15, a1, 8\n  l32i.n a0, a1, 12\n  addi a1, a1, 16\n  ret\n"
  ".size thunk_jrCompileThunked, . - thunk_jrCompileThunked\n");
extern "C" void thunk_jrCompileThunked();
#endif

String jrCompile(const char* path, String& out, String& warnings, String& seedOut) {
  thPath = path; thOut = &out; thWarn = &warnings; thSeed = &seedOut; thErr = "";
#if SMARTCLOCK_EMULATOR
  if (!emuStackThunk(jrCompileThunked, JR_STACK)) return F("Not enough memory");
#else
  constexpr size_t words = JR_STACK / 4;
  jr_stack_ptr = static_cast<uint32_t*>(malloc(JR_STACK));
  if (!jr_stack_ptr) return F("Not enough memory");
  for (size_t i = 0; i < words; ++i) jr_stack_ptr[i] = 0xdeadbeef;
  jr_stack_top = jr_stack_ptr + words - 1;
  thunk_jrCompileThunked();
  size_t unused = 0;
  while (unused < words && jr_stack_ptr[unused] == 0xdeadbeef) ++unused;
  jrStackDeepest = std::max<uint32_t>(jrStackDeepest, (words - unused) * 4);
  free(jr_stack_ptr);
  jr_stack_ptr = jr_stack_top = nullptr;
#endif
  String e = thErr; thErr = "";
  return e;
}

namespace {
// Parses the spec from its file: the request's copy is already freed (storeFace()).
String jrCompileHere(const char* path, String& out, String& warnings, String& seedOut) {
  File src = LittleFS.open(path, "r");
  if (!src) return F("Could not read the spec");
  if (src.size() > JR_SOURCE_MAX) { src.close(); return F("The spec is larger than 12 KB"); }
  JsonDocument doc;
  DeserializationError e = parseJson(doc, src);
  src.close();
  if (e == DeserializationError::NoMemory) return F("Not enough memory");
  if (e) return String(F("Not JSON: ")) + e.c_str();
  JsonObjectConst elements = doc["elements"];
  const char* root = doc["root"];
  if (elements.isNull() || !root) return F("A spec has \"root\" and \"elements\"");
  int count = elements.size();
  if (count > JR_ELEMENTS_MAX) return F("At most 96 elements");
  nodes = new (std::nothrow) Node[count];
  if (!nodes) return F("Not enough memory");
  nodeCount = 0;
  for (JsonPairConst kv : elements) {
    Node& n = nodes[nodeCount++];
    n.id = kv.key().c_str();
    n.e = kv.value().as<JsonObjectConst>();
    n.p = n.e["props"].as<JsonObjectConst>();
    const char* t = n.e["type"] | "";
    n.type = T_UNKNOWN;
    for (int k = 0; k < T_UNKNOWN; ++k) if (!strcmp(t, TYPE_NAMES[k])) n.type = k;
  }
  String err;
  warn = ""; items = ""; vis = ""; itemCount = 0; overflowItems = false;
  // Children lists (by id), checked for loops by a visit count.
  uint8_t* seen = new (std::nothrow) uint8_t[count]();
  int rootI = indexOf(elements, root);
  if (!seen) err = F("Not enough memory");
  else if (rootI < 0) err = String(F("root: no element ")) + root;
  if (err.isEmpty()) {
    int* stack = new (std::nothrow) int[count];
    int sp = 0;
    if (!stack) err = F("Not enough memory");
    else {
    if (stack) { stack[sp++] = rootI; seen[rootI] = 1; }
    while (stack && sp && err.isEmpty()) {
      int i = stack[--sp];
      int prev = -1;
      for (JsonVariantConst c : nodes[i].e["children"].as<JsonArrayConst>()) {
        int ci = indexOf(elements, c | "");
        if (ci < 0) { warnOnce(String("child ") + (c | "?") + " of " + nodes[i].id + " does not exist"); continue; }
        if (seen[ci]) { err = String(F("element ")) + nodes[ci].id + F(" is used twice or in a loop"); break; }
        seen[ci] = 1;
        if (prev < 0) nodes[i].first = ci; else nodes[prev].next = ci;
        prev = ci;
        if (sp < count) stack[sp++] = ci;
      }
      if (prev >= 0 && !isContainer(nodes[i].type)) warnOnce(String(TYPE_NAMES[min((int)nodes[i].type, (int)T_UNKNOWN - 1)]) + " has no children: they are not drawn");
    }
    delete[] stack;
    }
  }
  if (err.isEmpty()) {
    const Node& r = nodes[rootI];
    JsonObjectConst rp = r.p;
    bool frame = r.type == T_FRAME;
    float fw = frame ? num(rp, "width", 240) : 240, fh = frame ? num(rp, "height", 240) : 240;
    scale = 1;
    if (fw > 0 && fh > 0 && (fw != 240 || fh != 240)) { scale = min(240 / fw, 240 / fh); warnOnce(String("Frame ") + (int)fw + "x" + (int)fh + " scaled to the 240x240 screen"); }
    ink = !frame;   // an Ink spec: rooted at a Box (or any other component)
    String bg = value(rp["backgroundColor"].isNull() ? rp["background"] : rp["backgroundColor"], K_COLOUR);
    if (!bg.length()) bg = ink ? "#000000" : "#ffffff";   // a terminal is dark; the image renderer's frame white
    bgS = bg; bgKnown = hexOf(bg, &bgHex);
    if (!bgKnown) bgHex = 0;
    // The face's head first, then its items appended in place: the face is never copied
    // (it was, items into the whole face: twice its size at once).
    items = "{\"bg\":"; quote(items, bg);
    for (const char* k : {"when", "duration", "fps", "invert", "palette", "palettes", "maps"}) {
      if (!frame || rp[k].isNull()) continue;
      items += ",\""; items += k; items += "\":"; appendJson(rp[k], items);
    }
    items += ",\"items\":[";
    if (frame) place(rootI, 0, 0, (int)lroundf(fw * scale), (int)lroundf(fh * scale), 0);
    else {   // the root fills the screen, with a small margin unless it has padding
      int m = rp["padding"].isNull() ? 6 : 0;
      place(rootI, m, m, 240 - 2 * m, 240 - 2 * m, 0);
    }
    items += "]";
    // The spec's state seeds variables that are not set yet.
    JsonObjectConst st = doc["state"]["vars"].is<JsonObjectConst>() ? doc["state"]["vars"].as<JsonObjectConst>() : doc["state"].as<JsonObjectConst>();
    String seed;
    for (JsonPairConst kv : st) {
      if (kv.value().is<JsonObjectConst>() || kv.value().is<JsonArrayConst>()) continue;
      seed += seed.length() ? "," : "";
      quote(seed, kv.key().c_str()); seed += ':'; appendJson(kv.value(), seed);
    }
    seedOut = seed.length() ? "{" + seed + "}" : "";
    items += "}";
    out = std::move(items);
    items = String();
    if (overflowItems) err = String(F("The face needs ")) + itemCount + F(" items; at most 64");
    else if (out.length() > JR_FACE_MAX) err = String(F("The face is ")) + out.length() + F(" bytes; at most 8 KB");
  }
  delete[] seen;
  delete[] nodes; nodes = nullptr;
  warnings = warn;
  warn = ""; items = ""; vis = ""; bgS = "";
  return err;
}
}  // namespace

// Both are stored gzipped. Every browser, fetch(), requests and curl --compressed take
// that; a client that does not say so gets a hint rather than binary.
// The catalog and its prompt are files that come with the update, gzipped (/sys, spec 048:
// 7.7 KB that cost 15 KB of update room in the image).
void jrSendCatalog(bool prompt) {
  if (server.header(F("Accept-Encoding")).indexOf(F("gzip")) < 0) {
    server.send(406, PSTR("text/plain"), PSTR("Stored gzipped: send Accept-Encoding: gzip (curl --compressed)\n"));
    return;
  }
  File f = LittleFS.open(prompt ? F("/sys/prompt.md.gz") : F("/sys/catalog.json.gz"), "r");
  if (!f) {
    server.send(503, PSTR("text/plain"), PSTR("The catalog comes with the update: install the .update file\n"));
    return;
  }
  server.streamFile(f, prompt ? F("text/markdown; charset=utf-8") : F("application/json"));   // .gz: gzip
  f.close();
}

#else
bool jrIsSpec(const String&) { return false; }
String jrCompile(const char*, String&, String&, String&) { return F("json-render is not in this build"); }
uint32_t jrStackUsed() { return 0; }
void jrSendCatalog(bool) {}
#endif

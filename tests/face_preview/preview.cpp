// Renders every clock face from src/faces.cpp into raw RGB565 frames, and checks that
// incremental redraws (second by second, across minute changes and a weather update)
// end up pixel-identical to a full redraw. run_face_preview.py builds and runs this.
#include <Arduino.h>
#include <LittleFS.h>
#include <TFT_eSPI.h>
#include <filesystem>
#include <vector>
#include "config.h"
#include "faces.h"
#include "weather.h"

Config cfg;
Weather wx;
Forecast fc;
// Integration hooks used by faces.cpp (integrations.cpp is not built here).
static std::string facesDir;
static uint32_t facesVer = 1;
static std::vector<std::pair<std::string, std::string>> previewVars;
uint32_t customFacesVersion() { return facesVer; }
bool customFaceExists(const String& name) {
  FILE* f = fopen((facesDir + "/" + name.c_str() + ".json").c_str(), "rb");
  if (f) fclose(f);
  return f != nullptr;
}
String faceVar(const String& key) {
  for (auto& kv : previewVars) if (kv.first == key.c_str()) return String(kv.second);
  return String();
}
bool photoEnabled(const String&) { return true; }
// The agent message the Message face shows (integrations.cpp is not built here).
static std::string msgTitle, msgText, msgIcon, msgColor;
static int msgPct = -1;
static uint32_t msgVer = 1;
bool messageActive(int slot) { return slot == 0 && !msgIcon.empty(); }
int messageShowing() { return 0; }
uint32_t messageVersion() { return msgVer; }
String messageField(int, const String& key) {
  if (key == "title") return String(msgTitle);
  if (key == "text") return String(msgText);
  if (key == "icon") return String(msgIcon);
  if (key == "color") return String(msgColor);
  if (key == "pct") return String(msgPct);
  if (key == "ttl") return String(540);
  return String();
}
static void setMessage(const char* icon, const char* title, const char* text, int pct = -1, const char* color = "") {
  msgIcon = icon; msgTitle = title; msgText = text; msgPct = pct; msgColor = color; msgVer++;
}
// weather.cpp is not built here either: just enough words for the faces.
void wmoText(int, bool fr, String& main, String& desc) {
  main = fr ? "Nuageux" : "Clouds";
  desc = fr ? "partiellement nuageux" : "scattered clouds";
}
LittleFSClass LittleFS;
static uint32_t fakeMillis = 0;
uint32_t millis() { return fakeMillis; }
void delay(unsigned long ms) { fakeMillis += ms; }
TFT_eSPI& clockDisplay() { static TFT_eSPI panel; return panel; }

static std::string outDir;
static void save(const std::string& name) {
  FILE* f = fopen((outDir + "/" + name + ".raw").c_str(), "wb");
  fwrite(clockDisplay().fb, 2, TFT_eSPI::W * TFT_eSPI::H, f);
  fclose(f);
}

static struct tm at(int h, int m, int s) {   // Monday 5 October 2026
  struct tm t{};
  t.tm_year = 126; t.tm_mon = 9; t.tm_mday = 5; t.tm_wday = 1; t.tm_yday = 277;
  t.tm_hour = h; t.tm_min = m; t.tm_sec = s;
  return t;
}
static void tick(struct tm& t) {
  if (++t.tm_sec == 60) { t.tm_sec = 0; if (++t.tm_min == 60) { t.tm_min = 0; ++t.tm_hour; } }
  fakeMillis += 1000;
}

static void setWeather(const char* icon, const char* main, const char* desc, float temp) {
  wx.valid = true; wx.temp = temp; wx.feels = temp - 1.6f; wx.tempMin = temp - 6; wx.tempMax = temp + 3;
  wx.humidity = 64; wx.wind = 4.2f; wx.main = main; wx.desc = desc; wx.icon = icon; wx.cityName = "Sydney";
  wx.version++;
}
static void setForecast(std::vector<const char*> icons) {
  fc.valid = true; fc.count = (int)icons.size(); fc.version++;
  const float hi[] = {24, 27, 19, 22}, lo[] = {15, 17, 12, 13};
  const int pop[] = {60, 0, 20, 80};
  const float wind[] = {4.2f, 15.5f, 2.0f, 27.8f};   // the last: 100 km/h, the widest fact line
  const int hum[] = {64, 100, 45, 100}, rise[] = {361, 363, 364, 623}, set[] = {1076, 1074, 1073, 1256};
  for (int i = 0; i < fc.count; ++i) {
    fc.days[i].wday = (1 + i) % 7; fc.days[i].today = i == 0;
    fc.days[i].hi = hi[i]; fc.days[i].lo = lo[i]; fc.days[i].pop = pop[i]; fc.days[i].icon = icons[i];
    fc.days[i].feels = hi[i] - 2;
    fc.days[i].wind = wind[i]; fc.days[i].humidity = hum[i];
    fc.days[i].sunrise = rise[i]; fc.days[i].sunset = set[i];
  }
}

static void fresh(int face, const struct tm& t, const std::string& name) {
  faceInvalidate();
  faceDraw(face, t, true);
  save(name);
}

int main(int argc, char** argv) {
  outDir = argc > 1 ? argv[1] : ".";
  const char* names[] = {"classic", "weather", "photo", "date", "simple", "forecast", "", "bigtime", "bigweather", "agents",
                         "today", "tomorrow", "tomorrow1", "tomorrow2"};
  setWeather("03d", "Clouds", "scattered clouds", 21.4f);
  setForecast({"10d", "01d", "04d", "11d"});

  // 1. Every face at 9:54:26 pm.
  for (int f = 0; f < FACE_COUNT; ++f)
    if (f != FACE_FLIP) fresh(f, at(21, 54, 26), std::string("face_") + std::to_string(f) + "_" + names[f]);   // 6: retired

  // 2. Incremental vs full redraw.
  int failures = 0;
  for (int f = 0; f < FACE_COUNT; ++f) {
    if (f == FACE_PHOTO || f == FACE_FLIP) continue;   // the photo advances on a timer by design; 6 is retired
    struct tm t = at(21, 58, 50);
    faceInvalidate();
    faceDraw(f, t, true);
    for (int s = 0; s < 150; ++s) {
      tick(t);
      if (s == 40) setWeather("10d", "Rain", "light rain", 18.2f);
      if (s == 41) setForecast({"10d", "01d", "04d", "11d"});
      faceDraw(f, t, true);
    }
    std::vector<uint16_t> inc(clockDisplay().fb, clockDisplay().fb + TFT_eSPI::W * TFT_eSPI::H);
    save(std::string("incremental_") + names[f]);
    faceInvalidate();
    faceDraw(f, t, true);
    int diff = 0;
    for (int i = 0; i < TFT_eSPI::W * TFT_eSPI::H; ++i) diff += inc[i] != clockDisplay().fb[i];
    printf("incremental %-9s %5d pixels differ from a full redraw\n", names[f], diff);
    if (diff) { failures++; save(std::string("incremental_") + names[f] + "_expected"); }
    setWeather("03d", "Clouds", "scattered clouds", 21.4f);
  }

  // 3. Variants: other icons, units and formats.
  setWeather("01n", "Clear", "clear sky", 14.0f);
  fresh(FACE_WEATHER, at(21, 54, 26), "variant_weather_clear_night");
  setWeather("11d", "Thunderstorm", "thunderstorm with heavy rain", -3.0f);
  fresh(FACE_WEATHER, at(21, 54, 26), "variant_weather_storm_negative");
  setForecast({"02d", "09d", "13d", "50d"});
  fresh(FACE_FORECAST, at(21, 54, 26), "variant_forecast_icons");
  setForecast({"13d", "13d", "01d", "02d"});   // deep cold: the widest numbers
  fc.days[0].hi = -24; fc.days[0].lo = -43; fc.days[0].feels = -31; fc.days[0].pop = 90;
  fc.days[1].hi = -38; fc.days[1].lo = -46; fc.days[1].feels = -51; fc.days[1].pop = 100;
  fresh(FACE_DAY0, at(21, 54, 26), "variant_today_snow_cold");
  fresh(FACE_DAY0, at(5, 30, 0), "variant_today_before_sunrise");
  fresh(FACE_DAY0, at(13, 0, 0), "variant_today_afternoon");
  fresh(FACE_DAY0 + 3, at(13, 0, 0), "variant_day3_widest_facts");
  for (int i = 0; i < 2; ++i) {   // the worst case: -NN everywhere, 100 km/h wind, 100% humidity
    ForecastDay& d = fc.days[i];
    d.hi = -38; d.lo = -46; d.feels = -52; d.wind = 27.8f; d.humidity = 100; d.sunrise = 623; d.sunset = 1256;
  }
  wx.temp = -44; wx.feels = -58; wx.wind = 27.8f; wx.humidity = 100; wx.version++;
  fresh(FACE_DAY0, at(5, 30, 0), "variant_today_worst");
  fresh(FACE_DAY0 + 1, at(5, 30, 0), "variant_day1_worst");
  fresh(FACE_DAY0 + 1, at(21, 54, 26), "variant_tomorrow_snow_colder");
  cfg.hour12 = false; cfg.celsius = false;
  setWeather("02d", "Clouds", "few clouds", 21.4f);
  fresh(FACE_CLASSIC, at(9, 5, 7), "variant_classic_24h_fahrenheit");
  fresh(FACE_SIMPLE, at(9, 5, 7), "variant_simple_24h");
  cfg.hour12 = true; cfg.celsius = true;

  // 4. Custom faces (integrations): the settings page's example, and one using
  // variables, a bar and transparent text; then the incremental check for both.
  facesDir = outDir + "/faces";
  std::filesystem::create_directories(facesDir);
#ifdef _WIN32
  _putenv_s("FACES_DIR", facesDir.c_str());
#else
  setenv("FACES_DIR", facesDir.c_str(), 1);
#endif
  auto writeFace = [&](const char* name, const char* json) {
    FILE* f = fopen((facesDir + "/" + name + ".json").c_str(), "wb");
    fputs(json, f); fclose(f); facesVer++;
  };
  writeFace("example", R"({"bg":"#000000","items":[
    {"type":"text","x":120,"y":24,"text":"{dow} {day} {mon3}","font":2,"color":"#9aa0a6","align":"mc","bg":"#000000","pad":200},
    {"type":"text","x":120,"y":78,"text":"{time}","font":6,"color":"#ffa826","align":"mc","bg":"#000000","pad":220},
    {"type":"ring","x":60,"y":170,"r":44,"w":8,"value":"{hum}","max":100,"color":"#6ab7ff"},
    {"type":"text","x":60,"y":170,"text":"{hum}%","font":4,"color":"#ffffff","align":"mc","bg":"#000000","pad":60},
    {"type":"icon","x":180,"y":155,"size":56,"icon":"{icon}","bg":"#000000"},
    {"type":"text","x":180,"y":205,"text":"{temp} {unit}","font":4,"color":"#4cd9a0","align":"mc","bg":"#000000","pad":90}]})");
  writeFace("power", R"({"bg":"#101820","items":[
    {"type":"rect","x":10,"y":10,"w":220,"h":220,"r":18,"color":"#1c2733"},
    {"type":"text","x":120,"y":40,"text":"Solar now","font":4,"color":"#9aa0a6","align":"mc"},
    {"type":"text","x":120,"y":100,"text":"{solar}","font":6,"color":"#ffd60a","align":"mc","bg":"#1c2733","pad":200},
    {"type":"text","x":120,"y":140,"text":"kW of {cap} kW","font":2,"color":"#9aa0a6","align":"mc","bg":"#1c2733","pad":200},
    {"type":"bar","x":30,"y":170,"w":180,"h":14,"value":"{pct}","max":100,"color":"#30d158","track":"#2c3a48"},
    {"type":"line","x":30,"y":200,"x2":210,"y2":200,"w":2,"color":"#2c3a48"},
    {"type":"text","x":120,"y":215,"text":"{time}  {hh}:{mm}:{ss}","font":2,"color":"#ffffff","align":"mc","bg":"#1c2733","pad":200},
    {"type":"circle","x":36,"y":40,"r":6,"color":"#30d158"}]})");
  cfg.facesApi = true;
  previewVars = {{"solar", "3.4"}, {"cap", "6.6"}, {"pct", "52"}};
  setWeather("03d", "Clouds", "scattered clouds", 21.4f);
  for (const char* name : {"example", "power"}) {
    cfg.customFace = name;
    fresh(FACE_CUSTOM, at(21, 54, 26), std::string("custom_") + name);
    struct tm t = at(21, 58, 50);
    faceInvalidate(); faceDraw(FACE_CUSTOM, t, true);
    for (int s = 0; s < 150; ++s) {
      tick(t);
      if (s == 30) previewVars = {{"solar", "4.1"}, {"cap", "6.6"}, {"pct", "62"}};
      if (s == 40) setWeather("10d", "Rain", "light rain", 18.2f);
      faceDraw(FACE_CUSTOM, t, true);
    }
    std::vector<uint16_t> inc(clockDisplay().fb, clockDisplay().fb + TFT_eSPI::W * TFT_eSPI::H);
    faceInvalidate(); faceDraw(FACE_CUSTOM, t, true);
    int diff = 0;
    for (int i = 0; i < TFT_eSPI::W * TFT_eSPI::H; ++i) diff += inc[i] != clockDisplay().fb[i];
    printf("incremental custom:%-7s %5d pixels differ from a full redraw\n", name, diff);
    if (diff) failures++;
    setWeather("03d", "Clouds", "scattered clouds", 21.4f);
  }
  cfg.customFace = "missing";
  fresh(FACE_CUSTOM, at(21, 54, 26), "custom_missing");
  cfg.facesApi = false;

  // 5. The Message face: every built-in icon, then the incremental check while the
  // clock ticks and the message changes.
  for (const char* icon : {"info", "success", "warning", "error", "waiting", "running", "bell", "done"}) {
    setMessage(icon, icon, "Claude needs your approval to deploy faces-v2 to the clock.", !strcmp(icon, "running") ? 64 : -1);
    fresh(FACE_MESSAGE, at(21, 54, 26), std::string("message_") + icon);
  }
  {
    setMessage("running", "Deploying", "Uploading the firmware", 10);
    struct tm t = at(21, 58, 50);
    faceInvalidate(); faceDraw(FACE_MESSAGE, t, true);
    for (int s = 0; s < 150; ++s) {
      tick(t);
      if (s == 30) setMessage("running", "Deploying", "Uploading the firmware", 70);
      if (s == 90) setMessage("success", "Deployed", "faces-v2 is on the clock", -1, "#64d2ff");
      faceDraw(FACE_MESSAGE, t, true);
    }
    std::vector<uint16_t> inc(clockDisplay().fb, clockDisplay().fb + TFT_eSPI::W * TFT_eSPI::H);
    faceInvalidate(); faceDraw(FACE_MESSAGE, t, true);
    int diff = 0;
    for (int i = 0; i < TFT_eSPI::W * TFT_eSPI::H; ++i) diff += inc[i] != clockDisplay().fb[i];
    printf("incremental message       %5d pixels differ from a full redraw\n", diff);
    if (diff) failures++;
  }

  fresh(FACE_AGENTS, at(21, 54, 26), "agents_one_message");

  // 6. French.
  cfg.lang = "fr";
  setWeather("03d", "Clouds", "scattered clouds", 21.4f);
  fresh(FACE_CLASSIC, at(21, 54, 26), "french_classic");
  fresh(FACE_WEATHER, at(21, 54, 26), "french_weather");
  fresh(FACE_FORECAST, at(21, 54, 26), "french_forecast");
  fresh(FACE_DATE, at(9, 5, 7), "french_date");
  fresh(FACE_BIGWEATHER, at(9, 5, 7), "french_bigweather");
  fresh(FACE_DAY0 + 2, at(9, 5, 7), "french_tomorrow1");
  cfg.lang = "en";

  // 7. No data yet.
  wx = Weather(); fc = Forecast();
  fresh(FACE_CLASSIC, at(21, 54, 26), "empty_classic");
  fresh(FACE_WEATHER, at(21, 54, 26), "empty_weather_waiting");
  wx.error = "City not found";
  fresh(FACE_FORECAST, at(21, 54, 26), "empty_forecast_bad_city");
  wx.error = "";
  #ifdef _WIN32
  _putenv_s("PHOTO_DIR", "");
#else
  unsetenv("PHOTO_DIR");
#endif
  facePhotosChanged();
  fresh(FACE_PHOTO, at(21, 54, 26), "empty_photo");
  faceInvalidate(); faceDraw(FACE_CLASSIC, at(0, 0, 0), false); save("empty_setting_time");

  printf(failures ? "FAIL: %d faces leave pixels behind\n" : "PASS: incremental redraws match full redraws\n", failures);
  return failures ? 1 : 0;
}

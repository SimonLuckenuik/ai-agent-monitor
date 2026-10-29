#include "weather.h"
#include <algorithm>
#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <ArduinoJson.h>
#include "config.h"

Weather wx;
Forecast fc;

static String urlEncode(const String& s) {
  static const char hex[] = "0123456789ABCDEF";
  String out;
  for (unsigned i = 0; i < s.length(); ++i) {
    char c = s[i];
    if (isalnum((unsigned char)c) || c == '-' || c == '_' || c == '.' || c == '~') out += c;
    else { out += '%'; out += hex[(c >> 4) & 15]; out += hex[c & 15]; }
  }
  return out;
}

// GET a JSON document over plain HTTP. HTTP/1.0 keeps the body un-chunked so
// ArduinoJson can parse the raw stream; the filter keeps only the fields we use.
static bool httpJson(const String& url, JsonDocument& doc, JsonDocument* filter = nullptr) {
  if (WiFi.status() != WL_CONNECTED) { wx.error = F("Not connected"); return false; }
  WiFiClient client; HTTPClient http;
  http.useHTTP10(true);
  http.setTimeout(10000);   // api.open-meteo.com takes ~1.2 s from Australia
  if (!http.begin(client, url)) { wx.error = F("Bad request"); return false; }
  int code = http.GET();
  if (code != 200) {
    http.end();
    if (code == 404) wx.error = F("City not found");
    else if (code < 0) wx.error = String(F("Weather service unreachable (")) + code + ")";
    else wx.error = String(F("Weather service error ")) + code;
    return false;
  }
  DeserializationError e = filter ? deserializeJson(doc, http.getStream(), DeserializationOption::Filter(*filter))
                                  : deserializeJson(doc, http.getStream());
  http.end();
  if (e) { wx.error = F("Unreadable weather data"); return false; }
  return true;
}

// ---- Open-Meteo: no key; one request gives current conditions + 4 days ----------
// WMO weather code -> wording. Written as a switch with F() strings: on the ESP8266 a
// table of literals would sit in RAM. The icon name comes from weatherIconName().
void wmoText(int code, bool fr, String& main, String& desc) {
#define W(m, d, mf, df) do { main = fr ? F(mf) : F(m); desc = fr ? F(df) : F(d); } while (0)
  switch (code) {
    case 0:  W("Clear", "clear sky", "Degage", "ciel degage"); break;
    case 1:  W("Clear", "mainly clear", "Degage", "plutot degage"); break;
    case 2:  W("Clouds", "partly cloudy", "Nuageux", "partiellement nuageux"); break;
    case 45: W("Fog", "fog", "Brouillard", "brouillard"); break;
    case 48: W("Fog", "freezing fog", "Brouillard", "brouillard givrant"); break;
    case 51: W("Drizzle", "light drizzle", "Bruine", "bruine faible"); break;
    case 53: W("Drizzle", "drizzle", "Bruine", "bruine"); break;
    case 55: W("Drizzle", "heavy drizzle", "Bruine", "bruine forte"); break;
    case 56: case 57: W("Drizzle", "freezing drizzle", "Bruine", "bruine verglacante"); break;
    case 61: W("Rain", "light rain", "Pluie", "pluie faible"); break;
    case 63: W("Rain", "rain", "Pluie", "pluie"); break;
    case 65: W("Rain", "heavy rain", "Pluie", "pluie forte"); break;
    case 66: case 67: W("Rain", "freezing rain", "Pluie", "pluie verglacante"); break;
    case 71: W("Snow", "light snow", "Neige", "neige faible"); break;
    case 73: W("Snow", "snow", "Neige", "neige"); break;
    case 75: W("Snow", "heavy snow", "Neige", "neige forte"); break;
    case 77: W("Snow", "snow grains", "Neige", "grains de neige"); break;
    case 80: W("Rain", "light showers", "Pluie", "averses faibles"); break;
    case 81: W("Rain", "showers", "Pluie", "averses"); break;
    case 82: W("Rain", "heavy showers", "Pluie", "averses fortes"); break;
    case 85: W("Snow", "snow showers", "Neige", "averses de neige"); break;
    case 86: W("Snow", "heavy snow showers", "Neige", "fortes averses de neige"); break;
    case 95: W("Thunderstorm", "thunderstorm", "Orage", "orage"); break;
    case 96: case 99: W("Thunderstorm", "thunderstorm with hail", "Orage", "orage avec grele"); break;
    default: W("Clouds", "overcast", "Nuageux", "couvert"); break;
  }
#undef W
}

// "Sydney" or "Sydney, AU" (an ISO country code narrows the search).
static bool geocode() {
  String name = cfg.city, cc;
  name.trim();
  int comma = name.lastIndexOf(',');
  if (comma > 0) { cc = name.substring(comma + 1); cc.trim(); cc.toUpperCase(); name = name.substring(0, comma); name.trim(); }
  if (name.length() < 2) { wx.error = F("Set a city"); return false; }
  String url = "http://geocoding-api.open-meteo.com/v1/search?count=1&language=en&format=json&name=" + urlEncode(name);
  if (cc.length() == 2) url += "&countryCode=" + cc;
  JsonDocument filter;   // results also carry long postcode lists
  JsonObject r = filter["results"].add<JsonObject>();
  r["name"] = true; r["latitude"] = true; r["longitude"] = true; r["country_code"] = true; r["admin1"] = true;
  JsonDocument doc;
  if (!httpJson(url, doc, &filter)) return false;
  JsonObject hit = doc["results"][0];
  if (hit.isNull()) { wx.error = F("City not found"); return false; }
  cfg.lat = hit["latitude"] | NAN;
  cfg.lon = hit["longitude"] | NAN;
  String label = (const char*)(hit["name"] | "");
  const char* region = hit["admin1"] | "";
  if (*region && label != region) label += String(", ") + region;
  const char* country = hit["country_code"] | "";
  if (*country) label += String(", ") + country;
  cfg.geoName = label;
  cfg.geoFor = cfg.city;
  cfg.save();   // once per city change, not per fetch
  return !isnan(cfg.lat) && !isnan(cfg.lon);
}

static int weekdayOf(const char* d) {   // "2026-10-05" -> 0 = Sunday (Sakamoto)
  if (strlen(d) < 10) return 0;
  int y = atoi(d), m = atoi(d + 5), day = atoi(d + 8);
  static const uint8_t t[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
  if (m < 1 || m > 12) return 0;
  if (m < 3) y -= 1;
  return (y + y / 4 - y / 100 + y / 400 + t[m - 1] + day) % 7;
}

// "-33.8679" without String(float), which drags in dtostrf.
static String coord(float v) {
  long q = lroundf(v * 1e4f);
  String out = q < 0 ? "-" : "";
  q = labs(q);
  char frac[6]; snprintf(frac, sizeof(frac), "%04ld", q % 10000);
  return out + String(q / 10000) + "." + frac;
}

#if SMARTCLOCK_EMULATOR
String emuWeather();   // emulator.cpp: a fixed reply set on its port 8080, or ""
#endif

static bool fetchOpenMeteo() {
  JsonDocument doc;   // ~1 KB response
#if SMARTCLOCK_EMULATOR
  String fixedJson = emuWeather();   // tests: the same weather every run, no network
  bool fixed = fixedJson.length() && !deserializeJson(doc, fixedJson);
  if (!fixed) {
#endif
  if (cfg.geoFor != cfg.city || isnan(cfg.lat) || isnan(cfg.lon)) {
    if (!geocode()) return false;
  }
  String url = "http://api.open-meteo.com/v1/forecast?latitude=" + coord(cfg.lat) +
               "&longitude=" + coord(cfg.lon) +
               "&current=temperature_2m,apparent_temperature,relative_humidity_2m,wind_speed_10m,weather_code,is_day,pressure_msl,cloud_cover"
               "&daily=weather_code,temperature_2m_max,temperature_2m_min,precipitation_probability_max,apparent_temperature_max,sunrise,sunset,"
               "wind_speed_10m_max,relative_humidity_2m_mean"
               "&forecast_days=4&timezone=auto&wind_speed_unit=ms";
  if (!httpJson(url, doc)) return false;
#if SMARTCLOCK_EMULATOR
  }
#endif
  JsonObject cur = doc["current"];
  JsonObject daily = doc["daily"];
  if (cur.isNull() || daily.isNull()) { wx.error = F("Unreadable weather data"); return false; }

  int code = cur["weather_code"] | 3;
  bool day = (cur["is_day"] | 1) != 0;
  wx.temp = cur["temperature_2m"] | 0.0f;
  wx.feels = cur["apparent_temperature"] | wx.temp;
  wx.humidity = cur["relative_humidity_2m"] | 0;
  wx.wind = cur["wind_speed_10m"] | 0.0f;
  wx.pressure = lroundf(cur["pressure_msl"] | 0.0f);
  wx.clouds = cur["cloud_cover"] | 0;
  JsonArray dates = daily["time"], codes = daily["weather_code"], his = daily["temperature_2m_max"],
            los = daily["temperature_2m_min"], pops = daily["precipitation_probability_max"],
            feels = daily["apparent_temperature_max"], winds = daily["wind_speed_10m_max"],
            hums = daily["relative_humidity_2m_mean"], rises = daily["sunrise"], sets = daily["sunset"];
  auto minutes = [](const char* iso) -> int16_t {   // "2026-10-06T05:41"
    return iso && strlen(iso) >= 16 ? atoi(iso + 11) * 60 + atoi(iso + 14) : -1;
  };
  wx.sunrise = minutes(rises[0] | "");
  wx.sunset = minutes(sets[0] | "");
  wx.tempMax = his[0] | wx.temp;
  wx.tempMin = los[0] | wx.temp;
  wmoText(code, false, wx.main, wx.desc);
  wx.code = code;
  wx.icon = weatherIconName(code, day);
  int comma = cfg.geoName.indexOf(',');
  wx.cityName = comma > 0 ? cfg.geoName.substring(0, comma) : cfg.geoName;
#if SMARTCLOCK_EMULATOR
  if (fixed && doc["city"].is<const char*>()) wx.cityName = (const char*)doc["city"];
#endif
  wx.valid = true; wx.error = ""; wx.version++;
  wx.lastSuccess = millis();

  Forecast next;
  for (size_t i = 0; i < dates.size() && next.count < FORECAST_DAYS; ++i) {
    ForecastDay& d = next.days[next.count++];
    d.wday = weekdayOf(dates[i] | "");
    d.today = i == 0;
    d.hi = his[i] | 0.0f;
    d.lo = los[i] | 0.0f;
    d.feels = feels[i] | d.hi;
    d.pop = pops[i] | 0;
    d.wind = winds[i] | 0.0f;
    d.humidity = lroundf(hums[i] | 0.0f);
    d.sunrise = minutes(rises[i] | "");
    d.sunset = minutes(sets[i] | "");
    d.icon = weatherIconName(codes[i] | 3, true);
  }
  next.valid = next.count > 0;
  next.lastFetch = millis();
  next.version = fc.version + 1;
  fc = next;
  return true;
}

bool fetchWeather() {
  wx.lastFetch = millis();
  return fetchOpenMeteo();
}

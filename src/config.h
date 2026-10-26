#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>
#include <LittleFS.h>

// Persistent configuration. Mirrors the stock /config JSON keys so the existing
// web UI (and expectations) line up. Stored at /config.json in LittleFS.
struct Config {
  String ssid;
  String password;
  String city        = "Sydney";
  String ntp         = "pool.ntp.org";
  // Open-Meteo works from coordinates. The city is geocoded once and the result
  // cached here; geoFor records which city string it belongs to.
  float  lat = NAN, lon = NAN;
  String geoName, geoFor;
  int    timezoneMin = 600;          // minutes offset (stock stores minutes)
  // POSIX TZ rule, so daylight saving switches automatically. Takes precedence over
  // timezoneMin; empty means "fixed timezoneMin offset" (stock behaviour).
  String tz          = "AEST-10AEDT,M10.1.0,M4.1.0/3";   // Sydney/Melbourne/Canberra
  bool   celsius     = true;
  bool   hour12      = false;        // 24 h by default
  bool   mile        = false;
  String lang        = "en";         // the faces' language: "en" or "fr"
  String faceOrder   = "0123456789"; // the rotation's order: face ids as digits
  int    theme       = 0;            // 0=Classic 1=Weather 2=Photo 3=Date 4=Simple 5=Forecast (6 retired)
  int    themeInterval = 10;         // seconds; 0 = no rotation
  int    faceMask    = 0x3ff;        // bit n set = face n takes part in rotation
  int    brightness  = 60;           // 0..100
  bool   nightMode   = true;
  bool   nightAuto   = false;          // dim from sunset to sunrise instead of fixed hours
  int    nightBrightness = 15;       // 0..100
  int    nightFace   = -1;           // during night mode: this face only; -1 = the rotation
  bool   nightTint   = true;         // during night mode: faces drawn in nightColor
  uint16_t nightColor = 0x528A;      // RGB565: dark gray #525252
  int    startHour   = 23;           // night start
  int    stopHour    = 7;            // night end
  uint16_t color1 = 0xFD44;          // RGB565 accents: time and hands (amber #FFA826)
  uint16_t color2 = 0x6DBF;          // date and highlights (soft blue #6AB7FF)
  uint16_t color3 = 0x4ED4;          // weather (mint #4CD9A0)
  // Photos: seconds per photo, and names left out of the slideshow ("|a.jpg|b.jpg|").
  int    photoInterval = 60;
  bool   photoClock = true;          // the time pill on the Photo face
  String photoOff;
  String customFace;                 // the custom face shown when theme is FACE_CUSTOM
  // Integrations, all off by default (settings page, Integrations card).
  bool   facesApi = false;           // custom faces over HTTP (and MQTT when that is on)
  bool   haCompat = false;           // stock SD Pro photo API for geekmagic-hacs; no sign-in
  bool   mqttOn   = false;           // MQTT with Home Assistant discovery
  String mqttHost, mqttUser, mqttPass, mqttBase;   // mqttBase "" = the hostname
  int    mqttPort = 1883;
  String apiKey;                     // for scripts calling the faces API; never exported

  // Face ids (0-9, FACE_COUNT in faces.h) as digits: unknown and repeated ones dropped,
  // missing ones appended, so every face has a place and new faces join at the end.
  static String orderOf(const String& s) {
    String out;
    uint16_t seen = 0;
    for (unsigned i = 0; i < s.length(); ++i) {
      char c = s[i];
      if (c >= '0' && c <= '9' && !(seen >> (c - '0') & 1)) { seen |= 1 << (c - '0'); out += c; }
    }
    for (char c = '0'; c <= '9'; ++c) if (!(seen >> (c - '0') & 1)) out += c;
    return out;
  }

  bool load() {
    if (!LittleFS.exists("/config.json")) return false;
    File f = LittleFS.open("/config.json", "r");
    if (!f) return false;
    JsonDocument doc;
    if (deserializeJson(doc, f)) { f.close(); return false; }
    f.close();
    ssid        = doc["ssid"]        | ssid;
    password    = doc["password"]    | password;
    city        = doc["city"]        | city;
    ntp         = doc["ntp"]         | ntp;
    if (doc["lat5"].is<long>() && doc["lon5"].is<long>()) {   // 1e-5 degrees
      lat = (doc["lat5"].as<long>()) / 1e5f;
      lon = (doc["lon5"].as<long>()) / 1e5f;
    }
    geoName     = doc["geoname"]     | geoName;
    geoFor      = doc["geofor"]      | geoFor;
    timezoneMin = doc["timezone"]    | timezoneMin;
    tz          = doc["tz"]          | tz;
    celsius     = doc["celsius"]     | celsius;
    hour12      = doc["hour12"]      | hour12;
    mile        = doc["mile"]        | mile;
    lang        = doc["lang"]        | lang;
    faceOrder   = orderOf(doc["faceorder"] | faceOrder);
    theme       = doc["theme"]       | theme;
    themeInterval = doc["themeInterval"] | themeInterval;
    faceMask    = doc["faces"]       | faceMask;
    brightness  = doc["brightness"]  | brightness;
    nightMode   = doc["nightmode"]   | nightMode;
    nightAuto   = doc["nightauto"]   | nightAuto;
    nightBrightness = doc["nightbrightness"] | nightBrightness;
    nightFace   = doc["nightface"]   | nightFace;
    nightTint   = doc["nighttint"]   | nightTint;
    nightColor  = doc["nightcolor"]  | nightColor;
    startHour   = doc["starttime"]   | startHour;
    stopHour    = doc["stoptime"]    | stopHour;
    color1      = doc["color1"]      | color1;
    color2      = doc["color2"]      | color2;
    color3      = doc["color3"]      | color3;
    photoInterval = doc["photointerval"] | photoInterval;
    photoClock  = doc["photoclock"]  | photoClock;
    photoOff    = doc["photooff"]    | photoOff;
    customFace  = doc["customface"]  | customFace;
    // Face ids moved: 1.3.x had custom faces at 7 (facesv 1); 1.4 development builds
    // had the Agents board at 7 and custom faces at 8 (facesv 2). The new faces join
    // the rotation.
    int facesv = doc["facesv"] | 1;
    if (facesv < 3) {
      if (facesv < 2 && theme == 7) theme = 10;
      else if (facesv == 2 && theme == 7) theme = 9;
      else if (facesv == 2 && theme == 8) theme = 10;
      faceMask = (faceMask & 0x7f) | 0x380;
    }
    facesApi    = doc["facesapi"]    | facesApi;
    haCompat    = doc["hacompat"]    | haCompat;
    mqttOn      = doc["mqtt"]        | mqttOn;
    mqttHost    = doc["mqtthost"]    | mqttHost;
    mqttPort    = doc["mqttport"]    | mqttPort;
    mqttUser    = doc["mqttuser"]    | mqttUser;
    mqttPass    = doc["mqttpass"]    | mqttPass;
    mqttBase    = doc["mqttbase"]    | mqttBase;
    apiKey      = doc["apikey"]      | apiKey;
    return true;
  }

  bool save() {
    JsonDocument doc;
    doc["ssid"] = ssid;           doc["password"] = password;
    doc["city"] = city;           doc["ntp"] = ntp;
    doc["timezone"] = timezoneMin;
    // Integers, not floats: saves pulling a float writer into the file path.
    if (!isnan(lat) && !isnan(lon)) { doc["lat5"] = lroundf(lat * 1e5f); doc["lon5"] = lroundf(lon * 1e5f); }
    doc["geoname"] = geoName; doc["geofor"] = geoFor;
    doc["tz"] = tz;
    doc["celsius"] = celsius;     doc["hour12"] = hour12;   doc["mile"] = mile;   doc["lang"] = lang;
    doc["faceorder"] = faceOrder;
    doc["theme"] = theme;         doc["themeInterval"] = themeInterval;
    doc["faces"] = faceMask;     doc["facesv"] = 3;
    doc["brightness"] = brightness;
    doc["nightmode"] = nightMode; doc["nightauto"] = nightAuto; doc["nightbrightness"] = nightBrightness; doc["nightface"] = nightFace;
    doc["nighttint"] = nightTint; doc["nightcolor"] = nightColor;
    doc["starttime"] = startHour; doc["stoptime"] = stopHour;
    doc["color1"] = color1;       doc["color2"] = color2;   doc["color3"] = color3;
    doc["photointerval"] = photoInterval; doc["photoclock"] = photoClock; doc["photooff"] = photoOff; doc["customface"] = customFace;
    doc["facesapi"] = facesApi;   doc["hacompat"] = haCompat; doc["mqtt"] = mqttOn;
    doc["mqtthost"] = mqttHost;   doc["mqttport"] = mqttPort; doc["mqttuser"] = mqttUser;
    doc["mqttpass"] = mqttPass;   doc["mqttbase"] = mqttBase; doc["apikey"] = apiKey;
    File f = LittleFS.open("/config.json", "w");
    if (!f) return false;
    serializeJson(doc, f);
    f.close();
    return true;
  }
};

extern Config cfg;   // defined in main.cpp

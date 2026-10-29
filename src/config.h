#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>
#include <LittleFS.h>
#include "faces.h"

// The default palettes: "night" has the day's colour names, dimmer, for a dark room.
#define DEFAULT_PALETTES "{\"day\":{\"accent1\":\"#ffa826\",\"accent2\":\"#6ab7ff\",\"accent3\":\"#4cd9a0\"," \
  "\"black\":\"#000000\",\"white\":\"#ffffff\",\"rain\":\"#5ab2ff\",\"green\":\"#30d158\",\"orange\":\"#ff9f0a\"," \
  "\"red\":\"#ff453a\",\"blue\":\"#0a84ff\"},\"night\":{\"accent1\":\"#a86a14\",\"accent2\":\"#3a6694\"," \
  "\"accent3\":\"#2a7a5c\",\"black\":\"#000000\",\"white\":\"#8e8e93\",\"rain\":\"#2f6496\",\"green\":\"#1e7a35\"," \
  "\"orange\":\"#a85f0a\",\"red\":\"#a3271f\",\"blue\":\"#0a4f99\"}}"

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
  int    theme      = 0;            // 0=Classic 1=Weather 2=Photo 3=Date 4=Simple 5=Forecast (6 retired)
  int    themeInterval = 10;         // seconds; 0 = no rotation
  int    faceMask    = (1 << FACE_COUNT) - 1;   // bit n set = face n takes part in rotation
  int    brightness  = 60;           // 0..100
  int    orientation = 0;            // the screen turned by 0-3 quarter turns (clockwise), in hardware
  bool   nightMode   = true;
  bool   nightAuto   = false;          // dim from sunset to sunrise instead of fixed hours
  int    nightBrightness = 15;       // 0..100
  // Face entries (docs/specifications/settings.md): "#<id>" a built-in face, else a
  // custom face's name.
  String order;                      // the rotation: comma list of entries; missing faces follow
  String customOff;                  // custom faces out of the rotation: comma list of names
  String turns;                      // own times on screen: "#7=20,calendar=45"
  String nightFace;                  // during night mode: this entry only; "" = the rotation
  bool   nightTint   = true;         // during night mode: faces drawn in nightColor
  uint16_t nightColor = 0x528A;      // RGB565: dark gray #525252
  int    startHour   = 23;           // night start
  int    stopHour    = 7;            // night end
  // Colours: palettes of named colours (JSON), the day and night palettes' names, and
  // the roles' bindings that differ from the defaults (JSON). colorsRev counts changes.
  String palettes    = DEFAULT_PALETTES;
  String palette     = "day";
  String nightPalette = "night";
  String roles       = "{}";
  uint16_t colorsRev = 0;
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

  // The older "faceorder" setting: built-in face ids as base-36 characters (0-9, a-d),
  // turned into face entries ("#3,#7,...").
  static String entriesOf(const String& s) {
    String out;
    for (unsigned i = 0; i < s.length(); ++i) {
      char c = s[i];
      int id = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'z' ? c - 'a' + 10 : -1;
      if (id < 0 || id >= FACE_COUNT) continue;
      if (out.length()) out += ',';
      out += "#" + String(id);
    }
    return out;
  }

  // A colour of a palette, set in the palettes' JSON (main.cpp).
  bool setColor(const char* pal, const char* name, uint16_t c);

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
    order       = doc["order"]       | order;
    if (order.isEmpty()) order = entriesOf(doc["faceorder"] | "");
    customOff   = doc["customoff"]   | customOff;
    turns       = doc["turns"]       | turns;
    theme       = doc["theme"]       | theme;
    themeInterval = doc["themeInterval"] | themeInterval;
    faceMask    = doc["faces"]       | faceMask;
    brightness  = doc["brightness"]  | brightness;
    orientation = ((doc["orientation"] | 0) / 90 % 4 + 4) % 4;
    nightMode   = doc["nightmode"]   | nightMode;
    nightAuto   = doc["nightauto"]   | nightAuto;
    nightBrightness = doc["nightbrightness"] | nightBrightness;
    if (doc["nightface"].is<int>()) {   // a built-in id before 1.6; -1 the rotation
      int id = doc["nightface"];
      nightFace = id >= 0 && id < FACE_COUNT ? "#" + String(id) : String();
    } else nightFace = doc["nightface"] | nightFace;
    nightTint   = doc["nighttint"]   | nightTint;
    nightColor  = doc["nightcolor"]  | nightColor;
    startHour   = doc["starttime"]   | startHour;
    stopHour    = doc["stoptime"]    | stopHour;
    if (doc["palettes"].is<JsonObject>()) { palettes = ""; serializeJson(doc["palettes"], palettes); }
    else {   // before palettes: the three accent colours
      const char* accents[] = {"color1", "color2", "color3"}, *names[] = {"accent1", "accent2", "accent3"};
      for (int i = 0; i < 3; ++i) if (doc[accents[i]].is<int>()) setColor("day", names[i], doc[accents[i]].as<int>());
    }
    if (doc["roles"].is<JsonObject>()) { roles = ""; serializeJson(doc["roles"], roles); }
    palette     = doc["palette"]     | palette;
    nightPalette = doc["nightpalette"] | nightPalette;
    colorsRev++;
    photoInterval = doc["photointerval"] | photoInterval;
    photoClock  = doc["photoclock"]  | photoClock;
    photoOff    = doc["photooff"]    | photoOff;
    customFace  = doc["customface"]  | customFace;
    // Face ids moved: 1.3.x had custom faces at 7 (facesv 1); 1.4 development builds
    // had the Agents board at 7 and custom faces at 8 (facesv 2), then custom faces at
    // 10 and messages at 11 (facesv 3). The new faces join the rotation.
    int facesv = doc["facesv"] | 1;
    if (facesv < 3) {
      if (facesv < 2 && theme == 7) theme = 10;
      else if (facesv == 2 && theme == 7) theme = 9;
      else if (facesv == 2 && theme == 8) theme = 10;
      faceMask = (faceMask & 0x7f) | 0x380;
    }
    if (facesv < 4) {
      if (theme == 10) theme = FACE_CUSTOM;
      else if (theme == 11) theme = FACE_MESSAGE;
      faceMask |= 0xf << FACE_DAY0;
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
    doc["order"] = order;         doc["customoff"] = customOff; doc["turns"] = turns;
    doc["theme"] = theme;         doc["themeInterval"] = themeInterval;
    doc["faces"] = faceMask;     doc["facesv"] = 4;
    doc["brightness"] = brightness; doc["orientation"] = orientation * 90;
    doc["nightmode"] = nightMode; doc["nightauto"] = nightAuto; doc["nightbrightness"] = nightBrightness; doc["nightface"] = nightFace;
    doc["nighttint"] = nightTint; doc["nightcolor"] = nightColor;
    doc["starttime"] = startHour; doc["stoptime"] = stopHour;
    doc["palettes"] = serialized(palettes); doc["roles"] = serialized(roles);
    doc["palette"] = palette;     doc["nightpalette"] = nightPalette;
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

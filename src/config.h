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
inline const char DEFAULT_PALETTES_P[] PROGMEM = DEFAULT_PALETTES;

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
  int    gamma       = 0;            // the panel's gamma curve: 0 2.2, 1 1.8, 2 2.5, 3 1.0 (GAMSET)
  int    nightGamma  = -1;           // at night; -1 the day's
  bool   nightOff    = false;        // during night mode: the panel off (urgent messages wake it)
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
  String palettes    = FPSTR(DEFAULT_PALETTES_P);   // the default in flash, not RAM
  String palette     = "day";
  String nightPalette = "night";
  String roles       = "{}";
  uint16_t colorsRev = 0;
  // Photos: seconds per photo, and names left out of the slideshow ("|a.jpg|b.jpg|").
  int    photoInterval = 60;
  bool   photoClock = true;          // the time pill on the Photo face
  bool   msgAnim  = true;            // the Message face's figure and flashes (spec 039)
  String photoOff;
  String customFace;                 // the custom face shown when theme is FACE_CUSTOM
  // Integrations, all off by default (settings page, Integrations card).
  bool   facesApi = false;           // custom faces over HTTP (and MQTT when that is on)
  bool   haCompat = false;           // stock SD Pro photo API for geekmagic-hacs; no sign-in
  bool   mqttOn   = false;           // MQTT with Home Assistant discovery
  String mqttHost, mqttUser, mqttPass, mqttBase;   // mqttBase "" = the hostname
  String hostnames;                  // other names the clock answers to: comma list (spec 029)
  String tzname;                     // the zone tz came from, to show ("America/Toronto"); "" custom
  int    mqttPort = 1883;
  String apiKey;                     // for scripts calling the faces API; never exported

  // The panel's four gamma curves, as the settings name them; -1 for anything else.
  static constexpr const char* GAMMAS[4] = {"2.2", "1.8", "2.5", "1.0"};
  static int gammaIndex(const String& s) {
    for (int i = 0; i < 4; ++i) if (s == GAMMAS[i]) return i;
    return -1;
  }

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
    ssid        = doc[F("ssid")]        | ssid;
    password    = doc[F("password")]    | password;
    city        = doc[F("city")]        | city;
    ntp         = doc[F("ntp")]         | ntp;
    if (doc[F("lat5")].is<long>() && doc[F("lon5")].is<long>()) {   // 1e-5 degrees
      lat = (doc[F("lat5")].as<long>()) / 1e5f;
      lon = (doc[F("lon5")].as<long>()) / 1e5f;
    }
    geoName     = doc[F("geoname")]     | geoName;
    geoFor      = doc[F("geofor")]      | geoFor;
    timezoneMin = doc[F("timezone")]    | timezoneMin;
    tz          = doc[F("tz")]          | tz;
    celsius     = doc[F("celsius")]     | celsius;
    hour12      = doc[F("hour12")]      | hour12;
    mile        = doc[F("mile")]        | mile;
    lang        = doc[F("lang")]        | lang;
    order       = doc[F("order")]       | order;
    if (order.isEmpty()) order = entriesOf(doc[F("faceorder")] | "");
    customOff   = doc[F("customoff")]   | customOff;
    turns       = doc[F("turns")]       | turns;
    theme       = doc[F("theme")]       | theme;
    themeInterval = doc[F("themeInterval")] | themeInterval;
    faceMask    = doc[F("faces")]       | faceMask;
    brightness  = doc[F("brightness")]  | brightness;
    orientation = ((doc[F("orientation")] | 0) / 90 % 4 + 4) % 4;
    gamma       = constrain(gammaIndex(doc[F("gamma")] | "2.2"), 0, 3);
    nightGamma  = gammaIndex(doc[F("nightgamma")] | "");
    nightOff    = doc[F("nightoff")]    | nightOff;
    nightMode   = doc[F("nightmode")]   | nightMode;
    nightAuto   = doc[F("nightauto")]   | nightAuto;
    nightBrightness = doc[F("nightbrightness")] | nightBrightness;
    if (doc[F("nightface")].is<int>()) {   // a built-in id before 1.6; -1 the rotation
      int id = doc[F("nightface")];
      nightFace = id >= 0 && id < FACE_COUNT ? "#" + String(id) : String();
    } else nightFace = doc[F("nightface")] | nightFace;
    nightTint   = doc[F("nighttint")]   | nightTint;
    nightColor  = doc[F("nightcolor")]  | nightColor;
    startHour   = doc[F("starttime")]   | startHour;
    stopHour    = doc[F("stoptime")]    | stopHour;
    if (doc[F("palettes")].is<JsonObject>()) { palettes = ""; serializeJson(doc[F("palettes")], palettes); }
    else {   // before palettes: the three accent colours
      const char* accents[] = {"color1", "color2", "color3"}, *names[] = {"accent1", "accent2", "accent3"};
      for (int i = 0; i < 3; ++i) if (doc[accents[i]].is<int>()) setColor("day", names[i], doc[accents[i]].as<int>());
    }
    if (doc[F("roles")].is<JsonObject>()) { roles = ""; serializeJson(doc[F("roles")], roles); }
    palette     = doc[F("palette")]     | palette;
    nightPalette = doc[F("nightpalette")] | nightPalette;
    colorsRev++;
    photoInterval = doc[F("photointerval")] | photoInterval;
    photoClock  = doc[F("photoclock")]  | photoClock;
    msgAnim     = doc[F("msganim")]     | msgAnim;
    photoOff    = doc[F("photooff")]    | photoOff;
    customFace  = doc[F("customface")]  | customFace;
    // Face ids moved: 1.3.x had custom faces at 7 (facesv 1); 1.4 development builds
    // had the Agents board at 7 and custom faces at 8 (facesv 2), then custom faces at
    // 10 and messages at 11 (facesv 3). The new faces join the rotation.
    int facesv = doc[F("facesv")] | 1;
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
    if (facesv < 5)   // id 6 was retired Flip; now the Day 4 tile, on when Day 3 is
      faceMask = (faceMask & ~(1 << FACE_DAY4)) | ((faceMask >> FACE_DAY3 & 1) << FACE_DAY4);
    facesApi    = doc[F("facesapi")]    | facesApi;
    haCompat    = doc[F("hacompat")]    | haCompat;
    mqttOn      = doc[F("mqtt")]        | mqttOn;
    mqttHost    = doc[F("mqtthost")]    | mqttHost;
    mqttPort    = doc[F("mqttport")]    | mqttPort;
    mqttUser    = doc[F("mqttuser")]    | mqttUser;
    mqttPass    = doc[F("mqttpass")]    | mqttPass;
    mqttBase    = doc[F("mqttbase")]    | mqttBase;
    hostnames   = doc[F("hostnames")]   | hostnames;
    tzname      = doc[F("tzname")]      | tzname;
    apiKey      = doc[F("apikey")]      | apiKey;
    return true;
  }

  bool save() {
    JsonDocument doc;
    doc[F("ssid")] = ssid;           doc[F("password")] = password;
    doc[F("city")] = city;           doc[F("ntp")] = ntp;
    doc[F("timezone")] = timezoneMin;
    // Integers, not floats: saves pulling a float writer into the file path.
    if (!isnan(lat) && !isnan(lon)) { doc[F("lat5")] = lroundf(lat * 1e5f); doc[F("lon5")] = lroundf(lon * 1e5f); }
    doc[F("geoname")] = geoName; doc[F("geofor")] = geoFor;
    doc[F("tz")] = tz;
    doc[F("celsius")] = celsius;     doc[F("hour12")] = hour12;   doc[F("mile")] = mile;   doc[F("lang")] = lang;
    doc[F("order")] = order;         doc[F("customoff")] = customOff; doc[F("turns")] = turns;
    doc[F("theme")] = theme;         doc[F("themeInterval")] = themeInterval;
    doc[F("faces")] = faceMask;     doc[F("facesv")] = 5;
    doc[F("brightness")] = brightness; doc[F("orientation")] = orientation * 90;
    doc[F("gamma")] = GAMMAS[gamma]; doc[F("nightgamma")] = nightGamma < 0 ? "" : GAMMAS[nightGamma]; doc[F("nightoff")] = nightOff;
    doc[F("nightmode")] = nightMode; doc[F("nightauto")] = nightAuto; doc[F("nightbrightness")] = nightBrightness; doc[F("nightface")] = nightFace;
    doc[F("nighttint")] = nightTint; doc[F("nightcolor")] = nightColor;
    doc[F("starttime")] = startHour; doc[F("stoptime")] = stopHour;
    doc[F("palettes")] = serialized(palettes); doc[F("roles")] = serialized(roles);
    doc[F("palette")] = palette;     doc[F("nightpalette")] = nightPalette;
    doc[F("photointerval")] = photoInterval; doc[F("photoclock")] = photoClock; doc[F("msganim")] = msgAnim; doc[F("photooff")] = photoOff; doc[F("customface")] = customFace;
    doc[F("facesapi")] = facesApi;   doc[F("hacompat")] = haCompat; doc[F("mqtt")] = mqttOn;
    doc[F("mqtthost")] = mqttHost;   doc[F("mqttport")] = mqttPort; doc[F("mqttuser")] = mqttUser;
    doc[F("mqttpass")] = mqttPass;   doc[F("mqttbase")] = mqttBase; doc[F("apikey")] = apiKey;
    doc[F("hostnames")] = hostnames; doc[F("tzname")] = tzname;
    File f = LittleFS.open("/config.json", "w");
    if (!f) return false;
    serializeJson(doc, f);
    f.close();
    return true;
  }
};

extern Config cfg;   // defined in main.cpp

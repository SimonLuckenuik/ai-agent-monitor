#pragma once
#include <stdint.h>
#include <time.h>

// Clock faces. Ids 0-6 match the stock theme numbers (3 was the stock Dial) and
// /api/set?key=theme; the others were added after them, the Agents board last.
enum Face { FACE_CLASSIC = 0, FACE_WEATHER = 1, FACE_PHOTO = 2, FACE_DATE = 3,
            FACE_SIMPLE = 4, FACE_FORECAST = 5, FACE_DAY4 = 6 /* the last Day tile, on retired Flip's id */, FACE_BIGTIME = 7,
            FACE_BIGWEATHER = 8, FACE_AGENTS = 9,
            FACE_DAY0 = 10, FACE_DAY3 = 13,   // the Day tiles: today, then the next days (FACE_DAY4 last)
            FACE_COUNT = 14,
            FACE_CUSTOM = 14,    // cfg.customFace, from /faces/<name>.json (integrations)
            FACE_MESSAGE = 15 }; // an agent message (integrations), never saved as the face

// The forecast day a Day tile shows (0 today), or -1 for another face.
inline int dayOfFace(int face) {
  return face == FACE_DAY4 ? 4 : face >= FACE_DAY0 && face <= FACE_DAY3 ? face - FACE_DAY0 : -1;
}

// Draws only what changed since the last call; a face switch or faceInvalidate()
// redraws everything. timeValid is false until NTP has set the clock.
// customFace: the custom face to draw for FACE_CUSTOM (null: cfg.customFace).
void faceDraw(int face, const struct tm& t, bool timeValid, const char* customFace = nullptr);
void faceInvalidate();            // something else drew on the panel, or settings changed
void customRelease();             // frees the custom face's document; loaded again when drawn
bool faceAvailable(int face);     // has something to show (weather, forecast, photos)
void facePhotosChanged();         // a photo was added or removed
int  photoCount();
class String;
// A custom face's rotation turn: false while its "when" is false; *seconds (if not
// null) gets its "duration", 0 for the rotation's own interval.
bool customFaceDue(const String& name, uint32_t* seconds);
// A custom face's own rules, for the settings page: whether it has "when", its "duration".
void customFaceRule(const String& name, bool* hasWhen, uint32_t* seconds);
bool roleKnown(const char* name);   // a colour role ("time", "bg", ...)
// main.cpp: {net.*}, {sys.*}, {theme.night}, {time.zone} for custom faces; "" if unknown.
String deviceValue(const String& key);
bool nightActive();               // main.cpp: inside the night hours (night palette)
// The night tint: colour c as the panel shows it with tint t (RGB565, 0 = none).
uint16_t tintColor(uint32_t c, uint16_t t);
uint16_t panelTint();             // main.cpp: the tint the panel adds as it draws
float readNumber(const char*& s, bool* ok);
// main.cpp: local time by the clock's time zone rule (tz_rule.h; never localtime_r, spec 036)
struct tm* clockLocal(time_t t, struct tm* out, const char** abbr = nullptr);   // faces.cpp: a number, without libc's float parser

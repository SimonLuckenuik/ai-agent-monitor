#pragma once
#include <stdint.h>
#include <time.h>

// Clock faces. Ids 0-6 match the stock theme numbers (3 was the stock Dial) and
// /api/set?key=theme; the others were added after them, the Agents board last.
enum Face { FACE_CLASSIC = 0, FACE_WEATHER = 1, FACE_PHOTO = 2, FACE_DATE = 3,
            FACE_SIMPLE = 4, FACE_FORECAST = 5, FACE_FLIP = 6 /* retired: Flip left (flash), the id stays reserved */, FACE_BIGTIME = 7,
            FACE_BIGWEATHER = 8, FACE_AGENTS = 9,
            FACE_DAY0 = 10, FACE_DAY3 = 13,   // the Day tiles: today, then the next three days
            FACE_COUNT = 14,
            FACE_CUSTOM = 14,    // cfg.customFace, from /faces/<name>.json (integrations)
            FACE_MESSAGE = 15 }; // an agent message (integrations), never saved as the face

// Draws only what changed since the last call; a face switch or faceInvalidate()
// redraws everything. timeValid is false until NTP has set the clock.
void faceDraw(int face, const struct tm& t, bool timeValid);
void faceInvalidate();            // something else drew on the panel, or settings changed
bool faceAvailable(int face);     // has something to show (weather, forecast, photos)
void facePhotosChanged();         // a photo was added or removed
int  photoCount();
class String;
// A custom face's rotation turn: false while its "when" is false; *seconds (if not
// null) gets its "duration", 0 for the rotation's own interval.
bool customFaceDue(const String& name, uint32_t* seconds);
// main.cpp: {net.*}, {sys.*}, {theme.night}, {time.zone} for custom faces; "" if unknown.
String deviceValue(const String& key);

#pragma once
#include <time.h>

// Clock faces. Ids 0-6 match the stock theme numbers (3 was the stock Dial) and
// /api/set?key=theme; the others were added after them, the Agents board last.
enum Face { FACE_CLASSIC = 0, FACE_WEATHER = 1, FACE_PHOTO = 2, FACE_DATE = 3,
            FACE_SIMPLE = 4, FACE_FORECAST = 5, FACE_FLIP = 6, FACE_BIGTIME = 7,
            FACE_BIGWEATHER = 8, FACE_AGENTS = 9, FACE_COUNT = 10,
            FACE_CUSTOM = 10,    // cfg.customFace, from /faces/<name>.json (integrations)
            FACE_MESSAGE = 11 }; // an agent message (integrations), never saved as the face

// Draws only what changed since the last call; a face switch or faceInvalidate()
// redraws everything. timeValid is false until NTP has set the clock.
void faceDraw(int face, const struct tm& t, bool timeValid);
void faceInvalidate();            // something else drew on the panel, or settings changed
bool faceAvailable(int face);     // has something to show (weather, forecast, photos)
void facePhotosChanged();         // a photo was added or removed
int  photoCount();

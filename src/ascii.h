#pragma once
#include <Arduino.h>

// The panel's fonts are ASCII. Messages and face variables arrive as UTF-8: this
// folds Latin-1 letters to their base letter (e-acute to e, sharp s to ss), turns
// typographic quotes, dashes and the ellipsis into plain ones, and anything else
// into '?'.
String asciiFold(const String& s);

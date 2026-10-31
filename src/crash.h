// Crash records and crash-loop protection (spec 021).
#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>

// What loop() is drawing, for the crash record: a face id, and the custom face's name.
extern int crashFace;
extern const char* crashCustom;

// At boot, after storage mounts: moves a crash record from RTC memory into the log and
// counts crash loops. Then once a loop: 10 minutes up clears the loop count.
void crashBoot(const char* version);
void crashTick();

// 0: normal; 1: custom faces off (3 crashes in a row); 2: safe mode, no faces (2 more).
int crashLevel();
int crashLoopCount();

void crashStatus(JsonDocument& doc);   // /status: crashes, crashLoop, safeMode, lastCrash
const char* crashLogPath();            // GET /api/crashes serves this file (null: no crashes)
void crashResume();                    // POST /api/crashes/resume: back to normal
void crashClear();                     // POST /api/crashes/clear: the log too

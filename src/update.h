#pragma once
#include <Arduino.h>

// Updates (spec 009, spec 048): a firmware image (.bin, .bin.gz) or an update file
// (.update: the files the firmware reads from storage, then its .bin.gz), from an
// upload (/update_ota) or, in the starter, a download. Shared by the firmware and the
// starter (src/starter.cpp).
//
// The image is staged in free sketch space and checked (firmware_validation.h); the
// files go to /sys.new/ and move to /sys/ only once the image is committed, so a
// failed update leaves the running firmware and its files as they were.

void updateBegin();                                // a new update; drops any unfinished one
bool updateFeed(const uint8_t* data, size_t n);    // false once it has failed
bool updateFinish();                               // after the last byte: commit image, then files
void updateFail(const String& why);
const String& updateError();
const String& updateNote();     // after a success: what was not done (files skipped)
bool updateBusy();              // staging: the loop must leave flash alone
uint32_t updateReceived();      // bytes taken so far

void handleOtaUpload();         // POST /update_ota, the upload
void finishOta();               // and its reply (restarts after a success)

// The files the image was built with (FILES_HASH) against /sys/manifest: "ok",
// "missing", "stale", or "unmounted" (storage not in use). Checked once at boot.
const __FlashStringHelper* filesState();
bool filesOk();
void filesCheck();              // after storage mounts

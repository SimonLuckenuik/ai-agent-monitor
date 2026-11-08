// The starter (src/starter.cpp, spec 048) in the emulator, as fw_main.cpp does for the
// firmware: setup()/loop() wrapped by emulator.cpp, the backlight PWM to the emulator.
// build.sh builds it instead of the firmware with EMU_STARTER=1. Its NAT is the chip's
// (lwIP): the emulator has none, so its page reaches GitHub only from the host's network.
#include <Arduino.h>
#include <EEPROM.h>

void emuAnalogWrite(uint8_t pin, int value);
void emuAnalogWriteRange(uint32_t range);

#define setup fw_setup
#define loop fw_loop
#define analogWriteRange emuAnalogWriteRange
#define analogWrite emuAnalogWrite
#include "starter.cpp"
#undef setup
#undef loop

// The screen page's panels for the full firmware (MQTT, variables, OpenClaw, weather,
// crash records): the starter has none of them, so they answer that.
#include "openclaw.h"
#include "weather.h"
Weather wx;
String deviceHost() { char n[24]; snprintf(n, sizeof(n), "agentmonitor-%06x", (unsigned)(ESP.getChipId() & 0xffffff)); return n; }
String emuMqtt(const String&, const String&) { return "The starter has no MQTT"; }
String emuVarsJson() { return "{}"; }
bool fetchWeather() { wx.error = "The starter has no weather"; return false; }
String openclawPair(const String&) { return "The starter has no OpenClaw node"; }
void openclawForget() {}
void openclawSettingsChanged() {}
void openclawStatus(JsonDocument& doc) { doc["openclaw"] = "off"; }
extern "C" void custom_crash_callback(struct rst_info*, uint32_t, uint32_t) {}

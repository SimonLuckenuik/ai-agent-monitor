// The firmware's main.cpp, with the pieces the host emulation needs rerouted:
// setup()/loop() are wrapped by emulator.cpp, and the backlight PWM goes to the
// emulator instead of the core's logging stub.
#include <Arduino.h>
#include <EEPROM.h>
#include <time.h>

void emuAnalogWrite(uint8_t pin, int value);
void emuAnalogWriteRange(uint32_t range);

// The core's EEPROM mock returns void from end(); the chip's returns whether it committed.
struct EmuEeprom {
  void begin(size_t size) { EEPROM.begin(size); }
  uint8_t read(int address) { return EEPROM.read(address); }
  void write(int address, uint8_t value) { EEPROM.write(address, value); }
  bool end() { EEPROM.end(); return true; }
} emuEeprom;
#define EEPROM emuEeprom

// newlib (the chip) and glibc (the host) spell these differently.
#ifndef digitalPinToInterrupt
#define digitalPinToInterrupt(pin) (pin)
#endif
#define _tzname tzname

// The core's host configTime() is a stub that ignores the zone. Time comes from the
// host clock (no NTP needed); the zone is applied the way the chip's libc does it.
inline void emuConfigTime(const char* tz, const char*) {
  setenv("TZ", tz, 1);
  tzset();
}
inline void emuConfigTime(long offsetSec, int, const char*) {
  char tz[24];   // POSIX: the offset is west of UTC, so the sign flips
  long m = labs(offsetSec) / 60;
  snprintf(tz, sizeof(tz), "UTC%c%ld:%02ld", offsetSec >= 0 ? '-' : '+', m / 60, m % 60);
  emuConfigTime(tz, nullptr);
}
#define configTime emuConfigTime

#define setup fw_setup
#define loop fw_loop
#define analogWriteRange emuAnalogWriteRange
#define analogWrite emuAnalogWrite
#include "main.cpp"

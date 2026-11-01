// The clock's own mDNS responder (spec 027): <host>.local and the _http._tcp service,
// in place of the core's LEAmDNS.
#pragma once
#include <Arduino.h>

bool mdnsStart(const String& host, const char* fwVersion);   // joins and announces; false if it cannot
void mdnsLoop();                      // answers queries; joins again when the IP changes
void mdnsGoodbye();                   // tells the network the records are gone (before a restart)
bool mdnsRunning();

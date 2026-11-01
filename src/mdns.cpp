// See mdns.h. The packets are mdns_packet.cpp; this file only moves them.
#include "mdns.h"

#include <ESP8266WiFi.h>
#include <WiFiUdp.h>

#include "mdns_packet.h"

namespace {

const IPAddress GROUP(224, 0, 0, 251);
constexpr uint16_t PORT = 5353;
constexpr size_t PACKET_MAX = 512;   // our answers fit in under 400 B; bigger queries are dropped
constexpr uint8_t ALL = MDNS_PTR | MDNS_SERVICES | MDNS_SRV | MDNS_TXT | MDNS_A;

WiFiUDP udp;
String hostName;
IPAddress joinedIp;
bool running = false;
uint32_t announceAt = 0;   // the second announcement, a second after the first (RFC 6762 8.3)
String version;
const char* txt[5];

MdnsInfo info() {
  static const char MODEL[] = "model=AI Agent Monitor", BOARD[] = "board=SD Pro (GeekMagic-style)",
                    API[] = "api=ai-agent-monitor", PATH[] = "path=/";
  txt[0] = MODEL; txt[1] = version.c_str(); txt[2] = BOARD; txt[3] = API; txt[4] = PATH;
  MdnsInfo i{hostName.c_str(), {joinedIp[0], joinedIp[1], joinedIp[2], joinedIp[3]}, 80, txt, 5};
  return i;
}

// Sends the records in `want`: to the group, or to whoever asked.
void send(uint8_t want, uint16_t id, bool legacy, bool direct, bool goodbye = false) {
  uint8_t* buf = (uint8_t*)malloc(PACKET_MAX);
  if (!buf) return;
  size_t n = mdnsWriteAnswer(buf, PACKET_MAX, info(), want, id, legacy, goodbye);
  if (n) {
    bool begun = direct ? udp.beginPacket(udp.remoteIP(), udp.remotePort())
                        : udp.beginPacketMulticast(GROUP, PORT, joinedIp);
    if (begun) {
      udp.write(buf, n);
      udp.endPacket();
    }
  }
  free(buf);
}

bool join() {
  joinedIp = WiFi.localIP();
  udp.stop();
  return joinedIp.isSet() && udp.beginMulticast(joinedIp, GROUP, PORT);
}

}  // namespace

bool mdnsStart(const String& host, const char* fwVersion) {
  hostName = host;
  version = String(F("version=")) + fwVersion;
  if (!join()) return false;
  running = true;
  send(ALL, 0, false, false);
  announceAt = millis() + 1000;
  return true;
}

bool mdnsRunning() { return running; }

void mdnsLoop() {
  if (!running) return;
  if (WiFi.status() != WL_CONNECTED) return;
  if (WiFi.localIP() != joinedIp) {   // a new address, or Wi-Fi back: join again and say so
    if (!join()) return;
    send(ALL, 0, false, false);
    announceAt = millis() + 1000;
  }
  if (announceAt && (int32_t)(millis() - announceAt) >= 0) {
    announceAt = 0;
    send(ALL, 0, false, false);
  }
  int len = udp.parsePacket();
  if (len <= 0) return;
  if ((size_t)len > PACKET_MAX) { udp.flush(); return; }
  uint8_t* buf = (uint8_t*)malloc(len);
  if (!buf) { udp.flush(); return; }
  int got = udp.read(buf, len);
  MdnsQuery q;
  bool ours = got > 0 && mdnsReadQuery(buf, got, hostName.c_str(), q);
  free(buf);
  if (!ours) return;
  bool legacy = udp.remotePort() != PORT;   // a one-shot resolver: answer it alone, with its id
  send(q.want, q.id, legacy, legacy || q.unicast);
}

void mdnsGoodbye() {
  if (!running || WiFi.status() != WL_CONNECTED) return;
  send(ALL, 0, false, false, true);
  running = false;
}

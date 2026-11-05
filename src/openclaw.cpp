// The clock as an OpenClaw node (spec 041). The gateway's protocol, node side:
//  1. a WebSocket to the gateway (ws:// only: no TLS on this chip);
//  2. the gateway sends connect.challenge {nonce, ts};
//  3. the clock answers connect, signed with its Ed25519 key over
//     v3|deviceId|node-host|node|node||ts|token|nonce|esp8266|ai agent monitor;
//  4. hello-ok carries the device token, the credential from then on;
//  5. node.invoke.request events are run by openclawCommand() (integrations.cpp) and
//     answered with node.invoke.result.
// Frames in are read from the socket as a stream: hello-ok's snapshot (it can be large)
// is skipped by ArduinoJson's filter, never held. Frames out are written as text.
// Details: specs/041-openclaw-node.
#include "openclaw.h"
#include <ESP8266WiFi.h>
#include "json_parse.h"
#include "config.h"
#include "ed25519.h"
#include "openclaw_proto.h"

bool configSave();
String deviceHost();
const char* firmwareVersion();
extern const char OPENCLAW_COMMANDS[] PROGMEM;
String openclawCommand(const String& cmd, JsonObject params, String& r);

String jsonQuote(const String& s) {
  String o;
  o.reserve(s.length() + 2);
  o += '"';
  for (unsigned i = 0; i < s.length(); ++i) {
    char c = s[i];
    if (c == '"' || c == '\\') { o += '\\'; o += c; }
    else if ((uint8_t)c < 0x20) { char u[7]; snprintf_P(u, sizeof(u), PSTR("\\u%04x"), c); o += u; }
    else o += c;
  }
  return o + '"';
}

void jsonAddRaw(String& o, const __FlashStringHelper* key, const String& raw) {
  char last = o[o.length() - 1];
  if (last != '{' && last != '[') o += ',';
  o += '"'; o += key; o += F("\":"); o += raw;
}

void jsonAdd(String& o, const __FlashStringHelper* key, const String& text) { jsonAddRaw(o, key, jsonQuote(text)); }

JsonVariant jsonAt(JsonVariant v, const __FlashStringHelper* path) {
  String p = path;
  for (int from = 0;;) {
    int dot = p.indexOf('.', from);
    v = v[p.substring(from, dot < 0 ? p.length() : dot)];
    if (dot < 0) return v;
    from = dot + 1;
  }
}

const char* jsonText(JsonVariant v, const __FlashStringHelper* path) {
  const char* s = jsonAt(v, path).as<const char*>();
  return s ? s : "";
}

namespace {

enum Phase : uint8_t { IDLE, CHALLENGE, HELLO, READY };
constexpr uint32_t MIN_HEAP = 12288;      // free heap needed to connect (spec 041 R6)
constexpr uint32_t FRAME_MAX = 8192;      // once connected, bigger frames are skipped unread
constexpr uint32_t BACKOFF_MIN = 5000, BACKOFF_MAX = 300000, PENDING_RETRY = 30000;

WiFiClient net;
Phase phase = IDLE;
String state = "off";
bool reconnect = false, refused = false;
uint32_t retryAt = 0, backoff = BACKOFF_MIN, phaseAt = 0, lastRx = 0, pingAt = 0, seq = 0;
uint32_t heapBefore = 0, heapLeast = 0, usedHeap = 0, signMs = 0;
String connectId;

void sample() { heapLeast = std::min<uint32_t>(heapLeast, ESP.getFreeHeap()); }

void stop() { net.stop(); phase = IDLE; }

// A failure worth retrying: later and later, up to 5 minutes.
void fail(const String& why) {
  stop();
  state = why;
  retryAt = millis() + backoff;
  backoff = std::min(backoff * 2, BACKOFF_MAX);
}

void lost() { fail(F("connection lost")); }

bool parseUrl(const String& url, String& host, uint16_t& port, String& path) {
  if (!url.startsWith(F("ws://"))) return false;
  String rest = url.substring(5);
  int slash = rest.indexOf('/');
  path = slash < 0 ? String('/') : rest.substring(slash);
  if (slash >= 0) rest.remove(slash);
  int colon = rest.lastIndexOf(':');
  port = colon < 0 ? 18789 : rest.substring(colon + 1).toInt();
  host = colon < 0 ? rest : rest.substring(0, colon);
  return host.length() && port;
}

// ---------------------------------------------------------------- frames out
bool sendText(String& s) {
  sample();
  uint8_t mask[4], h[8];
  uint32_t r = ESP.random();
  memcpy(mask, &r, 4);
  size_t hn = wsTextHeader(h, s.length(), mask);
  wsMask((uint8_t*)s.begin(), s.length(), mask, 0);
  bool ok = net.write(h, hn) == hn && net.write((const uint8_t*)s.c_str(), s.length()) == s.length();
  if (!ok) lost();
  return ok;
}

void sendControl(uint8_t op, const uint8_t* d, size_t n) {   // pong, ping, close: mask of zeros
  uint8_t f[6 + 125] = {(uint8_t)(0x80 | op), (uint8_t)(0x80 | n)};
  if (n) memcpy(f + 6, d, n);
  net.write(f, 6 + n);
}

// ---------------------------------------------------------------- frames in
bool readExact(uint8_t* b, size_t n) { return net.readBytes(b, n) == n; }

bool skip(uint32_t n) {
  uint8_t b[64];
  while (n) {
    size_t k = std::min<uint32_t>(n, sizeof(b));
    if (!readExact(b, k)) return false;
    n -= k;
  }
  return true;
}

// A frame's payload as a stream, so ArduinoJson reads it straight from the socket.
class Payload : public Stream {
 public:
  explicit Payload(uint32_t n) : left(n) {}
  uint32_t left;
  int available() override { return left ? 1 : 0; }
  int peek() override { return -1; }
  size_t write(uint8_t) override { return 0; }
  int read() override {
    if (!left) return -1;
    uint32_t start = millis();
    while (!net.available()) {
      if (!net.connected() || millis() - start > 5000) { left = 0; return -1; }
      delay(1);
    }
    left--;
    return net.read();
  }
};

void sendConnect(const String& nonce, const String& ts) {
  uint8_t seed[32], pub[32], sig[64];
  if (b64urlDecode(seed, 32, cfg.ocSeed.c_str(), cfg.ocSeed.length()) != 32 ||
      b64urlDecode(pub, 32, cfg.ocPub.c_str(), cfg.ocPub.length()) != 32) {
    stop(); refused = true; state = F("refused: key damaged, forget and pair again");
    return;
  }
  bool device = cfg.ocToken.length() > 0;
  int colon = cfg.ocAuth.indexOf(':');
  String kind = device ? String(F("deviceToken")) : cfg.ocAuth.substring(0, colon);
  String token = device ? cfg.ocToken : cfg.ocAuth.substring(colon + 1);
  String f = String(F("v3|")) + cfg.ocId + F("|node-host|node|node||") + ts + '|' + (kind == F("password") ? String() : token) +
             '|' + nonce + F("|esp8266|ai agent monitor");   // a password is not signed
  sample();
  uint32_t t0 = millis();
  bool ok = ed25519Sign(sig, (const uint8_t*)f.c_str(), f.length(), seed, pub);
  signMs = millis() - t0;
  memset(seed, 0, sizeof(seed));
  if (!ok) { fail(F("waiting for memory")); return; }
  char sig64[90];
  b64urlEncode(sig64, sig, 64);

  connectId = "c" + String(++seq);
  f = F("{\"type\":\"req\",\"id\":\"");
  f += connectId;
  f += F("\",\"method\":\"connect\",\"params\":{\"minProtocol\":3,\"maxProtocol\":4,\"client\":{\"id\":\"node-host\","
         "\"mode\":\"node\",\"displayName\":");
  f += jsonQuote(cfg.ocName.length() ? cfg.ocName : deviceHost());
  f += F(",\"version\":\"");
  f += firmwareVersion();
  f += F("\",\"platform\":\"esp8266\",\"deviceFamily\":\"AI Agent Monitor\",\"modelIdentifier\":\"SD PRO\"},"
         "\"role\":\"node\",\"scopes\":[],\"caps\":[],\"commands\":[");   // output only: no caps (R9)
  f += FPSTR(OPENCLAW_COMMANDS);
  f += F("],\"locale\":\"en-US\",\"userAgent\":\"ai-agent-monitor/");
  f += firmwareVersion();
  f += F("\",\"auth\":{\"");
  f += kind;
  f += F("\":");
  f += jsonQuote(token);
  f += F("},\"device\":{\"id\":\"");
  f += cfg.ocId;
  f += F("\",\"publicKey\":\"");
  f += cfg.ocPub;
  f += F("\",\"signature\":\"");
  f += sig64;
  f += F("\",\"signedAt\":");
  f += ts;   // milliseconds, past 32 bits: the gateway's digits as they came
  f += F(",\"nonce\":");
  f += jsonQuote(nonce);
  f += F("}}}");
  token = "";
  if (!sendText(f)) return;
  phase = HELLO;
  phaseAt = millis();
}

// The challenge is small: read whole, ts taken as text (ArduinoJson here has no 64-bit numbers).
void onChallenge(uint32_t len) {
  if (len > 1024) { if (!skip(len)) lost(); return; }
  String s;
  s.reserve(len);
  Payload in(len);
  for (int ch; (ch = in.read()) >= 0;) s += (char)ch;
  if (s.indexOf(F("\"connect.challenge\"")) < 0) return;
  JsonDocument d;
  String nonce = parseJson(d, s) ? "" : jsonText(d.as<JsonVariant>(), F("payload.nonce"));
  int at = s.indexOf(F("\"ts\":"));
  String ts;
  if (at >= 0)
    for (unsigned i = at + 5; i < s.length() && ts.length() < 16; ++i) {
      if (isdigit((unsigned char)s[i])) ts += s[i];
      else if (ts.length() || s[i] != ' ') break;
    }
  if (nonce.isEmpty() || nonce.length() > 128 || ts.isEmpty()) { fail(F("bad challenge")); return; }
  sendConnect(nonce, ts);
}

void onHello(Payload& in) {
  JsonDocument filter, d;
  parseJson(filter, String(F("{\"type\":1,\"id\":1,\"ok\":1,\"error\":{\"code\":1,\"message\":1,"
                             "\"details\":{\"code\":1}},\"payload\":{\"auth\":{\"deviceToken\":1}}}")));
  sample();
  if (parseJson(d, in, filter)) { fail(F("bad answer from the gateway")); return; }
  sample();
  JsonVariant v = d.as<JsonVariant>();
  if (strcmp_P(jsonText(v, F("type")), PSTR("res"))) return;   // the connect is the only request out
  if (jsonAt(v, F("ok")).as<bool>()) {
    const char* tok = jsonText(v, F("payload.auth.deviceToken"));
    bool dirty = false;
    if (*tok && cfg.ocToken != tok) { cfg.ocToken = tok; dirty = true; }
    if (cfg.ocToken.length() && cfg.ocAuth.length()) { cfg.ocAuth = ""; dirty = true; }   // the setup code is spent
    if (dirty) configSave();
    phase = READY;
    state = F("connected");
    backoff = BACKOFF_MIN;
    lastRx = pingAt = millis();
    usedHeap = heapBefore - heapLeast;
    return;
  }
  String code = jsonText(v, F("error.details.code"));
  if (code.isEmpty()) code = jsonText(v, F("error.code"));
  stop();
  if (code.indexOf(F("PAIRING")) >= 0 || code == F("NOT_PAIRED")) {   // keep asking: an approval is picked up
    state = F("pairing pending: approve the clock on the gateway");
    retryAt = millis() + PENDING_RETRY;
    return;
  }
  refused = true;   // until the settings change
  state = String(F("refused: ")) + code + ' ' + String(jsonText(v, F("error.message"))).substring(0, 80);
}

void invoke(JsonVariant p) {
  String r = "{", err;
  {
    JsonDocument params;
    const char* pj = jsonText(p, F("paramsJSON"));
    if (parseJson(params, *pj ? pj : "{}") || !params.is<JsonObject>())
      err = F("INVALID_PARAMS paramsJSON: not an object");
    else
      err = openclawCommand(jsonText(p, F("command")), params.as<JsonObject>(), r);
  }
  String f = F("{\"type\":\"req\",\"id\":\"r");
  f += ++seq;
  f += F("\",\"method\":\"node.invoke.result\",\"params\":{");
  jsonAdd(f, F("id"), jsonText(p, F("id")));
  jsonAdd(f, F("nodeId"), jsonText(p, F("nodeId")));
  jsonAddRaw(f, F("ok"), err.isEmpty() ? F("true") : F("false"));
  if (err.isEmpty()) {
    jsonAdd(f, F("payloadJSON"), r + '}');
  } else {
    int sp = err.indexOf(' ');
    String e = "{";
    jsonAdd(e, F("code"), err.substring(0, sp));
    jsonAdd(e, F("message"), err.substring(sp + 1));
    jsonAddRaw(f, F("error"), e + '}');
  }
  r = "";
  f += F("}}");
  sendText(f);
}

void onEvent(Payload& in) {
  JsonDocument filter, d;
  parseJson(filter, String(F("{\"type\":1,\"event\":1,\"payload\":{\"id\":1,\"nodeId\":1,\"command\":1,\"paramsJSON\":1}}")));
  if (parseJson(d, in, filter)) { fail(F("bad frame from the gateway")); return; }
  JsonVariant v = d.as<JsonVariant>();
  if (!strcmp_P(jsonText(v, F("event")), PSTR("node.invoke.request"))) invoke(jsonAt(v, F("payload")));
}

void readFrame() {
  uint8_t h[8];
  if (!readExact(h, 2)) { lost(); return; }
  uint8_t op = h[0] & 15;
  bool fin = h[0] & 0x80;
  uint32_t len = h[1] & 127;
  if (len == 126) {
    if (!readExact(h, 2)) { lost(); return; }
    len = h[0] << 8 | h[1];
  } else if (len == 127) {
    if (!readExact(h, 8) || (h[0] | h[1] | h[2] | h[3])) { lost(); return; }
    len = (uint32_t)h[4] << 24 | h[5] << 16 | h[6] << 8 | h[7];
  }
  lastRx = millis();
  if (op >= 8) {   // control: close, ping, pong
    uint8_t b[125];
    if (len > sizeof(b) || !readExact(b, len)) { lost(); return; }
    if (op == 9) sendControl(10, b, len);
    if (op == 8) { sendControl(8, b, len >= 2 ? 2 : 0); fail(F("closed by the gateway")); }
    return;
  }
  if (op != 1 || !fin || (phase == READY && len > FRAME_MAX)) {   // binary, fragmented, too big
    if (!skip(len)) lost();
    return;
  }
  if (phase == CHALLENGE) { onChallenge(len); return; }
  Payload in(len);
  if (phase == HELLO) onHello(in);
  else onEvent(in);
  if (in.left && phase != IDLE && !skip(in.left)) lost();
}

void open() {
  String host, path;
  uint16_t port;
  if (!parseUrl(cfg.ocUrl, host, port, path)) { refused = true; state = F("no gateway address"); return; }
  heapBefore = heapLeast = ESP.getFreeHeap();
  net.setTimeout(3000);
  if (!net.connect(host.c_str(), port)) { fail(F("cannot reach the gateway")); return; }
  net.setNoDelay(true);
  char key[25];   // 16 random bytes in base64: 21 free characters, one with 4 zero bits, "=="
  for (int i = 0; i < 21; ++i) { uint32_t r = ESP.random() % 62; key[i] = r < 26 ? 'A' + r : r < 52 ? 'a' + r - 26 : '0' + r - 52; }
  uint32_t z = ESP.random() & 3;
  key[21] = z < 2 ? 'A' + 16 * z : 'g' + 16 * (z - 2);   // A Q g w: the last 4 bits zero
  key[22] = key[23] = '=';
  key[24] = 0;
  net.print(String(F("GET ")) + path + F(" HTTP/1.1\r\nHost: ") + host + ':' + port +
            F("\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Key: ") + key +
            F("\r\nSec-WebSocket-Version: 13\r\n\r\n"));
  String line = net.readStringUntil('\n');
  if (line.indexOf(F(" 101")) < 0) { fail(F("no WebSocket at that address")); return; }
  while (net.connected() && (line = net.readStringUntil('\n')).length() > 1) {}   // headers, to the empty line
  sample();
  phase = CHALLENGE;
  phaseAt = lastRx = millis();
  state = F("connecting");
}

}  // namespace

void openclawSettingsChanged() { reconnect = true; }

void openclawLoop() {
  if (reconnect) { reconnect = false; refused = false; backoff = BACKOFF_MIN; retryAt = millis(); stop(); }
  if (!cfg.ocOn || WiFi.status() != WL_CONNECTED) {
    if (phase != IDLE) stop();
    state = cfg.ocOn ? F("waiting for WiFi") : F("off");
    return;
  }
  uint32_t now = millis();
  if (phase == IDLE) {
    if (refused || (int32_t)(now - retryAt) < 0) return;
    if (cfg.ocSeed.isEmpty() || (cfg.ocToken.isEmpty() && cfg.ocAuth.isEmpty())) { state = F("not paired"); return; }
    if (ESP.getFreeHeap() < MIN_HEAP) { state = F("waiting for memory"); retryAt = now + BACKOFF_MIN; return; }
    open();
    return;
  }
  if (!net.connected() && !net.available()) { lost(); return; }
  if (phase != READY && now - phaseAt > 15000) { fail(F("no answer from the gateway")); return; }
  if (phase == READY) {
    // The gateway ticks every 15 s; quiet for 20 s, a ping; for 45 s, gone (a socket closed
    // under us is not always reported as such).
    if (now - lastRx > 45000) { lost(); return; }
    if (now - lastRx > 20000 && now - pingAt > 20000) { pingAt = now; sendControl(9, nullptr, 0); }
  }
  for (int n = 0; n < 4 && phase != IDLE && net.available() >= 2; ++n) readFrame();
}

String openclawPair(const String& code) {
  String kind, value;
  int colon = code.indexOf(':');
  if (code.startsWith(F("token:")) || code.startsWith(F("password:"))) {
    kind = code.substring(0, colon);
    value = code.substring(colon + 1);
  } else {   // a setup code: base64url of {"url", one of "bootstrapToken", "token", "password"}
    if (code.length() > 2048) return F("Not a setup code");
    uint8_t* raw = new (std::nothrow) uint8_t[code.length() + 1];
    if (!raw) return F("Not enough memory");
    int n = b64urlDecode(raw, code.length(), code.c_str(), code.length());
    JsonDocument d;
    bool bad = n <= 0 || (raw[n] = 0, parseJson(d, (const char*)raw)) || !d.is<JsonObject>();
    delete[] raw;
    if (bad) return F("Not a setup code");
    int found = 0;
    for (const __FlashStringHelper* k : {F("bootstrapToken"), F("token"), F("password")})
      if (*jsonText(d.as<JsonVariant>(), k)) { kind = k; value = jsonText(d.as<JsonVariant>(), k); found++; }
    String url = jsonText(d.as<JsonVariant>(), F("url"));
    if (found != 1) return F("Setup code without one credential");
    if (!url.startsWith(F("ws://")) || url.length() > 120) return F("The gateway must be ws:// (no wss:// on the clock)");
    cfg.ocUrl = url;
  }
  if (value.isEmpty() || value.length() > 512) return F("Bad credential");
  if (cfg.ocSeed.isEmpty() || cfg.ocPub.isEmpty()) {   // a new identity: the hardware RNG's seed
    uint8_t seed[32], pub[32], id[32];
    for (int i = 0; i < 32; i += 4) { uint32_t r = ESP.random(); memcpy(seed + i, &r, 4); }
    if (!ed25519Public(pub, seed)) return F("Not enough memory");
    char b64[44], hex[65];
    b64urlEncode(b64, seed, 32); cfg.ocSeed = b64;
    memset(seed, 0, sizeof(seed));
    b64urlEncode(b64, pub, 32); cfg.ocPub = b64;
    sha256(id, pub, 32);
    for (int i = 0; i < 32; ++i) snprintf(hex + 2 * i, 3, "%02x", id[i]);
    cfg.ocId = hex;
  }
  cfg.ocAuth = kind + ':' + value;
  cfg.ocToken = "";
  cfg.ocOn = true;
  openclawSettingsChanged();
  return "";
}

void openclawForget() {
  cfg.ocSeed = cfg.ocPub = cfg.ocId = cfg.ocToken = cfg.ocAuth = "";
  openclawSettingsChanged();
}

void openclawStatus(JsonDocument& doc) {
  doc[F("openclaw")] = state;
  if (cfg.ocId.length()) doc[F("openclawId")] = cfg.ocId;
  if (usedHeap) doc[F("openclawHeap")] = usedHeap;
  if (signMs) doc[F("openclawSignMs")] = signMs;
}

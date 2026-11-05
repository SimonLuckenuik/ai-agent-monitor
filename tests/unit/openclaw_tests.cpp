// Host tests for the OpenClaw node's signing and byte helpers (spec 041):
// RFC 8032 Ed25519 vectors 1-3, SHA-256 (FIPS 180-2), base64url, WebSocket frame headers.
#include <stdio.h>
#include <string.h>
#include <string>
#include "../../src/ed25519.h"
#include "../../src/openclaw_proto.h"

static int failures = 0, checks = 0;
static void check(bool ok, const char* what) {
  checks++;
  if (!ok) { failures++; printf("FAIL: %s\n", what); }
}

static void hex(uint8_t* out, const char* h) {
  for (size_t i = 0; h[2 * i]; ++i) { unsigned v; sscanf(h + 2 * i, "%2x", &v); out[i] = (uint8_t)v; }
}
static std::string tohex(const uint8_t* d, size_t n) {
  std::string s; char b[3];
  for (size_t i = 0; i < n; ++i) { snprintf(b, 3, "%02x", d[i]); s += b; }
  return s;
}

struct Vector { const char *seed, *pub, *msg, *sig; };
static const Vector RFC8032[] = {
  {"9d61b19deffd5a60ba844af492ec2cc44449c5697b326919703bac031cae7f60",
   "d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a", "",
   "e5564300c360ac729086e2cc806e828a84877f1eb8e5d974d873e065224901555fb8821590a33bacc61e39701cf9b46bd25bf5f0595bbe24655141438e7a100b"},
  {"4ccd089b28ff96da9db6c346ec114e0f5b8a319f35aba624da8cf6ed4fb8a6fb",
   "3d4017c3e843895a92b70aa74d1b7ebc9c982ccf2ec4968cc0cd55f12af4660c", "72",
   "92a009a9f0d4cab8720e820b5f642540a2b27b5416503f8fb3762223ebdb69da085ac1e43e15996e458f3613d0f11d8c387b2eaeb4302aeeb00d291612bb0c00"},
  {"c5aa8df43f9f837bedb7442f31dcb7b166d38535076f094b85ce3a2e0b4458f7",
   "fc51cd8e6218a1a38da47ed00230f0580816ed13ba3303ac5deb911548908025", "af82",
   "6291d657deec24024827e69c3abe01a30ce548a284743a445e3680d7db5ac3ac18ff9b538d16f290ae67f760984dc6594a7c15e9716ed28dc027beceea1ec40a"},
};

int main() {
  for (const Vector& v : RFC8032) {
    uint8_t seed[32], pub[32], msg[8], sig[64];
    hex(seed, v.seed);
    hex(msg, v.msg);
    check(ed25519Public(pub, seed) && tohex(pub, 32) == v.pub, "Ed25519 public key (RFC 8032)");
    check(ed25519Sign(sig, msg, strlen(v.msg) / 2, seed, pub) && tohex(sig, 64) == v.sig, "Ed25519 signature (RFC 8032)");
  }
  // A long message (several SHA-512 blocks) signs the same twice and differs from a
  // one-byte change.
  {
    uint8_t seed[32], pub[32], a[64], b[64];
    hex(seed, RFC8032[0].seed);
    ed25519Public(pub, seed);
    std::string m(300, 'x');
    ed25519Sign(a, (const uint8_t*)m.data(), m.size(), seed, pub);
    ed25519Sign(b, (const uint8_t*)m.data(), m.size(), seed, pub);
    check(!memcmp(a, b, 64), "Ed25519 is deterministic");
    m[299] = 'y';
    ed25519Sign(b, (const uint8_t*)m.data(), m.size(), seed, pub);
    check(memcmp(a, b, 64) != 0, "Ed25519 depends on the whole message");
  }

  uint8_t h[32];
  sha256(h, (const uint8_t*)"abc", 3);
  check(tohex(h, 32) == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad", "SHA-256 abc");
  sha256(h, (const uint8_t*)"", 0);
  check(tohex(h, 32) == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855", "SHA-256 empty");
  const char* two = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";   // 56 bytes: two blocks
  sha256(h, (const uint8_t*)two, strlen(two));
  check(tohex(h, 32) == "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1", "SHA-256 two blocks");
  std::string million(64, 'a');
  sha256(h, (const uint8_t*)million.data(), 64);   // exactly one block of data
  check(tohex(h, 32) == "ffe054fe7ae0cb6dc65c3af9b61d5209f439851db43d0ba5997337df154668eb", "SHA-256 64 bytes");

  char enc[64];
  const uint8_t bytes[] = {0xfb, 0xff, 0xbf, 0x01};
  check(b64urlEncode(enc, bytes, 4) == 6 && !strcmp(enc, "-_-_AQ"), "base64url encode, no padding");
  check(b64urlEncode(enc, (const uint8_t*)"abc", 3) == 4 && !strcmp(enc, "YWJj"), "base64url encode, whole groups");
  uint8_t dec[16];
  check(b64urlDecode(dec, sizeof dec, "-_-_AQ", 6) == 4 && !memcmp(dec, bytes, 4), "base64url decode");
  check(b64urlDecode(dec, sizeof dec, "+/+/AQ==", 8) == 4 && !memcmp(dec, bytes, 4), "base64 decode, padded");
  check(b64urlDecode(dec, sizeof dec, "a*b", 3) == -1, "base64url refuses other characters");
  check(b64urlDecode(dec, 2, "YWJj", 4) == -1, "base64url refuses what does not fit");

  uint8_t hd[8], mask[4] = {1, 2, 3, 4};
  check(wsTextHeader(hd, 5, mask) == 6 && hd[0] == 0x81 && hd[1] == 0x85 && hd[2] == 1 && hd[5] == 4, "WebSocket header, short");
  check(wsTextHeader(hd, 1000, mask) == 8 && hd[1] == 0xfe && hd[2] == 0x03 && hd[3] == 0xe8 && hd[4] == 1, "WebSocket header, 16-bit length");
  uint8_t data[6] = {'H', 'e', 'l', 'l', 'o', '!'};
  wsMask(data, 3, mask, 0); wsMask(data + 3, 3, mask, 3);
  wsMask(data, 6, mask, 0);
  check(!memcmp(data, "Hello!", 6), "WebSocket mask in pieces, then unmasked whole");

  printf("openclaw: %d checks, %d failed\n", checks, failures);
  return failures ? 1 : 0;
}

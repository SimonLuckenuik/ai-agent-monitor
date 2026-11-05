// Ed25519 signing (RFC 8032), SHA-512 and SHA-256, for the OpenClaw node (spec 041).
//
// The field and group arithmetic follow TweetNaCl (Bernstein, van Gastel, Janssen,
// Lange, Schwabe, Smetsers; public domain), signing only. Changes for the ESP8266:
//  - every temporary of the point arithmetic lives in one working space taken from the
//    heap (about 3 KB) and given back after: the 4 KB stack cannot hold them;
//  - SHA-512 is streamed (the message is not copied next to the key);
//  - the constants are in flash (PROGMEM), not in RAM; SHA-256 uses the top halves of
//    SHA-512's constants and initial values, which are SHA-256's;
//  - a yield() every 16 steps of the scalar multiplication (on the clock).
#include "ed25519.h"
#include <string.h>
#include <new>

#if defined(ARDUINO)
#include <Arduino.h>
#define ED_YIELD() yield()
#else
#define PROGMEM
#define pgm_read_byte(p) (*(const uint8_t*)(p))
#define pgm_read_word(p) (*(const uint16_t*)(p))
#define memcpy_P memcpy
#define ED_YIELD() ((void)0)
#endif

namespace {

typedef int64_t i64;
typedef i64 gf[16];

// ---------------------------------------------------------------- SHA-2
const uint64_t K512[80] PROGMEM = {
  0x428a2f98d728ae22ULL, 0x7137449123ef65cdULL, 0xb5c0fbcfec4d3b2fULL, 0xe9b5dba58189dbbcULL,
  0x3956c25bf348b538ULL, 0x59f111f1b605d019ULL, 0x923f82a4af194f9bULL, 0xab1c5ed5da6d8118ULL,
  0xd807aa98a3030242ULL, 0x12835b0145706fbeULL, 0x243185be4ee4b28cULL, 0x550c7dc3d5ffb4e2ULL,
  0x72be5d74f27b896fULL, 0x80deb1fe3b1696b1ULL, 0x9bdc06a725c71235ULL, 0xc19bf174cf692694ULL,
  0xe49b69c19ef14ad2ULL, 0xefbe4786384f25e3ULL, 0x0fc19dc68b8cd5b5ULL, 0x240ca1cc77ac9c65ULL,
  0x2de92c6f592b0275ULL, 0x4a7484aa6ea6e483ULL, 0x5cb0a9dcbd41fbd4ULL, 0x76f988da831153b5ULL,
  0x983e5152ee66dfabULL, 0xa831c66d2db43210ULL, 0xb00327c898fb213fULL, 0xbf597fc7beef0ee4ULL,
  0xc6e00bf33da88fc2ULL, 0xd5a79147930aa725ULL, 0x06ca6351e003826fULL, 0x142929670a0e6e70ULL,
  0x27b70a8546d22ffcULL, 0x2e1b21385c26c926ULL, 0x4d2c6dfc5ac42aedULL, 0x53380d139d95b3dfULL,
  0x650a73548baf63deULL, 0x766a0abb3c77b2a8ULL, 0x81c2c92e47edaee6ULL, 0x92722c851482353bULL,
  0xa2bfe8a14cf10364ULL, 0xa81a664bbc423001ULL, 0xc24b8b70d0f89791ULL, 0xc76c51a30654be30ULL,
  0xd192e819d6ef5218ULL, 0xd69906245565a910ULL, 0xf40e35855771202aULL, 0x106aa07032bbd1b8ULL,
  0x19a4c116b8d2d0c8ULL, 0x1e376c085141ab53ULL, 0x2748774cdf8eeb99ULL, 0x34b0bcb5e19b48a8ULL,
  0x391c0cb3c5c95a63ULL, 0x4ed8aa4ae3418acbULL, 0x5b9cca4f7763e373ULL, 0x682e6ff3d6b2b8a3ULL,
  0x748f82ee5defb2fcULL, 0x78a5636f43172f60ULL, 0x84c87814a1f0ab72ULL, 0x8cc702081a6439ecULL,
  0x90befffa23631e28ULL, 0xa4506cebde82bde9ULL, 0xbef9a3f7b2c67915ULL, 0xc67178f2e372532bULL,
  0xca273eceea26619cULL, 0xd186b8c721c0c207ULL, 0xeada7dd6cde0eb1eULL, 0xf57d4f7fee6ed178ULL,
  0x06f067aa72176fbaULL, 0x0a637dc5a2c898a6ULL, 0x113f9804bef90daeULL, 0x1b710b35131c471bULL,
  0x28db77f523047d84ULL, 0x32caab7b40c72493ULL, 0x3c9ebe0a15c9bebcULL, 0x431d67c49c100d4cULL,
  0x4cc5d4becb3e42b6ULL, 0x597f299cfc657e2aULL, 0x5fcb6fab3ad6faecULL, 0x6c44198c4a475817ULL};
const uint64_t IV512[8] PROGMEM = {
  0x6a09e667f3bcc908ULL, 0xbb67ae8584caa73bULL, 0x3c6ef372fe94f82bULL, 0xa54ff53a5f1d36f1ULL,
  0x510e527fade682d1ULL, 0x9b05688c2b3e6c1fULL, 0x1f83d9abfb41bd6bULL, 0x5be0cd19137e2179ULL};

uint64_t k64(const uint64_t* p) { uint64_t v; memcpy_P(&v, p, 8); return v; }
uint64_t rd64(const uint8_t* p) { uint64_t v = 0; for (int i = 0; i < 8; ++i) v = v << 8 | p[i]; return v; }
void wr64(uint8_t* p, uint64_t v) { for (int i = 7; i >= 0; --i) { p[i] = (uint8_t)v; v >>= 8; } }
uint64_t R64(uint64_t x, int c) { return (x >> c) | (x << (64 - c)); }
uint32_t R32(uint32_t x, int c) { return (x >> c) | (x << (32 - c)); }

struct Sha512 { uint64_t h[8]; uint8_t buf[128]; uint32_t len; };

void sha512Block(Sha512* s, const uint8_t* m) {
  uint64_t a[8], b[8], w[16], t;
  for (int i = 0; i < 8; ++i) a[i] = s->h[i];
  for (int i = 0; i < 16; ++i) w[i] = rd64(m + 8 * i);
  for (int i = 0; i < 80; ++i) {
    for (int j = 0; j < 8; ++j) b[j] = a[j];
    t = a[7] + (R64(a[4], 14) ^ R64(a[4], 18) ^ R64(a[4], 41)) + ((a[4] & a[5]) ^ (~a[4] & a[6])) + k64(&K512[i]) + w[i % 16];
    b[7] = t + (R64(a[0], 28) ^ R64(a[0], 34) ^ R64(a[0], 39)) + ((a[0] & a[1]) ^ (a[0] & a[2]) ^ (a[1] & a[2]));
    b[3] += t;
    for (int j = 0; j < 8; ++j) a[(j + 1) % 8] = b[j];
    if (i % 16 == 15)
      for (int j = 0; j < 16; ++j) {
        uint64_t x = w[(j + 1) % 16], y = w[(j + 14) % 16];
        w[j] += w[(j + 9) % 16] + (R64(x, 1) ^ R64(x, 8) ^ (x >> 7)) + (R64(y, 19) ^ R64(y, 61) ^ (y >> 6));
      }
  }
  for (int i = 0; i < 8; ++i) s->h[i] += a[i];
}

void sha512Init(Sha512* s) { for (int i = 0; i < 8; ++i) s->h[i] = k64(&IV512[i]); s->len = 0; }

void sha512Update(Sha512* s, const uint8_t* d, size_t n) {
  while (n--) {
    s->buf[s->len++ % 128] = *d++;
    if (s->len % 128 == 0) sha512Block(s, s->buf);
  }
}

void sha512Final(Sha512* s, uint8_t out[64]) {
  uint32_t bits = s->len;   // bytes: messages here are short
  size_t fill = s->len % 128;
  s->buf[fill++] = 0x80;
  if (fill > 112) { memset(s->buf + fill, 0, 128 - fill); sha512Block(s, s->buf); fill = 0; }
  memset(s->buf + fill, 0, 128 - fill);
  wr64(s->buf + 120, (uint64_t)bits << 3);
  sha512Block(s, s->buf);
  for (int i = 0; i < 8; ++i) wr64(out + 8 * i, s->h[i]);
}

// ---------------------------------------------------------------- field and group
const uint16_t D2_[16] PROGMEM = {0xf159, 0x26b2, 0x9b94, 0xebd6, 0xb156, 0x8283, 0x149a, 0x00e0,
                                  0xd130, 0xeef3, 0x80f2, 0x198e, 0xfce7, 0x56df, 0xd9dc, 0x2406};
const uint16_t X_[16] PROGMEM = {0xd51a, 0x8f25, 0x2d60, 0xc956, 0xa7b2, 0x9525, 0xc760, 0x692c,
                                 0xdc5c, 0xfdd6, 0xe231, 0xc0a4, 0x53fe, 0xcd6e, 0x36d3, 0x2169};
const uint8_t L_[32] PROGMEM = {0xed, 0xd3, 0xf5, 0x5c, 0x1a, 0x63, 0x12, 0x58, 0xd6, 0x9c, 0xf7, 0xa2,
                                0xde, 0xf9, 0xde, 0x14, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x10};

// Everything signing needs beyond a few hundred bytes of stack.
struct Work {
  gf p[4], q[4];
  gf a, b, c, d, t, e, f, g, h;   // add() and pack()
  gf d2;
  i64 x[64];
  uint8_t az[64], r[64], hr[64];
  Sha512 sha;
};

void load(gf o, const uint16_t* pgm) { for (int i = 0; i < 16; ++i) o[i] = pgm_read_word(pgm + i); }
void set(gf o, i64 v) { o[0] = v; for (int i = 1; i < 16; ++i) o[i] = 0; }
void cpy(gf o, const gf a) { for (int i = 0; i < 16; ++i) o[i] = a[i]; }

void car(gf o) {
  for (int i = 0; i < 16; ++i) {
    o[i] += (i64)1 << 16;
    i64 c = o[i] >> 16;
    o[(i + 1) * (i < 15)] += c - 1 + 37 * (c - 1) * (i == 15);
    o[i] -= c * ((i64)1 << 16);
  }
}

void sel(gf p, gf q, int b) {
  i64 c = ~(i64)(b - 1);
  for (int i = 0; i < 16; ++i) { i64 t = c & (p[i] ^ q[i]); p[i] ^= t; q[i] ^= t; }
}

void pack25519(uint8_t* o, const gf n) {
  gf m, t;
  cpy(t, n);
  car(t); car(t); car(t);
  for (int j = 0; j < 2; ++j) {
    m[0] = t[0] - 0xffed;
    for (int i = 1; i < 15; ++i) { m[i] = t[i] - 0xffff - ((m[i - 1] >> 16) & 1); m[i - 1] &= 0xffff; }
    m[15] = t[15] - 0x7fff - ((m[14] >> 16) & 1);
    int b = (m[15] >> 16) & 1;
    m[14] &= 0xffff;
    sel(t, m, 1 - b);
  }
  for (int i = 0; i < 16; ++i) { o[2 * i] = t[i] & 0xff; o[2 * i + 1] = (uint8_t)(t[i] >> 8); }
}

void A(gf o, const gf a, const gf b) { for (int i = 0; i < 16; ++i) o[i] = a[i] + b[i]; }
void Z(gf o, const gf a, const gf b) { for (int i = 0; i < 16; ++i) o[i] = a[i] - b[i]; }

void M(gf o, const gf a, const gf b) {
  i64 t[31];
  for (int i = 0; i < 31; ++i) t[i] = 0;
  for (int i = 0; i < 16; ++i)
    for (int j = 0; j < 16; ++j) t[i + j] += a[i] * b[j];
  for (int i = 0; i < 15; ++i) t[i] += 38 * t[i + 16];
  for (int i = 0; i < 16; ++i) o[i] = t[i];
  car(o); car(o);
}

void inv(gf o, const gf in) {
  gf c;
  cpy(c, in);
  for (int a = 253; a >= 0; --a) { M(c, c, c); if (a != 2 && a != 4) M(c, c, in); }
  cpy(o, c);
}

void add(Work* w, gf p[4], gf q[4]) {
  Z(w->a, p[1], p[0]); Z(w->t, q[1], q[0]); M(w->a, w->a, w->t);
  A(w->b, p[0], p[1]); A(w->t, q[0], q[1]); M(w->b, w->b, w->t);
  M(w->c, p[3], q[3]); M(w->c, w->c, w->d2);
  M(w->d, p[2], q[2]); A(w->d, w->d, w->d);
  Z(w->e, w->b, w->a); Z(w->f, w->d, w->c); A(w->g, w->d, w->c); A(w->h, w->b, w->a);
  M(p[0], w->e, w->f); M(p[1], w->h, w->g); M(p[2], w->g, w->f); M(p[3], w->e, w->h);
}

void pack(Work* w, uint8_t* r, gf p[4]) {
  inv(w->a, p[2]); M(w->b, p[0], w->a); M(w->c, p[1], w->a);
  pack25519(r, w->c);
  uint8_t d[32];
  pack25519(d, w->b);
  r[31] ^= (d[0] & 1) << 7;
}

// p = s * B, the base point.
void scalarbase(Work* w, const uint8_t* s) {
  gf* p = w->p; gf* q = w->q;
  load(w->d2, D2_);
  load(q[0], X_); set(q[1], 0x6666); for (int i = 1; i < 16; ++i) q[1][i] = 0x6666;
  q[1][0] = 0x6658;
  set(q[2], 1); M(q[3], q[0], q[1]);
  set(p[0], 0); set(p[1], 1); set(p[2], 1); set(p[3], 0);
  for (int i = 255; i >= 0; --i) {
    int b = (s[i / 8] >> (i & 7)) & 1;
    for (int k = 0; k < 4; ++k) sel(p[k], q[k], b);
    add(w, q, p); add(w, p, p);
    for (int k = 0; k < 4; ++k) sel(p[k], q[k], b);
    if (i % 16 == 0) ED_YIELD();
  }
}

void modL(Work* w, uint8_t* r) {   // r = w->x mod L
  i64* x = w->x;
  for (int i = 63; i >= 32; --i) {
    i64 carry = 0;
    int j;
    for (j = i - 32; j < i - 12; ++j) {
      x[j] += carry - 16 * x[i] * pgm_read_byte(&L_[j - (i - 32)]);
      carry = (x[j] + 128) >> 8;
      x[j] -= carry * 256;
    }
    x[j] += carry;
    x[i] = 0;
  }
  i64 carry = 0;
  for (int j = 0; j < 32; ++j) {
    x[j] += carry - (x[31] >> 4) * pgm_read_byte(&L_[j]);
    carry = x[j] >> 8;
    x[j] &= 255;
  }
  for (int j = 0; j < 32; ++j) x[j] -= carry * pgm_read_byte(&L_[j]);
  for (int i = 0; i < 32; ++i) { x[i + 1] += x[i] >> 8; r[i] = x[i] & 255; }
}

void reduce(Work* w, uint8_t* r) {   // a 64-byte hash, mod L, into its first 32 bytes
  for (int i = 0; i < 64; ++i) w->x[i] = r[i];
  memset(r, 0, 64);
  modL(w, r);
}

Work* begin(const uint8_t seed[32]) {
  Work* w = new (std::nothrow) Work;
  if (!w) return nullptr;
  sha512Init(&w->sha);
  sha512Update(&w->sha, seed, 32);
  sha512Final(&w->sha, w->az);
  w->az[0] &= 248; w->az[31] &= 127; w->az[31] |= 64;
  return w;
}

void end(Work* w) {
  volatile uint8_t* p = (volatile uint8_t*)w;   // the key's hash must not stay in the heap
  for (size_t i = 0; i < sizeof(Work); ++i) p[i] = 0;
  delete w;
}

}  // namespace

bool ed25519Public(uint8_t pub[32], const uint8_t seed[32]) {
  Work* w = begin(seed);
  if (!w) return false;
  scalarbase(w, w->az);
  pack(w, pub, w->p);
  end(w);
  return true;
}

bool ed25519Sign(uint8_t sig[64], const uint8_t* msg, size_t len, const uint8_t seed[32], const uint8_t pub[32]) {
  Work* w = begin(seed);
  if (!w) return false;
  sha512Init(&w->sha);                       // r = H(prefix || msg) mod L
  sha512Update(&w->sha, w->az + 32, 32);
  sha512Update(&w->sha, msg, len);
  sha512Final(&w->sha, w->r);
  reduce(w, w->r);
  scalarbase(w, w->r);                       // R = r * B
  pack(w, sig, w->p);
  sha512Init(&w->sha);                       // k = H(R || A || msg) mod L
  sha512Update(&w->sha, sig, 32);
  sha512Update(&w->sha, pub, 32);
  sha512Update(&w->sha, msg, len);
  sha512Final(&w->sha, w->hr);
  reduce(w, w->hr);
  for (int i = 0; i < 64; ++i) w->x[i] = 0;  // S = r + k * a mod L
  for (int i = 0; i < 32; ++i) w->x[i] = w->r[i];
  for (int i = 0; i < 32; ++i)
    for (int j = 0; j < 32; ++j) w->x[i + j] += (i64)w->hr[i] * w->az[j];
  modL(w, sig + 32);
  end(w);
  return true;
}

void sha256(uint8_t out[32], const uint8_t* data, size_t len) {
  uint32_t h[8], w[16];
  for (int i = 0; i < 8; ++i) h[i] = (uint32_t)(k64(&IV512[i]) >> 32);
  uint8_t blk[64];
  size_t done = 0;
  bool padded = false, lenDone = false;
  while (!lenDone) {
    // The next block: data, then 0x80, zeros and the length in bits.
    size_t n = len > done ? (len - done < 64 ? len - done : 64) : 0;
    memcpy(blk, data + done, n);
    memset(blk + n, 0, 64 - n);
    done += n;
    if (n < 64 && !padded) { blk[n] = 0x80; padded = true; n++; }
    if (padded && n <= 56) {
      uint64_t bits = (uint64_t)len * 8;
      for (int i = 0; i < 8; ++i) blk[63 - i] = (uint8_t)(bits >> (8 * i));
      lenDone = true;
    }
    uint32_t a[8];
    for (int i = 0; i < 8; ++i) a[i] = h[i];
    for (int i = 0; i < 64; ++i) {
      uint32_t wi;
      if (i < 16) wi = w[i] = (uint32_t)blk[4 * i] << 24 | blk[4 * i + 1] << 16 | blk[4 * i + 2] << 8 | blk[4 * i + 3];
      else {
        uint32_t x = w[(i + 1) & 15], y = w[(i + 14) & 15];
        wi = w[i & 15] += w[(i + 9) & 15] + (R32(x, 7) ^ R32(x, 18) ^ (x >> 3)) + (R32(y, 17) ^ R32(y, 19) ^ (y >> 10));
      }
      uint32_t e = a[4];
      uint32_t t1 = a[7] + (R32(e, 6) ^ R32(e, 11) ^ R32(e, 25)) + ((e & a[5]) ^ (~e & a[6])) + (uint32_t)(k64(&K512[i]) >> 32) + wi;
      uint32_t t2 = (R32(a[0], 2) ^ R32(a[0], 13) ^ R32(a[0], 22)) + ((a[0] & a[1]) ^ (a[0] & a[2]) ^ (a[1] & a[2]));
      for (int j = 7; j > 0; --j) a[j] = a[j - 1];
      a[4] += t1;
      a[0] = t1 + t2;
    }
    for (int i = 0; i < 8; ++i) h[i] += a[i];
  }
  for (int i = 0; i < 8; ++i) for (int j = 0; j < 4; ++j) out[4 * i + j] = (uint8_t)(h[i] >> (24 - 8 * j));
}

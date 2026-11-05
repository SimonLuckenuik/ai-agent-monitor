// The OpenClaw node's byte-level helpers (spec 041): base64url and WebSocket client
// frame headers. Plain C++, built as it is by the host unit tests.
#pragma once
#include <stddef.h>
#include <stdint.h>

// base64url without padding; out needs 4 * ceil(n / 3) + 1 bytes. Returns the length.
size_t b64urlEncode(char* out, const uint8_t* in, size_t n);
// base64url or base64 (padding and whitespace ignored). Returns the length, -1 when the
// text is not base64 or does not fit in max.
int b64urlDecode(uint8_t* out, size_t max, const char* in, size_t n);

// A client's text frame header (FIN, masked) for len bytes of payload: 6 or 8 bytes.
size_t wsTextHeader(uint8_t h[8], size_t len, const uint8_t mask[4]);
// Masks (or unmasks) data in place; offset is data's position in the payload.
void wsMask(uint8_t* data, size_t n, const uint8_t mask[4], size_t offset);

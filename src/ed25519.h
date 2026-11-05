// Ed25519 signing, SHA-512 and SHA-256 for the OpenClaw node (spec 041).
// Plain C++ (no Arduino), so the host unit tests build it as it is.
#pragma once
#include <stddef.h>
#include <stdint.h>

// The public key of a 32-byte seed. false: no memory for the working space (about 3 KB,
// taken from the heap and given back: the 4 KB stack has no room for it).
bool ed25519Public(uint8_t pub[32], const uint8_t seed[32]);
// The signature of msg by the key of seed (pub is that seed's public key).
bool ed25519Sign(uint8_t sig[64], const uint8_t* msg, size_t len, const uint8_t seed[32], const uint8_t pub[32]);

void sha256(uint8_t out[32], const uint8_t* data, size_t len);

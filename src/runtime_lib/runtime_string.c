#include "runtime_string.h"

uint32_t runtime_hash_string(const char* chars, size_t length) {
    // FNV-1a hash algorithm
    // FNV offset basis for 32-bit hash
    uint32_t hash = 2166136261u;

    for (size_t i = 0; i < length; i++) {
        // XOR with byte value
        hash ^= (uint8_t)chars[i];
        // Multiply by FNV prime
        hash *= 16777619;
    }

    return hash;
}

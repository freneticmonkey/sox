#ifndef SOX_STRING_H
#define SOX_STRING_H

#include <stdint.h>

static uint32_t l_hash_string(const char* key, int length) {
    uint32_t hash = 2166136261u;
    for (int i = 0; i < length; i++) {
        hash ^= (uint8_t)key[i];
        hash *= 16777619;
    }
return hash;
}

#endif
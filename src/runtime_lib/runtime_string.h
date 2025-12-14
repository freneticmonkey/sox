#ifndef SOX_RUNTIME_STRING_H
#define SOX_RUNTIME_STRING_H

#include <stdint.h>
#include <stddef.h>

/**
 * Computes FNV-1a hash for a string.
 *
 * FNV-1a (Fowler-Noll-Vo hash function, variant 1a) is a simple, fast,
 * non-cryptographic hash function with good distribution properties.
 *
 * Algorithm:
 * - Start with FNV offset basis (2166136261u)
 * - For each byte: XOR with byte, then multiply by FNV prime (16777619)
 * - Return 32-bit hash value
 *
 * @param chars Pointer to character data
 * @param length Number of characters to hash
 * @return 32-bit hash value
 */
uint32_t runtime_hash_string(const char* chars, size_t length);

#endif

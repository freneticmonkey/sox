#ifndef SOX_ELF_READER_H
#define SOX_ELF_READER_H

#include <stdint.h>
#include <stddef.h>
#include "linker_core.h"

/*
 * ELF Object File Reader
 *
 * This module provides ELF-specific object file parsing functionality.
 * It reads ELF64 object files and converts them into the unified
 * linker_object_t representation.
 *
 * Phase 1.3: ELF object file reader (stub for Phase 1.2)
 */

/**
 * Read an ELF object file and parse it into a linker_object_t structure.
 * Called by linker_read_object() when an ELF file is detected.
 *
 * @param filename Path to ELF object file (for error messages)
 * @param data Raw ELF file data
 * @param size Size of file data in bytes
 * @return Pointer to populated linker_object_t on success, NULL on failure
 */
linker_object_t* elf_read_object(const char* filename, const uint8_t* data, size_t size);

#endif

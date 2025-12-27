#ifndef SOX_MACHO_READER_H
#define SOX_MACHO_READER_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "linker_core.h"

/*
 * Mach-O Object File Reader
 *
 * This module provides Mach-O-specific object file parsing functionality.
 * It reads Mach-O 64-bit object files and converts them into the unified
 * linker_object_t representation.
 *
 * Phase 1.4: Complete Mach-O object file reader
 */

/**
 * Read a Mach-O object file and parse it into a linker_object_t structure.
 * Called by linker_read_object() when a Mach-O file is detected.
 *
 * @param filename Path to Mach-O object file (for error messages)
 * @param data Raw Mach-O file data
 * @param size Size of file data in bytes
 * @return Pointer to populated linker_object_t on success, NULL on failure
 */
linker_object_t* macho_read_object(const char* filename, const uint8_t* data, size_t size);

/**
 * Parse Mach-O load commands and extract segment/section information.
 * Internal helper function.
 *
 * @param obj Linker object to populate
 * @param data Raw Mach-O file data
 * @param size File size
 * @param ncmds Number of load commands
 * @param sizeofcmds Total size of load commands
 * @return true on success, false on failure
 */
bool macho_parse_load_commands(linker_object_t* obj, const uint8_t* data, size_t size,
                                uint32_t ncmds, uint32_t sizeofcmds);

/**
 * Parse sections from a Mach-O segment command.
 * Internal helper function.
 *
 * @param obj Linker object to populate
 * @param data Raw Mach-O file data
 * @param size File size
 * @param seg_cmd Pointer to segment_command_64 structure
 * @return true on success, false on failure
 */
bool macho_parse_sections(linker_object_t* obj, const uint8_t* data, size_t size,
                           const void* seg_cmd);

/**
 * Parse Mach-O symbol table (nlist_64).
 * Internal helper function.
 *
 * @param obj Linker object to populate
 * @param data Raw Mach-O file data
 * @param size File size
 * @param symoff Symbol table file offset
 * @param nsyms Number of symbols
 * @param stroff String table file offset
 * @param strsize String table size
 * @return true on success, false on failure
 */
bool macho_parse_symbols(linker_object_t* obj, const uint8_t* data, size_t size,
                          uint32_t symoff, uint32_t nsyms,
                          uint32_t stroff, uint32_t strsize);

/**
 * Parse Mach-O relocations for all sections.
 * Internal helper function.
 *
 * @param obj Linker object to populate
 * @param data Raw Mach-O file data
 * @param size File size
 * @return true on success, false on failure
 */
bool macho_parse_relocations(linker_object_t* obj, const uint8_t* data, size_t size);

/**
 * Map Mach-O ARM64 relocation type to unified relocation type.
 * Internal helper function.
 *
 * @param macho_type Mach-O relocation type constant
 * @return Unified relocation_type_t value
 */
relocation_type_t macho_map_relocation_type(uint32_t macho_type);

#endif

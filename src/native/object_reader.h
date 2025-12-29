#ifndef SOX_OBJECT_READER_H
#define SOX_OBJECT_READER_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "linker_core.h"

/*
 * Object File Reader Interface
 *
 * This module provides a unified interface for reading platform-specific
 * object files (ELF, Mach-O, etc.) into a common linker_object_t representation.
 * It handles format detection and dispatches to platform-specific readers.
 *
 * Phase 1.2: Object file reader interface and format detection
 */

/*
 * Main API - High-level functions for reading object files
 */

/**
 * Read an object file and parse it into a unified linker_object_t structure.
 * Automatically detects the file format (ELF, Mach-O) and uses the appropriate
 * platform-specific reader.
 *
 * @param filename Path to the object file to read
 * @return Pointer to populated linker_object_t on success, NULL on failure
 */
linker_object_t* linker_read_object(const char* filename);

/**
 * Free a linker_object_t structure and all associated memory.
 * This is the proper way to clean up objects created by linker_read_object().
 *
 * @param obj Object to free (can be NULL)
 */
void linker_free_object(linker_object_t* obj);

/**
 * Detect the format of an object file by reading its magic number.
 * Does not parse the entire file - only reads the first few bytes.
 *
 * @param filename Path to the object file
 * @return Platform format enum (PLATFORM_FORMAT_UNKNOWN on error or unsupported format)
 */
platform_format_t linker_detect_format(const char* filename);

/*
 * Low-level file I/O utilities
 */

/**
 * Read an entire file into a newly allocated buffer.
 * Caller is responsible for freeing the returned buffer.
 *
 * @param filename Path to file
 * @param out_size Pointer to receive file size in bytes
 * @return Pointer to file data on success, NULL on failure
 */
uint8_t* linker_read_file(const char* filename, size_t* out_size);

/**
 * Read exactly 'size' bytes from a file starting at 'offset'.
 * Caller is responsible for freeing the returned buffer.
 *
 * @param filename Path to file
 * @param offset Byte offset to start reading from
 * @param size Number of bytes to read
 * @return Pointer to data on success, NULL on failure
 */
uint8_t* linker_read_file_range(const char* filename, size_t offset, size_t size);

/*
 * Platform-specific reader interfaces
 * These are implemented in separate modules (elf_reader.c, macho_reader.c)
 * and are called by linker_read_object() after format detection.
 */

/**
 * Read an ELF object file into a linker_object_t structure.
 * Internal function called by linker_read_object() for ELF files.
 *
 * @param filename Path to ELF object file
 * @param data Raw file data
 * @param size Size of file data in bytes
 * @return Pointer to populated linker_object_t on success, NULL on failure
 */
linker_object_t* elf_read_object(const char* filename, const uint8_t* data, size_t size);

/**
 * Read a Mach-O object file into a linker_object_t structure.
 * Internal function called by linker_read_object() for Mach-O files.
 *
 * @param filename Path to Mach-O object file
 * @param data Raw file data
 * @param size Size of file data in bytes
 * @return Pointer to populated linker_object_t on success, NULL on failure
 */
linker_object_t* macho_read_object(const char* filename, const uint8_t* data, size_t size);

/*
 * Magic number constants for format detection
 */

/* ELF magic: 0x7f 'E' 'L' 'F' */
#define ELF_MAGIC_0 0x7f
#define ELF_MAGIC_1 'E'
#define ELF_MAGIC_2 'L'
#define ELF_MAGIC_3 'F'

/* Mach-O 64-bit magic (little-endian) */
#define MACH_O_MAGIC_64 0xfeedfacf

/* Mach-O 64-bit magic (big-endian) */
#define MACH_O_CIGAM_64 0xcffaedfe

/* Minimum bytes needed for format detection */
#define MAGIC_NUMBER_SIZE 4

#endif

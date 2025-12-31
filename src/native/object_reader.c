#include "object_reader.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * Object File Reader Implementation
 *
 * This module implements the unified object file reading interface.
 * It provides:
 * - Format detection by magic number
 * - File I/O utilities
 * - Dispatcher to platform-specific readers
 *
 * Phase 1.2: Object file reader interface and format detection
 */

/*
 * File I/O Utilities
 */

uint8_t* linker_read_file(const char* filename, size_t* out_size) {
    if (filename == NULL || out_size == NULL) {
        fprintf(stderr, "Linker error: Invalid arguments to linker_read_file\n");
        return NULL;
    }

    FILE* file = fopen(filename, "rb");
    if (file == NULL) {
        fprintf(stderr, "Linker error: Could not open file '%s'\n", filename);
        return NULL;
    }

    /* Determine file size */
    if (fseek(file, 0, SEEK_END) != 0) {
        fprintf(stderr, "Linker error: Failed to seek to end of file '%s'\n", filename);
        fclose(file);
        return NULL;
    }

    long file_size = ftell(file);
    if (file_size < 0) {
        fprintf(stderr, "Linker error: Failed to determine size of file '%s'\n", filename);
        fclose(file);
        return NULL;
    }

    if (file_size == 0) {
        fprintf(stderr, "Linker error: File '%s' is empty\n", filename);
        fclose(file);
        return NULL;
    }

    rewind(file);

    /* Allocate buffer for entire file */
    uint8_t* buffer = malloc(file_size);
    if (buffer == NULL) {
        fprintf(stderr, "Linker error: Failed to allocate %ld bytes for file '%s'\n",
                file_size, filename);
        fclose(file);
        return NULL;
    }

    /* Read entire file */
    size_t bytes_read = fread(buffer, 1, file_size, file);
    if (bytes_read != (size_t)file_size) {
        fprintf(stderr, "Linker error: Failed to read file '%s' (read %zu of %ld bytes)\n",
                filename, bytes_read, file_size);
        free(buffer);
        fclose(file);
        return NULL;
    }

    fclose(file);
    *out_size = file_size;
    return buffer;
}

uint8_t* linker_read_file_range(const char* filename, size_t offset, size_t size) {
    if (filename == NULL) {
        fprintf(stderr, "Linker error: Invalid filename argument to linker_read_file_range\n");
        return NULL;
    }

    if (size == 0) {
        fprintf(stderr, "Linker error: Invalid size argument to linker_read_file_range\n");
        return NULL;
    }

    FILE* file = fopen(filename, "rb");
    if (file == NULL) {
        fprintf(stderr, "Linker error: Could not open file '%s'\n", filename);
        return NULL;
    }

    /* Seek to offset */
    if (fseek(file, offset, SEEK_SET) != 0) {
        fprintf(stderr, "Linker error: Failed to seek to offset %zu in file '%s'\n",
                offset, filename);
        fclose(file);
        return NULL;
    }

    /* Allocate buffer */
    uint8_t* buffer = malloc(size);
    if (buffer == NULL) {
        fprintf(stderr, "Linker error: Failed to allocate %zu bytes\n", size);
        fclose(file);
        return NULL;
    }

    /* Read requested bytes */
    size_t bytes_read = fread(buffer, 1, size, file);
    if (bytes_read != size) {
        fprintf(stderr, "Linker error: Failed to read %zu bytes from file '%s' at offset %zu (read %zu)\n",
                size, filename, offset, bytes_read);
        free(buffer);
        fclose(file);
        return NULL;
    }

    fclose(file);
    return buffer;
}

/*
 * Format Detection
 */

platform_format_t linker_detect_format(const char* filename) {
    if (filename == NULL) {
        fprintf(stderr, "Linker error: NULL filename passed to linker_detect_format\n");
        return PLATFORM_FORMAT_UNKNOWN;
    }

    /* Read just the first 4 bytes for magic number detection */
    uint8_t* magic = linker_read_file_range(filename, 0, MAGIC_NUMBER_SIZE);
    if (magic == NULL) {
        /* Error already printed by linker_read_file_range */
        return PLATFORM_FORMAT_UNKNOWN;
    }

    platform_format_t format = PLATFORM_FORMAT_UNKNOWN;

    /* Check for ELF magic: 0x7f 'E' 'L' 'F' */
    if (magic[0] == ELF_MAGIC_0 &&
        magic[1] == ELF_MAGIC_1 &&
        magic[2] == ELF_MAGIC_2 &&
        magic[3] == ELF_MAGIC_3) {
        format = PLATFORM_FORMAT_ELF;
    }
    /* Check for Mach-O 64-bit magic (little-endian): 0xCF 0xFA 0xED 0xFE */
    else if (magic[0] == 0xcf &&
             magic[1] == 0xfa &&
             magic[2] == 0xed &&
             magic[3] == 0xfe) {
        format = PLATFORM_FORMAT_MACH_O;
    }
    /* Check for Mach-O 64-bit magic (big-endian): 0xFE 0xED 0xFA 0xCF */
    else if (magic[0] == 0xfe &&
             magic[1] == 0xed &&
             magic[2] == 0xfa &&
             magic[3] == 0xcf) {
        format = PLATFORM_FORMAT_MACH_O;
    }
    /* Otherwise unknown/unsupported format */
    else {
        fprintf(stderr, "Linker error: Unknown or unsupported object file format in '%s'\n", filename);
        fprintf(stderr, "  Magic bytes: 0x%02x 0x%02x 0x%02x 0x%02x\n",
                magic[0], magic[1], magic[2], magic[3]);
    }

    free(magic);
    return format;
}

/*
 * Main Object Reading API
 */

linker_object_t* linker_read_object(const char* filename) {
    if (filename == NULL) {
        fprintf(stderr, "Linker error: NULL filename passed to linker_read_object\n");
        return NULL;
    }

    /* Detect file format */
    platform_format_t format = linker_detect_format(filename);
    if (format == PLATFORM_FORMAT_UNKNOWN) {
        /* Error already printed by linker_detect_format */
        return NULL;
    }

    /* Read entire file into memory */
    size_t file_size;
    uint8_t* file_data = linker_read_file(filename, &file_size);
    if (file_data == NULL) {
        /* Error already printed by linker_read_file */
        return NULL;
    }

    /* Dispatch to platform-specific reader */
    linker_object_t* obj = NULL;

    switch (format) {
        case PLATFORM_FORMAT_ELF:
            obj = elf_read_object(filename, file_data, file_size);
            break;

        case PLATFORM_FORMAT_MACH_O:
            obj = macho_read_object(filename, file_data, file_size);
            break;

        case PLATFORM_FORMAT_PE_COFF:
            fprintf(stderr, "Linker error: PE/COFF format not yet supported\n");
            break;

        default:
            fprintf(stderr, "Linker error: Unsupported format %d for file '%s'\n",
                    format, filename);
            break;
    }

    /* Free file data - the platform-specific reader has copied what it needs */
    free(file_data);

    /* Validate that the object was successfully parsed */
    if (obj == NULL) {
        fprintf(stderr, "Linker error: Failed to parse object file '%s'\n", filename);
        return NULL;
    }

    if (getenv("SOX_MACHO_GOT_DEBUG")) {
        fprintf(stderr,
                "[GOT-DEBUG] Read object %s format=%s sections=%d symbols=%d relocs=%d\n",
                filename,
                platform_format_name(obj->format),
                obj->section_count,
                obj->symbol_count,
                obj->relocation_count);
    }

    /* Ensure format was set correctly */
    if (obj->format != format) {
        fprintf(stderr, "Linker warning: Format mismatch in object file '%s' (detected %s, parsed as %s)\n",
                filename, platform_format_name(format), platform_format_name(obj->format));
    }

    return obj;
}

void linker_free_object(linker_object_t* obj) {
    if (obj == NULL) {
        return;
    }

    /* Free sections */
    if (obj->sections != NULL) {
        for (int i = 0; i < obj->section_count; i++) {
            linker_section_free(&obj->sections[i]);
        }
        free(obj->sections);
    }

    /* Free symbols */
    if (obj->symbols != NULL) {
        for (int i = 0; i < obj->symbol_count; i++) {
            linker_symbol_free(&obj->symbols[i]);
        }
        free(obj->symbols);
    }

    /* Free relocations */
    if (obj->relocations != NULL) {
        free(obj->relocations);
    }

    /* Free raw data if present */
    if (obj->raw_data != NULL) {
        free(obj->raw_data);
    }

    /* Free filename */
    if (obj->filename != NULL) {
        free(obj->filename);
    }

    /* Free the object itself */
    free(obj);
}

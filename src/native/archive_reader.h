#ifndef SOX_ARCHIVE_READER_H
#define SOX_ARCHIVE_READER_H

#include <stdint.h>
#include <stdbool.h>
#include "linker_core.h"

/*
 * Unix Archive (.a) Reader
 *
 * This module reads Unix archive files (.a static libraries) and extracts
 * object files for linking. Archive files contain multiple object files
 * with a simple header format.
 *
 * Archive format:
 * - Magic signature: "!<arch>\n" (8 bytes)
 * - Series of file entries, each with:
 *   - 60-byte header (filename, size, etc.)
 *   - File data (padded to even boundary)
 */

/* Opaque archive handle */
typedef struct archive_reader_t archive_reader_t;

/* Archive entry information */
typedef struct {
    char name[256];          /* Entry filename */
    uint64_t size;           /* Entry size in bytes */
    uint8_t* data;           /* Entry data (valid until next call) */
} archive_entry_t;

/*
 * Archive Reader API
 */

/* Open an archive file for reading */
archive_reader_t* archive_open(const char* filename);

/* Read next entry from archive (returns NULL at end) */
archive_entry_t* archive_next_entry(archive_reader_t* archive);

/* Close archive and free resources */
void archive_close(archive_reader_t* archive);

/* Extract all object files from archive and add to linker context */
bool archive_extract_objects(const char* archive_path,
                             linker_context_t* context,
                             bool verbose);

#endif /* SOX_ARCHIVE_READER_H */

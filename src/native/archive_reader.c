#include "archive_reader.h"
#include "object_reader.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * Unix Archive (.a) Reader Implementation
 *
 * Reads static library archive files in Unix ar format.
 */

#define AR_MAGIC "!<arch>\n"
#define AR_MAGIC_LEN 8
#define AR_HEADER_LEN 60
#define AR_FMAG "`\n"

/* Archive file header (60 bytes) */
struct ar_hdr {
    char ar_name[16];    /* File name */
    char ar_date[12];    /* Modification timestamp */
    char ar_uid[6];      /* User ID */
    char ar_gid[6];      /* Group ID */
    char ar_mode[8];     /* File mode (octal) */
    char ar_size[10];    /* File size (decimal) */
    char ar_fmag[2];     /* Header magic: "`\n" */
};

/* Archive reader state */
struct archive_reader_t {
    FILE* file;              /* Archive file handle */
    char* filename;          /* Archive filename */
    uint8_t* buffer;         /* Buffer for current entry */
    size_t buffer_size;      /* Buffer size */
    archive_entry_t entry;   /* Current entry info */
    bool eof;                /* End of file reached */
};

/* Helper: parse decimal number from fixed-width field */
static uint64_t parse_decimal(const char* field, size_t len) {
    char buf[32];
    if (len >= sizeof(buf)) {
        len = sizeof(buf) - 1;
    }
    memcpy(buf, field, len);
    buf[len] = '\0';

    /* Trim trailing spaces */
    for (int i = len - 1; i >= 0; i--) {
        if (buf[i] == ' ' || buf[i] == '\0') {
            buf[i] = '\0';
        } else {
            break;
        }
    }

    return strtoull(buf, NULL, 10);
}

/* Helper: extract filename from header */
static void extract_filename(char* dest, const char* src, size_t max_len) {
    size_t len = 0;

    /* Find end of filename (space or slash) */
    while (len < 16 && src[len] != ' ' && src[len] != '/' && src[len] != '\0') {
        dest[len] = src[len];
        len++;
    }
    dest[len] = '\0';
}

archive_reader_t* archive_open(const char* filename) {
    if (!filename) {
        return NULL;
    }

    /* Allocate archive reader */
    archive_reader_t* ar = (archive_reader_t*)malloc(sizeof(archive_reader_t));
    if (!ar) {
        fprintf(stderr, "Archive error: Failed to allocate reader\n");
        return NULL;
    }

    memset(ar, 0, sizeof(archive_reader_t));
    ar->filename = strdup(filename);

    /* Open archive file */
    ar->file = fopen(filename, "rb");
    if (!ar->file) {
        fprintf(stderr, "Archive error: Failed to open %s\n", filename);
        free(ar->filename);
        free(ar);
        return NULL;
    }

    /* Read and validate magic signature */
    char magic[AR_MAGIC_LEN];
    if (fread(magic, 1, AR_MAGIC_LEN, ar->file) != AR_MAGIC_LEN) {
        fprintf(stderr, "Archive error: Failed to read magic from %s\n", filename);
        fclose(ar->file);
        free(ar->filename);
        free(ar);
        return NULL;
    }

    if (memcmp(magic, AR_MAGIC, AR_MAGIC_LEN) != 0) {
        fprintf(stderr, "Archive error: Invalid magic signature in %s\n", filename);
        fclose(ar->file);
        free(ar->filename);
        free(ar);
        return NULL;
    }

    return ar;
}

archive_entry_t* archive_next_entry(archive_reader_t* ar) {
    if (!ar || ar->eof) {
        return NULL;
    }

    /* Read entry header */
    struct ar_hdr header;
    size_t read_count = fread(&header, 1, AR_HEADER_LEN, ar->file);

    /* Check for EOF */
    if (read_count == 0) {
        ar->eof = true;
        return NULL;
    }

    if (read_count != AR_HEADER_LEN) {
        fprintf(stderr, "Archive error: Incomplete header in %s\n", ar->filename);
        ar->eof = true;
        return NULL;
    }

    /* Validate header magic */
    if (memcmp(header.ar_fmag, AR_FMAG, 2) != 0) {
        fprintf(stderr, "Archive error: Invalid header magic in %s\n", ar->filename);
        ar->eof = true;
        return NULL;
    }

    /* Parse entry size (total size including filename if BSD format) */
    uint64_t total_size = parse_decimal(header.ar_size, sizeof(header.ar_size));
    ar->entry.size = total_size;

    /* Check for BSD extended filename format (#1/NN) */
    int filename_len = 0;
    if (header.ar_name[0] == '#' && header.ar_name[1] == '1' && header.ar_name[2] == '/') {
        /* BSD extended filename - length follows #1/ */
        filename_len = (int)parse_decimal(header.ar_name + 3, 13);
    }

    /* Allocate buffer for entry data */
    if (ar->entry.size > ar->buffer_size) {
        free(ar->buffer);
        ar->buffer_size = ar->entry.size;
        ar->buffer = (uint8_t*)malloc(ar->buffer_size);
        if (!ar->buffer) {
            fprintf(stderr, "Archive error: Failed to allocate %llu bytes\n",
                    (unsigned long long)ar->entry.size);
            ar->eof = true;
            return NULL;
        }
    }

    /* Read entry data */
    if (fread(ar->buffer, 1, ar->entry.size, ar->file) != ar->entry.size) {
        fprintf(stderr, "Archive error: Failed to read entry data\n");
        ar->eof = true;
        return NULL;
    }

    /* Extract filename */
    if (filename_len > 0) {
        /* BSD extended filename - read from data buffer */
        if (filename_len >= (int)sizeof(ar->entry.name)) {
            filename_len = sizeof(ar->entry.name) - 1;
        }
        memcpy(ar->entry.name, ar->buffer, filename_len);
        ar->entry.name[filename_len] = '\0';

        /* Actual file data starts after filename */
        ar->entry.data = ar->buffer + filename_len;
        ar->entry.size -= filename_len;
    } else {
        /* Standard filename format */
        extract_filename(ar->entry.name, header.ar_name, sizeof(ar->entry.name));
        ar->entry.data = ar->buffer;
    }

    /* Skip padding to even boundary (based on total size from header) */
    if (total_size % 2 != 0) {
        fseek(ar->file, 1, SEEK_CUR);
    }

    return &ar->entry;
}

void archive_close(archive_reader_t* ar) {
    if (!ar) {
        return;
    }

    if (ar->file) {
        fclose(ar->file);
    }
    free(ar->filename);
    free(ar->buffer);
    free(ar);
}

bool archive_extract_objects(const char* archive_path,
                             linker_context_t* context,
                             bool verbose) {
    if (!archive_path || !context) {
        return false;
    }

    if (verbose) {
        fprintf(stderr, "[ARCHIVE] Extracting objects from %s\n", archive_path);
    }

    /* Open archive */
    archive_reader_t* ar = archive_open(archive_path);
    if (!ar) {
        return false;
    }

    int object_count = 0;
    archive_entry_t* entry;

    /* Extract all object file entries */
    while ((entry = archive_next_entry(ar)) != NULL) {
        /* Skip special entries (symbol table, etc.) */
        if (entry->name[0] == '_' && entry->name[1] == '_') {
            if (verbose) {
                fprintf(stderr, "[ARCHIVE] Skipping special entry: %s\n", entry->name);
            }
            continue;
        }

        /* Skip non-object files */
        size_t name_len = strlen(entry->name);
        if (name_len < 2 || strcmp(entry->name + name_len - 2, ".o") != 0) {
            if (verbose) {
                fprintf(stderr, "[ARCHIVE] Skipping non-object entry: %s\n", entry->name);
            }
            continue;
        }

        if (verbose) {
            fprintf(stderr, "[ARCHIVE] Extracting object: %s (%llu bytes)\n",
                    entry->name, (unsigned long long)entry->size);
        }

        /* Write entry to temporary file */
        char temp_path[512];
        snprintf(temp_path, sizeof(temp_path), "/tmp/sox_archive_%s", entry->name);

        FILE* temp_file = fopen(temp_path, "wb");
        if (!temp_file) {
            fprintf(stderr, "Archive error: Failed to create temp file %s\n", temp_path);
            archive_close(ar);
            return false;
        }

        if (fwrite(entry->data, 1, entry->size, temp_file) != entry->size) {
            fprintf(stderr, "Archive error: Failed to write temp file\n");
            fclose(temp_file);
            archive_close(ar);
            return false;
        }
        fclose(temp_file);

        /* Read object file */
        linker_object_t* obj = linker_read_object(temp_path);
        if (!obj) {
            fprintf(stderr, "Archive error: Failed to read object %s\n", entry->name);
            remove(temp_path);
            archive_close(ar);
            return false;
        }

        /* Add to context */
        if (!linker_context_add_object(context, obj)) {
            fprintf(stderr, "Archive error: Failed to add object to context\n");
            linker_object_free(obj);
            remove(temp_path);
            archive_close(ar);
            return false;
        }

        /* Clean up temp file */
        remove(temp_path);
        object_count++;
    }

    archive_close(ar);

    if (verbose) {
        fprintf(stderr, "[ARCHIVE] Extracted %d object file(s)\n", object_count);
    }

    return object_count > 0;
}

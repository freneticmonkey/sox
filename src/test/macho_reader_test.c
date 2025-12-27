/*
 * Unit tests for macho_reader.c
 *
 * Tests Mach-O object file parsing functionality including:
 * - Round-trip testing (write -> read -> verify)
 * - Section parsing and mapping
 * - Symbol table parsing
 * - Relocation parsing and type mapping
 * - ARM64-specific functionality
 */

#include "../native/macho_reader.h"
#include "../native/macho_writer.h"
#include "../native/linker_core.h"
#include "../native/arm64_encoder.h"
#include "../../ext/munit/munit.h"
#include <string.h>
#include <stdio.h>
#include <unistd.h>

/* Helper: Read file into memory */
static uint8_t* read_file(const char* filename, size_t* out_size) {
    FILE* fp = fopen(filename, "rb");
    if (!fp) {
        return NULL;
    }

    fseek(fp, 0, SEEK_END);
    size_t size = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    uint8_t* data = (uint8_t*)malloc(size);
    if (!data) {
        fclose(fp);
        return NULL;
    }

    size_t bytes_read = fread(data, 1, size, fp);
    fclose(fp);

    if (bytes_read != size) {
        free(data);
        return NULL;
    }

    *out_size = size;
    return data;
}

/* Test: Round-trip parsing of simple object file */
static MunitResult test_macho_roundtrip_simple(const MunitParameter params[], void* data) {
    (void)params;
    (void)data;

    const char* temp_file = "/tmp/test_macho_simple.o";

    /* Create a simple object file with code */
    uint8_t code[] = {
        0xd2, 0x80, 0x00, 0x00,  /* mov x0, #0 */
        0xc0, 0x03, 0x5f, 0xd6   /* ret */
    };

    bool write_ok = macho_create_object_file(temp_file, code, sizeof(code), "test_func",
                                              CPU_TYPE_ARM64, CPU_SUBTYPE_ARM64_ALL);
    munit_assert_true(write_ok);

    /* Read the file back */
    size_t file_size = 0;
    uint8_t* file_data = read_file(temp_file, &file_size);
    munit_assert_not_null(file_data);
    munit_assert_size(file_size, >, 0);

    /* Parse the Mach-O file */
    linker_object_t* obj = macho_read_object(temp_file, file_data, file_size);
    munit_assert_not_null(obj);

    /* Verify object properties */
    munit_assert_string_equal(obj->filename, temp_file);
    munit_assert_int(obj->format, ==, PLATFORM_FORMAT_MACH_O);

    /* Should have at least one section (__text) */
    munit_assert_int(obj->section_count, >=, 1);

    /* Find __text section */
    linker_section_t* text_section = NULL;
    for (int i = 0; i < obj->section_count; i++) {
        if (strcmp(obj->sections[i].name, "__text") == 0) {
            text_section = &obj->sections[i];
            break;
        }
    }
    munit_assert_not_null(text_section);
    munit_assert_int(text_section->type, ==, SECTION_TYPE_TEXT);
    munit_assert_size(text_section->size, ==, sizeof(code));
    munit_assert_not_null(text_section->data);

    /* Verify code data matches */
    munit_assert_memory_equal(sizeof(code), text_section->data, code);

    /* Should have at least one symbol (test_func) */
    munit_assert_int(obj->symbol_count, >=, 1);

    /* Find test_func symbol (without underscore prefix) */
    linker_symbol_t* func_symbol = NULL;
    for (int i = 0; i < obj->symbol_count; i++) {
        if (strcmp(obj->symbols[i].name, "test_func") == 0) {
            func_symbol = &obj->symbols[i];
            break;
        }
    }
    munit_assert_not_null(func_symbol);
    munit_assert_int(func_symbol->binding, ==, SYMBOL_BINDING_GLOBAL);
    munit_assert_true(func_symbol->is_defined);
    munit_assert_int(func_symbol->section_index, >=, 0);

    /* Cleanup */
    linker_object_free(obj);
    free(file_data);
    unlink(temp_file);

    return MUNIT_OK;
}

/* Test: Parse object file with relocations */
static MunitResult test_macho_roundtrip_with_relocations(const MunitParameter params[], void* data) {
    (void)params;
    (void)data;

    const char* temp_file = "/tmp/test_macho_relocs.o";

    /* Create code with a function call (requires relocation) */
    uint8_t code[] = {
        0x00, 0x00, 0x00, 0x94,  /* bl <placeholder> */
        0xc0, 0x03, 0x5f, 0xd6   /* ret */
    };

    /* Create relocation for the BL instruction */
    arm64_relocation_t reloc = {
        .offset = 0,  /* First instruction */
        .type = ARM64_RELOC_CALL26,
        .symbol = NULL,
        .addend = 0
    };
    reloc.symbol = strdup("external_func");

    bool write_ok = macho_create_object_file_with_arm64_relocs(
        temp_file, code, sizeof(code), "test_caller",
        CPU_TYPE_ARM64, CPU_SUBTYPE_ARM64_ALL,
        (const arm64_relocation*)&reloc, 1
    );
    munit_assert_true(write_ok);

    /* Read the file back */
    size_t file_size = 0;
    uint8_t* file_data = read_file(temp_file, &file_size);
    munit_assert_not_null(file_data);

    /* Parse the Mach-O file */
    linker_object_t* obj = macho_read_object(temp_file, file_data, file_size);
    munit_assert_not_null(obj);

    /* Should have relocations */
    munit_assert_int(obj->relocation_count, >=, 1);

    /* Find the BL relocation */
    linker_relocation_t* bl_reloc = NULL;
    for (int i = 0; i < obj->relocation_count; i++) {
        if (obj->relocations[i].type == RELOC_ARM64_CALL26) {
            bl_reloc = &obj->relocations[i];
            break;
        }
    }
    munit_assert_not_null(bl_reloc);
    munit_assert_uint64(bl_reloc->offset, ==, 0);  /* First instruction */

    /* Should have external_func symbol (undefined) */
    linker_symbol_t* extern_symbol = NULL;
    for (int i = 0; i < obj->symbol_count; i++) {
        if (strcmp(obj->symbols[i].name, "external_func") == 0) {
            extern_symbol = &obj->symbols[i];
            break;
        }
    }
    munit_assert_not_null(extern_symbol);
    munit_assert_false(extern_symbol->is_defined);
    munit_assert_int(extern_symbol->section_index, ==, -1);

    /* Cleanup */
    free(reloc.symbol);
    linker_object_free(obj);
    free(file_data);
    unlink(temp_file);

    return MUNIT_OK;
}

/* Test: Relocation type mapping */
static MunitResult test_macho_relocation_type_mapping(const MunitParameter params[], void* data) {
    (void)params;
    (void)data;

    /* Test ARM64 relocation type mapping */
    munit_assert_int(macho_map_relocation_type(2), ==, RELOC_ARM64_CALL26);       /* BRANCH26 */
    munit_assert_int(macho_map_relocation_type(3), ==, RELOC_ARM64_ADR_PREL_PG_HI21);  /* PAGE21 */
    munit_assert_int(macho_map_relocation_type(4), ==, RELOC_ARM64_ADD_ABS_LO12_NC);   /* PAGEOFF12 */
    munit_assert_int(macho_map_relocation_type(0), ==, RELOC_ARM64_ABS64);        /* UNSIGNED */

    /* Test unknown type returns RELOC_NONE */
    munit_assert_int(macho_map_relocation_type(99), ==, RELOC_NONE);

    return MUNIT_OK;
}

/* Test: Section type mapping */
static MunitResult test_macho_section_type_mapping(const MunitParameter params[], void* data) {
    (void)params;
    (void)data;

    const char* temp_file = "/tmp/test_macho_sections.o";

    /* Create a simple object file */
    uint8_t code[] = {0xc0, 0x03, 0x5f, 0xd6};  /* ret */

    bool write_ok = macho_create_object_file(temp_file, code, sizeof(code), "test",
                                              CPU_TYPE_ARM64, CPU_SUBTYPE_ARM64_ALL);
    munit_assert_true(write_ok);

    /* Read and parse */
    size_t file_size = 0;
    uint8_t* file_data = read_file(temp_file, &file_size);
    munit_assert_not_null(file_data);

    linker_object_t* obj = macho_read_object(temp_file, file_data, file_size);
    munit_assert_not_null(obj);

    /* Verify __text section is mapped to SECTION_TYPE_TEXT */
    linker_section_t* text_section = NULL;
    for (int i = 0; i < obj->section_count; i++) {
        if (strcmp(obj->sections[i].name, "__text") == 0) {
            text_section = &obj->sections[i];
            break;
        }
    }
    munit_assert_not_null(text_section);
    munit_assert_int(text_section->type, ==, SECTION_TYPE_TEXT);

    /* Cleanup */
    linker_object_free(obj);
    free(file_data);
    unlink(temp_file);

    return MUNIT_OK;
}

/* Test: Symbol name underscore removal */
static MunitResult test_macho_symbol_underscore_removal(const MunitParameter params[], void* data) {
    (void)params;
    (void)data;

    const char* temp_file = "/tmp/test_macho_symbols.o";

    /* Create object file */
    uint8_t code[] = {0xc0, 0x03, 0x5f, 0xd6};  /* ret */

    bool write_ok = macho_create_object_file(temp_file, code, sizeof(code), "my_function",
                                              CPU_TYPE_ARM64, CPU_SUBTYPE_ARM64_ALL);
    munit_assert_true(write_ok);

    /* Read and parse */
    size_t file_size = 0;
    uint8_t* file_data = read_file(temp_file, &file_size);
    munit_assert_not_null(file_data);

    linker_object_t* obj = macho_read_object(temp_file, file_data, file_size);
    munit_assert_not_null(obj);

    /* Find symbol - should NOT have underscore prefix */
    linker_symbol_t* symbol = NULL;
    for (int i = 0; i < obj->symbol_count; i++) {
        if (strcmp(obj->symbols[i].name, "my_function") == 0) {
            symbol = &obj->symbols[i];
            break;
        }
    }
    munit_assert_not_null(symbol);

    /* Verify no symbol has underscore prefix */
    for (int i = 0; i < obj->symbol_count; i++) {
        if (obj->symbols[i].name && obj->symbols[i].name[0] != '\0') {
            munit_assert_char(obj->symbols[i].name[0], !=, '_');
        }
    }

    /* Cleanup */
    linker_object_free(obj);
    free(file_data);
    unlink(temp_file);

    return MUNIT_OK;
}

/* Test: Parsing invalid Mach-O file */
static MunitResult test_macho_invalid_file(const MunitParameter params[], void* data) {
    (void)params;
    (void)data;

    /* Create invalid data (too small) */
    uint8_t invalid_data[] = {0x01, 0x02, 0x03};

    linker_object_t* obj = macho_read_object("invalid.o", invalid_data, sizeof(invalid_data));
    munit_assert_null(obj);

    /* Create data with wrong magic number */
    uint8_t wrong_magic[sizeof(mach_header_64_t)];
    memset(wrong_magic, 0, sizeof(wrong_magic));
    ((mach_header_64_t*)wrong_magic)->magic = 0x12345678;  /* Wrong magic */

    obj = macho_read_object("wrong_magic.o", wrong_magic, sizeof(wrong_magic));
    munit_assert_null(obj);

    return MUNIT_OK;
}

/* Test: Section alignment parsing */
static MunitResult test_macho_section_alignment(const MunitParameter params[], void* data) {
    (void)params;
    (void)data;

    const char* temp_file = "/tmp/test_macho_align.o";

    /* Create object file with specific alignment */
    uint8_t code[] = {0xc0, 0x03, 0x5f, 0xd6};  /* ret */

    bool write_ok = macho_create_object_file(temp_file, code, sizeof(code), "test",
                                              CPU_TYPE_ARM64, CPU_SUBTYPE_ARM64_ALL);
    munit_assert_true(write_ok);

    /* Read and parse */
    size_t file_size = 0;
    uint8_t* file_data = read_file(temp_file, &file_size);
    munit_assert_not_null(file_data);

    linker_object_t* obj = macho_read_object(temp_file, file_data, file_size);
    munit_assert_not_null(obj);

    /* Check that alignment is properly parsed */
    linker_section_t* text_section = NULL;
    for (int i = 0; i < obj->section_count; i++) {
        if (strcmp(obj->sections[i].name, "__text") == 0) {
            text_section = &obj->sections[i];
            break;
        }
    }
    munit_assert_not_null(text_section);
    /* Default alignment for __text is 4 (2^4 = 16 bytes) */
    munit_assert_size(text_section->alignment, ==, 16);

    /* Cleanup */
    linker_object_free(obj);
    free(file_data);
    unlink(temp_file);

    return MUNIT_OK;
}

/* Define test suite */
static MunitTest macho_reader_tests[] = {
    {"/roundtrip_simple", test_macho_roundtrip_simple, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL},
    {"/roundtrip_relocations", test_macho_roundtrip_with_relocations, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL},
    {"/relocation_type_mapping", test_macho_relocation_type_mapping, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL},
    {"/section_type_mapping", test_macho_section_type_mapping, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL},
    {"/symbol_underscore_removal", test_macho_symbol_underscore_removal, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL},
    {"/invalid_file", test_macho_invalid_file, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL},
    {"/section_alignment", test_macho_section_alignment, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL},
    {NULL, NULL, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL}
};

/* Export test suite */
const MunitSuite macho_reader_test_suite = {
    "/macho_reader",
    macho_reader_tests,
    NULL,
    1,
    MUNIT_SUITE_OPTION_NONE
};

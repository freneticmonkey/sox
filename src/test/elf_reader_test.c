/*
 * Unit tests for elf_reader.c
 *
 * Tests the ELF object file reader with round-trip verification.
 * Validates parsing of sections, symbols, and relocations.
 */

#include "../native/elf_reader.h"
#include "../native/elf_writer.h"
#include "../native/linker_core.h"
#include "../native/object_reader.h"
#include "../../ext/munit/munit.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

/* ARM64 relocation types for tests (if not in elf_writer.h) */
#ifndef R_AARCH64_CALL26
#define R_AARCH64_CALL26 283
#endif

/* Test file path */
#define TEST_ELF_FILE "/tmp/sox_elf_reader_test.o"

/* Helper: Clean up test files */
static void cleanup_test_files(void) {
    remove(TEST_ELF_FILE);
}

/* Test: Parse a simple ELF file with .text section */
static MunitResult test_parse_simple_elf(const MunitParameter params[], void* data) {
    (void)params;
    (void)data;

    /* Create a simple ELF file with .text section */
    uint8_t code[] = {
        0x48, 0x89, 0xC7,  /* mov rdi, rax */
        0xC3               /* ret */
    };

    bool success = elf_create_object_file(TEST_ELF_FILE, code, sizeof(code),
                                          "test_func", EM_X86_64);
    munit_assert_true(success);

    /* Read the file back */
    size_t file_size;
    uint8_t* file_data = linker_read_file(TEST_ELF_FILE, &file_size);
    munit_assert_not_null(file_data);

    /* Parse the ELF file */
    linker_object_t* obj = elf_read_object(TEST_ELF_FILE, file_data, file_size);
    munit_assert_not_null(obj);

    /* Verify basic properties */
    munit_assert_int(obj->format, ==, PLATFORM_FORMAT_ELF);
    munit_assert_string_equal(obj->filename, TEST_ELF_FILE);

    /* Verify sections - should have at least .text */
    munit_assert_int(obj->section_count, >, 0);

    /* Find .text section */
    linker_section_t* text_section = NULL;
    for (int i = 0; i < obj->section_count; i++) {
        if (strcmp(obj->sections[i].name, ".text") == 0) {
            text_section = &obj->sections[i];
            break;
        }
    }
    munit_assert_not_null(text_section);
    munit_assert_int(text_section->type, ==, SECTION_TYPE_TEXT);
    munit_assert_size(text_section->size, ==, sizeof(code));
    munit_assert_memory_equal(sizeof(code), text_section->data, code);

    /* Verify symbols - should have test_func */
    munit_assert_int(obj->symbol_count, >, 0);

    linker_symbol_t* func_symbol = NULL;
    for (int i = 0; i < obj->symbol_count; i++) {
        if (strcmp(obj->symbols[i].name, "test_func") == 0) {
            func_symbol = &obj->symbols[i];
            break;
        }
    }
    munit_assert_not_null(func_symbol);
    munit_assert_int(func_symbol->type, ==, SYMBOL_TYPE_FUNC);
    munit_assert_int(func_symbol->binding, ==, SYMBOL_BINDING_GLOBAL);
    munit_assert_true(func_symbol->is_defined);
    munit_assert_size(func_symbol->size, ==, sizeof(code));

    /* Clean up */
    linker_object_free(obj);
    free(file_data);
    cleanup_test_files();

    return MUNIT_OK;
}

/* Test: Parse ELF file with relocations */
static MunitResult test_parse_elf_with_relocations(const MunitParameter params[], void* data) {
    (void)params;
    (void)data;

    /* Create ELF file with relocations */
    uint8_t code[] = {
        0xE8, 0x00, 0x00, 0x00, 0x00,  /* call 0 (needs relocation) */
        0xC3                            /* ret */
    };

    /* Create a relocation entry (must match codegen_relocation_t layout) */
    typedef struct {
        size_t offset;
        const char* symbol;
        uint32_t type;
        int64_t addend;
    } test_relocation_t;

    test_relocation_t reloc = {
        .offset = 1,
        .symbol = "external_func",
        .type = R_X86_64_PC32,
        .addend = -4
    };

    bool success = elf_create_object_file_with_relocations(
        TEST_ELF_FILE, code, sizeof(code), "caller",
        EM_X86_64, &reloc, 1);
    munit_assert_true(success);

    /* Read and parse */
    size_t file_size;
    uint8_t* file_data = linker_read_file(TEST_ELF_FILE, &file_size);
    munit_assert_not_null(file_data);

    linker_object_t* obj = elf_read_object(TEST_ELF_FILE, file_data, file_size);
    munit_assert_not_null(obj);

    /* Verify relocations */
    munit_assert_int(obj->relocation_count, ==, 1);

    linker_relocation_t* r = &obj->relocations[0];
    munit_assert_int((int)r->offset, ==, (int)reloc.offset);
    munit_assert_int(r->type, ==, RELOC_X64_PC32);
    munit_assert_int((int)r->addend, ==, (int)reloc.addend);

    /* Verify external symbol exists */
    bool found_external = false;
    for (int i = 0; i < obj->symbol_count; i++) {
        if (strcmp(obj->symbols[i].name, "external_func") == 0) {
            found_external = true;
            munit_assert_false(obj->symbols[i].is_defined);
            munit_assert_int(obj->symbols[i].section_index, ==, -1);
            break;
        }
    }
    munit_assert_true(found_external);

    /* Clean up */
    linker_object_free(obj);
    free(file_data);
    cleanup_test_files();

    return MUNIT_OK;
}

/* Test: Round-trip verification - write and read back */
static MunitResult test_roundtrip_simple(const MunitParameter params[], void* data) {
    (void)params;
    (void)data;

    /* Original code */
    uint8_t original_code[] = {
        0x55,              /* push rbp */
        0x48, 0x89, 0xE5,  /* mov rbp, rsp */
        0x5D,              /* pop rbp */
        0xC3               /* ret */
    };

    /* Write ELF file */
    bool write_success = elf_create_object_file(TEST_ELF_FILE, original_code,
                                                 sizeof(original_code),
                                                 "roundtrip_test", EM_X86_64);
    munit_assert_true(write_success);

    /* Read back */
    size_t file_size;
    uint8_t* file_data = linker_read_file(TEST_ELF_FILE, &file_size);
    munit_assert_not_null(file_data);

    linker_object_t* obj = elf_read_object(TEST_ELF_FILE, file_data, file_size);
    munit_assert_not_null(obj);

    /* Verify .text section data matches original */
    linker_section_t* text = NULL;
    for (int i = 0; i < obj->section_count; i++) {
        if (strcmp(obj->sections[i].name, ".text") == 0) {
            text = &obj->sections[i];
            break;
        }
    }
    munit_assert_not_null(text);
    munit_assert_size(text->size, ==, sizeof(original_code));
    munit_assert_memory_equal(sizeof(original_code), text->data, original_code);

    /* Clean up */
    linker_object_free(obj);
    free(file_data);
    cleanup_test_files();

    return MUNIT_OK;
}

/* Test: Parse ELF with ARM64 relocations */
static MunitResult test_parse_arm64_relocations(const MunitParameter params[], void* data) {
    (void)params;
    (void)data;

    /* ARM64 code with placeholder for BL instruction */
    uint8_t code[] = {
        0x00, 0x00, 0x00, 0x94,  /* bl 0 (needs R_AARCH64_CALL26 relocation) */
        0xC0, 0x03, 0x5F, 0xD6   /* ret */
    };

    /* Create relocation for ARM64 (must match codegen_relocation_t layout) */
    typedef struct {
        size_t offset;
        const char* symbol;
        uint32_t type;
        int64_t addend;
    } test_relocation_t;

    test_relocation_t reloc = {
        .offset = 0,
        .symbol = "arm64_func",
        .type = R_AARCH64_CALL26,
        .addend = 0
    };

    bool success = elf_create_object_file_with_relocations(
        TEST_ELF_FILE, code, sizeof(code), "arm64_caller",
        EM_AARCH64, &reloc, 1);
    munit_assert_true(success);

    /* Read and parse */
    size_t file_size;
    uint8_t* file_data = linker_read_file(TEST_ELF_FILE, &file_size);
    munit_assert_not_null(file_data);

    linker_object_t* obj = elf_read_object(TEST_ELF_FILE, file_data, file_size);
    munit_assert_not_null(obj);

    /* Verify ARM64 relocation was correctly mapped */
    munit_assert_int(obj->relocation_count, ==, 1);

    linker_relocation_t* r = &obj->relocations[0];
    munit_assert_int(r->type, ==, RELOC_ARM64_CALL26);
    munit_assert_int((int)r->offset, ==, (int)reloc.offset);

    /* Clean up */
    linker_object_free(obj);
    free(file_data);
    cleanup_test_files();

    return MUNIT_OK;
}

/* Test: Reject invalid ELF magic number */
static MunitResult test_reject_invalid_magic(const MunitParameter params[], void* data) {
    (void)params;
    (void)data;

    /* Create invalid ELF data with wrong magic */
    uint8_t invalid_data[128];
    memset(invalid_data, 0, sizeof(invalid_data));
    invalid_data[0] = 0xDE;
    invalid_data[1] = 0xAD;
    invalid_data[2] = 0xBE;
    invalid_data[3] = 0xEF;

    /* Try to parse - should fail */
    linker_object_t* obj = elf_read_object("invalid.o", invalid_data, sizeof(invalid_data));
    munit_assert_null(obj);

    return MUNIT_OK;
}

/* Test: Reject non-64-bit ELF */
static MunitResult test_reject_non_64bit(const MunitParameter params[], void* data) {
    (void)params;
    (void)data;

    /* Create ELF header but with 32-bit class */
    uint8_t invalid_data[128];
    memset(invalid_data, 0, sizeof(invalid_data));
    invalid_data[0] = 0x7f;  /* ELFMAG0 */
    invalid_data[1] = 'E';   /* ELFMAG1 */
    invalid_data[2] = 'L';   /* ELFMAG2 */
    invalid_data[3] = 'F';   /* ELFMAG3 */
    invalid_data[4] = 1;     /* ELFCLASS32 (not 64-bit) */

    /* Try to parse - should fail */
    linker_object_t* obj = elf_read_object("invalid32.o", invalid_data, sizeof(invalid_data));
    munit_assert_null(obj);

    return MUNIT_OK;
}

/* Test: Verify section count matches */
static MunitResult test_verify_section_count(const MunitParameter params[], void* data) {
    (void)params;
    (void)data;

    /* Create ELF with known sections */
    uint8_t code[] = {0x90, 0x90, 0x90, 0x90}; /* nops */

    bool success = elf_create_object_file(TEST_ELF_FILE, code, sizeof(code),
                                          "test", EM_X86_64);
    munit_assert_true(success);

    /* Read and parse */
    size_t file_size;
    uint8_t* file_data = linker_read_file(TEST_ELF_FILE, &file_size);
    munit_assert_not_null(file_data);

    linker_object_t* obj = elf_read_object(TEST_ELF_FILE, file_data, file_size);
    munit_assert_not_null(obj);

    /* Should have .text section at minimum */
    bool has_text = false;
    for (int i = 0; i < obj->section_count; i++) {
        if (strcmp(obj->sections[i].name, ".text") == 0) {
            has_text = true;

            /* Verify section properties */
            munit_assert_int(obj->sections[i].type, ==, SECTION_TYPE_TEXT);
            munit_assert_size(obj->sections[i].size, ==, sizeof(code));
            munit_assert_int(obj->sections[i].flags & 0x4, !=, 0); /* SHF_EXECINSTR */
        }
    }
    munit_assert_true(has_text);

    /* Clean up */
    linker_object_free(obj);
    free(file_data);
    cleanup_test_files();

    return MUNIT_OK;
}

/* Test: Verify symbol table parsing */
static MunitResult test_verify_symbol_table(const MunitParameter params[], void* data) {
    (void)params;
    (void)data;

    uint8_t code[] = {0xC3}; /* ret */

    bool success = elf_create_object_file(TEST_ELF_FILE, code, sizeof(code),
                                          "my_function", EM_X86_64);
    munit_assert_true(success);

    /* Read and parse */
    size_t file_size;
    uint8_t* file_data = linker_read_file(TEST_ELF_FILE, &file_size);
    munit_assert_not_null(file_data);

    linker_object_t* obj = elf_read_object(TEST_ELF_FILE, file_data, file_size);
    munit_assert_not_null(obj);

    /* Verify we found the function symbol */
    bool found_func = false;
    for (int i = 0; i < obj->symbol_count; i++) {
        if (strcmp(obj->symbols[i].name, "my_function") == 0) {
            found_func = true;

            /* Verify symbol properties */
            munit_assert_int(obj->symbols[i].type, ==, SYMBOL_TYPE_FUNC);
            munit_assert_int(obj->symbols[i].binding, ==, SYMBOL_BINDING_GLOBAL);
            munit_assert_true(obj->symbols[i].is_defined);
            munit_assert_size(obj->symbols[i].size, ==, sizeof(code));
            munit_assert_int((int)obj->symbols[i].value, ==, 0);
        }
    }
    munit_assert_true(found_func);

    /* Clean up */
    linker_object_free(obj);
    free(file_data);
    cleanup_test_files();

    return MUNIT_OK;
}

/* Test: File too small for ELF header */
static MunitResult test_file_too_small(const MunitParameter params[], void* data) {
    (void)params;
    (void)data;

    /* Data too small to contain ELF header */
    uint8_t tiny_data[8] = {0x7f, 'E', 'L', 'F', 0, 0, 0, 0};

    linker_object_t* obj = elf_read_object("tiny.o", tiny_data, sizeof(tiny_data));
    munit_assert_null(obj);

    return MUNIT_OK;
}

/* Test array - all tests for elf_reader */
static MunitTest elf_reader_tests[] = {
    {"/parse_simple_elf", test_parse_simple_elf, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL},
    {"/parse_elf_with_relocations", test_parse_elf_with_relocations, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL},
    {"/roundtrip_simple", test_roundtrip_simple, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL},
    {"/parse_arm64_relocations", test_parse_arm64_relocations, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL},
    {"/reject_invalid_magic", test_reject_invalid_magic, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL},
    {"/reject_non_64bit", test_reject_non_64bit, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL},
    {"/verify_section_count", test_verify_section_count, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL},
    {"/verify_symbol_table", test_verify_symbol_table, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL},
    {"/file_too_small", test_file_too_small, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL},
    {NULL, NULL, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL}
};

/* Test suite for elf_reader */
const MunitSuite elf_reader_suite = {
    "elf_reader",
    elf_reader_tests,
    NULL,
    1,
    MUNIT_SUITE_OPTION_NONE
};

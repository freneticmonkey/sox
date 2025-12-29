/*
 * Unit tests for object_reader.c
 *
 * Tests the object file reader interface, format detection, and file I/O utilities.
 */

#include "../native/object_reader.h"
#include "../native/linker_core.h"
#include "../../ext/munit/munit.h"
#include <string.h>
#include <stdio.h>

/* Test file paths - these will be created during testing */
#define TEST_ELF_FILE "/tmp/sox_test_elf.o"
#define TEST_MACHO_FILE "/tmp/sox_test_macho.o"
#define TEST_UNKNOWN_FILE "/tmp/sox_test_unknown.o"

/* Helper: Create a test file with specific magic bytes */
static bool create_test_file(const char* path, const uint8_t* magic, size_t magic_size, size_t total_size) {
    FILE* f = fopen(path, "wb");
    if (f == NULL) {
        return false;
    }

    /* Write magic bytes */
    if (fwrite(magic, 1, magic_size, f) != magic_size) {
        fclose(f);
        return false;
    }

    /* Pad with zeros to reach total size */
    for (size_t i = magic_size; i < total_size; i++) {
        if (fputc(0, f) == EOF) {
            fclose(f);
            return false;
        }
    }

    fclose(f);
    return true;
}

/* Helper: Clean up test files */
static void cleanup_test_files(void) {
    remove(TEST_ELF_FILE);
    remove(TEST_MACHO_FILE);
    remove(TEST_UNKNOWN_FILE);
}

/* Test: Format detection for ELF files */
static MunitResult test_detect_elf_format(const MunitParameter params[], void* data) {
    (void)params;
    (void)data;

    /* Create test ELF file with correct magic: 0x7f 'E' 'L' 'F' */
    uint8_t elf_magic[] = {0x7f, 'E', 'L', 'F'};
    munit_assert_true(create_test_file(TEST_ELF_FILE, elf_magic, sizeof(elf_magic), 64));

    /* Detect format */
    platform_format_t format = linker_detect_format(TEST_ELF_FILE);
    munit_assert_int(format, ==, PLATFORM_FORMAT_ELF);

    cleanup_test_files();
    return MUNIT_OK;
}

/* Test: Format detection for Mach-O files (little-endian) */
static MunitResult test_detect_macho_format_le(const MunitParameter params[], void* data) {
    (void)params;
    (void)data;

    /* Create test Mach-O file with little-endian magic: 0xCF 0xFA 0xED 0xFE */
    uint8_t macho_magic_le[] = {0xcf, 0xfa, 0xed, 0xfe};
    munit_assert_true(create_test_file(TEST_MACHO_FILE, macho_magic_le, sizeof(macho_magic_le), 64));

    /* Detect format */
    platform_format_t format = linker_detect_format(TEST_MACHO_FILE);
    munit_assert_int(format, ==, PLATFORM_FORMAT_MACH_O);

    cleanup_test_files();
    return MUNIT_OK;
}

/* Test: Format detection for Mach-O files (big-endian) */
static MunitResult test_detect_macho_format_be(const MunitParameter params[], void* data) {
    (void)params;
    (void)data;

    /* Create test Mach-O file with big-endian magic: 0xFE 0xED 0xFA 0xCF */
    uint8_t macho_magic_be[] = {0xfe, 0xed, 0xfa, 0xcf};
    munit_assert_true(create_test_file(TEST_MACHO_FILE, macho_magic_be, sizeof(macho_magic_be), 64));

    /* Detect format */
    platform_format_t format = linker_detect_format(TEST_MACHO_FILE);
    munit_assert_int(format, ==, PLATFORM_FORMAT_MACH_O);

    cleanup_test_files();
    return MUNIT_OK;
}

/* Test: Format detection for unknown format */
static MunitResult test_detect_unknown_format(const MunitParameter params[], void* data) {
    (void)params;
    (void)data;

    /* Create test file with invalid magic */
    uint8_t unknown_magic[] = {0xDE, 0xAD, 0xBE, 0xEF};
    munit_assert_true(create_test_file(TEST_UNKNOWN_FILE, unknown_magic, sizeof(unknown_magic), 64));

    /* Detect format - should return UNKNOWN */
    platform_format_t format = linker_detect_format(TEST_UNKNOWN_FILE);
    munit_assert_int(format, ==, PLATFORM_FORMAT_UNKNOWN);

    cleanup_test_files();
    return MUNIT_OK;
}

/* Test: Format detection with NULL filename */
static MunitResult test_detect_format_null_filename(const MunitParameter params[], void* data) {
    (void)params;
    (void)data;

    platform_format_t format = linker_detect_format(NULL);
    munit_assert_int(format, ==, PLATFORM_FORMAT_UNKNOWN);

    return MUNIT_OK;
}

/* Test: Format detection with non-existent file */
static MunitResult test_detect_format_nonexistent_file(const MunitParameter params[], void* data) {
    (void)params;
    (void)data;

    platform_format_t format = linker_detect_format("/tmp/nonexistent_file_sox_test.o");
    munit_assert_int(format, ==, PLATFORM_FORMAT_UNKNOWN);

    return MUNIT_OK;
}

/* Test: Read entire file */
static MunitResult test_read_file(const MunitParameter params[], void* user_data) {
    (void)params;
    (void)user_data;

    /* Create a test file with known content */
    uint8_t test_data[] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};
    munit_assert_true(create_test_file(TEST_ELF_FILE, test_data, sizeof(test_data), sizeof(test_data)));

    /* Read file */
    size_t file_size;
    uint8_t* data = linker_read_file(TEST_ELF_FILE, &file_size);

    /* Verify */
    munit_assert_not_null(data);
    munit_assert_size(file_size, ==, sizeof(test_data));
    munit_assert_memory_equal(file_size, data, test_data);

    free(data);
    cleanup_test_files();
    return MUNIT_OK;
}

/* Test: Read file with NULL filename */
static MunitResult test_read_file_null_filename(const MunitParameter params[], void* user_data) {
    (void)params;
    (void)user_data;

    size_t file_size;
    uint8_t* data = linker_read_file(NULL, &file_size);
    munit_assert_null(data);

    return MUNIT_OK;
}

/* Test: Read file with NULL size pointer */
static MunitResult test_read_file_null_size(const MunitParameter params[], void* user_data) {
    (void)params;
    (void)user_data;

    uint8_t test_data[] = {0x01, 0x02, 0x03, 0x04};
    munit_assert_true(create_test_file(TEST_ELF_FILE, test_data, sizeof(test_data), sizeof(test_data)));

    uint8_t* data = linker_read_file(TEST_ELF_FILE, NULL);
    munit_assert_null(data);

    cleanup_test_files();
    return MUNIT_OK;
}

/* Test: Read non-existent file */
static MunitResult test_read_file_nonexistent(const MunitParameter params[], void* user_data) {
    (void)params;
    (void)user_data;

    size_t file_size;
    uint8_t* data = linker_read_file("/tmp/nonexistent_file_sox_test.o", &file_size);
    munit_assert_null(data);

    return MUNIT_OK;
}

/* Test: Read file range */
static MunitResult test_read_file_range(const MunitParameter params[], void* user_data) {
    (void)params;
    (void)user_data;

    /* Create a test file */
    uint8_t test_data[] = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99};
    munit_assert_true(create_test_file(TEST_ELF_FILE, test_data, sizeof(test_data), sizeof(test_data)));

    /* Read middle 4 bytes (offset 3, size 4) */
    uint8_t* data = linker_read_file_range(TEST_ELF_FILE, 3, 4);
    munit_assert_not_null(data);

    /* Should have read: 0x33, 0x44, 0x55, 0x66 */
    uint8_t expected[] = {0x33, 0x44, 0x55, 0x66};
    munit_assert_memory_equal(4, data, expected);

    free(data);
    cleanup_test_files();
    return MUNIT_OK;
}

/* Test: Read file range with invalid arguments */
static MunitResult test_read_file_range_invalid_args(const MunitParameter params[], void* user_data) {
    (void)params;
    (void)user_data;

    /* NULL filename */
    uint8_t* data1 = linker_read_file_range(NULL, 0, 4);
    munit_assert_null(data1);

    /* Zero size */
    uint8_t* data2 = linker_read_file_range("/tmp/test.o", 0, 0);
    munit_assert_null(data2);

    return MUNIT_OK;
}

/* Test: Free NULL object (should not crash) */
static MunitResult test_free_object_null(const MunitParameter params[], void* data) {
    (void)params;
    (void)data;

    /* Should not crash */
    linker_free_object(NULL);

    return MUNIT_OK;
}

/* Test: Free object with allocated data */
static MunitResult test_free_object_with_data(const MunitParameter params[], void* data) {
    (void)params;
    (void)data;

    /* Create an object manually with some allocated data */
    linker_object_t* obj = linker_object_new("test.o", PLATFORM_FORMAT_ELF);
    munit_assert_not_null(obj);

    /* Add a section with data */
    linker_section_t* section = linker_object_add_section(obj);
    section->name = malloc(6);
    strcpy(section->name, ".text");
    section->data = malloc(16);
    section->size = 16;

    /* Add a symbol */
    linker_symbol_t* symbol = linker_object_add_symbol(obj);
    symbol->name = malloc(5);
    strcpy(symbol->name, "main");

    /* Add a relocation */
    linker_object_add_relocation(obj);

    /* Free the object - should clean up everything */
    linker_free_object(obj);

    /* No assertions needed - we just verify no crash/leak */
    return MUNIT_OK;
}

/* Test array - all tests for object_reader */
static MunitTest object_reader_tests[] = {
    {"/detect_elf_format", test_detect_elf_format, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL},
    {"/detect_macho_format_le", test_detect_macho_format_le, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL},
    {"/detect_macho_format_be", test_detect_macho_format_be, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL},
    {"/detect_unknown_format", test_detect_unknown_format, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL},
    {"/detect_format_null_filename", test_detect_format_null_filename, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL},
    {"/detect_format_nonexistent_file", test_detect_format_nonexistent_file, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL},
    {"/read_file", test_read_file, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL},
    {"/read_file_null_filename", test_read_file_null_filename, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL},
    {"/read_file_null_size", test_read_file_null_size, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL},
    {"/read_file_nonexistent", test_read_file_nonexistent, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL},
    {"/read_file_range", test_read_file_range, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL},
    {"/read_file_range_invalid_args", test_read_file_range_invalid_args, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL},
    {"/free_object_null", test_free_object_null, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL},
    {"/free_object_with_data", test_free_object_with_data, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL},
    {NULL, NULL, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL}
};

/* Test suite for object_reader */
const MunitSuite object_reader_suite = {
    "/object_reader",
    object_reader_tests,
    NULL,
    1,
    MUNIT_SUITE_OPTION_NONE
};

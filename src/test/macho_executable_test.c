/*
 * Unit tests for macho_executable.c
 *
 * Tests the Mach-O executable generation functionality.
 */

#include "native/macho_executable.h"
#include "native/macho_writer.h"
#include "native/linker_core.h"
#include "native/section_layout.h"
#include <munit/munit.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>

/* Helper: Create a minimal linker context for testing */
static linker_context_t* create_test_context(void) {
    linker_context_t* context = linker_context_new();
    if (context == NULL) {
        return NULL;
    }

    /* Set target platform */
    context->target_format = PLATFORM_FORMAT_MACH_O;
    context->base_address = 0x100000000;  /* Default ARM64 Mach-O base */

    /* Allocate merged sections */
    context->merged_section_count = 2;  /* .text and .data */
    context->merged_sections = calloc(2, sizeof(linker_section_t));
    if (context->merged_sections == NULL) {
        linker_context_free(context);
        return NULL;
    }

    /* Create .text section */
    linker_section_init(&context->merged_sections[0]);
    context->merged_sections[0].name = strdup(".text");
    context->merged_sections[0].type = SECTION_TYPE_TEXT;
    context->merged_sections[0].size = 16;  /* Minimal code */
    context->merged_sections[0].alignment = 16;
    context->merged_sections[0].vaddr = context->base_address;
    context->merged_sections[0].data = calloc(16, 1);
    /* Simple ARM64 code: mov x0, #0; ret */
    context->merged_sections[0].data[0] = 0x00;
    context->merged_sections[0].data[1] = 0x00;
    context->merged_sections[0].data[2] = 0x80;
    context->merged_sections[0].data[3] = 0xd2;  /* mov x0, #0 */
    context->merged_sections[0].data[4] = 0xc0;
    context->merged_sections[0].data[5] = 0x03;
    context->merged_sections[0].data[6] = 0x5f;
    context->merged_sections[0].data[7] = 0xd6;  /* ret */

    /* Create .data section */
    linker_section_init(&context->merged_sections[1]);
    context->merged_sections[1].name = strdup(".data");
    context->merged_sections[1].type = SECTION_TYPE_DATA;
    context->merged_sections[1].size = 8;
    context->merged_sections[1].alignment = 8;
    context->merged_sections[1].vaddr = context->base_address + 0x4000;
    context->merged_sections[1].data = calloc(8, 1);

    /* Create _main symbol */
    context->global_symbol_count = 1;
    context->global_symbols = calloc(1, sizeof(linker_symbol_t));
    if (context->global_symbols == NULL) {
        linker_context_free(context);
        return NULL;
    }

    linker_symbol_init(&context->global_symbols[0]);
    context->global_symbols[0].name = strdup("_main");
    context->global_symbols[0].type = SYMBOL_TYPE_FUNC;
    context->global_symbols[0].binding = SYMBOL_BINDING_GLOBAL;
    context->global_symbols[0].section_index = 0;  /* .text */
    context->global_symbols[0].value = 0;
    context->global_symbols[0].final_address = context->base_address;
    context->global_symbols[0].is_defined = true;
    context->global_symbols[0].defining_object = 0;

    return context;
}

/* Test: macho_get_segment_section_count */
static MunitResult test_macho_get_segment_section_count(const MunitParameter params[], void* data) {
    (void)params;
    (void)data;

    linker_context_t* context = create_test_context();
    munit_assert_not_null(context);

    /* __TEXT segment should have 1 section (__text) */
    uint32_t text_count = macho_get_segment_section_count(context, SEG_TEXT);
    munit_assert_uint32(text_count, ==, 1);

    /* __DATA segment should have 1 section (__data) */
    uint32_t data_count = macho_get_segment_section_count(context, SEG_DATA);
    munit_assert_uint32(data_count, ==, 1);

    /* Unknown segment should have 0 sections */
    uint32_t unknown_count = macho_get_segment_section_count(context, "__UNKNOWN");
    munit_assert_uint32(unknown_count, ==, 0);

    linker_context_free(context);

    return MUNIT_OK;
}

/* Test: macho_calculate_segment_size */
static MunitResult test_macho_calculate_segment_size(const MunitParameter params[], void* data) {
    (void)params;
    (void)data;

    linker_context_t* context = create_test_context();
    munit_assert_not_null(context);

    /* __TEXT segment size should be 16 bytes (.text only) */
    uint64_t text_size = macho_calculate_segment_size(context, SEG_TEXT);
    munit_assert_uint64(text_size, ==, 16);

    /* __DATA segment size should be 8 bytes (.data only) */
    uint64_t data_size = macho_calculate_segment_size(context, SEG_DATA);
    munit_assert_uint64(data_size, ==, 8);

    linker_context_free(context);

    return MUNIT_OK;
}

/* Test: macho_calculate_load_commands_size */
static MunitResult test_macho_calculate_load_commands_size(const MunitParameter params[], void* data) {
    (void)params;
    (void)data;

    linker_context_t* context = create_test_context();
    munit_assert_not_null(context);

    uint32_t size = macho_calculate_load_commands_size(context);

    /* Expected size (updated to match actual implementation):
     * - LC_SEGMENT_64 (__TEXT): sizeof(segment_command_64_t) + 1 * sizeof(section_64_t)
     * - LC_SEGMENT_64 (__DATA): sizeof(segment_command_64_t) + 1 * sizeof(section_64_t)
     * - LC_MAIN: sizeof(entry_point_command_t)
     * - LC_LOAD_DYLINKER: sizeof(dylinker_command_t) + aligned path length
     * - LC_LOAD_DYLIB: sizeof(dylib_command_t) + aligned path length
     * - LC_SYMTAB: sizeof(symtab_command_t)
     * - LC_DYSYMTAB: sizeof(dysymtab_command_t)
     * - LC_UUID: sizeof(uuid_command_t)
     * - LC_BUILD_VERSION: sizeof(build_version_command_t)
     * - LC_SEGMENT_64 (__LINKEDIT): sizeof(segment_command_64_t)
     * - LC_SEGMENT_64 (__PAGEZERO): sizeof(segment_command_64_t)
     */
    uint32_t expected_size = 0;
    expected_size += sizeof(segment_command_64_t) + sizeof(section_64_t);  /* __TEXT */
    expected_size += sizeof(segment_command_64_t) + sizeof(section_64_t);  /* __DATA */
    expected_size += sizeof(entry_point_command_t);  /* LC_MAIN */
    expected_size += align_to(sizeof(dylinker_command_t) + strlen(DYLD_PATH) + 1, 8);
    expected_size += align_to(sizeof(dylib_command_t) + strlen(LIBSYSTEM_PATH) + 1, 8);
    expected_size += sizeof(symtab_command_t);  /* LC_SYMTAB */
    expected_size += sizeof(dysymtab_command_t);  /* LC_DYSYMTAB */
    expected_size += sizeof(uuid_command_t);  /* LC_UUID */
    expected_size += sizeof(build_version_command_t);  /* LC_BUILD_VERSION */
    expected_size += sizeof(segment_command_64_t);  /* __LINKEDIT */
    expected_size += sizeof(segment_command_64_t);  /* __PAGEZERO */

    munit_assert_uint32(size, ==, expected_size);

    linker_context_free(context);

    return MUNIT_OK;
}

/* Test: macho_set_entry_point */
static MunitResult test_macho_set_entry_point(const MunitParameter params[], void* data) {
    (void)params;
    (void)data;

    linker_context_t* context = create_test_context();
    munit_assert_not_null(context);

    /* Entry point should be 0 initially */
    munit_assert_uint64(context->entry_point, ==, 0);

    /* Set entry point to _main */
    bool result = macho_set_entry_point(context);
    munit_assert_true(result);

    /* Entry point should now be set to _main's address */
    munit_assert_uint64(context->entry_point, ==, context->base_address);

    linker_context_free(context);

    return MUNIT_OK;
}

/* Test: macho_set_entry_point with missing _main */
static MunitResult test_macho_set_entry_point_missing_main(const MunitParameter params[], void* data) {
    (void)params;
    (void)data;

    linker_context_t* context = linker_context_new();
    munit_assert_not_null(context);

    /* No symbols, should fail */
    bool result = macho_set_entry_point(context);
    munit_assert_false(result);

    linker_context_free(context);

    return MUNIT_OK;
}

/* Test: macho_write_executable (basic file creation) */
static MunitResult test_macho_write_executable_basic(const MunitParameter params[], void* data) {
    (void)params;
    (void)data;

    linker_context_t* context = create_test_context();
    munit_assert_not_null(context);

    /* Set entry point */
    bool result = macho_set_entry_point(context);
    munit_assert_true(result);

    /* Write executable */
    const char* output_path = "/tmp/sox_test_executable";
    result = macho_write_executable(output_path, context);
    munit_assert_true(result);

    /* Verify file exists and is executable */
    munit_assert_int(access(output_path, F_OK), ==, 0);
    munit_assert_int(access(output_path, X_OK), ==, 0);

    /* Clean up */
    unlink(output_path);
    linker_context_free(context);

    return MUNIT_OK;
}

/* Test: macho_write_executable with NULL parameters */
static MunitResult test_macho_write_executable_null_params(const MunitParameter params[], void* data) {
    (void)params;
    (void)data;

    linker_context_t* context = create_test_context();
    munit_assert_not_null(context);

    /* NULL output path */
    bool result = macho_write_executable(NULL, context);
    munit_assert_false(result);

    /* NULL context */
    result = macho_write_executable("/tmp/test", NULL);
    munit_assert_false(result);

    linker_context_free(context);

    return MUNIT_OK;
}

/* Test: macho_write_executable with empty context */
static MunitResult test_macho_write_executable_empty_context(const MunitParameter params[], void* data) {
    (void)params;
    (void)data;

    linker_context_t* context = linker_context_new();
    munit_assert_not_null(context);

    /* Empty context (no sections) should fail */
    bool result = macho_write_executable("/tmp/test", context);
    munit_assert_false(result);

    linker_context_free(context);

    return MUNIT_OK;
}

/* Test: macho_write_executable with all section types */
static MunitResult test_macho_write_executable_all_sections(const MunitParameter params[], void* data) {
    (void)params;
    (void)data;

    linker_context_t* context = linker_context_new();
    munit_assert_not_null(context);

    context->target_format = PLATFORM_FORMAT_MACH_O;
    context->base_address = 0x100000000;

    /* Create all section types */
    context->merged_section_count = 4;
    context->merged_sections = calloc(4, sizeof(linker_section_t));
    munit_assert_not_null(context->merged_sections);

    /* .text section */
    linker_section_init(&context->merged_sections[0]);
    context->merged_sections[0].name = strdup(".text");
    context->merged_sections[0].type = SECTION_TYPE_TEXT;
    context->merged_sections[0].size = 16;
    context->merged_sections[0].data = calloc(16, 1);
    context->merged_sections[0].vaddr = context->base_address;

    /* .rodata section */
    linker_section_init(&context->merged_sections[1]);
    context->merged_sections[1].name = strdup(".rodata");
    context->merged_sections[1].type = SECTION_TYPE_RODATA;
    context->merged_sections[1].size = 16;
    context->merged_sections[1].data = calloc(16, 1);
    context->merged_sections[1].vaddr = context->base_address + 0x1000;

    /* .data section */
    linker_section_init(&context->merged_sections[2]);
    context->merged_sections[2].name = strdup(".data");
    context->merged_sections[2].type = SECTION_TYPE_DATA;
    context->merged_sections[2].size = 8;
    context->merged_sections[2].data = calloc(8, 1);
    context->merged_sections[2].vaddr = context->base_address + 0x4000;

    /* .bss section */
    linker_section_init(&context->merged_sections[3]);
    context->merged_sections[3].name = strdup(".bss");
    context->merged_sections[3].type = SECTION_TYPE_BSS;
    context->merged_sections[3].size = 8;
    context->merged_sections[3].data = NULL;  /* BSS has no data */
    context->merged_sections[3].vaddr = context->base_address + 0x5000;

    /* Create _main symbol */
    context->global_symbol_count = 1;
    context->global_symbols = calloc(1, sizeof(linker_symbol_t));
    linker_symbol_init(&context->global_symbols[0]);
    context->global_symbols[0].name = strdup("_main");
    context->global_symbols[0].type = SYMBOL_TYPE_FUNC;
    context->global_symbols[0].binding = SYMBOL_BINDING_GLOBAL;
    context->global_symbols[0].final_address = context->base_address;
    context->global_symbols[0].is_defined = true;

    /* Set entry point */
    bool result = macho_set_entry_point(context);
    munit_assert_true(result);

    /* Write executable */
    const char* output_path = "/tmp/sox_test_executable_full";
    result = macho_write_executable(output_path, context);
    munit_assert_true(result);

    /* Verify segment section counts */
    uint32_t text_count = macho_get_segment_section_count(context, SEG_TEXT);
    munit_assert_uint32(text_count, ==, 2);  /* __text + __const */

    uint32_t data_count = macho_get_segment_section_count(context, SEG_DATA);
    munit_assert_uint32(data_count, ==, 2);  /* __data + __bss */

    /* Clean up */
    unlink(output_path);
    linker_context_free(context);

    return MUNIT_OK;
}

/* Test suite definition */
static MunitTest macho_executable_tests[] = {
    {"/get_segment_section_count", test_macho_get_segment_section_count, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL},
    {"/calculate_segment_size", test_macho_calculate_segment_size, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL},
    {"/calculate_load_commands_size", test_macho_calculate_load_commands_size, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL},
    {"/set_entry_point", test_macho_set_entry_point, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL},
    {"/set_entry_point_missing_main", test_macho_set_entry_point_missing_main, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL},
    {"/write_executable_basic", test_macho_write_executable_basic, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL},
    {"/write_executable_null_params", test_macho_write_executable_null_params, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL},
    {"/write_executable_empty_context", test_macho_write_executable_empty_context, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL},
    {"/write_executable_all_sections", test_macho_write_executable_all_sections, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL},
    {NULL, NULL, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL}
};

/* Test suite */
const MunitSuite macho_executable_suite = {
    "macho_executable",
    macho_executable_tests,
    NULL,
    1,
    MUNIT_SUITE_OPTION_NONE
};

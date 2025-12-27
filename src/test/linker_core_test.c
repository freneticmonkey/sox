/*
 * Unit tests for linker_core.c
 *
 * Tests the core linker data structures and basic API functions.
 */

#include "../native/linker_core.h"
#include "../../ext/munit/munit.h"
#include <string.h>

/* Test: Create and destroy linker context */
static MunitResult test_linker_context_lifecycle(const MunitParameter params[], void* data) {
    (void)params;
    (void)data;

    linker_context_t* context = linker_context_new();
    munit_assert_not_null(context);
    munit_assert_int(context->object_count, ==, 0);
    munit_assert_not_null(context->objects);
    munit_assert_int(context->object_capacity, >, 0);

    linker_context_free(context);

    return MUNIT_OK;
}

/* Test: Create and destroy object file */
static MunitResult test_linker_object_lifecycle(const MunitParameter params[], void* data) {
    (void)params;
    (void)data;

    linker_object_t* object = linker_object_new("test.o", PLATFORM_FORMAT_ELF);
    munit_assert_not_null(object);
    munit_assert_string_equal(object->filename, "test.o");
    munit_assert_int(object->format, ==, PLATFORM_FORMAT_ELF);
    munit_assert_int(object->section_count, ==, 0);
    munit_assert_int(object->symbol_count, ==, 0);
    munit_assert_int(object->relocation_count, ==, 0);

    linker_object_free(object);

    return MUNIT_OK;
}

/* Test: Add object to context */
static MunitResult test_linker_context_add_object(const MunitParameter params[], void* data) {
    (void)params;
    (void)data;

    linker_context_t* context = linker_context_new();
    munit_assert_not_null(context);

    linker_object_t* obj1 = linker_object_new("obj1.o", PLATFORM_FORMAT_ELF);
    linker_object_t* obj2 = linker_object_new("obj2.o", PLATFORM_FORMAT_MACH_O);

    bool result1 = linker_context_add_object(context, obj1);
    bool result2 = linker_context_add_object(context, obj2);

    munit_assert_true(result1);
    munit_assert_true(result2);
    munit_assert_int(context->object_count, ==, 2);
    munit_assert_ptr_equal(context->objects[0], obj1);
    munit_assert_ptr_equal(context->objects[1], obj2);

    linker_context_free(context);

    return MUNIT_OK;
}

/* Test: Add sections to object */
static MunitResult test_linker_object_add_section(const MunitParameter params[], void* data) {
    (void)params;
    (void)data;

    linker_object_t* object = linker_object_new("test.o", PLATFORM_FORMAT_ELF);
    munit_assert_not_null(object);

    linker_section_t* section1 = linker_object_add_section(object);
    linker_section_t* section2 = linker_object_add_section(object);

    munit_assert_not_null(section1);
    munit_assert_not_null(section2);
    munit_assert_int(object->section_count, ==, 2);

    /* Sections should be properly initialized */
    munit_assert_int(section1->object_index, ==, -1);
    munit_assert_int(section2->object_index, ==, -1);

    linker_object_free(object);

    return MUNIT_OK;
}

/* Test: Add symbols to object */
static MunitResult test_linker_object_add_symbol(const MunitParameter params[], void* data) {
    (void)params;
    (void)data;

    linker_object_t* object = linker_object_new("test.o", PLATFORM_FORMAT_ELF);
    munit_assert_not_null(object);

    linker_symbol_t* symbol1 = linker_object_add_symbol(object);
    linker_symbol_t* symbol2 = linker_object_add_symbol(object);

    munit_assert_not_null(symbol1);
    munit_assert_not_null(symbol2);
    munit_assert_int(object->symbol_count, ==, 2);

    /* Symbols should be properly initialized */
    munit_assert_int(symbol1->section_index, ==, -1);
    munit_assert_int(symbol1->defining_object, ==, -1);
    munit_assert_false(symbol1->is_defined);

    linker_object_free(object);

    return MUNIT_OK;
}

/* Test: Add relocations to object */
static MunitResult test_linker_object_add_relocation(const MunitParameter params[], void* data) {
    (void)params;
    (void)data;

    linker_object_t* object = linker_object_new("test.o", PLATFORM_FORMAT_ELF);
    munit_assert_not_null(object);

    linker_relocation_t* reloc1 = linker_object_add_relocation(object);
    linker_relocation_t* reloc2 = linker_object_add_relocation(object);

    munit_assert_not_null(reloc1);
    munit_assert_not_null(reloc2);
    munit_assert_int(object->relocation_count, ==, 2);

    /* Relocations should be properly initialized */
    munit_assert_int(reloc1->section_index, ==, -1);
    munit_assert_int(reloc1->symbol_index, ==, -1);
    munit_assert_int(reloc1->object_index, ==, -1);

    linker_object_free(object);

    return MUNIT_OK;
}

/* Test: Utility functions for enum names */
static MunitResult test_utility_functions(const MunitParameter params[], void* data) {
    (void)params;
    (void)data;

    /* Platform format names */
    munit_assert_string_equal(platform_format_name(PLATFORM_FORMAT_ELF), "ELF");
    munit_assert_string_equal(platform_format_name(PLATFORM_FORMAT_MACH_O), "Mach-O");
    munit_assert_string_equal(platform_format_name(PLATFORM_FORMAT_PE_COFF), "PE/COFF");

    /* Section type names */
    munit_assert_string_equal(section_type_name(SECTION_TYPE_TEXT), "TEXT");
    munit_assert_string_equal(section_type_name(SECTION_TYPE_DATA), "DATA");
    munit_assert_string_equal(section_type_name(SECTION_TYPE_BSS), "BSS");
    munit_assert_string_equal(section_type_name(SECTION_TYPE_RODATA), "RODATA");

    /* Symbol type names */
    munit_assert_string_equal(symbol_type_name(SYMBOL_TYPE_FUNC), "FUNC");
    munit_assert_string_equal(symbol_type_name(SYMBOL_TYPE_OBJECT), "OBJECT");

    /* Symbol binding names */
    munit_assert_string_equal(symbol_binding_name(SYMBOL_BINDING_LOCAL), "LOCAL");
    munit_assert_string_equal(symbol_binding_name(SYMBOL_BINDING_GLOBAL), "GLOBAL");
    munit_assert_string_equal(symbol_binding_name(SYMBOL_BINDING_WEAK), "WEAK");

    /* Relocation type names */
    munit_assert_string_equal(relocation_type_name(RELOC_X64_PC32), "X64_PC32");
    munit_assert_string_equal(relocation_type_name(RELOC_ARM64_CALL26), "ARM64_CALL26");

    return MUNIT_OK;
}

/* Test: Array growth */
static MunitResult test_array_growth(const MunitParameter params[], void* data) {
    (void)params;
    (void)data;

    linker_object_t* object = linker_object_new("test.o", PLATFORM_FORMAT_ELF);
    munit_assert_not_null(object);

    int initial_capacity = object->section_capacity;

    /* Add sections until we exceed capacity */
    for (int i = 0; i < initial_capacity + 5; i++) {
        linker_section_t* section = linker_object_add_section(object);
        munit_assert_not_null(section);
    }

    /* Capacity should have grown */
    munit_assert_int(object->section_count, ==, initial_capacity + 5);
    munit_assert_int(object->section_capacity, >, initial_capacity);

    linker_object_free(object);

    return MUNIT_OK;
}

/* Test suite definition */
static MunitTest linker_core_tests[] = {
    { (char*)"/linker_context_lifecycle", test_linker_context_lifecycle, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL },
    { (char*)"/linker_object_lifecycle", test_linker_object_lifecycle, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL },
    { (char*)"/linker_context_add_object", test_linker_context_add_object, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL },
    { (char*)"/linker_object_add_section", test_linker_object_add_section, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL },
    { (char*)"/linker_object_add_symbol", test_linker_object_add_symbol, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL },
    { (char*)"/linker_object_add_relocation", test_linker_object_add_relocation, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL },
    { (char*)"/utility_functions", test_utility_functions, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL },
    { (char*)"/array_growth", test_array_growth, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL },
    { NULL, NULL, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL }
};

/* Export the suite for inclusion in main test runner */
const MunitSuite linker_core_suite = {
    (char*)"/linker_core",
    linker_core_tests,
    NULL,
    1,
    MUNIT_SUITE_OPTION_NONE
};

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "munit/munit.h"

#include "wat_generator.h"
#include "wasm_generator.h"
#include "compiler.h"
#include "vm.h"
#include "lib/memory.h"
#include "lib/file.h"

// Helper to generate unique test filenames
static void _get_unique_filename(char* buffer, size_t buflen, const char* prefix) {
    snprintf(buffer, buflen, "/tmp/%s_%d_%ld", prefix, (int)getpid(), (long)munit_rand_uint32());
}

static MunitResult test_wat_generation(const MunitParameter params[], void* data) {
    // Create a simple test source
    const char* source = "print(2 + 3)";
    char test_filename[256];
    _get_unique_filename(test_filename, sizeof(test_filename), "test_wat");

    l_init_memory();

    vm_config_t config = {
        .enable_serialisation = false,
        .suppress_print = true,
        .enable_wasm_output = false,
        .enable_wat_output = false,
    };

    l_init_vm(&config);

    obj_function_t* function = l_compile(source);
    munit_assert_not_null(function);

    wat_generator_t* generator = l_wat_new(test_filename);
    munit_assert_not_null(generator);

    WatErrorCode result = l_wat_generate_from_function(generator, function);
    munit_assert_int(result, ==, WAT_OK);

    result = l_wat_write_to_file(generator);
    munit_assert_int(result, ==, WAT_OK);

    // Check that the file was created
    char full_filename[512];
    snprintf(full_filename, sizeof(full_filename), "%s.wat", test_filename);
    munit_assert_true(l_file_exists(full_filename));

    // Clean up
    l_wat_del(generator);
    l_free_vm();
    l_free_memory();

    // Clean up test file
    l_file_delete(full_filename);

    return MUNIT_OK;
}

static MunitResult test_wasm_generation(const MunitParameter params[], void* data) {
    // Create a simple test source
    const char* source = "print(2 + 3)";
    char test_filename[256];
    _get_unique_filename(test_filename, sizeof(test_filename), "test_wasm");

    l_init_memory();

    vm_config_t config = {
        .enable_serialisation = false,
        .suppress_print = true,
        .enable_wasm_output = false,
        .enable_wat_output = false,
    };

    l_init_vm(&config);

    obj_function_t* function = l_compile(source);
    munit_assert_not_null(function);

    wasm_generator_t* generator = l_wasm_new(test_filename);
    munit_assert_not_null(generator);

    WasmErrorCode result = l_wasm_generate_from_function(generator, function);
    munit_assert_int(result, ==, WASM_OK);

    result = l_wasm_write_to_file(generator);
    munit_assert_int(result, ==, WASM_OK);

    // Check that the file was created
    char full_filename[512];
    snprintf(full_filename, sizeof(full_filename), "%s.wasm", test_filename);
    munit_assert_true(l_file_exists(full_filename));

    // Clean up
    l_wasm_del(generator);
    l_free_vm();
    l_free_memory();

    // Clean up test file
    l_file_delete(full_filename);

    return MUNIT_OK;
}

static MunitResult test_wat_arithmetic(const MunitParameter params[], void* data) {
    const char* source = "print(5 * 4 - 2 / 1)";
    char test_filename[256];
    _get_unique_filename(test_filename, sizeof(test_filename), "test_wat_arith");

    l_init_memory();
    vm_config_t config = {
        .enable_serialisation = false,
        .suppress_print = true,
        .enable_wasm_output = false,
        .enable_wat_output = false,
    };

    l_init_vm(&config);

    obj_function_t* function = l_compile(source);
    munit_assert_not_null(function);

    wat_generator_t* generator = l_wat_new(test_filename);
    munit_assert_not_null(generator);

    WatErrorCode result = l_wat_generate_from_function(generator, function);
    munit_assert_int(result, ==, WAT_OK);

    result = l_wat_write_to_file(generator);
    munit_assert_int(result, ==, WAT_OK);

    char full_filename[512];
    snprintf(full_filename, sizeof(full_filename), "%s.wat", test_filename);
    munit_assert_true(l_file_exists(full_filename));

    l_wat_del(generator);
    l_free_vm();
    l_free_memory();
    l_file_delete(full_filename);

    return MUNIT_OK;
}

static MunitResult test_wasm_arithmetic(const MunitParameter params[], void* data) {
    const char* source = "print(5 * 4 - 2 / 1)";
    char test_filename[256];
    _get_unique_filename(test_filename, sizeof(test_filename), "test_wasm_arith");

    l_init_memory();
    vm_config_t config = {
        .enable_serialisation = false,
        .suppress_print = true,
        .enable_wasm_output = false,
        .enable_wat_output = false,
    };

    l_init_vm(&config);

    obj_function_t* function = l_compile(source);
    munit_assert_not_null(function);

    wasm_generator_t* generator = l_wasm_new(test_filename);
    munit_assert_not_null(generator);

    WasmErrorCode result = l_wasm_generate_from_function(generator, function);
    munit_assert_int(result, ==, WASM_OK);

    result = l_wasm_write_to_file(generator);
    munit_assert_int(result, ==, WASM_OK);

    char full_filename[512];
    snprintf(full_filename, sizeof(full_filename), "%s.wasm", test_filename);
    munit_assert_true(l_file_exists(full_filename));

    l_wasm_del(generator);
    l_free_vm();
    l_free_memory();
    l_file_delete(full_filename);

    return MUNIT_OK;
}

static MunitTest wasm_tests[] = {
    { "wat_generation", test_wat_generation, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL },
    { "wasm_generation", test_wasm_generation, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL },
    { "wat_arithmetic", test_wat_arithmetic, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL },
    { "wasm_arithmetic", test_wasm_arithmetic, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL },
    { NULL, NULL, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL }
};

static const MunitSuite wasm_suite = {
    "/wasm", wasm_tests, NULL, 1, MUNIT_SUITE_OPTION_NONE
};

MunitSuite* get_wasm_suite() {
    return (MunitSuite*)&wasm_suite;
}

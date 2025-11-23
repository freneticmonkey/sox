#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <math.h>
#include "munit/munit.h"

#include "wat_generator.h"
#include "wasm_generator.h"
#include "compiler.h"
#include "vm.h"
#include "lib/memory.h"
#include "lib/file.h"
#include "wasm_verify.h"

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

static MunitResult test_wasm_verification_simple(const MunitParameter params[], void* data) {
    // Test simple arithmetic: 2 + 3 = 5
    const char* source = "print(2 + 3)";
    char test_filename[256];
    _get_unique_filename(test_filename, sizeof(test_filename), "test_verify_simple");

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

    // Now verify the WASM file with wazero
    double* output = NULL;
    int count = 0;
    char* error = NULL;
    MunitResult test_result = MUNIT_OK;

    int verify_result = GetWASMOutput(full_filename, &output, &count, &error);

    if (!verify_result) {
        printf("WASM verification failed: %s\n", error ? error : "unknown error");
        if (error) FreeString(error);
        test_result = MUNIT_FAIL;
        goto cleanup;
    }

    // Should have one output value: 5.0
    if (count != 1 || output == NULL || output[0] < 4.999999 || output[0] > 5.000001) {
        test_result = MUNIT_FAIL;
        goto cleanup;
    }

cleanup:
    // Clean up resources
    if (output) FreeDoubleArray(output);
    l_wasm_del(generator);
    l_free_vm();
    l_free_memory();
    l_file_delete(full_filename);

    return test_result;
}

static MunitResult test_wasm_verification_arithmetic(const MunitParameter params[], void* data) {
    // Test complex arithmetic: 5 * 4 - 2 / 1 = 18
    const char* source = "print(5 * 4 - 2 / 1)";
    char test_filename[256];
    _get_unique_filename(test_filename, sizeof(test_filename), "test_verify_arith");

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

    // Verify with wazero
    double* output = NULL;
    int count = 0;
    char* error = NULL;
    MunitResult test_result = MUNIT_OK;

    int verify_result = GetWASMOutput(full_filename, &output, &count, &error);

    if (!verify_result) {
        printf("WASM verification failed: %s\n", error ? error : "unknown error");
        if (error) FreeString(error);
        test_result = MUNIT_FAIL;
        goto cleanup;
    }

    // Should have one output value: 18.0
    if (count != 1 || output == NULL || output[0] < 17.999999 || output[0] > 18.000001) {
        test_result = MUNIT_FAIL;
        goto cleanup;
    }

cleanup:
    // Clean up resources
    if (output) FreeDoubleArray(output);
    l_wasm_del(generator);
    l_free_vm();
    l_free_memory();
    l_file_delete(full_filename);

    return test_result;
}

static MunitResult test_wasm_verification_multiple_prints(const MunitParameter params[], void* data) {
    // Test multiple print statements
    const char* source = "print(10)\nprint(20)\nprint(30)";
    char test_filename[256];
    _get_unique_filename(test_filename, sizeof(test_filename), "test_verify_multi");

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

    // Verify with wazero
    double* output = NULL;
    int count = 0;
    char* error = NULL;
    MunitResult test_result = MUNIT_OK;

    int verify_result = GetWASMOutput(full_filename, &output, &count, &error);

    if (!verify_result) {
        printf("WASM verification failed: %s\n", error ? error : "unknown error");
        if (error) FreeString(error);
        test_result = MUNIT_FAIL;
        goto cleanup;
    }

    // Should have three output values
    if (count != 3 || output == NULL ||
        output[0] < 9.999999 || output[0] > 10.000001 ||
        output[1] < 19.999999 || output[1] > 20.000001 ||
        output[2] < 29.999999 || output[2] > 30.000001) {
        test_result = MUNIT_FAIL;
        goto cleanup;
    }

cleanup:
    // Clean up resources
    if (output) FreeDoubleArray(output);
    l_wasm_del(generator);
    l_free_vm();
    l_free_memory();
    l_file_delete(full_filename);

    return test_result;
}

static MunitResult test_wasm_verification_string_api(const MunitParameter params[], void* data) {
    // Test the string-based API
    const char* source = "print(7 * 6)";
    char test_filename[256];
    _get_unique_filename(test_filename, sizeof(test_filename), "test_verify_string");

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

    // Verify with wazero using string API
    char* output_str = NULL;
    char* error = NULL;
    MunitResult test_result = MUNIT_OK;

    int verify_result = VerifyWASMFile(full_filename, &output_str, &error);

    if (!verify_result) {
        printf("WASM verification failed: %s\n", error ? error : "unknown error");
        if (error) FreeString(error);
        test_result = MUNIT_FAIL;
        goto cleanup;
    }

    // Output should be "42.000000"
    if (output_str == NULL) {
        test_result = MUNIT_FAIL;
        goto cleanup;
    }

    // Parse the output and verify
    double value;
    if (sscanf(output_str, "%lf", &value) != 1 || value < 41.999999 || value > 42.000001) {
        test_result = MUNIT_FAIL;
        goto cleanup;
    }

cleanup:
    // Clean up resources
    if (output_str) FreeString(output_str);
    l_wasm_del(generator);
    l_free_vm();
    l_free_memory();
    l_file_delete(full_filename);

    return test_result;
}

static MunitTest wasm_tests[] = {
    { "wat_generation", test_wat_generation, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL },
    { "wasm_generation", test_wasm_generation, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL },
    { "wat_arithmetic", test_wat_arithmetic, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL },
    { "wasm_arithmetic", test_wasm_arithmetic, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL },
    { "wasm_verification_simple", test_wasm_verification_simple, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL },
    { "wasm_verification_arithmetic", test_wasm_verification_arithmetic, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL },
    { "wasm_verification_multiple_prints", test_wasm_verification_multiple_prints, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL },
    { "wasm_verification_string_api", test_wasm_verification_string_api, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL },
    { NULL, NULL, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL }
};

static const MunitSuite wasm_suite = {
    "/wasm", wasm_tests, NULL, 1, MUNIT_SUITE_OPTION_NONE
};

MunitSuite* get_wasm_suite() {
    return (MunitSuite*)&wasm_suite;
}

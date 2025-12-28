/*
 * Custom Linker Integration Tests
 *
 * End-to-end tests for the custom linker implementation, including:
 * - Native executable generation with --custom-linker flag
 * - Execution of custom-linker-generated binaries
 * - Comparison with system linker output
 * - Cross-validation of results
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include "../lib/memory.h"
#include "../lib/file.h"
#include "../vm.h"
#include "../vm_config.h"
#include "../../ext/munit/munit.h"

#ifdef SOX_MACOS
    #define DYLD_LIBRARY_PATH "DYLD_LIBRARY_PATH"
#else
    #define DYLD_LIBRARY_PATH "LD_LIBRARY_PATH"
#endif

/* Helper: Execute a binary and capture output */
static char* execute_binary(const char* binary_path) {
    char command[1024];

    // Set up library path
    const char* old_path = getenv(DYLD_LIBRARY_PATH);
    char new_path[2048];
    if (old_path) {
        snprintf(new_path, sizeof(new_path), "./build:%s", old_path);
    } else {
        snprintf(new_path, sizeof(new_path), "./build");
    }
    setenv(DYLD_LIBRARY_PATH, new_path, 1);

    snprintf(command, sizeof(command), "%s 2>&1", binary_path);
    FILE* pipe = popen(command, "r");
    if (!pipe) {
        if (old_path) {
            setenv(DYLD_LIBRARY_PATH, old_path, 1);
        } else {
            unsetenv(DYLD_LIBRARY_PATH);
        }
        return NULL;
    }

    char* output = malloc(4096);
    size_t output_size = 0;
    size_t output_capacity = 4096;

    char buffer[256];
    while (fgets(buffer, sizeof(buffer), pipe) != NULL) {
        size_t len = strlen(buffer);

        if (output_size + len + 1 > output_capacity) {
            output_capacity *= 2;
            output = realloc(output, output_capacity);
        }

        strcpy(output + output_size, buffer);
        output_size += len;
    }

    int status = pclose(pipe);

    // Restore old library path
    if (old_path) {
        setenv(DYLD_LIBRARY_PATH, old_path, 1);
    } else {
        unsetenv(DYLD_LIBRARY_PATH);
    }

    if (status != 0) {
        free(output);
        return NULL;
    }

    return output;
}

/* Helper: Compile Sox source to native with custom linker */
static bool compile_with_custom_linker(const char* source_file, const char* output_file) {
    vm_config_t config = {
        .enable_serialisation = false,
        .suppress_print = false,
        .enable_native_output = true,
        .native_output_file = (char*)output_file,
        .native_target_arch = "arm64",
        .native_target_os = "macos",
        .native_emit_object = false,
        .native_debug_output = false,
        .native_optimization_level = 0,
        .use_custom_linker = true,  /* KEY: Use custom linker */
        .args = l_parse_args(2, (const char*[]){ "sox", source_file })
    };

    int status = l_run_file(&config);
    return status == 0;
}

/* Helper: Compile Sox source to native with system linker */
static bool compile_with_system_linker(const char* source_file, const char* output_file) {
    vm_config_t config = {
        .enable_serialisation = false,
        .suppress_print = false,
        .enable_native_output = true,
        .native_output_file = (char*)output_file,
        .native_target_arch = "arm64",
        .native_target_os = "macos",
        .native_emit_object = false,
        .native_debug_output = false,
        .native_optimization_level = 0,
        .use_custom_linker = false,  /* Use system linker */
        .args = l_parse_args(2, (const char*[]){ "sox", source_file })
    };

    int status = l_run_file(&config);
    return status == 0;
}

/* Test: Basic compilation with custom linker */
static MunitResult test_custom_linker_basic_compilation(const MunitParameter params[], void* data) {
    (void)params;
    (void)data;

#ifndef __aarch64__
    
    return MUNIT_SKIP;
#endif

    const char* source = "src/test/scripts/basic.sox";
    const char* output = "/tmp/sox_custom_linker_basic";

    // Clean up any existing file
    unlink(output);

    // Compile with custom linker
    bool result = compile_with_custom_linker(source, output);
    munit_assert_true(result);

    // Verify file exists and is executable
    struct stat st;
    munit_assert_int(stat(output, &st), ==, 0);
    munit_assert_true(st.st_mode & S_IXUSR);

    // Clean up
    unlink(output);

    return MUNIT_OK;
}

/* Test: Execute custom-linker-generated binary */
static MunitResult test_custom_linker_execution(const MunitParameter params[], void* data) {
    (void)params;
    (void)data;

#ifndef __aarch64__
    
    return MUNIT_SKIP;
#endif

    const char* source = "src/test/scripts/basic.sox";
    const char* output = "/tmp/sox_custom_linker_exec";

    // Clean up
    unlink(output);

    // Compile with custom linker
    bool result = compile_with_custom_linker(source, output);
    munit_assert_true(result);

    // Execute and capture output
    char* exec_output = execute_binary(output);
    munit_assert_not_null(exec_output);

    // Verify output contains expected text
    munit_assert_not_null(strstr(exec_output, "hello world"));

    // Clean up
    free(exec_output);
    unlink(output);

    return MUNIT_OK;
}

/* Test: Compare custom linker vs system linker output */
static MunitResult test_custom_vs_system_linker(const MunitParameter params[], void* data) {
    (void)data;

#ifndef __aarch64__
    
    return MUNIT_SKIP;
#endif

    const char* source_file = params[0].value;
    char custom_output[512];
    char system_output[512];

    snprintf(custom_output, sizeof(custom_output), "/tmp/sox_custom_%d", getpid());
    snprintf(system_output, sizeof(system_output), "/tmp/sox_system_%d", getpid());

    // Clean up
    unlink(custom_output);
    unlink(system_output);

    // Compile with both linkers
    bool custom_result = compile_with_custom_linker(source_file, custom_output);
    bool system_result = compile_with_system_linker(source_file, system_output);

    munit_assert_true(custom_result);
    munit_assert_true(system_result);

    // Execute both binaries
    char* custom_exec = execute_binary(custom_output);
    char* system_exec = execute_binary(system_output);

    munit_assert_not_null(custom_exec);
    munit_assert_not_null(system_exec);

    // Outputs should match
    munit_assert_string_equal(custom_exec, system_exec);

    // Clean up
    free(custom_exec);
    free(system_exec);
    unlink(custom_output);
    unlink(system_output);

    return MUNIT_OK;
}

/* Test: Arithmetic operations with custom linker */
static MunitResult test_custom_linker_arithmetic(const MunitParameter params[], void* data) {
    (void)params;
    (void)data;

#ifndef __aarch64__
    
    return MUNIT_SKIP;
#endif

    const char* source = "src/test/scripts/native/arithmetic.sox";
    const char* output = "/tmp/sox_custom_arithmetic";

    // Clean up
    unlink(output);

    // Compile and execute
    bool result = compile_with_custom_linker(source, output);
    munit_assert_true(result);

    char* exec_output = execute_binary(output);
    munit_assert_not_null(exec_output);

    // Clean up
    free(exec_output);
    unlink(output);

    return MUNIT_OK;
}

/* Test: String operations with custom linker */
static MunitResult test_custom_linker_strings(const MunitParameter params[], void* data) {
    (void)params;
    (void)data;

#ifndef __aarch64__
    
    return MUNIT_SKIP;
#endif

    const char* source = "src/test/scripts/native/strings.sox";
    const char* output = "/tmp/sox_custom_strings";

    // Clean up
    unlink(output);

    // Compile and execute
    bool result = compile_with_custom_linker(source, output);
    munit_assert_true(result);

    char* exec_output = execute_binary(output);
    munit_assert_not_null(exec_output);

    // Clean up
    free(exec_output);
    unlink(output);

    return MUNIT_OK;
}

/* Test: Multiple variables with custom linker */
static MunitResult test_custom_linker_variables(const MunitParameter params[], void* data) {
    (void)params;
    (void)data;

#ifndef __aarch64__
    
    return MUNIT_SKIP;
#endif

    const char* source = "src/test/scripts/native/variables.sox";
    const char* output = "/tmp/sox_custom_variables";

    // Clean up
    unlink(output);

    // Compile and execute
    bool result = compile_with_custom_linker(source, output);
    munit_assert_true(result);

    char* exec_output = execute_binary(output);
    munit_assert_not_null(exec_output);

    // Clean up
    free(exec_output);
    unlink(output);

    return MUNIT_OK;
}

/* Test: Custom linker doesn't crash on cleanup */
static MunitResult test_custom_linker_cleanup(const MunitParameter params[], void* data) {
    (void)params;
    (void)data;

#ifndef __aarch64__
    
    return MUNIT_SKIP;
#endif

    // Run multiple compilations to ensure cleanup works properly
    for (int i = 0; i < 5; i++) {
        char output[512];
        snprintf(output, sizeof(output), "/tmp/sox_custom_cleanup_%d", i);

        bool result = compile_with_custom_linker("src/test/scripts/basic.sox", output);
        munit_assert_true(result);

        unlink(output);
    }

    return MUNIT_OK;
}

/* Test suite definition */
static MunitTest custom_linker_tests[] = {
    {
        "/basic_compilation",
        test_custom_linker_basic_compilation,
        NULL, NULL,
        MUNIT_TEST_OPTION_NONE,
        NULL
    },
    {
        "/execution",
        test_custom_linker_execution,
        NULL, NULL,
        MUNIT_TEST_OPTION_NONE,
        NULL
    },
    {
        "/arithmetic",
        test_custom_linker_arithmetic,
        NULL, NULL,
        MUNIT_TEST_OPTION_NONE,
        NULL
    },
    {
        "/strings",
        test_custom_linker_strings,
        NULL, NULL,
        MUNIT_TEST_OPTION_NONE,
        NULL
    },
    {
        "/variables",
        test_custom_linker_variables,
        NULL, NULL,
        MUNIT_TEST_OPTION_NONE,
        NULL
    },
    {
        "/cleanup",
        test_custom_linker_cleanup,
        NULL, NULL,
        MUNIT_TEST_OPTION_NONE,
        NULL
    },
    {NULL, NULL, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL}
};

/* Cross-comparison test suite */
static char* comparison_files[] = {
    "src/test/scripts/native/constants.sox",
    "src/test/scripts/native/variables.sox",
    "src/test/scripts/native/arithmetic.sox",
    "src/test/scripts/native/strings.sox",
    NULL
};

static MunitParameterEnum comparison_params[] = {
    {"source", comparison_files},
    {NULL, NULL}
};

static MunitTest comparison_tests[] = {
    {
        "/custom_vs_system",
        test_custom_vs_system_linker,
        NULL, NULL,
        MUNIT_TEST_OPTION_NONE,
        comparison_params
    },
    {NULL, NULL, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL}
};

MunitSuite custom_linker_integration_suite(void) {
#ifndef __aarch64__
    printf("⚠️  Skipping custom linker integration tests: Only supported on ARM64\n");
    static MunitSuite empty_suite = {
        .prefix = (char*)"custom_linker/",
        .tests = NULL,
        .suites = NULL,
        .iterations = 1,
        .options = MUNIT_SUITE_OPTION_NONE
    };
    return empty_suite;
#else
    static MunitSuite sub_suites[] = {
        {
            .prefix = (char*)"comparison/",
            .tests = comparison_tests,
            .suites = NULL,
            .iterations = 1,
            .options = MUNIT_SUITE_OPTION_NONE
        },
        {NULL, NULL, NULL, 0, MUNIT_SUITE_OPTION_NONE}
    };

    static MunitSuite suite = {
        .prefix = (char*)"custom_linker/",
        .tests = custom_linker_tests,
        .suites = sub_suites,
        .iterations = 1,
        .options = MUNIT_SUITE_OPTION_NONE
    };
    return suite;
#endif
}

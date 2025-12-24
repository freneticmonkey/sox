#include "chunk.h"
#include "vm.h"
#include "vm_config.h"

#include "lib/debug.h"
#include "lib/file.h"
#include "lib/print.h"

#include "test/native_test.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>

#ifdef SOX_MACOS
#define DYLD_LIBRARY_PATH "DYLD_LIBRARY_PATH"
#define LIBRARY_PATH_SEP ":"
#elif defined(SOX_LINUX)
#define DYLD_LIBRARY_PATH "LD_LIBRARY_PATH"
#define LIBRARY_PATH_SEP ":"
#else
#define DYLD_LIBRARY_PATH "PATH"
#define LIBRARY_PATH_SEP ";"
#endif

// Helper function to capture output from running a native binary
static char* capture_native_output(const char* binary_path) {
    char cmd[1024];

    // Set library path to include ./build for runtime library
    const char* old_path = getenv(DYLD_LIBRARY_PATH);
    char new_path[2048];
    if (old_path) {
        snprintf(new_path, sizeof(new_path), "./build%s%s", LIBRARY_PATH_SEP, old_path);
    } else {
        snprintf(new_path, sizeof(new_path), "./build");
    }
    setenv(DYLD_LIBRARY_PATH, new_path, 1);

    // Create command to run binary and capture output
    snprintf(cmd, sizeof(cmd), "%s 2>&1", binary_path);

    FILE* pipe = popen(cmd, "r");
    if (!pipe) {
        return NULL;
    }

    // Read output
    char* output = malloc(4096);
    size_t output_size = 0;
    size_t output_capacity = 4096;

    char buffer[256];
    while (fgets(buffer, sizeof(buffer), pipe) != NULL) {
        size_t len = strlen(buffer);

        // Resize if needed
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

// Helper function to filter runtime debug output from native binary output
static char* filter_runtime_debug(const char* raw_output) {
    if (!raw_output) return NULL;

    char* filtered = malloc(strlen(raw_output) + 1);
    char* dst = filtered;
    const char* src = raw_output;

    while (*src) {
        // Skip lines starting with [RUNTIME]
        if (strncmp(src, "[RUNTIME]", 9) == 0) {
            // Skip to end of line
            while (*src && *src != '\n') {
                src++;
            }
            if (*src == '\n') src++;
            continue;
        }

        // Copy character
        *dst++ = *src++;
    }

    *dst = '\0';
    return filtered;
}

static MunitResult _test_native_compilation(const MunitParameter params[], void *user_data)
{
    (void)user_data;

    const char* filename = params[0].value;

    munit_logf(MUNIT_LOG_INFO, "Testing native compilation: %s", filename);

    // Generate output filename
    char native_output[512];
    snprintf(native_output, sizeof(native_output), "/tmp/sox_test_native_%d", getpid());

    // Determine current platform
    const char* target_arch = "arm64";
    const char* target_os = "macos";

#ifdef SOX_LINUX
    target_os = "linux";
    #ifdef __x86_64__
        target_arch = "x86_64";
    #else
        target_arch = "arm64";
    #endif
#elif defined(SOX_MACOS)
    target_os = "macos";
    #ifdef __x86_64__
        target_arch = "x86_64";
    #else
        target_arch = "arm64";
    #endif
#endif

    // Configure for native compilation
    vm_config_t config = {
        .enable_serialisation = false,
        .suppress_print = false,
        .enable_native_output = true,
        .native_output_file = native_output,
        .native_target_arch = (char*)target_arch,
        .native_target_os = (char*)target_os,
        .native_emit_object = false,  // Generate executable
        .native_debug_output = false,
        .native_optimization_level = 0,
        .args = l_parse_args(
            2,
            (const char *[]){
                "sox",
                filename
            }
        )
    };

    // Compile to native
    int status = l_run_file(&config);

    munit_assert_int(status, ==, 0);

    // Check if output file exists
    munit_assert_true(access(native_output, X_OK) == 0);

    // Run the native binary and capture output
    char* raw_output = capture_native_output(native_output);
    munit_assert_not_null(raw_output);

    // Filter out debug output
    char* output = filter_runtime_debug(raw_output);
    free(raw_output);

    // Check for expected output file
    char expected_output_file[512];
    snprintf(expected_output_file, sizeof(expected_output_file), "%s.out", filename);

    if (l_file_exists(expected_output_file)) {
        char* expected = l_read_file(expected_output_file);
        munit_assert_string_equal(output, expected);
        free(expected);
    } else {
        munit_logf(MUNIT_LOG_WARNING, "No output validation file: %s", expected_output_file);
    }

    // Cleanup
    free(output);
    unlink(native_output);

    return MUNIT_OK;
}

MunitSuite l_native_test_setup() {

    static char* files[] = {
        "src/test/scripts/native/constants.sox",
        "src/test/scripts/native/variables.sox",
        "src/test/scripts/native/arithmetic.sox",
        "src/test/scripts/native/multiple_vars.sox",
        "src/test/scripts/native/strings.sox",
        "src/test/scripts/native/comparisons.sox",
        "src/test/scripts/native/logic.sox",
        NULL,
    };

    static MunitParameterEnum params[] = {
        {"files", files},
        NULL,
    };

    static MunitTest native_suite_tests[] = {
        {
            .name = (char *)"native_compilation",
            .test = _test_native_compilation,
            .setup = NULL,
            .tear_down = NULL,
            .options = MUNIT_TEST_OPTION_NONE,
            .parameters = params
        },
        NULL,
    };

    static MunitSuite suite = {
        .prefix = (char *)"native/",
        .tests = native_suite_tests,
        .suites = NULL,
        .iterations = 1,
        .options = MUNIT_SUITE_OPTION_NONE
    };

    return suite;
}

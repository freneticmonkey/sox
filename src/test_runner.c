#include "test_runner.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32) || defined(_WIN64)
#include <windows.h>
#else
#include <sys/stat.h>
#endif

#include "scanner.h"
#include "testing.h"
#include "vm.h"
#include "lib/file.h"
#include "lib/memory.h"
#include "lib/print.h"

#define TEST_NAME_LEN 37

typedef struct {
    char** names;
    int count;
    int capacity;
} test_list_t;

static void _test_list_add(test_list_t* list, const char* name, int length) {
    if (list->count + 1 > list->capacity) {
        int old_capacity = list->capacity;
        list->capacity = old_capacity < 8 ? 8 : old_capacity * 2;
        list->names = GROW_ARRAY(char*, list->names, old_capacity, list->capacity);
    }

    char* copy = ALLOCATE(char, length + 1);
    memcpy(copy, name, length);
    copy[length] = '\0';
    list->names[list->count++] = copy;
}

static void _test_list_free(test_list_t* list) {
    for (int i = 0; i < list->count; i++) {
        FREE_ARRAY(char, list->names[i], (int)strlen(list->names[i]) + 1);
    }
    FREE_ARRAY(char*, list->names, list->capacity);
    list->names = NULL;
    list->count = 0;
    list->capacity = 0;
}

static bool _is_test_attribute(token_t token) {
    return token.type == TOKEN_IDENTIFIER &&
           token.length == 4 &&
           memcmp(token.start, "test", 4) == 0;
}

static void _discover_tests(const char* source, test_list_t* list) {
    l_init_scanner(source);

    for (;;) {
        token_t token = l_scan_token();
        if (token.type == TOKEN_EOF) {
            break;
        }

        if (token.type != TOKEN_HASH) {
            continue;
        }

        token_t left = l_scan_token();
        if (left.type != TOKEN_LEFT_BRACKET) {
            continue;
        }

        token_t attr = l_scan_token();
        if (!_is_test_attribute(attr)) {
            continue;
        }

        token_t right = l_scan_token();
        if (right.type != TOKEN_RIGHT_BRACKET) {
            continue;
        }

        token_t fun = l_scan_token();
        if (fun.type != TOKEN_FUN) {
            continue;
        }

        token_t name = l_scan_token();
        if (name.type != TOKEN_IDENTIFIER) {
            continue;
        }

        _test_list_add(list, name.start, name.length);
    }
}

static bool _is_directory(const char* path) {
#if defined(_WIN32) || defined(_WIN64)
    DWORD attrs = GetFileAttributesA(path);
    if (attrs == INVALID_FILE_ATTRIBUTES) {
        return false;
    }
    return (attrs & FILE_ATTRIBUTE_DIRECTORY) != 0;
#else
    struct stat st;
    if (stat(path, &st) != 0) {
        return false;
    }
    return S_ISDIR(st.st_mode);
#endif
}

typedef enum {
    TEST_OK,
    TEST_FAIL,
    TEST_ERROR
} test_result_t;

typedef struct {
    int successes;
    int failures;
    int errors;
} test_summary_t;

static const char* _base_name(const char* path) {
    const char* last_slash = strrchr(path, '/');
    return last_slash ? last_slash + 1 : path;
}

static void _strip_suffix(char* name, const char* suffix) {
    size_t name_len = strlen(name);
    size_t suffix_len = strlen(suffix);
    if (name_len >= suffix_len &&
        strcmp(name + name_len - suffix_len, suffix) == 0) {
        name[name_len - suffix_len] = '\0';
    }
}

static char* _format_test_name(const char* file_path, const char* test_name) {
    const char* base = _base_name(file_path);
    char* file_copy = strdup(base);
    if (file_copy == NULL) {
        return NULL;
    }
    _strip_suffix(file_copy, ".sox");
    _strip_suffix(file_copy, "_test");

    size_t size = strlen("sox//") + strlen(file_copy) + strlen(test_name) + 1;
    char* full = malloc(size);
    if (full != NULL) {
        snprintf(full, size, "sox/%s/%s", file_copy, test_name);
    }
    free(file_copy);
    return full;
}

static test_result_t _run_single_test(const char* file_path, const char* test_name) {
    test_result_t result = TEST_OK;

    char* source = l_read_file(file_path);
    if (source == NULL) {
        fprintf(stderr, "Failed to read test file: %s\n", file_path);
        result = TEST_ERROR;
        return result;
    }

    vm_config_t config;
    l_init_vmconfig(&config, 2, (const char*[]){"sox", file_path});
    config.enable_serialisation = false;
    config.suppress_print = false;
    config.enable_wasm_output = false;
    config.enable_wat_output = false;
    config.enable_native_output = false;
    config.native_output_file = NULL;
    config.native_debug_output = false;
    config.native_optimization_level = 0;
    config.use_custom_linker = false;

    l_init_memory();
    l_init_vm(&config);

    InterpretResult interpret = l_interpret_with_options(source, true);
    if (interpret != INTERPRET_OK) {
        fprintf(stderr, "Compile error in %s\n", file_path);
        result = TEST_ERROR;
        goto cleanup;
    }

    InterpretResult run_result = l_run();
    if (run_result != INTERPRET_OK) {
        fprintf(stderr, "Runtime error in setup for %s\n", file_path);
        result = TEST_ERROR;
        goto cleanup;
    }

    test_state_t state = {
        .test_name = test_name,
        .failure_count = 0,
        .fatal_triggered = false,
    };

    vm.test_state = &state;
    obj_table_t* context = l_create_test_context(&state);
    l_push(OBJ_VAL(context));
    value_t args[1] = { OBJ_VAL(context) };

    InterpretResult call_result = l_call_global(test_name, 1, args);
    if (vm.stack_top_count > 0) {
        l_pop();
    }
    vm.test_state = NULL;

    if (state.failure_count > 0) {
        result = TEST_FAIL;
    } else if (call_result != INTERPRET_OK) {
        result = TEST_ERROR;
    }

cleanup:
    l_free_vm();
    l_free_memory();
    l_free_vmconfig(&config);
    free(source);
    return result;
}

static test_summary_t _run_tests_in_file(const char* file_path) {
    test_summary_t summary = {0};

    char* source = l_read_file(file_path);
    if (source == NULL) {
        fprintf(stderr, "Failed to read test file: %s\n", file_path);
        summary.errors++;
        return summary;
    }

    test_list_t list = {0};
    _discover_tests(source, &list);
    free(source);

    if (list.count == 0) {
        _test_list_free(&list);
        return summary;
    }

    for (int i = 0; i < list.count; i++) {
        char* display = _format_test_name(file_path, list.names[i]);
        if (display == NULL) {
            display = strdup(list.names[i]);
        }

        fprintf(stdout, "%-*s", TEST_NAME_LEN, display ? display : list.names[i]);
        test_result_t result = _run_single_test(file_path, list.names[i]);
        if (result == TEST_OK) {
            fprintf(stdout, "[ OK   ]\n");
            summary.successes++;
        } else if (result == TEST_FAIL) {
            fprintf(stdout, "[ FAIL ]\n");
            summary.failures++;
        } else {
            fprintf(stdout, "[ ERROR]\n");
            summary.errors++;
        }
        free(display);
    }

    _test_list_free(&list);
    return summary;
}

int l_run_tests(vm_config_t* base_config, const char* path) {
    (void)base_config;
    test_summary_t summary = {0};
    int total = 0;

    fprintf(stdout, "Running test suite with seed 0x%08x...\n", 0);

    if (_is_directory(path)) {
        int file_count = 0;
        char** files = l_scan_directory(path, "_test.sox", &file_count);
        if (!files || file_count == 0) {
            printf("No test files found in %s\n", path);
            l_free_file_list(files, file_count);
            return 0;
        }

        for (int i = 0; i < file_count; i++) {
            test_summary_t file_summary = _run_tests_in_file(files[i]);
            summary.successes += file_summary.successes;
            summary.failures += file_summary.failures;
            summary.errors += file_summary.errors;
        }

        l_free_file_list(files, file_count);
    } else {
        test_summary_t file_summary = _run_tests_in_file(path);
        summary.successes += file_summary.successes;
        summary.failures += file_summary.failures;
        summary.errors += file_summary.errors;
    }

    total = summary.failures + summary.errors + summary.successes;
    if (total == 0) {
        fprintf(stderr, "No tests run, 0 (100%%) skipped.\n");
    } else {
        double success_percent = ((double)summary.successes) / ((double)total) * 100.0;
        fprintf(stdout, "%d of %d (%0.0f%%) tests successful, 0 (0%%) test skipped.\n",
                summary.successes, total, success_percent);
    }

    if (summary.failures == 0 && summary.errors == 0) {
        return 0;
    }

    return 1;
}

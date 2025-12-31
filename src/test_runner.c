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

static int _run_single_test(const char* file_path, const char* test_name) {
    int result = 0;

    char* source = l_read_file(file_path);
    if (source == NULL) {
        fprintf(stderr, "Failed to read test file: %s\n", file_path);
        return 1;
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
        result = 1;
        goto cleanup;
    }

    InterpretResult run_result = l_run();
    if (run_result != INTERPRET_OK) {
        fprintf(stderr, "Runtime error in setup for %s\n", file_path);
        result = 1;
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

    if (call_result != INTERPRET_OK || state.failure_count > 0) {
        result = 1;
    }

cleanup:
    l_free_vm();
    l_free_memory();
    l_free_vmconfig(&config);
    free(source);
    return result;
}

static int _run_tests_in_file(const char* file_path) {
    int failures = 0;

    char* source = l_read_file(file_path);
    if (source == NULL) {
        fprintf(stderr, "Failed to read test file: %s\n", file_path);
        return 1;
    }

    test_list_t list = {0};
    _discover_tests(source, &list);
    free(source);

    if (list.count == 0) {
        printf("No tests found in %s\n", file_path);
        _test_list_free(&list);
        return 0;
    }

    printf("Running %d tests in %s\n", list.count, file_path);

    for (int i = 0; i < list.count; i++) {
        printf("TEST %s\n", list.names[i]);
        if (_run_single_test(file_path, list.names[i]) != 0) {
            failures++;
        }
    }

    _test_list_free(&list);
    return failures;
}

int l_run_tests(vm_config_t* base_config, const char* path) {
    (void)base_config;
    int failures = 0;

    if (_is_directory(path)) {
        int file_count = 0;
        char** files = l_scan_directory(path, "_test.sox", &file_count);
        if (!files || file_count == 0) {
            printf("No test files found in %s\n", path);
            l_free_file_list(files, file_count);
            return 0;
        }

        for (int i = 0; i < file_count; i++) {
            failures += _run_tests_in_file(files[i]);
        }

        l_free_file_list(files, file_count);
    } else {
        failures += _run_tests_in_file(path);
    }

    if (failures == 0) {
        printf("All tests passed.\n");
        return 0;
    }

    printf("Test failures: %d\n", failures);
    return 1;
}

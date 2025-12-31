#include "testing.h"

#include <stdio.h>
#include <string.h>

#include "lib/table.h"
#include "vm.h"

static const char* _test_message(int argCount, value_t* args) {
    if (argCount == 0) {
        return "test failure";
    }

    if (argCount != 1 || !IS_STRING(args[0])) {
        l_vm_runtime_error("Test helpers expect a single string message.");
        return NULL;
    }

    return AS_STRING(args[0])->chars;
}

static value_t _test_error(int argCount, value_t* args) {
    if (vm.test_state == NULL) {
        l_vm_runtime_error("t.Error used outside of test mode.");
        return NIL_VAL;
    }

    const char* message = _test_message(argCount, args);
    if (message == NULL) {
        return NIL_VAL;
    }

    vm.test_state->failure_count++;
    fprintf(stderr, "Error: %s\n", message);
    return NIL_VAL;
}

static value_t _test_fatal(int argCount, value_t* args) {
    if (vm.test_state == NULL) {
        l_vm_runtime_error("t.Fatal used outside of test mode.");
        return NIL_VAL;
    }

    const char* message = _test_message(argCount, args);
    if (message == NULL) {
        return NIL_VAL;
    }

    vm.test_state->failure_count++;
    vm.test_state->fatal_triggered = true;
    fprintf(stderr, "Error: %s\n", message);
    vm.runtime_error = true;
    return NIL_VAL;
}

obj_table_t* l_create_test_context(test_state_t* state) {
    (void)state;
    obj_table_t* table = l_new_table();

    l_table_set(&table->table, l_copy_string("Error", 5), OBJ_VAL(l_new_native(_test_error)));
    l_table_set(&table->table, l_copy_string("Fatal", 5), OBJ_VAL(l_new_native(_test_fatal)));

    return table;
}

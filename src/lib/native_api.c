#include "lib/native_api.h"

#include "lib/table.h"
#include "object.h"
#include "vm.h"

#include <string.h>

static obj_error_t* _native_error(const char * message) {
    l_vm_runtime_error(message);
    obj_string_t* msg = l_copy_string(message, strlen(message));
    return l_new_error(msg, NULL);
}

static value_t _new_table(int argCount, value_t* args) {
    obj_table_t* table = l_new_table();
    return OBJ_VAL(table);
}

static value_t _len(int argCount, value_t* args) {
    if (argCount > 1 || argCount == 0) {
        return OBJ_VAL(_native_error("len(): invalid parameter count"));
    }
    switch (args[0].type) {
        case VAL_OBJ: {
            obj_t* obj = AS_OBJ(args[0]);
            switch (obj->type) {
                case OBJ_TABLE: {
                    obj_table_t* table = AS_TABLE(args[0]);
                    return NUMBER_VAL(table->table.count);
                }
                // TODO: OBJ_LIST support in future
                case OBJ_BOUND_METHOD:
                case OBJ_CLASS:
                case OBJ_CLOSURE:
                case OBJ_FUNCTION:
                case OBJ_INSTANCE:
                case OBJ_NATIVE:
                case OBJ_STRING:
                case OBJ_UPVALUE:
                case OBJ_ERROR:
                    break;
            }
        }
        case VAL_BOOL:
        case VAL_NIL:
        case VAL_NUMBER:
            break;
    }
    return OBJ_VAL(_native_error("len(): invalid parameter type"));
}

void l_table_add_native() {
    l_vm_define_native("Table", _new_table);

    l_vm_define_native("len", _len);
    // l_vm_define_native("set", _set);
    // l_vm_define_native("get", _get);


    // TODO: Table functions
    // len()
    // add()
    // get()
    // set()
    // range()
}
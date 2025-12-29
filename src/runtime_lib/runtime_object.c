#include <stdio.h>
#include <string.h>

#include "runtime_memory.h"
#include "runtime_print.h"
#include "runtime_string.h"
#include "runtime_object.h"
#include "runtime_value.h"
#include "runtime_context.h"

// Thread-local runtime context (defined in runtime_context.c)
extern __thread sox_runtime_context_t* _sox_runtime_ctx;

#define ALLOCATE_OBJ(type, objectType) \
    (type*)_allocate_object(sizeof(type), objectType)

static runtime_obj_t* _allocate_object(size_t size, RuntimeObjType type) {
    runtime_obj_t* object = (runtime_obj_t*)runtime_malloc(size);
    object->type = type;
    object->is_marked = false;
    object->next = NULL;

    // Note: No l_add_object() call - we're not doing GC tracking in runtime library
    return object;
}

runtime_obj_bound_method_t* runtime_new_bound_method(value_t receiver, runtime_obj_closure_t* method) {
    runtime_obj_bound_method_t* bound = ALLOCATE_OBJ(runtime_obj_bound_method_t, RUNTIME_OBJ_BOUND_METHOD);
    bound->receiver = receiver;
    bound->method = method;
    return bound;
}

runtime_obj_class_t* runtime_new_class(runtime_obj_string_t* name) {
    runtime_obj_class_t* klass = ALLOCATE_OBJ(runtime_obj_class_t, RUNTIME_OBJ_CLASS);
    klass->name = name;
    runtime_init_table(&klass->methods);
    return klass;
}

runtime_obj_instance_t* runtime_new_instance(runtime_obj_class_t* klass) {
    runtime_obj_instance_t* instance = ALLOCATE_OBJ(runtime_obj_instance_t, RUNTIME_OBJ_INSTANCE);
    instance->klass = klass;
    runtime_init_table(&instance->fields);
    return instance;
}

runtime_obj_closure_t* runtime_new_closure(runtime_obj_function_t* function) {
    runtime_obj_upvalue_t** upvalues = RUNTIME_ALLOCATE(runtime_obj_upvalue_t*, function->upvalue_count);
    for (int i = 0; i < function->upvalue_count; i++) {
        upvalues[i] = NULL;
    }

    runtime_obj_closure_t* closure = ALLOCATE_OBJ(runtime_obj_closure_t, RUNTIME_OBJ_CLOSURE);
    closure->function = function;
    closure->upvalues = upvalues;
    closure->upvalue_count = function->upvalue_count;

    return closure;
}

runtime_obj_closure_t* runtime_new_closure_empty(int upvalue_count) {
    runtime_obj_upvalue_t** upvalues = RUNTIME_ALLOCATE(runtime_obj_upvalue_t*, upvalue_count);
    for (int i = 0; i < upvalue_count; i++) {
        upvalues[i] = NULL;
    }

    runtime_obj_closure_t* closure = ALLOCATE_OBJ(runtime_obj_closure_t, RUNTIME_OBJ_CLOSURE);
    closure->function = NULL;
    closure->upvalues = upvalues;
    closure->upvalue_count = upvalue_count;

    return closure;
}

runtime_obj_function_t* runtime_new_function(void) {
    runtime_obj_function_t* function = ALLOCATE_OBJ(runtime_obj_function_t, RUNTIME_OBJ_FUNCTION);
    function->arity = 0;
    function->upvalue_count = 0;
    function->name = NULL;
    runtime_init_chunk(&function->chunk);
    return function;
}

runtime_obj_native_t* runtime_new_native(runtime_native_func_t function) {
    runtime_obj_native_t* native = ALLOCATE_OBJ(runtime_obj_native_t, RUNTIME_OBJ_NATIVE);
    native->function = function;
    return native;
}

// String allocation helper with optional interning
static runtime_obj_string_t* _allocate_string(char* chars, size_t length, uint32_t hash) {
    runtime_obj_string_t* string = ALLOCATE_OBJ(runtime_obj_string_t, RUNTIME_OBJ_STRING);
    string->length = length;
    string->chars = chars;
    string->hash = hash;

    // Optionally intern in context
    // NOTE: No l_push()/l_pop() calls - we don't need VM stack protection
    sox_runtime_context_t* ctx = _sox_runtime_ctx;
    if (ctx && ctx->enable_interning && ctx->string_pool) {
        runtime_table_set(ctx->string_pool, string, NIL_VAL);
    }

    return string;
}

runtime_obj_string_t* runtime_take_string(char* chars, size_t length) {
    uint32_t hash = runtime_hash_string(chars, length);

    // Check for interned string if context exists
    sox_runtime_context_t* ctx = _sox_runtime_ctx;
    if (ctx && ctx->enable_interning && ctx->string_pool) {
        runtime_obj_string_t* interned = runtime_table_find_string(ctx->string_pool, chars, length, hash);
        if (interned != NULL) {
            RUNTIME_FREE_ARRAY(char, chars, length + 1);
            return interned;
        }
    }

    return _allocate_string(chars, length, hash);
}

runtime_obj_string_t* runtime_copy_string(const char* chars, size_t length) {
    uint32_t hash = runtime_hash_string(chars, length);

    // Check for interned string if context exists
    sox_runtime_context_t* ctx = _sox_runtime_ctx;
    if (ctx && ctx->enable_interning && ctx->string_pool) {
        runtime_obj_string_t* interned = runtime_table_find_string(ctx->string_pool, chars, length, hash);
        if (interned != NULL)
            return interned;
    }

    char* heapChars = RUNTIME_ALLOCATE(char, length + 1);
    memcpy(heapChars, chars, length);
    heapChars[length] = '\0';
    return _allocate_string(heapChars, length, hash);
}

runtime_obj_string_t* runtime_new_string(const char* chars) {
    return runtime_copy_string(chars, strlen(chars));
}

runtime_obj_upvalue_t* runtime_new_upvalue(value_t* slot) {
    runtime_obj_upvalue_t* upvalue = ALLOCATE_OBJ(runtime_obj_upvalue_t, RUNTIME_OBJ_UPVALUE);
    upvalue->location = slot;
    upvalue->closed = NIL_VAL;
    upvalue->next = NULL;
    return upvalue;
}

runtime_obj_table_t* runtime_new_table(void) {
    runtime_obj_table_t* table = ALLOCATE_OBJ(runtime_obj_table_t, RUNTIME_OBJ_TABLE);
    runtime_init_table(&table->table);
    return table;
}

runtime_obj_error_t* runtime_new_error(runtime_obj_string_t* msg, runtime_obj_error_t* enclosed) {
    runtime_obj_error_t* error = ALLOCATE_OBJ(runtime_obj_error_t, RUNTIME_OBJ_ERROR);
    error->msg = runtime_copy_string(msg->chars, msg->length);
    error->enclosed = (enclosed != NULL) ? enclosed : NULL;
    return error;
}

runtime_obj_array_t* runtime_new_array(void) {
    runtime_obj_array_t* array = ALLOCATE_OBJ(runtime_obj_array_t, RUNTIME_OBJ_ARRAY);

    runtime_init_value_array(&array->values);
    array->start = 0;
    array->end = 0;

    return array;
}

runtime_obj_array_t* runtime_copy_array(runtime_obj_array_t* array, int start, int end) {
    // Validate the array parameters
    if (start >= end) {
        return NULL;
    }

    // Validate the size source array with the start + end parameters
    if ((array->values.count < start) || (start < 0) ||
        (array->values.count < end) || (end < 0)) {
        return NULL;
    }

    runtime_obj_array_t* copy_array = runtime_new_array();

    // Setup the array size
    int size = (end - start) + 1;
    size_t new_capacity = runtime_calculate_capacity_with_size(copy_array->values.capacity, copy_array->values.count + size);
    copy_array->values.values = RUNTIME_GROW_ARRAY(value_t, copy_array->values.values, copy_array->values.capacity, new_capacity);
    copy_array->values.capacity = new_capacity;

    // Copy the array contents to the new array
    memcpy(copy_array->values.values, array->values.values + start, size * sizeof(value_t));
    copy_array->values.count = size;

    return copy_array;
}

runtime_obj_array_t* runtime_push_array(runtime_obj_array_t* array, value_t value) {
    runtime_write_value_array(&array->values, value);
    return array;
}

runtime_obj_array_t* runtime_push_array_front(runtime_obj_array_t* array, value_t value) {
    runtime_write_value_array_front(&array->values, value);
    return array;
}

value_t runtime_pop_array(runtime_obj_array_t* array) {
    if (array->values.count == 0) {
        return NIL_VAL;
    }

    value_t value = array->values.values[array->values.count - 1];
    array->values.count--;
    return value;
}

// Print helper for arrays
static void _print_array(runtime_obj_array_t* array) {
    value_t* values = array->values.values;

    runtime_printf("[");

    for (size_t i = 0; i < array->values.count; i++) {
        runtime_print_value(values[i]);
        if (i < array->values.count - 1)
            runtime_printf(",");
    }
    runtime_printf("]");
}

void runtime_print_table(runtime_obj_table_t* table) {
    runtime_table_t* t = &table->table;

    runtime_printf("{");

    bool needSeparate = false;
    for (int i = 0; i < t->capacity; i++) {
        runtime_entry_t* entry = &t->entries[i];
        if (entry->key == NULL)
            continue;

        if (needSeparate && i != 0)
            runtime_printf(",");

        runtime_printf("{\"%s\":\"", entry->key->chars);
        runtime_print_value(entry->value);
        runtime_printf("\"}");
        needSeparate = true;
    }
    runtime_printf("}");
}

void runtime_print_error(runtime_obj_error_t* error) {
    runtime_printf("error: %s", error->msg->chars);
    if (error->enclosed != NULL) {
        runtime_print_error(error->enclosed);
    }
}

// Print helper for functions
static void _print_function(runtime_obj_function_t* function) {
    if (function->name == NULL) {
        runtime_printf("<script>");
        return;
    }
    runtime_printf("<fn %s>", function->name->chars);
}

void runtime_print_object(value_t value) {
    if (value.as.obj == NULL) {
        runtime_printf("<NULL: ERROR>");
        return;
    }

    switch (RUNTIME_OBJ_TYPE(value)) {
        case RUNTIME_OBJ_BOUND_METHOD:
            _print_function(RUNTIME_AS_BOUND_METHOD(value)->method->function);
            break;
        case RUNTIME_OBJ_CLASS:
            runtime_printf("<class: %s>", RUNTIME_AS_CLASS(value)->name->chars);
            break;
        case RUNTIME_OBJ_CLOSURE:
            _print_function(RUNTIME_AS_CLOSURE(value)->function);
            break;
        case RUNTIME_OBJ_FUNCTION:
            _print_function(RUNTIME_AS_FUNCTION(value));
            break;
        case RUNTIME_OBJ_INSTANCE:
            runtime_printf("<instance: %s> ", RUNTIME_AS_INSTANCE(value)->klass->name->chars);
            break;
        case RUNTIME_OBJ_NATIVE:
            runtime_printf("<native fn>");
            break;
        case RUNTIME_OBJ_STRING:
            runtime_printf("%s", RUNTIME_AS_CSTRING(value));
            break;
        case RUNTIME_OBJ_UPVALUE:
            runtime_printf("<upvalue>");
            break;
        case RUNTIME_OBJ_TABLE:
            runtime_printf("<table>");
            runtime_print_table(RUNTIME_AS_TABLE(value));
            break;
        case RUNTIME_OBJ_ERROR:
            runtime_print_error(RUNTIME_AS_ERROR(value));
            break;
        case RUNTIME_OBJ_ARRAY:
            _print_array(RUNTIME_AS_ARRAY(value));
            break;
    }
}

// Chunk management functions
void runtime_init_chunk(runtime_chunk_t* chunk) {
    chunk->count = 0;
    chunk->capacity = 0;
    chunk->code = NULL;
    chunk->lines = NULL;
    runtime_init_value_array(&chunk->constants);
}

void runtime_free_chunk(runtime_chunk_t* chunk) {
    RUNTIME_FREE_ARRAY(uint8_t, chunk->code, chunk->capacity);
    RUNTIME_FREE_ARRAY(int, chunk->lines, chunk->capacity);
    runtime_free_value_array(&chunk->constants);
    runtime_init_chunk(chunk);
}

#include <stdio.h>
#include <string.h>

#include "lib/memory.h"
#include "lib/string.h"
#include "object.h"
#include "value.h"
#include "vm.h"

#define ALLOCATE_OBJ(type, objectType) \
    (type*)_allocate_object(sizeof(type), objectType)

static obj_t* _allocate_object(size_t size, ObjType type) {
    obj_t* object = (obj_t*)reallocate(NULL, 0, size);
    object->type = type;
    object->is_marked = false;
    l_add_object(object);

#ifdef DEBUG_LOG_GC
    printf("%p allocate %zu for %s\n", (void*)object, size, obj_type_to_string[type]);
#endif
    return object;
}

obj_bound_method_t* l_new_bound_method(value_t receiver, obj_closure_t* method) {
    obj_bound_method_t* bound = ALLOCATE_OBJ(obj_bound_method_t, OBJ_BOUND_METHOD);
    bound->receiver = receiver;
    bound->method = method;
    return bound;
}

obj_class_t* l_new_class(obj_string_t* name) {
    obj_class_t* klass = ALLOCATE_OBJ(obj_class_t, OBJ_CLASS);
    klass->name = name;
    l_init_table(&klass->methods);
    return klass;
}

obj_instance_t* l_new_instance(obj_class_t* klass) {
    obj_instance_t* instance = ALLOCATE_OBJ(obj_instance_t, OBJ_INSTANCE);
    instance->klass = klass;
    l_init_table(&instance->fields);
    return instance;
}

obj_closure_t* l_new_closure(obj_function_t* function) {
    obj_upvalue_t** upvalues = ALLOCATE(obj_upvalue_t*, function->upvalue_count);
    for (int i = 0; i < function->upvalue_count; i++) {
        upvalues[i] = NULL;
    }

    obj_closure_t* closure = ALLOCATE_OBJ(obj_closure_t, OBJ_CLOSURE);
    closure->function = function;
    closure->upvalues = upvalues;
    closure->upvalue_count = function->upvalue_count;

    return closure;
}

obj_closure_t* l_new_closure_empty(int upvalue_count) {
    
    obj_upvalue_t** upvalues = ALLOCATE(obj_upvalue_t*, upvalue_count);
    for (int i = 0; i < upvalue_count; i++) {
        upvalues[i] = NULL;
    }

    obj_closure_t* closure = ALLOCATE_OBJ(obj_closure_t, OBJ_CLOSURE);
    closure->function = NULL;
    closure->upvalues = upvalues;
    closure->upvalue_count = upvalue_count;

    return closure;
}

obj_function_t* l_new_function() {
    obj_function_t* function = ALLOCATE_OBJ(obj_function_t, OBJ_FUNCTION);
    function->arity = 0;
    function->upvalue_count = 0;
    function->name = NULL;
    l_init_chunk(&function->chunk);
    return function;
}

obj_native_t* l_new_native(native_func_t function) {
    obj_native_t* native = ALLOCATE_OBJ(obj_native_t, OBJ_NATIVE);
    native->function = function;
    return native;
}

static obj_string_t* _allocate_string(char* chars, int length, uint32_t hash) {
    obj_string_t* string = ALLOCATE_OBJ(obj_string_t, OBJ_STRING);
    string->length = length;
    string->chars = chars;
    string->hash = hash;
    l_push(OBJ_VAL(string));
    l_table_set(&vm.strings, string, NIL_VAL);
    l_pop();
    return string;
}

obj_string_t* l_take_string(char* chars, int length) {
    uint32_t hash = l_hash_string(chars, length);

    obj_string_t* interned = l_table_find_string(&vm.strings, chars, length, hash);
    if (interned != NULL) {
        FREE_ARRAY(char, chars, length + 1);
        return interned;
    }
    return _allocate_string(chars, length, hash);
}

obj_string_t* l_copy_string(const char* chars, int length) {
    uint32_t hash = l_hash_string(chars, length);

    obj_string_t* interned = l_table_find_string(&vm.strings, chars, length, hash);
    if (interned != NULL) 
        return interned;

    char* heapChars = ALLOCATE(char, length + 1);
    memcpy(heapChars, chars, length);
    heapChars[length] = '\0';
    return _allocate_string(heapChars, length, hash);
}

obj_upvalue_t*  l_new_upvalue(value_t* slot) {
    obj_upvalue_t* upvalue = ALLOCATE_OBJ(obj_upvalue_t, OBJ_UPVALUE);
    upvalue->location = slot;
    upvalue->closed = NIL_VAL;
    upvalue->next = NULL;
    return upvalue;
}

obj_table_t* l_new_table() {
    obj_table_t* table = ALLOCATE_OBJ(obj_table_t, OBJ_TABLE);
    l_init_table(&table->table);
    return table;
}

obj_error_t* l_new_error(obj_string_t* msg, obj_error_t* enclosed) {
    obj_error_t* error = ALLOCATE_OBJ(obj_error_t, OBJ_ERROR);
    error->msg = l_copy_string(msg->chars, msg->length);
    error->enclosed = (enclosed != NULL) ? enclosed : NULL;
    return error;
}

void l_print_table(obj_table_t* table) {
    // TODO: For each entry in table print <key> : var -> l_print_object
    table_t* t = &table->table;

    printf("{");

    bool needSeparate = false;
    for (int i = 0; i < t->capacity; i++) {
        entry_t* entry = &t->entries[i];
        if (entry->key == NULL) 
            continue;

        if ( needSeparate && i != 0 )
            printf(",");
        
        printf("{\"%s\":\"", entry->key->chars);
        l_print_value(entry->value);
        printf("\"}");      
        needSeparate = true;  
    }
    printf("}");
}

void l_print_error(obj_error_t* error) {
    printf("error: %s", error->msg->chars);
    if (error->enclosed != NULL) {
        l_print_error(error->enclosed);
    }
}

static void _print_function(obj_function_t* function) {
    if (function->name == NULL) {
        printf("<script>");
        return;
    }
    printf("<fn %s>", function->name->chars);
}

void l_print_object(value_t value) {
    switch (OBJ_TYPE(value)) {
        case OBJ_BOUND_METHOD:
            _print_function(AS_BOUND_METHOD(value)->method->function);
            break;
        case OBJ_CLASS:
            printf("<class: %s>", AS_CLASS(value)->name->chars);
        break;
        case OBJ_CLOSURE:
            _print_function(AS_CLOSURE(value)->function);
            break;
        case OBJ_FUNCTION:
            _print_function(AS_FUNCTION(value));
            break;
        case OBJ_INSTANCE:
            printf("<instance: %s> ", AS_INSTANCE(value)->klass->name->chars);
            break;
        case OBJ_NATIVE:
            printf("<native fn>");
            break;
        case OBJ_STRING:
            printf("%s", AS_CSTRING(value));
            break;
        case OBJ_UPVALUE:
            printf("<upvalue>");
            break;
        case OBJ_TABLE:
            printf("<table>");
            l_print_table(AS_TABLE(value));
            break;
        case OBJ_ERROR:
            l_print_error(AS_ERROR(value));
            break;
    }
}
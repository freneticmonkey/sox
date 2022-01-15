#include <stdlib.h>

#include "lib/memory.h"
#include "vm.h"

#ifdef DEBUG_LOG_GC
#include <stdio.h>
#include "debug.h"
#endif

#define GC_HEAP_GROW_FACTOR 2

static size_t _internal_alloc = 0;
static size_t _internal_dealloc = 0;
static size_t _internal_vm_alloc_max = 0;

void* reallocate(void* pointer, size_t oldSize, size_t newSize) {

    // deallocating
    size_t alloc_size = newSize - oldSize;
#ifdef DEBUG_LOG_GC
    size_t vm_bytes = vm.bytes_allocated;
#endif

    if ( (newSize < oldSize) && ( (SIZE_MAX - alloc_size + 1) > vm.bytes_allocated ) ) {
        printf("detected untracked vm memory. vm bytes: %zu. dealloc bytes: %zu\n", 
                vm.bytes_allocated, 
                (SIZE_MAX - alloc_size + 1)
        );
        printf("internal tracking. alloc bytes: %zu. dealloc bytes: %zu vm max alloc: %zu\n", 
                _internal_alloc, 
                _internal_dealloc,
                _internal_vm_alloc_max
        );
        exit(1);
    }

    if (newSize > oldSize) {
        _internal_alloc += alloc_size;
    } else {
        _internal_dealloc += (SIZE_MAX - alloc_size + 1);
    }   

    if ( vm.bytes_allocated > _internal_vm_alloc_max ) {
        _internal_vm_alloc_max = vm.bytes_allocated;
    }

    vm.bytes_allocated += alloc_size;
    if (newSize > oldSize) {
#ifdef DEBUG_STRESS_GC
        l_collect_garbage();
#endif
    }

    if (vm.bytes_allocated > vm.next_gc) {
        l_collect_garbage();
    }

    if (newSize == 0) {

#ifdef DEBUG_LOG_GC
    if (pointer == NULL) {
        printf("[free] <(nil)            vm bytes: %zu->%zu.\t dealloc bytes:-%zu\n",
                vm_bytes,
                vm.bytes_allocated,
                (SIZE_MAX - alloc_size + 1)
        );

    } else {
        printf("[free] <%p\t vm bytes: %zu->%zu.\t dealloc bytes:-%zu\n",
                pointer,
                vm_bytes,
                vm.bytes_allocated,
                (SIZE_MAX - alloc_size + 1)
        );
    }
    
#endif
        free(pointer);
        return NULL;
    }

    void* result = realloc(pointer, newSize);

    if (result == NULL)
        exit(1);

#ifdef DEBUG_LOG_GC
    printf("[new]  >%p\t vm bytes: %zu->%zu.\t alloc bytes: %zu\n",
            result,
            vm_bytes,
            vm.bytes_allocated, 
            alloc_size
    );
#endif

    return result;
}

void l_mark_object(obj_t* object) {
    if (object == NULL) 
        return;

    if (object->is_marked) 
        return;
    
#ifdef DEBUG_LOG_GC
    printf("%p mark ", (void*)object);
    l_print_value(OBJ_VAL(object));
    printf("\n");
#endif

    object->is_marked = true;

    if (vm.gray_capacity < vm.gray_count + 1) {
        vm.gray_capacity = GROW_CAPACITY(vm.gray_capacity);
        vm.gray_stack = (obj_t**)realloc(vm.gray_stack, sizeof(obj_t*) * vm.gray_capacity);

        if (vm.gray_stack == NULL) 
            exit(1);
    }

    vm.gray_stack[vm.gray_count++] = object;
}

void l_mark_value(value_t value) {
  if (IS_OBJ(value))
    l_mark_object(AS_OBJ(value));
}

static void l_mark_array(value_array_t* array) {
    for (int i = 0; i < array->count; i++) {
        l_mark_value(array->values[i]);
    }
}

static void _blacken_object(obj_t* object) {
    if (object == NULL) {
        return;
    }

#ifdef DEBUG_LOG_GC
    printf("%p blacken ", (void*)object);
    l_print_value(OBJ_VAL(object));
    printf("\n");
#endif

    switch (object->type) {
        case OBJ_BOUND_METHOD: {
            obj_bound_method_t* bound = (obj_bound_method_t*)object;
            l_mark_value(bound->receiver);
            l_mark_object((obj_t*)bound->method);
            break;
        }
        case OBJ_CLASS: {
            obj_class_t* klass = (obj_class_t*)object;
            l_mark_object((obj_t*)klass->name);
            l_mark_table(&klass->methods);
            break;
        }
        case OBJ_CLOSURE: {
            obj_closure_t* closure = (obj_closure_t*)object;
            l_mark_object((obj_t*)closure->function);
            for (int i = 0; i < closure->upvalue_count; i++) {
                l_mark_object((obj_t*)closure->upvalues[i]);
            }
            break;
        }
        case OBJ_FUNCTION: {
            obj_function_t* function = (obj_function_t*)object;
            l_mark_object((obj_t*)function->name);
            l_mark_array(&function->chunk.constants);
            break;
        }
        case OBJ_INSTANCE: {
            obj_instance_t* instance = (obj_instance_t*)object;
            l_mark_object((obj_t*)instance->klass);
            l_mark_table(&instance->fields);
            break;
        }
        case OBJ_UPVALUE:
            l_mark_value(((obj_upvalue_t*)object)->closed);
            break;
        case OBJ_TABLE: {
            obj_table_t* table = (obj_table_t*)object;
            l_mark_object((obj_t*)table);
            l_mark_table(&table->table);
            break;
        }
        case OBJ_ERROR: {
            obj_error_t* error = (obj_error_t*)object;
            l_mark_object((obj_t*)error->enclosed);
            l_mark_object((obj_t*)error->msg);
            l_mark_object((obj_t*)error);
        }
        case OBJ_NATIVE:
        case OBJ_STRING:
        break;
    }
}

static void _free_object(obj_t* object) {

    if (object == NULL) {
        return;
    }

#ifdef DEBUG_LOG_GC

    size_t free_size = 0;

    switch (object->type) {
        case OBJ_BOUND_METHOD:
            free_size = sizeof(obj_bound_method_t);
            break;
        case OBJ_CLASS: {
            free_size = sizeof(obj_class_t);
            break;
        } 
        case OBJ_CLOSURE: {
            free_size = sizeof(obj_closure_t);
            break;
        }
        case OBJ_FUNCTION: {
            free_size = sizeof(obj_function_t);
            break;
        }
        case OBJ_INSTANCE: {
            free_size = sizeof(obj_instance_t);
            break;
        }
        case OBJ_NATIVE: {
            free_size = sizeof(obj_native_t);
            break;
        }
        case OBJ_STRING: {
            free_size = sizeof(obj_string_t);
            break;
        }
        case OBJ_UPVALUE:
            free_size = sizeof(obj_upvalue_t);
            break;
        case OBJ_TABLE:
            free_size = sizeof(obj_table_t);
            break;
        case OBJ_ERROR:
            free_size = sizeof(obj_error_t);
            break;
    }

    printf("%p free %zu type %s\n",
            (void*)object, 
            free_size,
            obj_type_to_string[object->type]
    );
#endif

    switch (object->type) {
        case OBJ_BOUND_METHOD:
            FREE(obj_bound_method_t, object);
            break;
        case OBJ_CLASS: {
            obj_class_t* klass = (obj_class_t*)object;
            l_free_table(&klass->methods);
            FREE(obj_class_t, object);
            break;
        } 
        case OBJ_CLOSURE: {
            obj_closure_t* closure = (obj_closure_t*)object;
            FREE_ARRAY(obj_upvalue_t*, closure->upvalues, closure->upvalue_count);
            FREE(obj_closure_t, object);
            break;
        }
        case OBJ_FUNCTION: {
            obj_function_t* function = (obj_function_t*)object;
            l_free_chunk(&function->chunk);
            FREE(obj_function_t, object);
            break;
        }
        case OBJ_INSTANCE: {
            obj_instance_t* instance = (obj_instance_t *)object;
            l_free_table(&instance->fields);
            FREE(obj_instance_t, object);
            break;
        }
        case OBJ_NATIVE: {
            FREE(obj_native_t, object);
            break;
        }
        case OBJ_STRING: {
            obj_string_t* string = (obj_string_t*)object;
            FREE_ARRAY(char, string->chars, string->length + 1);
            FREE(obj_string_t, object);
            break;
        }
        case OBJ_UPVALUE:
            FREE(obj_upvalue_t, object);
            break;

        case OBJ_TABLE: {
            obj_table_t* table = (obj_table_t *)object;
            l_free_table(&table->table);
            FREE(obj_table_t, table);
            break;
        }
        case OBJ_ERROR: {
            obj_error_t* error = (obj_error_t *)object;
            _free_object((obj_t*)error->enclosed);
            _free_object((obj_t*)error->msg);
            FREE(obj_error_t, object);
            break;
        }
    }
}

// Garbage Collection
static void _mark_roots() {
    
    // mark the stack
    for (value_t* slot = vm.stack; slot < vm.stack_top; slot++) {
        l_mark_value(*slot);
    }

    // mark the call stack frames
    for (int i = 0; i < vm.frame_count; i++) {
        l_mark_object(
            (obj_t*)vm.frames[i].closure
        );
    }

    // mark any upvalues
    for (obj_upvalue_t* upvalue = vm.open_upvalues;
                                  upvalue != NULL;
                                  upvalue = upvalue->next) {
        l_mark_object((obj_t*)upvalue);
    }

    // mark any globals
    l_mark_table(&vm.globals);

    // ensure that compiler owned memory is also tracked
    l_mark_compiler_roots();

    l_mark_object((obj_t*)vm.init_string);
}

static void _trace_references() {
    while (vm.gray_count > 0) {
        obj_t* object = vm.gray_stack[--vm.gray_count];
        _blacken_object(object);
    }
}

static void _sweep() {
    obj_t* previous = NULL;
    obj_t* object = vm.objects;
    while (object != NULL) {
        if (object->is_marked) {
            object->is_marked = false;
            previous = object;
            object = object->next;
        } else {
            obj_t* unreached = object;
            object = object->next;

            if (previous != NULL) {
                previous->next = object;
            } else {
                vm.objects = object;
            }

            _free_object(unreached);
        }
    }
}


void  l_collect_garbage() {
#ifdef DEBUG_LOG_GC
    printf("-- gc begin\n");
    size_t before = vm.bytes_allocated;
#endif

    _mark_roots();

    _trace_references();

    l_table_remove_white(&vm.strings);

    _sweep();

    vm.next_gc = vm.bytes_allocated * GC_HEAP_GROW_FACTOR;

#ifdef DEBUG_LOG_GC
    printf("-- gc end\n");
    printf("   collected %zu bytes (from %zu to %zu) next at %zu\n",
         before - vm.bytes_allocated, before, vm.bytes_allocated,
         vm.next_gc);
#endif

}

void l_free_objects() {
    obj_t* object = vm.objects;
    while (object != NULL) {
        obj_t* next = object->next;
        _free_object(object);
        object = next;
    }

    free(vm.gray_stack);
}
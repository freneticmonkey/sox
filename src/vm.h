#ifndef SOX_VM_H
#define SOX_VM_H

#include "object.h"
#include "lib/table.h"
#include "value.h"

#define FRAMES_MAX 64
#define STACK_MAX (FRAMES_MAX * UINT8_COUNT)

typedef struct {
    obj_closure_t* closure;
    uint8_t* ip;
    value_t* slots;
} callframe_t;

typedef struct {
    callframe_t frames[FRAMES_MAX];
    int frame_count;

    value_t  stack[STACK_MAX];
    value_t* stack_top;
    table_t  globals;
    table_t  strings;
    obj_t*   objects;
    obj_upvalue_t* open_upvalues;
    obj_string_t*  init_string;

    // garbage collection
    size_t bytes_allocated;
    size_t next_gc;
    int    gray_count;
    int    gray_capacity;
    obj_t** gray_stack;

} vm_t;

typedef enum {
    INTERPRET_OK,
    INTERPRET_COMPILE_ERROR,
    INTERPRET_RUNTIME_ERROR
} InterpretResult;

// exposing the vm instance
extern vm_t vm;

void l_init_vm();
void l_free_vm();

void    l_push(value_t value);
value_t l_pop();

InterpretResult l_interpret(const char * source);

void l_vm_define_native(const char* name, native_func_t function);

void l_vm_runtime_error(const char* format, ...);

#endif
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "lib/memory.h"
#include "lib/debug.h"
#include "common.h"
#include "compiler.h"
#include "vm.h"

vm_t vm;

static value_t _clock_native(int argCount, value_t* args) {
    return NUMBER_VAL((double)clock() / CLOCKS_PER_SEC);
}

static value_t _usleep_native(int argCount, value_t* args) {
    if ( argCount == 1 && IS_NUMBER(args[0]) ) {
        return NUMBER_VAL(usleep((unsigned int)AS_NUMBER(args[0])));
    }
    return NUMBER_VAL(-1);
}

static value_t _peek(int distance);
static bool    _call(obj_closure_t* closure, int argCount);
static bool    _call_value(value_t callee, int argCount);
static bool    _invoke(obj_string_t* name, int argCount);
static bool    _invoke_from_class(obj_class_t* klass, obj_string_t* name, int argCount);
static bool    _bind_method(obj_class_t* klass, obj_string_t* name);

static obj_upvalue_t* _capture_upvalue(value_t* local);
static void    _close_upvalues(value_t* last);
static void    _define_method(obj_string_t* name);
static bool    _is_falsey(value_t value);
static void    _concatenate();

static void _reset_stack() {
    vm.stack_top = vm.stack;
    vm.frame_count = 0;
    vm.open_upvalues = NULL;
}

static void _runtime_error(const char* format, ...) {
    va_list args;
    va_start(args, format);
    vfprintf(stderr, format, args);
    va_end(args);
    fputs("\n", stderr);

    callframe_t* frame = &vm.frames[vm.frame_count - 1];
    size_t instruction = frame->ip - frame->closure->function->chunk.code - 1;
    int line = frame->closure->function->chunk.lines[instruction];
    fprintf(stderr, "[line %d] in script\n", line);

    for (int i = vm.frame_count - 1; i >= 0; i--) {
        callframe_t* frame = &vm.frames[i];
        obj_function_t* function = frame->closure->function;
        size_t instruction = frame->ip - function->chunk.code - 1;
        fprintf(stderr, "[line %d] in ", function->chunk.lines[instruction]);
        if (function->name == NULL) {
            fprintf(stderr, "script\n");
        } else {
            fprintf(stderr, "%s()\n", function->name->chars);
        }
    }
    _reset_stack();
}

static void _define_native(const char* name, native_func_t function) {
    l_push(OBJ_VAL(l_copy_string(name, (int)strlen(name))));
    l_push(OBJ_VAL(l_new_native(function)));
    l_table_set(&vm.globals, AS_STRING(vm.stack[0]), vm.stack[1]);
    l_pop();
    l_pop();
}

void l_init_vm() {
    _reset_stack();
    vm.objects = NULL;

    // garbage collection
    vm.bytes_allocated = 0;
    vm.next_gc = 1024 * 1024;
    vm.gray_count = 0;
    vm.gray_capacity = 0;
    vm.gray_stack = NULL;

    l_init_table(&vm.globals);
    l_init_table(&vm.strings);

    vm.init_string = NULL;
    vm.init_string = l_copy_string("init", 4);


    // native functions
    _define_native("clock", _clock_native);
    _define_native("usleep", _usleep_native);
}

void l_free_vm() {
    l_free_table(&vm.strings);
    l_free_table(&vm.globals);
    vm.init_string = NULL;
    l_free_objects();
}

static InterpretResult _run() {
    callframe_t* frame = &vm.frames[vm.frame_count - 1];

#define READ_BYTE() (*frame->ip++)
#define READ_CONSTANT() (frame->closure->function->chunk.constants.values[READ_BYTE()])
#define READ_SHORT() \
    (frame->ip += 2, (uint16_t)((frame->ip[-2] << 8) | frame->ip[-1]))
#define READ_STRING() AS_STRING(READ_CONSTANT())
#define BINARY_OP(valueType, op) \
    do { \
        if (!IS_NUMBER(_peek(0)) || !IS_NUMBER(_peek(1))) { \
            _runtime_error("Operands must be numbers."); \
            return INTERPRET_RUNTIME_ERROR; \
        } \
        double b = AS_NUMBER(l_pop()); \
        double a = AS_NUMBER(l_pop()); \
        l_push(valueType(a op b)); \
    } while (false)



    for (;;) {

#ifdef DEBUG_TRACE_EXECUTION
        printf("          ");
        for (value_t* slot = vm.stack; slot < vm.stack_top; slot++) {
            printf("[ ");
            l_print_value(*slot);
            printf(" ]");
        }
        printf("\n");
        
        l_disassemble_instruction(
            &frame->closure->function->chunk, 
            (int)(frame->ip - frame->closure->function->chunk.code)
        );
#endif

        uint8_t instruction;
        switch (instruction = READ_BYTE()) {
            case OP_CONSTANT: {
                value_t constant = READ_CONSTANT();
                l_push(constant);
                break;
            }
            case OP_NIL:   l_push(NIL_VAL); break;
            case OP_TRUE:  l_push(BOOL_VAL(true)); break;
            case OP_FALSE: l_push(BOOL_VAL(false)); break;
            case OP_POP:   l_pop(); break;
            case OP_GET_LOCAL: {
                uint8_t slot = READ_BYTE();
                l_push(frame->slots[slot]); 
                break;
            }
            case OP_SET_LOCAL: {
                uint8_t slot = READ_BYTE();
                frame->slots[slot] = _peek(0);
                break;
            }
            case OP_GET_GLOBAL: {
                obj_string_t* name = READ_STRING();
                value_t value;
                if (!l_table_get(&vm.globals, name, &value)) {
                    _runtime_error("Undefined variable '%s'.", name->chars);
                    return INTERPRET_RUNTIME_ERROR;
                }
                l_push(value);
                break;
            }
            case OP_DEFINE_GLOBAL: {
                obj_string_t* name = READ_STRING();
                l_table_set(&vm.globals, name, _peek(0));
                l_pop();
                break;
            }
            case OP_SET_GLOBAL: {
                obj_string_t* name = READ_STRING();
                if (l_table_set(&vm.globals, name, _peek(0))) {
                    l_table_delete(&vm.globals, name); 
                    _runtime_error("Undefined variable '%s'.", name->chars);
                    return INTERPRET_RUNTIME_ERROR;
                }
                break;
            }
            case OP_GET_UPVALUE: {
                uint8_t slot = READ_BYTE();
                l_push(*frame->closure->upvalues[slot]->location);
                break;
            }
            case OP_SET_UPVALUE: {
                uint8_t slot = READ_BYTE();
                *frame->closure->upvalues[slot]->location = _peek(0);
                break;
            }
            case OP_GET_PROPERTY: {
                if (!IS_INSTANCE(_peek(0))) {
                    _runtime_error("Only instances have properties.");
                    return INTERPRET_RUNTIME_ERROR;
                }

                obj_instance_t* instance = AS_INSTANCE(_peek(0));
                obj_string_t* name = READ_STRING();

                value_t value;
                if (l_table_get(&instance->fields, name, &value)) {
                    l_pop(); // Instance.
                    l_push(value);
                    break;
                }

                if (!_bind_method(instance->klass, name)) {
                    return INTERPRET_RUNTIME_ERROR;
                }
                break;
            }
            case OP_SET_PROPERTY: {
                if (!IS_INSTANCE(_peek(1))) {
                    _runtime_error("Only instances have properties.");
                    return INTERPRET_RUNTIME_ERROR;
                }

                obj_instance_t* instance = AS_INSTANCE(_peek(1));
                l_table_set(&instance->fields, READ_STRING(), _peek(0));
                value_t value = l_pop();
                l_pop();
                l_push(value);
                break;
            }
            case OP_GET_SUPER: {
                obj_string_t* name = READ_STRING();
                obj_class_t* superclass = AS_CLASS(l_pop());

                if (!_bind_method(superclass, name)) {
                    return INTERPRET_RUNTIME_ERROR;
                }
                break;
            }
            case OP_EQUAL: {
                value_t b = l_pop();
                value_t a = l_pop();
                l_push(BOOL_VAL(l_values_equal(a, b)));
                break;
            }
            case OP_GREATER:  BINARY_OP(BOOL_VAL, >); break;
            case OP_LESS:     BINARY_OP(BOOL_VAL, <); break;
            case OP_ADD: {
                if (IS_STRING(_peek(0)) && IS_STRING(_peek(1))) {
                    _concatenate();
                } 
                else if (IS_NUMBER(_peek(0)) && IS_NUMBER(_peek(1))) {
                    double b = AS_NUMBER(l_pop());
                    double a = AS_NUMBER(l_pop());
                    l_push(NUMBER_VAL(a + b));
                } 
                else {
                    _runtime_error(
                        "Operands must be two numbers or two strings.");
                    return INTERPRET_RUNTIME_ERROR;
                }
                break;
            }
            case OP_SUBTRACT: BINARY_OP(NUMBER_VAL, -); break;
            case OP_MULTIPLY: BINARY_OP(NUMBER_VAL, *); break;
            case OP_DIVIDE:   BINARY_OP(NUMBER_VAL, /); break;
            case OP_NOT:      l_push(BOOL_VAL(_is_falsey(l_pop()))); break;
            case OP_NEGATE: {
                if (!IS_NUMBER(_peek(0))) {
                    _runtime_error("Operand must be a number.");
                    return INTERPRET_RUNTIME_ERROR;
                }
                l_push(NUMBER_VAL(-AS_NUMBER(l_pop())));
                break;
            }
            case OP_PRINT: {
                l_print_value(l_pop());
                printf("\n");
                break;
            }
            case OP_JUMP: {
                uint16_t offset = READ_SHORT();
                frame->ip += offset;
                break;
            }
            case OP_JUMP_IF_FALSE: {
                uint16_t offset = READ_SHORT();
                if (_is_falsey(_peek(0))) 
                    frame->ip += offset;
                break;
            }
            case OP_LOOP: {
                uint16_t offset = READ_SHORT();
                frame->ip -= offset;
                break;
            }
            case OP_CALL: {
                int argCount = READ_BYTE();
                if (!_call_value(_peek(argCount), argCount)) {
                   return INTERPRET_RUNTIME_ERROR;
                }
                frame = &vm.frames[vm.frame_count - 1];
                break;
            }
            case OP_INVOKE: {
                obj_string_t* method = READ_STRING();
                int argCount = READ_BYTE();
                if (!_invoke(method, argCount)) {
                   return INTERPRET_RUNTIME_ERROR;
                }
                frame = &vm.frames[vm.frame_count - 1];
                break;
            }
            case OP_SUPER_INVOKE: {
                obj_string_t* method = READ_STRING();
                int argCount = READ_BYTE();
                obj_class_t* superclass = AS_CLASS(l_pop());
                if (!_invoke_from_class(superclass, method, argCount)) {
                    return INTERPRET_RUNTIME_ERROR;
                }
                frame = &vm.frames[vm.frame_count - 1];
                break;
            }
            case OP_CLOSURE: {
                obj_function_t* function = AS_FUNCTION(READ_CONSTANT());
                obj_closure_t*  closure = l_new_closure(function);
                l_push(OBJ_VAL(closure));

                for (int i = 0; i < closure->upvalue_count; i++) {
                    uint8_t isLocal = READ_BYTE();
                    uint8_t index = READ_BYTE();
                    if (isLocal) {
                        closure->upvalues[i] = _capture_upvalue(frame->slots + index);
                    } else {
                        closure->upvalues[i] = frame->closure->upvalues[index];
                    }
                }
                break;
            }
            case OP_CLOSE_UPVALUE:
                _close_upvalues(vm.stack_top - 1);
                l_pop();
                break;
            case OP_RETURN: {
                value_t result = l_pop();
                _close_upvalues(frame->slots);
                vm.frame_count--;
                if (vm.frame_count == 0) {
                    l_pop();
                    return INTERPRET_OK;
                }

                vm.stack_top = frame->slots;
                l_push(result);
                frame = &vm.frames[vm.frame_count - 1];
                break;
            }
            case OP_CLASS:
                l_push(OBJ_VAL(l_new_class(READ_STRING())));
                break;
            case OP_INHERIT: {
                value_t superclass = _peek(1);
                if (!IS_CLASS(superclass)) {
                    _runtime_error("Superclass must be a class.");
                    return INTERPRET_RUNTIME_ERROR;
                }
                obj_class_t* subclass = AS_CLASS(_peek(0));
                l_table_add_all(&AS_CLASS(superclass)->methods, &subclass->methods);
                l_pop(); // Subclass.
                break;
            }
            case OP_METHOD:
                _define_method(READ_STRING());
                break;
            break;
        }
    }

#undef READ_BYTE
#undef READ_STRING
#undef READ_SHORT
#undef READ_CONSTANT
#undef BINARY_OP
}

InterpretResult l_interpret(const char* source) {
    chunk_t chunk;
    l_init_chunk(&chunk);

    obj_function_t* function = l_compile(source);
    if (function == NULL) {
        return INTERPRET_COMPILE_ERROR;
    }

    l_push(OBJ_VAL(function));
    obj_closure_t* closure = l_new_closure(function);
    l_pop();
    l_push(OBJ_VAL(closure));
    _call(closure, 0);

    return _run();
}

void l_push(value_t value) {
    *vm.stack_top = value;
    vm.stack_top++;
}

value_t l_pop() {
    vm.stack_top--;
    return *vm.stack_top;
}

static value_t _peek(int distance) {
    return vm.stack_top[-1 - distance];
}

static bool _call(obj_closure_t* closure, int argCount) {
    if (argCount != closure->function->arity) {
        _runtime_error(
            "Expected %d arguments but got %d.",
            closure->function->arity, 
            argCount
        );
        return false;
    }

    if (vm.frame_count == FRAMES_MAX) {
        _runtime_error("Stack overflow.");
        return false;
    }

    callframe_t* frame = &vm.frames[vm.frame_count++];
    frame->closure = closure;
    frame->ip = closure->function->chunk.code;
    frame->slots = vm.stack_top - argCount - 1;
    return true;
}

static bool _call_value(value_t callee, int argCount) {
    if (IS_OBJ(callee)) {
        switch (OBJ_TYPE(callee)) {
            case OBJ_BOUND_METHOD: {
                obj_bound_method_t* bound = AS_BOUND_METHOD(callee);
                vm.stack_top[-argCount - 1] = bound->receiver;
                return _call(bound->method, argCount);
            }
            case OBJ_CLASS: {
                obj_class_t* klass = AS_CLASS(callee);
                vm.stack_top[-argCount - 1] = OBJ_VAL(l_new_instance(klass));

                // call init on the new class instance
                value_t initializer;
                if (l_table_get(&klass->methods, vm.init_string, &initializer)) {
                    return _call(AS_CLOSURE(initializer), argCount);
                } else if (argCount != 0) {
                    _runtime_error("Expected 0 arguments but got %d.", argCount);
                    return false;
                }

                return true;
            }
            case OBJ_CLOSURE:
                return _call(AS_CLOSURE(callee), argCount);
            case OBJ_NATIVE: {
                native_func_t native = AS_NATIVE(callee);
                value_t result = native(argCount, vm.stack_top - argCount);
                vm.stack_top -= argCount + 1;
                l_push(result);
                return true;
            }
            default:
                break; // Non-callable object type.
        }
    }
    _runtime_error("Can only call functions and classes.");
    return false;
}

static bool _invoke_from_class(obj_class_t* klass, obj_string_t* name, int argCount) {
    value_t method;
    if (!l_table_get(&klass->methods, name, &method)) {
        _runtime_error("Undefined property '%s'.", name->chars);
        return false;
    }
    return _call(AS_CLOSURE(method), argCount);
}

static bool _invoke(obj_string_t* name, int argCount) {
    value_t receiver = _peek(argCount);

    if (!IS_INSTANCE(receiver)) {
        _runtime_error("Only instances have methods.");
        return false;
    }

    obj_instance_t* instance = AS_INSTANCE(receiver);

    value_t value;
    if (l_table_get(&instance->fields, name, &value)) {
        vm.stack_top[-argCount - 1] = value;
        return _call_value(value, argCount);
    }

    return _invoke_from_class(instance->klass, name, argCount);
}

static bool _bind_method(obj_class_t* klass, obj_string_t* name) {
    value_t method;
    if (!l_table_get(&klass->methods, name, &method)) {
        _runtime_error("Undefined property '%s'.", name->chars);
        return false;
    }

    obj_bound_method_t* bound = l_new_bound_method(_peek(0), AS_CLOSURE(method));
    l_pop();
    l_push(OBJ_VAL(bound));
    return true;
}

static obj_upvalue_t* _capture_upvalue(value_t* local) {
    obj_upvalue_t* prevUpvalue = NULL;
    obj_upvalue_t* upvalue = vm.open_upvalues;
    while (upvalue != NULL && upvalue->location > local) {
        prevUpvalue = upvalue;
        upvalue = upvalue->next;
    }

    if (upvalue != NULL && upvalue->location == local) {
        return upvalue;
    }
    
    obj_upvalue_t* createdUpvalue = l_new_upvalue(local);

    createdUpvalue->next = upvalue;

    if (prevUpvalue == NULL) {
        vm.open_upvalues = createdUpvalue;
    } else {
        prevUpvalue->next = createdUpvalue;
    }

    return createdUpvalue;
}

static void _close_upvalues(value_t* last) {
    while (vm.open_upvalues != NULL &&
           vm.open_upvalues->location >= last) {

        obj_upvalue_t* upvalue = vm.open_upvalues;
        upvalue->closed = *upvalue->location;
        upvalue->location = &upvalue->closed;
        vm.open_upvalues = upvalue->next;
    }
}

static void _define_method(obj_string_t* name) {
    value_t method = _peek(0);
    obj_class_t* klass = AS_CLASS(_peek(1));
    l_table_set(&klass->methods, name, method);
    l_pop();
}

static bool _is_falsey(value_t value) {
    return IS_NIL(value) || (IS_BOOL(value) && !AS_BOOL(value));
}

static void _concatenate() {
    obj_string_t* b = AS_STRING(_peek(0));
    obj_string_t* a = AS_STRING(_peek(1));

    int length = a->length + b->length;
    char* chars = ALLOCATE(char, length + 1);
    memcpy(chars, a->chars, a->length);
    memcpy(chars + a->length, b->chars, b->length);
    chars[length] = '\0';

    obj_string_t* result = l_take_string(chars, length);

    l_pop();
    l_pop();

    l_push(OBJ_VAL(result));
}
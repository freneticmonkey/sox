#include <stdlib.h>
#include <stdio.h>

#include "chunk.h"
#include "vm.h"
#include "lib/memory.h"

void l_init_chunk(chunk_t* chunk) {
    chunk->count    = 0;
    chunk->capacity = 0;
    chunk->code     = NULL;
    chunk->lines    = NULL;
    l_init_value_array(&chunk->constants);
}

void l_free_chunk(chunk_t* chunk) {
    FREE_ARRAY(uint8_t, chunk->code, chunk->capacity);
    FREE_ARRAY(int, chunk->lines, chunk->capacity);
    l_init_chunk(chunk);
}


void l_write_chunk(chunk_t* chunk, uint8_t byte, int line)
{
    if (chunk->capacity < chunk->count + 1) {
        int oldCapacity = chunk->capacity;
        chunk->capacity = GROW_CAPACITY(oldCapacity);
        chunk->code = GROW_ARRAY(uint8_t, chunk->code, oldCapacity, chunk->capacity);
        chunk->lines = GROW_ARRAY(int, chunk->lines, oldCapacity, chunk->capacity);
    }

    chunk->code[chunk->count] = byte;
    chunk->lines[chunk->count] = line;
    chunk->count++;
}

int l_add_constant(chunk_t* chunk, value_t value) {
    // Check if constant pool is approaching bytecode limit (256 constants max)
    // This check should ideally be done in the compiler, but we validate here too
    if (chunk->constants.count >= 256) {
        fprintf(stderr, "ERROR: Constant pool full (max 256 constants)\n");
        return -1;
    }

    l_push(value);
    l_write_value_array(&chunk->constants, value);
    l_pop();

    // Safe cast - we know count <= 256 from check above
    return (int)(chunk->constants.count - 1);
}


// Returns the number of bytes for the arguments to the instruction 
// at [ip] in [fn]'s bytecode.
int l_op_get_arg_size_bytes(const chunk_t* chunk, int ip) {

    const uint8_t* code = chunk->code;

    OpCode instruction = (OpCode)code[ip];
    switch (instruction)
    {
        case OP_NIL:
        case OP_FALSE:
        case OP_TRUE:
        case OP_POP:
        case OP_CLOSE_UPVALUE:
        case OP_RETURN:
        case OP_EQUAL:
        case OP_GREATER:
        case OP_LESS:
        case OP_ADD:
        case OP_SUBTRACT:
        case OP_MULTIPLY:
        case OP_DIVIDE:
        case OP_NEGATE:
        case OP_NOT:
        case OP_PRINT:
        case OP_INHERIT:
            return 0;

        case OP_GET_LOCAL:
        case OP_SET_LOCAL:
        case OP_GET_UPVALUE:
        case OP_SET_UPVALUE:
        case OP_GET_GLOBAL:
        case OP_SET_GLOBAL:
        case OP_DEFINE_GLOBAL:
        case OP_GET_PROPERTY:
        case OP_SET_PROPERTY:
        case OP_GET_SUPER:
        case OP_GET_INDEX:
        case OP_SET_INDEX:
        case OP_CLASS:
        case OP_CALL:
        case OP_METHOD:
        case OP_CONSTANT:
        case OP_TABLE_FIELD:
        case OP_IMPORT:
            return 1;

        case OP_JUMP:
        case OP_LOOP:
        case OP_JUMP_IF_FALSE:
        case OP_INVOKE:
        case OP_SUPER_INVOKE:
            return 2;

        case OP_CLOSURE:
        {
            int constant = code[ip + 1];
            if (constant >= chunk->constants.count)
                return -1;

            obj_function_t* loadedFn = AS_FUNCTION(chunk->constants.values[constant]);

            // There are two bytes for the constant, then two for each upvalue.
            return 2 + (loadedFn->upvalue_count * 2);
        }

        case OP_BREAK:
        case OP_CONTINUE:
            return 2;

        case OP_CASE_FALLTHROUGH:
        case OP_ARRAY_EMPTY:
        case OP_ARRAY_PUSH:
        case OP_ARRAY_RANGE:
            return 0;
    }

    // TODO: Throw compile error here
    // UNREACHABLE();
    return 0;
}
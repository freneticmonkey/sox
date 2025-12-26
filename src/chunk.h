#ifndef SOX_CHUNK_H
#define SOX_CHUNK_H

#include "common.h"
#include "value.h"

typedef enum OpCode {
    OP_CONSTANT,
    OP_NIL,
    OP_TRUE,
    OP_FALSE,
    OP_POP,
    OP_GET_LOCAL,
    OP_SET_LOCAL,
    OP_GET_GLOBAL,
    OP_DEFINE_GLOBAL,
    OP_SET_GLOBAL,
    OP_GET_UPVALUE,
    OP_SET_UPVALUE,
    OP_GET_PROPERTY,
    OP_SET_PROPERTY,
    OP_GET_SUPER,
    OP_GET_INDEX,
    OP_SET_INDEX,
    OP_EQUAL,
    OP_GREATER,
    OP_LESS,
    OP_ADD,
    OP_SUBTRACT,
    OP_MULTIPLY,
    OP_DIVIDE,
    OP_NEGATE,
    OP_NOT,
    OP_PRINT,
    OP_JUMP,
    OP_JUMP_IF_FALSE,
    OP_LOOP,
    OP_CALL,
    OP_INVOKE,
    OP_SUPER_INVOKE,
    OP_CLOSURE,
    OP_CLOSE_UPVALUE,
    OP_RETURN,
    OP_CLASS,
    OP_INHERIT,
    OP_METHOD,
    OP_TABLE_FIELD,
    OP_IMPORT,
    OP_ARRAY_EMPTY,
    OP_ARRAY_PUSH,
    OP_ARRAY_RANGE,
    // VM NO-OP
    OP_BREAK,
    OP_CASE_FALLTHROUGH,
    OP_CONTINUE,
} OpCode;

typedef struct {
    int      count;
    int      capacity;
    uint8_t* code;
    int*     lines;
    value_array_t constants;
} chunk_t;

void l_init_chunk(chunk_t* chunk);
void l_free_chunk(chunk_t* chunk);

void l_write_chunk(chunk_t* chunk, uint8_t byte, int line);
int  l_add_constant(chunk_t* chunk, value_t value);

int l_op_get_arg_size_bytes(const chunk_t* chunk, int ip);

#endif
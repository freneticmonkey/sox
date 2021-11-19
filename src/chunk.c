#include <stdlib.h>

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
    l_push(value);
    l_write_value_array(&chunk->constants, value);
    l_pop();
    // return the index of the new constant
    return chunk->constants.count - 1;
}
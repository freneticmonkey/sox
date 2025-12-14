#ifndef SOX_RUNTIME_TABLE_H
#define SOX_RUNTIME_TABLE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "runtime_value.h"

// Forward declaration
typedef struct runtime_obj_string_t runtime_obj_string_t;

typedef struct {
    runtime_obj_string_t* key;
    value_t value;
} runtime_entry_t;

typedef struct {
    int count;
    int capacity;
    runtime_entry_t* entries;
} runtime_table_t;

void runtime_init_table(runtime_table_t *table);
void runtime_free_table(runtime_table_t* table);

bool runtime_table_get(runtime_table_t* table, runtime_obj_string_t* key, value_t* value);

bool runtime_table_set(runtime_table_t* table, runtime_obj_string_t* key, value_t value);
bool runtime_table_delete(runtime_table_t* table, runtime_obj_string_t* key);
void runtime_table_add_all(runtime_table_t* from, runtime_table_t* to);

runtime_obj_string_t* runtime_table_find_string(runtime_table_t* table, const char* chars, size_t length, uint32_t hash);

// Deserialization helper
runtime_entry_t* runtime_table_set_entry(runtime_table_t* table, runtime_obj_string_t* key);

#endif

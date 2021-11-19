#ifndef SOX_TABLE_H
#define SOX_TABLE_H

#include "common.h"
#include "value.h"

typedef struct {
    obj_string_t* key;
    value_t value;
} entry_t;

typedef struct {
  int count;
  int capacity;
  entry_t* entries;
} table_t;


void l_init_table(table_t *table);
void l_free_table(table_t* table);

bool l_table_get(table_t* table, obj_string_t* key, value_t* value);

bool l_table_set(table_t* table, obj_string_t* key, value_t value);
bool l_table_delete(table_t* table, obj_string_t* key);
void l_table_add_all(table_t* from, table_t* to);

obj_string_t* l_table_find_string(table_t* table, const char* chars, int length, uint32_t hash);

// garbage collection
void l_mark_table(table_t* table);
void l_table_remove_white(table_t* table);

#endif
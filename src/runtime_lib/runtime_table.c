#include <stddef.h>
#include <string.h>
#include <stdint.h>

#include "runtime_memory.h"
#include "runtime_object.h"
#include "runtime_table.h"
#include "runtime_value.h"

#define TABLE_MAX_LOAD 0.75

void runtime_init_table(runtime_table_t* table) {
    table->count = 0;
    table->capacity = 0;
    table->entries = NULL;
}

void runtime_free_table(runtime_table_t* table) {
    RUNTIME_FREE_ARRAY(runtime_entry_t, table->entries, table->capacity);
    runtime_init_table(table);
}

static runtime_entry_t* _find_entry(runtime_entry_t* entries, int capacity, runtime_obj_string_t* key) {
    uint32_t index = key->hash % capacity;
    runtime_entry_t* tombstone = NULL;

    for (;;) {
        runtime_entry_t* entry = &entries[index];
        if (entry->key == NULL) {
            if (IS_NIL(entry->value)) {
                // Empty entry.
                return tombstone != NULL ? tombstone : entry;
            }
            else {
                // We found a tombstone.
                if (tombstone == NULL)
                    tombstone = entry;
            }
        }
        else if (entry->key == key) {
            // We found the key.
            return entry;
        }

        index = (index + 1) % capacity;
    }
}

bool runtime_table_get(runtime_table_t* table, runtime_obj_string_t* key, value_t* value) {
    if (table->count == 0)
        return false;

    runtime_entry_t* entry = _find_entry(table->entries, table->capacity, key);
    if (entry->key == NULL)
        return false;

    *value = entry->value;
    return true;
}

static void _adjust_capacity(runtime_table_t* table, int capacity) {
    runtime_entry_t* entries = RUNTIME_ALLOCATE(runtime_entry_t, capacity);
    for (int i = 0; i < capacity; i++) {
        entries[i].key = NULL;
        entries[i].value = NIL_VAL;
    }

    table->count = 0;
    for (int i = 0; i < table->capacity; i++) {
        runtime_entry_t* entry = &table->entries[i];
        if (entry->key == NULL)
            continue;

        runtime_entry_t* dest = _find_entry(entries, capacity, entry->key);
        dest->key = entry->key;
        dest->value = entry->value;
        table->count++;
    }

    RUNTIME_FREE_ARRAY(runtime_entry_t, table->entries, table->capacity);

    table->entries = entries;
    table->capacity = capacity;

}

bool runtime_table_set(runtime_table_t* table, runtime_obj_string_t* key, value_t value) {

    if ( key == NULL)
    {
        // what is going on here
        return false;
    }

    if (table->count + 1 > table->capacity * TABLE_MAX_LOAD) {
        int capacity = RUNTIME_GROW_CAPACITY(table->capacity);
        _adjust_capacity(table, capacity);
    }

    runtime_entry_t* entry = _find_entry(table->entries, table->capacity, key);
    bool isNewKey = entry->key == NULL;
    if (isNewKey && IS_NIL(entry->value))
        table->count++;

    entry->key = key;
    entry->value = value;
    return isNewKey;
}

bool runtime_table_delete(runtime_table_t* table, runtime_obj_string_t* key) {
    if (table->count == 0)
        return false;

    // Find the entry.
    runtime_entry_t* entry = _find_entry(table->entries, table->capacity, key);
    if (entry->key == NULL)
        return false;

    // Place a tombstone in the entry.
    entry->key = NULL;
    entry->value = BOOL_VAL(true);
    return true;
}

void runtime_table_add_all(runtime_table_t* from, runtime_table_t* to) {
    for (int i = 0; i < from->capacity; i++) {
        runtime_entry_t* entry = &from->entries[i];
        if (entry->key != NULL) {
            runtime_table_set(to, entry->key, entry->value);
        }
    }
}

runtime_obj_string_t* runtime_table_find_string(runtime_table_t* table, const char* chars, size_t length, uint32_t hash) {
    if (table->count == 0)
        return NULL;

    uint32_t index = hash % table->capacity;
    for (;;) {
        runtime_entry_t* entry = &table->entries[index];

        if (entry->key == NULL) {
            // Stop if we find an empty non-tombstone entry.
            if (IS_NIL(entry->value))
                return NULL;

        }
        else if (entry->key->length == length &&
                 entry->key->hash == hash &&
                 memcmp(entry->key->chars, chars, length) == 0) {
            // We found it.
            return entry->key;
        }

        index = (index + 1) % table->capacity;
    }
}

runtime_entry_t* runtime_table_set_entry(runtime_table_t* table, runtime_obj_string_t* key) {
    if ( key == NULL)
    {
        // what is going on here
        return NULL;
    }

    if (table->count + 1 > table->capacity * TABLE_MAX_LOAD) {
        int capacity = RUNTIME_GROW_CAPACITY(table->capacity);
        _adjust_capacity(table, capacity);
    }

    runtime_entry_t* entry = _find_entry(table->entries, table->capacity, key);
    bool isNewKey = entry->key == NULL;
    if (isNewKey && IS_NIL(entry->value))
        table->count++;

    entry->key = key;
    // Initialise an object value to NULL
    entry->value = OBJ_VAL(NULL);
    return entry;
}

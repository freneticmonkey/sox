#include <stdlib.h>
#include <string.h>

#include "lib/memory.h"
#include "object.h"
#include "table.h"
#include "value.h"

#define TABLE_MAX_LOAD 0.75

void l_init_table(table_t* table) {
    table->count = 0;
    table->capacity = 0;
    table->entries = NULL;
}

void l_free_table(table_t* table) {
    FREE_ARRAY(entry_t, table->entries, table->capacity);
    l_init_table(table);
}

static entry_t* _find_entry(entry_t* entries, int capacity, obj_string_t* key) {
    uint32_t index = key->hash % capacity;
    entry_t* tombstone = NULL;

    for (;;) {
        entry_t* entry = &entries[index];
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

bool l_table_get(table_t* table, obj_string_t* key, value_t* value) {
    if (table->count == 0) 
        return false;

    entry_t* entry = _find_entry(table->entries, table->capacity, key);
    if (entry->key == NULL) 
        return false;

    *value = entry->value;
    return true;
}

static void _adjust_capacity(table_t* table, int capacity) {
    entry_t* entries = ALLOCATE(entry_t, capacity);
    for (int i = 0; i < capacity; i++) {
        entries[i].key = NULL;
        entries[i].value = NIL_VAL;
    }

    table->count = 0;
    for (int i = 0; i < table->capacity; i++) {
        entry_t* entry = &table->entries[i];
        if (entry->key == NULL) 
            continue;

        entry_t* dest = _find_entry(entries, capacity, entry->key);
        dest->key = entry->key;
        dest->value = entry->value;
        table->count++;
    }

    FREE_ARRAY(entry_t, table->entries, table->capacity);

    table->entries = entries;
    table->capacity = capacity;

}

bool l_table_set(table_t* table, obj_string_t* key, value_t value) {
    if (table->count + 1 > table->capacity * TABLE_MAX_LOAD) {
        int capacity = GROW_CAPACITY(table->capacity);
        _adjust_capacity(table, capacity);
    }
    
    entry_t* entry = _find_entry(table->entries, table->capacity, key);
    bool isNewKey = entry->key == NULL;
    if (isNewKey && IS_NIL(entry->value)) 
        table->count++;

    entry->key = key;
    entry->value = value;
    return isNewKey;
}

bool l_table_delete(table_t* table, obj_string_t* key) {
    if (table->count == 0)
        return false;

    // Find the entry.
    entry_t* entry = _find_entry(table->entries, table->capacity, key);
    if (entry->key == NULL) 
        return false;

    // Place a tombstone in the entry.
    entry->key = NULL;
    entry->value = BOOL_VAL(true);
    return true;
}

void l_table_add_all(table_t* from, table_t* to) {
    for (int i = 0; i < from->capacity; i++) {
        entry_t* entry = &from->entries[i];
        if (entry->key != NULL) {
            l_table_set(to, entry->key, entry->value);
        }
    }
}

obj_string_t* l_table_find_string(table_t* table, const char* chars, int length, uint32_t hash) {
    if (table->count == 0) 
        return NULL;

    uint32_t index = hash % table->capacity;
    for (;;) {
        entry_t* entry = &table->entries[index];

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

void l_mark_table(table_t* table) {
    for (int i = 0; i < table->capacity; i++) {
        entry_t* entry = &table->entries[i];
        l_mark_object((obj_t*)entry->key);
        l_mark_value(entry->value);
    }
}

void l_table_remove_white(table_t* table) {
    for (int i = 0; i < table->capacity; i++) {
        entry_t* entry = &table->entries[i];
        
        if (entry->key != NULL && !entry->key->obj.is_marked) {
            l_table_delete(table, entry->key);
        }
    }
}
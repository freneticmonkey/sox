#ifndef LIB_MEMORY_H
#define LIB_MEMORY_H

#include "common.h"
#include "compiler.h"
#include "object.h"


// General memory management
// 

#define ALLOCATE(type, count) \
    (type*)reallocate(NULL, 0, sizeof(type) * (count))


#define FREE(type, pointer) reallocate(pointer, sizeof(type), 0)

#define GROW_CAPACITY(capacity) \
    ((capacity) < 8 ? 8 : (capacity) * 2)

#define GROW_ARRAY(type, pointer, oldCount, newCount) \
    (type*)reallocate(pointer, sizeof(type) * (oldCount), \
        sizeof(type) * (newCount))

#define FREE_ARRAY(type, pointer, oldCount) \
    reallocate(pointer, sizeof(type) * (oldCount), 0)

void* reallocate(void* pointer, size_t oldSize, size_t newSize);

// Garbage collection
// 

void  l_collect_garbage();
void l_mark_object(obj_t* object);
void l_mark_value(value_t value);
void l_free_objects();


// Bytecode deserialisation
//

// native_lookup_t
// the following structure tracks a const char * native function name to a obj_native_t pointer
// this is used to resolve the native function pointer when a function is deserialised
typedef struct {
    const char *name;
    obj_native_t *native;
} native_lookup_item_t;

typedef struct native_lookup_t {
    native_lookup_item_t *items;
    size_t count;
    size_t capacity;
} native_lookup_t;

// object_lookup_t
// the following structure maps uintptr_t ids to obj_t pointers and stores an array of pointers
// which are resolved at the end of deserialisation

typedef struct {
    uintptr_t id;
    obj_t *address;
    size_t count;
    size_t capacity;
    obj_t * *registered_targets;
} object_lookup_item_t;

typedef struct object_lookup_t {
    object_lookup_item_t *items;
    size_t count;
    size_t capacity;
} object_lookup_t;

// initialise the bytecode deserialisation allocation tracking
void l_allocate_track_init();

// cleanup the allocation tracking data
void l_allocate_track_free();

// register a native function pointer
void l_allocate_track_register_native(const char *name, void * ptr);

// get a native function pointer
void * l_allocate_track_get_native_ptr(const char *name);

// get a native function name
const char * l_allocate_track_get_native_name(void * ptr);

// insert a new allocation for tracking into the allocation lookup table
void l_allocate_track_register(uintptr_t id, void *address);

// register interest in a target address
void l_allocate_track_target_register(uintptr_t id, void **target);

// link the registered targets to the allocated addresses for those targets
void l_allocate_track_link_targets();


#endif
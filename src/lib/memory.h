#ifndef LIB_MEMORY_H
#define LIB_MEMORY_H

#include "common.h"
#include "compiler.h"
#include "object.h"

//#define LINK_DEBUGGING 1
// #define DEBUG_LOG_ALLOC 1

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

// Memory Initialisation
//

// initialise the memory management system
void l_init_memory();

// cleanup the memory management system
void l_free_memory();

// Garbage collection
// 

void l_collect_garbage();
void l_mark_object(obj_t* object);
void l_mark_value(value_t value);

void l_add_object(obj_t* object);
obj_t* l_get_objects();

typedef void (*l_mark_roots_callback)(void);
typedef void (*l_gc_callback)(void);
void l_register_mark_roots_cb(l_mark_roots_callback fn);
void l_register_garbage_collect_cb(l_gc_callback fn);

void l_unregister_mark_roots_cb(l_mark_roots_callback fn);
void l_register_garbage_collect_cb(l_gc_callback fn);

// Bytecode deserialisation
//

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
void l_allocate_track_register(uintptr_t id, void **address);

// register interest in a target address
void l_allocate_track_target_register(uintptr_t id, void **target);

// link the registered targets to the allocated addresses for those targets
void l_allocate_track_link_targets();


#endif
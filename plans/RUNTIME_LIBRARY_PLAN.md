# Production-Ready Runtime Library Implementation Plan

## Overview

Create `libsox_runtime` - a standalone, VM-independent runtime library providing all 17 runtime functions needed by native-compiled Sox code. Available as both static (`.a`) and shared (`.dylib`/`.so`) libraries.

## Architecture

```
Native Executable (generated code)
    ↓ links to
libsox_runtime (.a / .dylib)
├── Public API (17 runtime functions)
├── Runtime Context (optional string interning)
├── Simplified Object System (no VM deps)
├── Value System (standalone)
└── Memory Management (malloc/free, no GC)
```

**Key Design Principle:** Zero VM dependencies. Library operates in simplified mode with direct malloc/free, acceptable memory leaks for short-lived native programs.

## Directory Structure

### Create: `src/runtime_lib/`

```
src/runtime_lib/
├── runtime_api.h          # Public API - install to /usr/local/include/sox/
├── runtime_api.c          # Implementation of 17 runtime functions
├── runtime_context.h      # Runtime context structure
├── runtime_context.c      # Context init/cleanup
├── runtime_value.h        # Standalone value system
├── runtime_value.c        # Extracted from src/value.c
├── runtime_object.h       # Simplified object system
├── runtime_object.c       # Adapted from src/object.c (remove VM deps)
├── runtime_string.h       # String utilities
├── runtime_string.c       # String allocation/hashing
├── runtime_table.h        # Standalone hash table
├── runtime_table.c        # Extracted from src/lib/table.c
├── runtime_memory.h       # Simple malloc/free wrappers
├── runtime_memory.c       # Memory allocation (no GC)
├── runtime_print.h        # Print utilities
└── runtime_print.c        # Output functions
```

## Critical Implementation Decisions

### 1. String Interning Strategy

**Approach:** Thread-local runtime context with optional interning

```c
typedef struct sox_runtime_context_t {
    runtime_table_t* string_pool;  // Optional interning table
    bool enable_interning;
    size_t bytes_allocated;
    size_t object_count;
    bool has_error;
    char error_message[256];
} sox_runtime_context_t;

// Global thread-local context
extern __thread sox_runtime_context_t* _sox_runtime_ctx;
```

**String Creation:**
- Check if context exists and interning is enabled
- If yes: check string_pool, return existing or create + intern
- If no: direct malloc allocation (no interning overhead)

**Removes VM dependency:** Replaces `vm.strings` with `context->string_pool`

### 2. Memory Management

**Approach:** No GC, simple malloc/free

```c
void* runtime_malloc(size_t size);
void* runtime_realloc(void* ptr, size_t old_size, size_t new_size);
void runtime_free(void* ptr, size_t size);

#define RUNTIME_ALLOCATE(type, count) \
    (type*)runtime_malloc(sizeof(type) * (count))
```

**Removes VM dependencies:**
- No `reallocate()` (GC-tracked) → use `runtime_malloc()`
- No `l_add_object()` → no object registration
- No `l_push()/l_pop()` → no stack protection needed

**Trade-off:** Acceptable memory leaks for short-lived native programs

### 3. Eliminating VM Dependencies in object.c

**Current code (lines 91-99):**
```c
static obj_string_t* _allocate_string(char* chars, size_t length, uint32_t hash) {
    obj_string_t* string = ALLOCATE_OBJ(obj_string_t, OBJ_STRING);
    string->length = length;
    string->chars = chars;
    string->hash = hash;
    l_push(OBJ_VAL(string));           // ← VM stack
    l_table_set(&vm.strings, string, NIL_VAL);  // ← VM global
    l_pop();                           // ← VM stack
    return string;
}
```

**Runtime library version:**
```c
static runtime_obj_string_t* _allocate_string(char* chars, size_t length, uint32_t hash) {
    runtime_obj_string_t* string = RUNTIME_ALLOCATE(runtime_obj_string_t, 1);
    string->obj.type = RUNTIME_OBJ_STRING;
    string->length = length;
    string->chars = chars;
    string->hash = hash;

    // Optionally intern in context
    sox_runtime_context_t* ctx = _sox_runtime_ctx;
    if (ctx && ctx->enable_interning) {
        runtime_table_set(ctx->string_pool, string, RUNTIME_NIL_VAL);
    }

    return string;
}
```

## Public API Design

### Main Header: `runtime_api.h`

```c
#ifndef SOX_RUNTIME_API_H
#define SOX_RUNTIME_API_H

// Symbol visibility for shared library
#ifdef SOX_RUNTIME_SHARED
  #ifdef SOX_RUNTIME_BUILD
    #ifdef _WIN32
      #define SOX_API __declspec(dllexport)
    #else
      #define SOX_API __attribute__((visibility("default")))
    #endif
  #else
    #ifdef _WIN32
      #define SOX_API __declspec(dllimport)
    #else
      #define SOX_API
    #endif
  #endif
#else
  #define SOX_API
#endif

// Value type (matches VM exactly)
typedef struct value_t {
    ValueType type;
    union as {
        double number;
        bool boolean;
        void* obj;
    } as;
} value_t;

// Value constructors/checks/extractors (as macros for performance)
#define BOOL_VAL(value)   ((value_t){VAL_BOOL, {.boolean = value}})
#define NIL_VAL           ((value_t){VAL_NIL, {.number = 0}})
// ... etc

// Runtime context management
SOX_API sox_runtime_context_t* sox_runtime_init(bool enable_string_interning);
SOX_API void sox_runtime_cleanup(sox_runtime_context_t* ctx);
SOX_API void sox_runtime_set_context(sox_runtime_context_t* ctx);

// 17 Runtime functions
SOX_API value_t sox_native_add(value_t left, value_t right);
SOX_API value_t sox_native_subtract(value_t left, value_t right);
SOX_API value_t sox_native_multiply(value_t left, value_t right);
SOX_API value_t sox_native_divide(value_t left, value_t right);
SOX_API value_t sox_native_negate(value_t operand);
SOX_API value_t sox_native_equal(value_t left, value_t right);
SOX_API value_t sox_native_greater(value_t left, value_t right);
SOX_API value_t sox_native_less(value_t left, value_t right);
SOX_API value_t sox_native_not(value_t operand);
SOX_API void sox_native_print(value_t value);

// NEW: Missing functions to implement
SOX_API value_t sox_native_get_property(value_t object, value_t name);
SOX_API void sox_native_set_property(value_t object, value_t name, value_t value);
SOX_API value_t sox_native_get_index(value_t object, value_t index);
SOX_API void sox_native_set_index(value_t object, value_t index, value_t value);
SOX_API value_t sox_native_alloc_string(const char* chars, size_t length);
SOX_API value_t sox_native_alloc_table(void);
SOX_API value_t sox_native_alloc_array(void);

#endif
```

## Missing Function Implementations

### Property Access

```c
value_t sox_native_get_property(value_t object, value_t name) {
    if (!IS_INSTANCE(object)) {
        fprintf(stderr, "Runtime error: Only instances have properties.\n");
        return NIL_VAL;
    }
    if (!IS_STRING(name)) {
        fprintf(stderr, "Runtime error: Property name must be a string.\n");
        return NIL_VAL;
    }

    runtime_obj_instance_t* instance = AS_INSTANCE(object);
    value_t result;
    if (runtime_table_get(&instance->fields, AS_STRING(name), &result)) {
        return result;
    }
    return NIL_VAL;
}

void sox_native_set_property(value_t object, value_t name, value_t value) {
    if (!IS_INSTANCE(object) || !IS_STRING(name)) return;
    runtime_obj_instance_t* instance = AS_INSTANCE(object);
    runtime_table_set(&instance->fields, AS_STRING(name), value);
}
```

### Index Access

```c
value_t sox_native_get_index(value_t object, value_t index) {
    if (IS_ARRAY(object)) {
        if (!IS_NUMBER(index)) return NIL_VAL;
        runtime_obj_array_t* array = AS_ARRAY(object);
        int idx = (int)AS_NUMBER(index);
        if (idx < 0 || idx >= array->values.count) return NIL_VAL;
        return array->values.values[idx];
    }
    else if (IS_TABLE(object)) {
        if (!IS_STRING(index)) return NIL_VAL;
        runtime_obj_table_t* table = AS_TABLE(object);
        value_t result;
        if (runtime_table_get(&table->table, AS_STRING(index), &result)) {
            return result;
        }
        return NIL_VAL;
    }
    return NIL_VAL;
}

void sox_native_set_index(value_t object, value_t index, value_t value) {
    if (IS_ARRAY(object) && IS_NUMBER(index)) {
        runtime_obj_array_t* array = AS_ARRAY(object);
        int idx = (int)AS_NUMBER(index);
        if (idx >= 0 && idx < array->values.count) {
            array->values.values[idx] = value;
        }
    }
    else if (IS_TABLE(object) && IS_STRING(index)) {
        runtime_obj_table_t* table = AS_TABLE(object);
        runtime_table_set(&table->table, AS_STRING(index), value);
    }
}
```

### Allocation Functions

```c
value_t sox_native_alloc_string(const char* chars, size_t length) {
    runtime_obj_string_t* string = runtime_alloc_string(chars, length);
    return OBJ_VAL(string);
}

value_t sox_native_alloc_table(void) {
    runtime_obj_table_t* table = RUNTIME_ALLOCATE(runtime_obj_table_t, 1);
    table->obj.type = RUNTIME_OBJ_TABLE;
    runtime_init_table(&table->table);
    return OBJ_VAL(table);
}

value_t sox_native_alloc_array(void) {
    runtime_obj_array_t* array = RUNTIME_ALLOCATE(runtime_obj_array_t, 1);
    array->obj.type = RUNTIME_OBJ_ARRAY;
    runtime_init_value_array(&array->values);
    array->start = 0;
    array->end = 0;
    return OBJ_VAL(array);
}
```

## Build System Changes

### 1. Update `premake5.lua`

Add two new library projects:

```lua
-- Static library
project "sox_runtime"
  kind "StaticLib"
  language "C"
  targetdir("build")
  targetname("sox_runtime")

  defines { "SOX_RUNTIME_BUILD" }
  includedirs { "src/runtime_lib" }
  files { "src/runtime_lib/**.h", "src/runtime_lib/**.c" }

  filter "system:macosx"
    defines { "SOX_RUNTIME_MACOS" }
    buildoptions { "-fvisibility=hidden" }

  filter "system:linux"
    defines { "SOX_RUNTIME_LINUX" }
    buildoptions { "-fvisibility=hidden", "-fPIC" }

  filter "system:windows"
    defines { "SOX_RUNTIME_WINDOWS" }
  filter {}

-- Shared library
project "sox_runtime_shared"
  kind "SharedLib"
  language "C"
  targetdir("build")
  targetname("sox_runtime")

  defines { "SOX_RUNTIME_BUILD", "SOX_RUNTIME_SHARED" }
  includedirs { "src/runtime_lib" }
  files { "src/runtime_lib/**.h", "src/runtime_lib/**.c" }

  filter "system:macosx"
    defines { "SOX_RUNTIME_MACOS" }
    buildoptions { "-fvisibility=hidden" }
    linkoptions { "-dynamiclib" }

  filter "system:linux"
    defines { "SOX_RUNTIME_LINUX" }
    buildoptions { "-fvisibility=hidden", "-fPIC" }
    linkoptions { "-shared" }

  filter "system:windows"
    defines { "SOX_RUNTIME_WINDOWS", "_WINDLL" }
  filter {}
```

### 2. Update `Makefile`

Add new targets:

```makefile
build-runtime-static: gen
ifeq (${THIS_OS},darwin)
	xcodebuild -configuration "Release" ARCHS="${THIS_ARCH}" \
	  -destination 'platform=macOS' \
	  -project "projects/sox_runtime.xcodeproj" -target sox_runtime
endif
# ... similar for Linux and Windows

build-runtime-shared: gen
ifeq (${THIS_OS},darwin)
	xcodebuild -configuration "Release" ARCHS="${THIS_ARCH}" \
	  -destination 'platform=macOS' \
	  -project "projects/sox_runtime_shared.xcodeproj" -target sox_runtime_shared
endif
# ... similar for Linux and Windows

build-runtime: build-runtime-static build-runtime-shared

install-runtime: build-runtime
	@echo "Installing Sox runtime library..."
	mkdir -p /usr/local/lib /usr/local/include/sox
	cp build/libsox_runtime.a /usr/local/lib/
ifeq (${THIS_OS},darwin)
	cp build/libsox_runtime.dylib /usr/local/lib/
	install_name_tool -id /usr/local/lib/libsox_runtime.dylib \
	  /usr/local/lib/libsox_runtime.dylib
endif
ifeq (${THIS_OS},linux)
	cp build/libsox_runtime.so /usr/local/lib/
	ldconfig
endif
	cp src/runtime_lib/runtime_api.h /usr/local/include/sox/
	@echo "Runtime library installed successfully"
```

## Native Codegen Integration

### Update Linker Configuration

**File: `src/lib/linker.c`**

Current linker commands already support `-lsox_runtime` via the `link_runtime` flag. Enable it:

**File: `src/lib/file.c` (line 339)**

Change:
```c
.link_runtime = false,  // TODO: Requires building a proper libsox_runtime library
```

To:
```c
.link_runtime = true,  // Link with runtime library
```

### Generated Code Initialization

Native executables should include runtime initialization. Add to generated code prologue:

```c
// Generated preamble
#include <sox/runtime_api.h>

static sox_runtime_context_t* _sox_ctx;

__attribute__((constructor))
void _sox_init_runtime() {
    _sox_ctx = sox_runtime_init(false);
    sox_runtime_set_context(_sox_ctx);
}

__attribute__((destructor))
void _sox_cleanup_runtime() {
    sox_runtime_cleanup(_sox_ctx);
}
```

## Code Extraction Strategy

### 1. `runtime_value.c` ← `src/value.c`

- Copy entire file (145 lines)
- Replace `l_` prefix with `runtime_`
- Replace `GROW_ARRAY` macro with `RUNTIME_GROW_ARRAY`
- Replace memory functions: `reallocate()` → `runtime_malloc()`
- Update includes to `runtime_*` headers

### 2. `runtime_object.c` ← `src/object.c`

- Copy file structure
- Remove VM dependencies:
  - Line 96-98: Remove `l_push()`, `vm.strings`, `l_pop()`
  - Line 105, 116: Replace `vm.strings` with `ctx->string_pool`
  - Replace `ALLOCATE_OBJ` with `RUNTIME_ALLOCATE`
- Replace memory macros with `RUNTIME_*` versions
- Update all `l_` prefixes to `runtime_`

### 3. `runtime_table.c` ← `src/lib/table.c`

- Copy entire file
- Remove `#include "vm.h"` (line 9)
- Replace `l_` prefix with `runtime_`
- Replace memory macros with `RUNTIME_*` versions
- Verify no actual VM usage (just included but unused)

### 4. `runtime_string.c` ← `src/lib/string.h`

- Extract `l_hash_string()` inline function
- Rename to `runtime_hash_string()`
- Standalone - no dependencies

### 5. `runtime_print.c` ← `src/lib/print.c`

- Copy `l_printf()` and related functions
- Rename to `runtime_printf()`
- Simple stdout wrapper

## Testing Strategy

### Unit Tests

Create `src/test/runtime_lib_test.c`:

```c
#include "../runtime_lib/runtime_api.h"
#include "munit/munit.h"

static MunitResult test_add_numbers(const MunitParameter params[], void* data) {
    value_t a = NUMBER_VAL(5.0);
    value_t b = NUMBER_VAL(3.0);
    value_t result = sox_native_add(a, b);
    munit_assert_true(IS_NUMBER(result));
    munit_assert_double_equal(AS_NUMBER(result), 8.0, 2);
    return MUNIT_OK;
}

static MunitResult test_alloc_string(const MunitParameter params[], void* data) {
    sox_runtime_context_t* ctx = sox_runtime_init(true);
    sox_runtime_set_context(ctx);
    value_t str = sox_native_alloc_string("hello", 5);
    munit_assert_true(IS_OBJ(str));
    sox_runtime_cleanup(ctx);
    return MUNIT_OK;
}

// ... test all 17 functions
```

### Integration Test

Create `test_native_link.c`:

```c
#include <sox/runtime_api.h>
#include <stdio.h>

int main() {
    sox_runtime_context_t* ctx = sox_runtime_init(false);
    sox_runtime_set_context(ctx);

    value_t a = NUMBER_VAL(10.0);
    value_t b = NUMBER_VAL(20.0);
    value_t sum = sox_native_add(a, b);

    printf("10 + 20 = ");
    sox_native_print(sum);

    sox_runtime_cleanup(ctx);
    return 0;
}
```

Compile: `gcc -o test_native test_native_link.c -L./build -lsox_runtime -I./build/include`

## Implementation Phases

### Phase 1: Foundation (2-3 days)
1. Create `src/runtime_lib/` directory
2. Implement `runtime_memory.{c,h}`
3. Extract `runtime_value.{c,h}` from `src/value.c`
4. Extract `runtime_string.{c,h}` from `src/lib/string.h`
5. Unit tests for value system

### Phase 2: Core Data Structures (2-3 days)
1. Extract `runtime_table.{c,h}` from `src/lib/table.c`
2. Adapt `runtime_object.{c,h}` from `src/object.c` (remove VM deps)
3. Implement `runtime_context.{c,h}`
4. Unit tests for object system

### Phase 3: Runtime Functions (3-4 days)
1. Implement 10 existing functions in `runtime_api.c`
2. Implement 7 missing functions
3. Create `runtime_print.{c,h}`
4. Unit tests for all 17 functions

### Phase 4: Build System (1-2 days)
1. Update `premake5.lua` with static/shared library projects
2. Update `Makefile` with new targets
3. Test builds on all platforms

### Phase 5: Integration (2-3 days)
1. Update native codegen linker configuration
2. Add runtime initialization to generated code
3. End-to-end testing with native compilation
4. Verify standalone executables work

### Phase 6: Documentation (1-2 days)
1. Create `src/runtime_lib/README.md`
2. Add API documentation comments
3. Create usage examples
4. Cross-platform verification

**Total: 11-17 days**

## Critical Files

**To Create:**
- All files in `src/runtime_lib/` (14 new files)

**To Modify:**
- `/Users/scott/development/projects/sox/premake5.lua` - Add library projects
- `/Users/scott/development/projects/sox/Makefile` - Add build targets
- `/Users/scott/development/projects/sox/src/lib/file.c` - Enable link_runtime (line 339)

**Source Files for Extraction:**
- `/Users/scott/development/projects/sox/src/value.c` → `runtime_value.c`
- `/Users/scott/development/projects/sox/src/object.c` → `runtime_object.c` (adapt)
- `/Users/scott/development/projects/sox/src/lib/table.c` → `runtime_table.c`
- `/Users/scott/development/projects/sox/src/lib/string.h` → `runtime_string.c`
- `/Users/scott/development/projects/sox/src/lib/print.c` → `runtime_print.c`

# Go-Style Module System Implementation for Sox

**Author:** Claude
**Date:** 2025-12-25
**Status:** Implementation Plan
**Version:** 1.0
**Based On:** Module System Design Plan v1.0

---

## Executive Summary

This document details the implementation of a **Go-style module system** for Sox, featuring:
- **Implicit naming**: `import "math"` creates a `math` variable automatically
- **Optional aliasing**: `import mymath "math"` for conflict resolution
- **Multiple imports**: `import "math", "string", custom "my/package"`
- **Clean syntax**: No manual variable assignment needed

This approach combines the simplicity of Lua's `require()` with Go's elegant import semantics.

---

## Syntax Design

### Basic Import (Implicit Name)

```sox
// Import with implicit name from module/file name
import "math";

// Creates local variable 'math' containing the module
print(math.pi);         // 3.14159
print(math.sqrt(16));   // 4
```

### Import with Alias

```sox
// Import with explicit alias to avoid conflicts
import mymath "math";

print(mymath.pi);       // 3.14159
print(mymath.sqrt(16)); // 4
```

### Multiple Imports

```sox
// Import multiple modules in one statement
import "math", "string", "array";

print(math.pi);
print(string.upper("hello"));
print(array.length([1, 2, 3]));

// Mix implicit and explicit names
import "math", str "string", arr "array";

print(math.pi);
print(str.upper("hello"));
print(arr.length([1, 2, 3]));
```

### Path-Based Imports

```sox
// Import from relative path
import "./utils/helpers";  // Creates 'helpers' variable

// Import from nested path
import "mylib/math/advanced";  // Creates 'advanced' variable

// With alias
import adv "mylib/math/advanced";
```

---

## How It Works

### Import Statement Parsing

The `import` statement is a **compile-time declaration** that:
1. Parses module path(s)
2. Determines variable name (implicit or explicit)
3. Emits bytecode to load module and assign to local variable
4. Creates local variable in current scope

**Syntax Grammar:**
```
import_stmt     → "import" import_list ";"
import_list     → import_spec ( "," import_spec )*
import_spec     → IDENTIFIER STRING    // alias "path"
                | STRING               // "path" (implicit name)
```

### Implicit Name Derivation

**Rule:** Extract the last component of the path as the variable name.

Examples:
- `"math"` → variable `math`
- `"string"` → variable `string`
- `"mylib/math"` → variable `math`
- `"mylib/math/advanced"` → variable `advanced`
- `"./utils/helpers"` → variable `helpers`

**Implementation:**
```c
// Extract implicit name from path
const char* derive_module_name(const char* path) {
    // Find last '/' or use entire path
    const char* last_slash = strrchr(path, '/');
    const char* name = last_slash ? last_slash + 1 : path;

    // Remove .sox extension if present
    size_t len = strlen(name);
    if (len > 4 && strcmp(name + len - 4, ".sox") == 0) {
        char* result = malloc(len - 3);
        strncpy(result, name, len - 4);
        result[len - 4] = '\0';
        return result;
    }

    return strdup(name);
}
```

### Module Loading Flow

```
import "math", str "string"
    ↓
1. Parse import statement at compile time
    ↓
2. For each import spec:
   a. Extract module path string
   b. Determine variable name (implicit or explicit)
   c. Emit OP_IMPORT with path constant
   d. Emit OP_DEFINE_LOCAL with variable name
   e. Add to local variable table
    ↓
3. At runtime:
   a. OP_IMPORT loads module (via require-like mechanism)
   b. Pushes module value on stack
   c. OP_DEFINE_LOCAL pops and assigns to local variable
    ↓
4. Module is now accessible via local variable
```

---

## Technical Implementation

### New Keywords and Tokens

**File: `src/scanner.c`**

Add new keyword:
```c
// In check_keyword() or keyword table
case 'i': return check_keyword(1, 5, "mport", TOKEN_IMPORT);
```

Add token type:
```c
// In scanner.h
typedef enum {
    // ... existing tokens
    TOKEN_IMPORT,
    // ...
} TokenType;
```

### Compiler Changes

**File: `src/compiler.c`**

#### 1. Add Import Declaration Handler

```c
static void import_declaration() {
    // import_declaration → "import" import_list ";"

    do {
        // Check for alias: IDENTIFIER STRING
        token_t var_name;
        bool has_alias = false;

        if (check(TOKEN_IDENTIFIER) && peek_next() == TOKEN_STRING) {
            // Explicit alias: import mymath "math"
            consume(TOKEN_IDENTIFIER, "Expect alias name");
            var_name = parser.previous;
            has_alias = true;
        }

        // Module path (required)
        consume(TOKEN_STRING, "Expect module path string");
        uint8_t path_constant = make_constant(OBJ_VAL(copy_string(
            parser.previous.start + 1,  // Skip opening quote
            parser.previous.length - 2   // Skip quotes
        )));

        // Derive implicit name if no alias
        if (!has_alias) {
            const char* path = AS_CSTRING(current_chunk()->constants.values[path_constant]);
            const char* implicit_name = derive_module_name(path);

            // Create token for implicit name
            var_name.start = implicit_name;
            var_name.length = strlen(implicit_name);
            var_name.line = parser.previous.line;
        }

        // Emit bytecode to load module
        emit_bytes(OP_IMPORT, path_constant);

        // Define local variable with module value
        uint8_t var_constant = identifier_constant(&var_name);
        define_variable(var_constant);

        // Add to local scope
        if (current->scope_depth > 0) {
            add_local(var_name);
        }

    } while (match(TOKEN_COMMA));

    consume(TOKEN_SEMICOLON, "Expect ';' after import");
}
```

#### 2. Add Peek Next Token Helper

```c
static TokenType peek_next() {
    // Look ahead two tokens
    parser_t saved = parser;
    advance();
    TokenType type = parser.current.type;
    parser = saved;
    return type;
}
```

#### 3. Update Declaration Handler

```c
static void declaration() {
    if (match(TOKEN_IMPORT)) {
        import_declaration();
    } else if (match(TOKEN_CLASS)) {
        class_declaration();
    } else if (match(TOKEN_FUN)) {
        fun_declaration();
    } else if (match(TOKEN_VAR)) {
        var_declaration();
    } else {
        statement();
    }

    if (parser.panic_mode) synchronize();
}
```

### VM Changes

**File: `src/vm.c` and `src/vm.h`**

#### 1. Add Module Cache

```c
// In vm.h
typedef struct {
    table_t globals;
    table_t strings;
    table_t modules;  // NEW: Module cache
    // ... rest of vm_t
} vm_t;
```

#### 2. Initialize Module Cache

```c
// In vm.c - l_init_vm()
void l_init_vm(vm_config_t *config) {
    // ... existing initialization
    l_init_table(&vm.modules);  // NEW
}
```

#### 3. Add New Opcode

```c
// In chunk.h
typedef enum {
    // ... existing opcodes
    OP_IMPORT,
    // ...
} OpCode;
```

#### 4. Implement OP_IMPORT Handler

```c
// In vm.c - run() switch statement
case OP_IMPORT: {
    obj_string_t* path = READ_STRING();
    value_t module = load_module(path);

    if (IS_NIL(module)) {
        // Error already reported by load_module
        return INTERPRET_RUNTIME_ERROR;
    }

    push(module);
    break;
}
```

#### 5. Implement Module Loader

```c
// In vm.c or new file src/lib/module.c
static value_t load_module(obj_string_t* path) {
    // Check cache first
    value_t cached;
    if (l_table_get(&vm.modules, path, &cached)) {
        return cached;
    }

    // Resolve file path
    char* file_path = resolve_module_path(path->chars);
    if (!file_path) {
        runtime_error("Module not found: '%s'", path->chars);
        return NIL_VAL;
    }

    // Read source file
    char* source = l_read_file(file_path);
    if (!source) {
        runtime_error("Could not read module: '%s'", file_path);
        free(file_path);
        return NIL_VAL;
    }

    // Compile module
    obj_function_t* module_fn = l_compile_module(source, file_path);
    free(source);

    if (!module_fn) {
        runtime_error("Failed to compile module: '%s'", file_path);
        free(file_path);
        return NIL_VAL;
    }

    // Mark as loading (circular dependency detection)
    l_table_set(&vm.modules, path, BOOL_VAL(true));

    // Execute module
    push(OBJ_VAL(module_fn));
    obj_closure_t* closure = l_new_closure(module_fn);
    pop();
    push(OBJ_VAL(closure));

    call_value(OBJ_VAL(closure), 0);
    value_t result = run();  // Execute module code

    // Get return value (top of stack)
    value_t module_exports = pop();

    // Cache result
    l_table_set(&vm.modules, path, module_exports);

    free(file_path);
    return module_exports;
}
```

### Module Compilation

**File: `src/compiler.c`**

```c
// Add new function type
typedef enum {
    TYPE_FUNCTION,
    TYPE_INITIALIZER,
    TYPE_METHOD,
    TYPE_MODULE,     // NEW
    TYPE_SCRIPT
} FunctionType;

// Compile module source
obj_function_t* l_compile_module(const char* source, const char* path) {
    init_scanner(source);
    compiler_t compiler;
    init_compiler(&compiler, TYPE_MODULE);

    parser.had_error = false;
    parser.panic_mode = false;

    advance();

    // Parse module body
    while (!match(TOKEN_EOF)) {
        declaration();
    }

    obj_function_t* function = end_compiler();

    // Set module name from path
    function->name = copy_string(path, strlen(path));

    return parser.had_error ? NULL : function;
}
```

### Path Resolution

**File: `src/lib/file.c`**

```c
char* resolve_module_path(const char* module_path) {
    char resolved[4096];

    // Try direct path
    if (file_exists(module_path)) {
        return realpath(module_path, NULL);
    }

    // Try with .sox extension
    snprintf(resolved, sizeof(resolved), "%s.sox", module_path);
    if (file_exists(resolved)) {
        return realpath(resolved, NULL);
    }

    // Try in current directory
    snprintf(resolved, sizeof(resolved), "./%s.sox", module_path);
    if (file_exists(resolved)) {
        return realpath(resolved, NULL);
    }

    // Try in module search paths
    // TODO: Implement search path array

    return NULL;  // Not found
}

bool file_exists(const char* path) {
    struct stat st;
    return stat(path, &st) == 0 && S_ISREG(st.st_mode);
}
```

---

## Module Format

Modules in Sox are simply source files that **return a value** (typically a table).

### Example Module

**File: `math.sox`**
```sox
// Module-level variables are local to the module
var pi = 3.14159;
var e = 2.71828;

// Private helper (not exported)
var _validate = fn(x) {
    if (type(x) != "number") {
        print("Error: expected number");
        return false;
    }
    return true;
};

// Public functions
var sqrt = fn(x) {
    if (!_validate(x)) return nil;
    return x ^ 0.5;
};

var square = fn(x) {
    if (!_validate(x)) return nil;
    return x * x;
};

var abs = fn(x) {
    if (!_validate(x)) return nil;
    if (x < 0) return -x;
    return x;
};

// Export public API
return Table{
    "pi": pi,
    "e": e,
    "sqrt": sqrt,
    "square": square,
    "abs": abs
    // _validate is NOT exported (private)
};
```

**Using the Module:**
```sox
import "math";

print(math.pi);         // 3.14159
print(math.sqrt(16));   // 4
print(math.square(5));  // 25
print(math.abs(-10));   // 10
```

### Alternative: Export Class

**File: `string.sox`**
```sox
class String {
    static upper(s) {
        // Implementation
        return s;  // Placeholder
    }

    static lower(s) {
        return s;  // Placeholder
    }

    static split(s, delimiter) {
        // Implementation
        return [];
    }
}

return String;  // Export the class itself
```

**Using the Module:**
```sox
import str "string";

var result = str.upper("hello");  // "HELLO"
var parts = str.split("a,b,c", ",");  // ["a", "b", "c"]
```

---

## Comparison with Go

### Go Import Syntax

```go
import "fmt"                    // implicit name: fmt
import "math"                   // implicit name: math
import customMath "math"        // explicit alias
import (                        // grouped imports
    "fmt"
    "math"
    m "math/big"
)
```

### Sox Import Syntax (Our Design)

```sox
import "fmt";                   // implicit name: fmt
import "math";                  // implicit name: math
import customMath "math";       // explicit alias
import "fmt", "math", m "math/big";  // multiple imports
```

**Similarities:**
- ✅ Implicit naming from package path
- ✅ Optional aliasing
- ✅ Clean, minimal syntax
- ✅ Compile-time declaration

**Differences:**
- Sox uses `;` terminator (Go doesn't)
- Sox uses `,` for multiple imports (Go uses parentheses)
- Go has package names in source (Sox uses return values)

---

## Implementation Phases

### Phase 1: Basic Import (Days 1-2)

**Goal:** Single import with implicit name

```sox
import "math";
print(math.pi);
```

**Tasks:**
1. Add `TOKEN_IMPORT` to scanner
2. Add `import_declaration()` to compiler
3. Add `OP_IMPORT` opcode
4. Implement basic `load_module()`
5. Add module cache to VM

**Test:**
```sox
// test_basic_import.sox
import "math";
print(math.sqrt(16));  // Should print 4
```

### Phase 2: Aliasing (Day 3)

**Goal:** Import with explicit alias

```sox
import mymath "math";
print(mymath.pi);
```

**Tasks:**
1. Update parser to detect alias pattern
2. Handle `IDENTIFIER STRING` syntax
3. Test alias resolution

**Test:**
```sox
// test_alias.sox
import m "math", s "string";
print(m.pi);
print(s.upper("hello"));
```

### Phase 3: Multiple Imports (Day 4)

**Goal:** Import multiple modules in one statement

```sox
import "math", "string", "array";
```

**Tasks:**
1. Update parser to handle comma-separated list
2. Test mixed implicit/explicit names
3. Handle errors gracefully

**Test:**
```sox
// test_multiple.sox
import "math", str "string", "array";
print(math.pi);
print(str.upper("test"));
print(array.length([1, 2, 3]));
```

### Phase 4: Path Resolution (Day 5)

**Goal:** Support relative and nested paths

```sox
import "./utils/helpers";
import "mylib/math/advanced";
```

**Tasks:**
1. Implement `resolve_module_path()`
2. Handle relative paths (`./ ../`)
3. Handle nested paths (`a/b/c`)
4. Extract implicit names from paths

**Test:**
```sox
// test_paths.sox
import "./modules/math";
import "lib/string/utils";
import h "helpers";
```

### Phase 5: Circular Dependencies (Day 6)

**Goal:** Handle circular imports gracefully

**Tasks:**
1. Implement loading sentinel
2. Detect circular dependencies
3. Provide clear error messages

**Test:**
```sox
// a.sox
import "b";
return Table{"name": "A"};

// b.sox
import "a";  // Circular!
return Table{"name": "B"};
```

### Phase 6: Error Handling & Polish (Day 7)

**Goal:** Production-ready error handling

**Tasks:**
1. Improve error messages
2. Handle missing files
3. Handle compilation errors
4. Show module stack traces
5. Add module search paths

---

## Example Use Cases

### Use Case 1: Standard Library

**File: `stdlib/math.sox`**
```sox
return Table{
    "pi": 3.14159265359,
    "e": 2.71828182846,
    "sqrt": fn(x) { return x ^ 0.5; },
    "pow": fn(x, y) { return x ^ y; },
    "abs": fn(x) { return x < 0 ? -x : x; },
    "min": fn(a, b) { return a < b ? a : b; },
    "max": fn(a, b) { return a > b ? a : b; }
};
```

**Usage:**
```sox
import "stdlib/math";

var result = math.sqrt(144);
print(result);  // 12
```

### Use Case 2: Custom Utilities

**File: `./utils/helpers.sox`**
```sox
var debug = fn(msg) {
    print("[DEBUG] " + msg);
};

var assert = fn(condition, msg) {
    if (!condition) {
        print("[ASSERT FAILED] " + msg);
    }
};

return Table{
    "debug": debug,
    "assert": assert
};
```

**Usage:**
```sox
import helpers "./utils/helpers";

helpers.debug("Starting application");
helpers.assert(true, "This should pass");
```

### Use Case 3: Class Library

**File: `lib/vector.sox`**
```sox
class Vector {
    init(x, y) {
        this.x = x;
        this.y = y;
    }

    add(other) {
        return Vector(this.x + other.x, this.y + other.y);
    }

    magnitude() {
        return (this.x * this.x + this.y * this.y) ^ 0.5;
    }
}

return Vector;
```

**Usage:**
```sox
import Vec "lib/vector";

var v1 = Vec(3, 4);
var v2 = Vec(1, 2);
var v3 = v1.add(v2);
print(v3.magnitude());  // 7.071...
```

---

## Testing Strategy

### Unit Tests

**File: `src/test/module_test.c`**

```c
MU_TEST(test_implicit_name_derivation) {
    mu_assert_string_eq("math", derive_module_name("math"));
    mu_assert_string_eq("math", derive_module_name("lib/math"));
    mu_assert_string_eq("advanced", derive_module_name("lib/math/advanced"));
    mu_assert_string_eq("helpers", derive_module_name("./utils/helpers"));
}

MU_TEST(test_module_caching) {
    // Load module twice, should execute once
    value_t m1 = load_module(copy_string("test_module", 11));
    value_t m2 = load_module(copy_string("test_module", 11));
    mu_assert(IS_TABLE(m1), "Module should return table");
    mu_assert(m1 == m2, "Module should be cached");
}

MU_TEST(test_circular_dependency) {
    // Should detect circular imports
    value_t result = load_module(copy_string("circular_a", 10));
    mu_assert(IS_NIL(result), "Should fail on circular dependency");
}
```

### Integration Tests

**File: `src/test/scripts/test_import.sox`**
```sox
// Test basic import
import "test_modules/math";
print(math.sqrt(16));  // Expected: 4

// Test alias
import m "test_modules/math";
print(m.pi);  // Expected: 3.14159

// Test multiple
import "test_modules/math", s "test_modules/string";
print(math.sqrt(9));  // Expected: 3
print(s.upper("hello"));  // Expected: HELLO
```

**Expected Output: `src/test/scripts/test_import.sox.out`**
```
4
3.14159
3
HELLO
```

---

## Documentation

### User Guide

**File: `docs/modules.md`**

```markdown
# Sox Module System

Sox supports a Go-style module system with automatic naming and aliasing.

## Basic Import

Import a module by path:

sox
import "math";
print(math.pi);


The module name is derived from the file path automatically.

## Aliasing

Avoid naming conflicts with aliases:

sox
import mymath "math";
print(mymath.sqrt(16));


## Multiple Imports

Import multiple modules at once:

sox
import "math", "string", "array";


Mix implicit names and aliases:

sox
import "math", str "string", arr "array";


## Creating Modules

A module is a `.sox` file that returns a value:

sox
// mymodule.sox
var x = 10;
var y = 20;

return Table{
    "sum": x + y,
    "product": x * y
};


Import and use:

sox
import mod "mymodule";
print(mod.sum);  // 30

```

---

## Summary

### Advantages of Go-Style Imports

1. ✅ **Clean Syntax**: No manual variable assignment
2. ✅ **Automatic Naming**: Derive from module path
3. ✅ **Conflict Resolution**: Optional aliasing
4. ✅ **Multiple Imports**: Batch imports in one statement
5. ✅ **Familiar**: Similar to Go, TypeScript, Python
6. ✅ **Compile-Time**: Static analysis possible
7. ✅ **Scoped**: Imports create local variables

### Implementation Effort

**Total: 6-7 days**

| Phase | Days | Description |
|-------|------|-------------|
| Basic Import | 2 | Single import with implicit name |
| Aliasing | 1 | Explicit alias support |
| Multiple Imports | 1 | Comma-separated imports |
| Path Resolution | 1 | Relative and nested paths |
| Circular Deps | 1 | Detection and handling |
| Polish | 1 | Error handling, docs, tests |

### Key Files to Modify

1. `src/scanner.c` - Add `TOKEN_IMPORT`
2. `src/compiler.c` - Add `import_declaration()`
3. `src/vm.c` - Add `OP_IMPORT`, module cache
4. `src/lib/file.c` - Add path resolution
5. `src/chunk.h` - Add `OP_IMPORT` opcode

---

**Next Steps:**
1. Review and approve this design
2. Implement Phase 1 (basic import)
3. Test and iterate
4. Complete remaining phases
5. Write documentation and examples

**Status:** Ready for Implementation ✅

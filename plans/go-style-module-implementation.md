# Go-Style Module System Implementation for Sox

**Author:** Claude
**Date:** 2025-12-25
**Status:** Implementation Plan
**Version:** 2.0
**Based On:** Module System Design Plan v1.0

---

## Executive Summary

This document details the implementation of a **Go-style module system** for Sox, featuring:
- **Implicit naming**: `import "math"` creates a `math` variable automatically
- **Optional aliasing**: `import mymath "math"` for conflict resolution
- **Single import**: No parentheses for single imports
- **Multiple imports**: Parentheses with multi-line support like Go
- **Clean syntax**: Matches Go's import style closely

This approach combines the simplicity of Lua's `require()` with Go's elegant import semantics.

---

## Syntax Design

### Single Import (No Parentheses)

```sox
// Import with implicit name from module/file name
import "math"

// Creates local variable 'math' containing the module
print(math.pi)         // 3.14159
print(math.sqrt(16))   // 4
```

### Single Import with Alias

```sox
// Import with explicit alias to avoid conflicts
import mymath "math"

print(mymath.pi)       // 3.14159
print(mymath.sqrt(16)) // 4
```

### Multiple Imports (With Parentheses)

```sox
// Import multiple modules using parentheses
import (
    "math"
    "string"
    "array"
)

print(math.pi)
print(string.upper("hello"))
print(array.length([1, 2, 3]))
```

### Multiple Imports with Aliases

```sox
// Mix implicit and explicit names
import (
    "math"
    str "string"
    arr "array"
)

print(math.pi)
print(str.upper("hello"))
print(arr.length([1, 2, 3]))
```

### Multiple Imports (Single Line)

```sox
// Also support single-line for short lists
import ("math", "string", "array")
```

### Path-Based Imports

```sox
// Import from relative path
import "./utils/helpers"  // Creates 'helpers' variable

// Import from nested path
import "mylib/math/advanced"  // Creates 'advanced' variable

// Multiple paths
import (
    "./utils/helpers"
    "mylib/math/advanced"
    adv "mylib/math/advanced"
)
```

---

## Exact Go Comparison

### Go Import Syntax

```go
// Single import
import "fmt"

// Single import with alias
import f "fmt"

// Multiple imports
import (
    "fmt"
    "math"
    "net/http"
)

// Multiple imports with aliases
import (
    "fmt"
    m "math"
    "strings"
)
```

### Sox Import Syntax (Our Design)

```sox
// Single import
import "fmt"

// Single import with alias
import f "fmt"

// Multiple imports
import (
    "fmt"
    "math"
    "net/http"
)

// Multiple imports with aliases
import (
    "fmt"
    m "math"
    "strings"
)
```

**Differences from Go:**
- Sox supports optional `,` in multi-line imports (for flexibility)
- Go packages have package declarations in source; Sox uses return values
- Sox allows optional `;` after imports (like all Sox statements)

**Similarities:**
- ✅ Identical parentheses usage
- ✅ Identical alias syntax
- ✅ Implicit naming from path
- ✅ Multi-line support
- ✅ Compile-time declaration
- ✅ No semicolons required (matching Go and Sox style)

---

## Syntax Grammar

```
import_stmt     → "import" ( import_single | import_group ) ";"?

import_single   → import_spec

import_group    → "(" import_list ")"

import_list     → import_spec ( NEWLINE | "," )* import_spec

import_spec     → IDENTIFIER STRING    // alias "path"
                | STRING               // "path" (implicit name)
```

**Note:** The `";"?` indicates that semicolons are optional (matching Sox's existing style).

**Examples:**
```
import "math"                           // Single (no semicolon)
import m "math"                         // Single with alias
import ("a", "b")                       // Group (single line)
import ("a" "b")                        // Group (no commas)
import (                                // Group (multi-line)
    "a"
    "b"
)
import (                                // Group (multi-line with commas)
    "a",
    "b",
)
```

---

## How It Works

### Import Statement Parsing

The `import` statement is a **compile-time declaration** that:
1. Parses module path(s)
2. Determines variable name (implicit or explicit)
3. Emits bytecode to load module and assign to local variable
4. Creates local variable in current scope

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
import (
    "math"
    str "string"
)
    ↓
1. Parse import statement at compile time
    ↓
2. Detect parentheses → multiple imports
    ↓
3. For each import spec (until closing paren):
   a. Extract module path string
   b. Determine variable name (implicit or explicit)
   c. Emit OP_IMPORT with path constant
   d. Emit OP_DEFINE_LOCAL with variable name
   e. Add to local variable table
    ↓
4. At runtime:
   a. OP_IMPORT loads module (via require-like mechanism)
   b. Pushes module value on stack
   c. OP_DEFINE_LOCAL pops and assigns to local variable
    ↓
5. Modules are now accessible via local variables
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
    // import_declaration → "import" ( import_single | import_group ) ";"?

    if (match(TOKEN_LEFT_PAREN)) {
        // Multiple imports: import ( ... )
        import_group();
    } else {
        // Single import: import "path" or import alias "path"
        import_single();
    }

    // Semicolon is optional (matching Sox style)
    match(TOKEN_SEMICOLON);
}

static void import_single() {
    // Parse one import spec
    token_t var_name;
    bool has_alias = false;

    // Check for alias: IDENTIFIER STRING
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
}

static void import_group() {
    // import_group → "(" import_list ")"
    // import_list  → import_spec ( NEWLINE | "," )* import_spec

    // Parse until closing paren
    while (!check(TOKEN_RIGHT_PAREN) && !check(TOKEN_EOF)) {
        // Skip newlines
        while (match(TOKEN_NEWLINE)) {
            // Continue
        }

        if (check(TOKEN_RIGHT_PAREN)) break;

        // Parse single import spec
        import_single();

        // Allow optional comma or newline separator
        if (!check(TOKEN_RIGHT_PAREN)) {
            if (!match(TOKEN_COMMA) && !match(TOKEN_NEWLINE)) {
                // If not comma or newline, expect closing paren
                if (!check(TOKEN_RIGHT_PAREN)) {
                    error("Expect ',' or newline between imports");
                }
            }
        }
    }

    consume(TOKEN_RIGHT_PAREN, "Expect ')' after import list");
}
```

#### 2. Add Peek Next Token Helper

```c
static TokenType peek_next() {
    // Look ahead two tokens
    scanner_t saved_scanner = scanner;
    token_t saved_current = parser.current;

    advance();
    TokenType type = parser.current.type;

    scanner = saved_scanner;
    parser.current = saved_current;

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

#### 4. Handle Newlines in Scanner

**Note:** Sox scanner may need to track newlines if not already doing so for multi-line imports.

```c
// In scanner.c - scanToken()
case '\n':
    scanner.line++;
    return make_token(TOKEN_NEWLINE);  // Return newline token
```

Alternatively, skip newlines in most contexts but handle them in import lists.

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
        // Return cached module (unless loading sentinel)
        if (IS_BOOL(cached)) {
            // Circular dependency detected
            runtime_error("Circular import: '%s'", path->chars);
            return NIL_VAL;
        }
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
import "math"

print(math.pi)         // 3.14159
print(math.sqrt(16))   // 4
print(math.square(5))  // 25
print(math.abs(-10))   // 10
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
import str "string"

var result = str.upper("hello")  // "HELLO"
var parts = str.split("a,b,c", ",")  // ["a", "b", "c"]
```

---

## Syntax Examples

### Example 1: Simple Single Import

```sox
import "math"

var result = math.sqrt(144)
print(result)  // 12
```

### Example 2: Single Import with Alias

```sox
import m "math"

print(m.pi)  // 3.14159
```

### Example 3: Multiple Imports (Multi-line)

```sox
import (
    "math"
    "string"
    "array"
)

print(math.sqrt(16))
print(string.upper("hello"))
print(array.length([1, 2, 3]))
```

### Example 4: Multiple Imports with Aliases

```sox
import (
    "math"
    str "string"
    arr "array"
)

print(math.pi)
print(str.lower("HELLO"))
print(arr.push([1, 2], 3))
```

### Example 5: Multiple Imports (Single Line)

```sox
import ("math", "string", "array")
```

### Example 6: Relative and Nested Paths

```sox
import (
    "./utils/helpers"
    "lib/math/advanced"
    geo "lib/geometry/shapes"
)

helpers.debug("Starting")
print(advanced.calculate())
var circle = geo.Circle(5)
```

### Example 7: Real-World Usage

```sox
// app.sox
import (
    "stdlib/math"
    "stdlib/string"
    utils "./utils/helpers"
    cfg "./config"
)

fn main() {
    utils.log("Application started")

    var radius = 10
    var area = math.pi * math.pow(radius, 2)

    var message = string.format("Circle area: {}", area)
    utils.log(message)

    if (cfg.debug) {
        utils.debug("Debug mode enabled")
    }
}

main()
```

---

## Implementation Phases

### Phase 1: Single Import (Days 1-2)

**Goal:** Single import with implicit name (no parentheses)

```sox
import "math"
print(math.pi)
```

**Tasks:**
1. Add `TOKEN_IMPORT` to scanner
2. Add `import_declaration()` to compiler
3. Implement `import_single()` for single imports
4. Add `OP_IMPORT` opcode
5. Implement basic `load_module()`
6. Add module cache to VM

**Test:**
```sox
// test_basic_import.sox
import "math"
print(math.sqrt(16))  // Should print 4
```

### Phase 2: Import with Alias (Day 2)

**Goal:** Single import with explicit alias

```sox
import mymath "math"
print(mymath.pi)
```

**Tasks:**
1. Update parser to detect alias pattern
2. Handle `IDENTIFIER STRING` syntax
3. Test alias resolution

**Test:**
```sox
// test_alias.sox
import m "math"
print(m.pi)
print(m.sqrt(9))
```

### Phase 3: Multiple Imports with Parentheses (Days 3-4)

**Goal:** Multiple imports using Go-style parentheses

```sox
import (
    "math"
    "string"
)
```

**Tasks:**
1. Implement `import_group()` for parenthesized imports
2. Handle newlines as separators
3. Support optional commas
4. Test multi-line and single-line variants

**Test:**
```sox
// test_multiple.sox
import (
    "math"
    str "string"
)
print(math.pi)
print(str.upper("test"))
```

### Phase 4: Path Resolution (Day 5)

**Goal:** Support relative and nested paths

```sox
import "./utils/helpers"
import "mylib/math/advanced"
```

**Tasks:**
1. Implement `resolve_module_path()`
2. Handle relative paths (`./ ../`)
3. Handle nested paths (`a/b/c`)
4. Extract implicit names from paths

**Test:**
```sox
// test_paths.sox
import (
    "./modules/math"
    "lib/string/utils"
)
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

**File: `src/test/scripts/test_import_single.sox`**
```sox
// Test single import
import "test_modules/math"
print(math.sqrt(16))  // Expected: 4
```

**File: `src/test/scripts/test_import_alias.sox`**
```sox
// Test alias
import m "test_modules/math"
print(m.pi)  // Expected: 3.14159
```

**File: `src/test/scripts/test_import_multiple.sox`**
```sox
// Test multiple imports
import (
    "test_modules/math"
    s "test_modules/string"
)
print(math.sqrt(9))        // Expected: 3
print(s.upper("hello"))    // Expected: HELLO
```

**Expected Output Files:**
- `test_import_single.sox.out`
- `test_import_alias.sox.out`
- `test_import_multiple.sox.out`

---

## Documentation

### User Guide

**File: `docs/modules.md`**

````markdown
# Sox Module System

Sox supports a Go-style module system with automatic naming and clean syntax.

## Single Import

Import a module by path:

```sox
import "math"
print(math.pi)
```

The module name is derived from the file path automatically.

## Import with Alias

Avoid naming conflicts with aliases:

```sox
import mymath "math"
print(mymath.sqrt(16))
```

## Multiple Imports

Import multiple modules using parentheses:

```sox
import (
    "math"
    "string"
    "array"
)
```

Mix implicit names and aliases:

```sox
import (
    "math"
    str "string"
    arr "array"
)
```

Single-line variant (optional commas):

```sox
import ("math", "string", "array")
```

## Creating Modules

A module is a `.sox` file that returns a value:

```sox
// mymodule.sox
var x = 10
var y = 20

return Table{
    "sum": x + y,
    "product": x * y
}
```

Import and use:

```sox
import mod "mymodule"
print(mod.sum)  // 30
```

## Path Resolution

- Direct path: `"math"` → `math.sox`
- Relative: `"./utils/helpers"` → `./utils/helpers.sox`
- Nested: `"lib/math/advanced"` → `lib/math/advanced.sox`

## Module Names

The variable name is derived from the last path component:

- `"math"` → variable `math`
- `"lib/math"` → variable `math`
- `"lib/math/advanced"` → variable `advanced`
````

---

## Summary

### Advantages of This Design

1. ✅ **Go-Compatible Syntax**: Nearly identical to Go imports
2. ✅ **Clean and Minimal**: No unnecessary punctuation
3. ✅ **Multi-line Support**: Natural formatting for many imports
4. ✅ **Flexible Separators**: Newlines or commas (or both)
5. ✅ **Automatic Naming**: Derive from module path
6. ✅ **Conflict Resolution**: Optional aliasing
7. ✅ **Compile-Time**: Static analysis possible
8. ✅ **Scoped**: Imports create local variables

### Syntax Comparison

| Language | Single Import | Multiple Imports |
|----------|---------------|------------------|
| **Go** | `import "fmt"` | `import (\n  "fmt"\n  "math"\n)` |
| **Sox** | `import "fmt"` | `import (\n  "fmt"\n  "math"\n)` |
| **Python** | `import math` | `import math, sys` |
| **JavaScript** | `import fmt from "fmt"` | `import { a, b } from "lib"` |

Sox syntax is **identical** to Go!

### Implementation Effort

**Total: 6-7 days**

| Phase | Days | Description |
|-------|------|-------------|
| Single Import | 2 | Import with implicit name |
| Aliasing | 0.5 | Explicit alias support |
| Multiple Imports | 1.5 | Parentheses and multi-line |
| Path Resolution | 1 | Relative and nested paths |
| Circular Deps | 1 | Detection and handling |
| Polish | 1 | Error handling, docs, tests |

### Key Files to Modify

1. `src/scanner.c` - Add `TOKEN_IMPORT`, handle newlines
2. `src/compiler.c` - Add `import_declaration()`, `import_single()`, `import_group()`
3. `src/vm.c` - Add `OP_IMPORT`, `load_module()`, module cache
4. `src/lib/file.c` - Add path resolution
5. `src/chunk.h` - Add `OP_IMPORT` opcode

---

## Next Steps

1. ✅ Review and approve this design
2. Implement Phase 1 (single import)
3. Implement Phase 2 (aliasing)
4. Implement Phase 3 (multiple imports with parentheses)
5. Implement Phase 4 (path resolution)
6. Implement Phase 5 (circular dependency handling)
7. Implement Phase 6 (polish and documentation)
8. Write comprehensive tests
9. Update README and documentation

**Status:** Ready for Implementation ✅

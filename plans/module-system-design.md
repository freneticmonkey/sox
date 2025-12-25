# Module System Design Plan for Sox

**Author:** Claude
**Date:** 2025-12-25
**Status:** Proposal
**Version:** 1.0

---

## Executive Summary

This document proposes multiple design options for adding module/package support to the Sox programming language. After analyzing Sox's existing architecture and studying module systems from similar dynamic languages (Lua, Wren, Python, JavaScript), we present four distinct approaches with varying complexity and capabilities.

**Recommended Approach:** **Option 2 (Lua-Style Simple Modules)** provides the best balance of simplicity, familiarity, and integration with Sox's existing architecture.

---

## Table of Contents

1. [Current State Analysis](#current-state-analysis)
2. [Design Goals](#design-goals)
3. [Module System Options](#module-system-options)
   - [Option 1: Namespace-Only (Minimal)](#option-1-namespace-only-minimal)
   - [Option 2: Lua-Style Simple Modules](#option-2-lua-style-simple-modules)
   - [Option 3: Wren-Style Explicit Imports](#option-3-wren-style-explicit-imports)
   - [Option 4: Python-Style Package System](#option-4-python-style-package-system)
4. [Comparison Matrix](#comparison-matrix)
5. [Implementation Roadmap](#implementation-roadmap)
6. [Technical Architecture](#technical-architecture)
7. [Edge Cases and Considerations](#edge-cases-and-considerations)
8. [References](#references)

---

## Current State Analysis

### Existing Architecture Strengths

Sox has several architectural foundations that support module system development:

1. **Hash Table Infrastructure**: `vm.globals` uses efficient `table_t` for O(1) lookups
2. **Object System**: Extensible object types (`ObjType` enum) allow new module object types
3. **String Interning**: Prevents duplication of module names and identifiers
4. **Native Function Pattern**: Shows how to register external functionality into global namespace
5. **Closure Upvalues**: Demonstrates cross-scope reference capture mechanism
6. **Three-Tier Scoping**: Local → Upvalue → Global resolution provides clear precedence
7. **Compilation Independence**: Functions compile to self-contained bytecode chunks

### Current Limitations

1. **No Import Mechanism**: No keywords for loading external files
2. **Single Namespace**: All globals share one `vm.globals` table
3. **No Module Cache**: No mechanism to prevent duplicate loading
4. **Single Compilation Unit**: Only one source file per execution
5. **No Export Control**: All global declarations are equally accessible

### File: `src/scanner.c`

Keywords that could conflict or relate to module syntax:
- `TOKEN_VAR`, `TOKEN_FUN`, `TOKEN_CLASS` (declaration keywords)
- No existing `import`, `export`, `module`, `require`, `from`, `as` keywords

---

## Design Goals

### Primary Goals

1. **Simplicity**: Keep the implementation minimal and understandable
2. **Familiarity**: Use patterns familiar from other dynamic languages
3. **No Breaking Changes**: Existing Sox code must continue to work
4. **Circular Dependency Handling**: Support mutual imports without deadlock
5. **Namespace Isolation**: Prevent naming conflicts between modules
6. **Caching**: Avoid re-executing module code on repeated imports

### Secondary Goals

1. **Selective Imports**: Import only needed symbols (optional)
2. **Export Control**: Explicit public/private distinction (optional)
3. **Package Hierarchies**: Nested module structures (optional)
4. **Path Resolution**: Configurable search paths for modules
5. **REPL Integration**: Support interactive module loading

---

## Module System Options

---

## Option 1: Namespace-Only (Minimal)

### Overview

Add minimal namespace support without file loading. Modules are tables that group related functions and variables. This is the simplest possible approach.

### Syntax

```sox
// Define a module as a table
var Math = Table();
Math.pi = 3.14159;
Math.sqrt = fn(x) {
    // implementation
    return x ^ 0.5;
};

// Use the module
print(Math.pi);
print(Math.sqrt(16));
```

### How It Works

1. Use existing `Table()` native function to create namespace objects
2. Store functions/variables as table entries
3. Access via dot notation (already supported: `table.key`)
4. No new keywords or file loading

### Pros

- ✅ Zero language changes required
- ✅ Works with existing syntax
- ✅ Simplest implementation (documentation only)
- ✅ No circular dependency issues
- ✅ Users can organize code immediately

### Cons

- ❌ No automatic file loading
- ❌ No import/export mechanism
- ❌ Manual namespace management
- ❌ All code still in single file
- ❌ Not a true module system

### Implementation Effort

**Effort:** 0-1 day (documentation only)

**Changes Required:**
- Document the pattern in README
- Add example scripts showing namespace organization
- Possibly add convenience function: `fn module(name) { return Table(); }`

### Example from Real Language

**JavaScript Object Namespaces (pre-modules):**
```javascript
// Old-school JavaScript namespace pattern
var MyApp = MyApp || {};
MyApp.utils = {
    capitalize: function(str) { return str.toUpperCase(); }
};
```

### Verdict

❌ **Not Recommended** - This is not a real module system, just a coding pattern. Doesn't solve file organization or code reuse across files.

---

## Option 2: Lua-Style Simple Modules

### Overview

Simple, elegant module system inspired by Lua. Files are modules. `require()` loads and caches modules. Modules return a table of exports. No new keywords needed.

### Syntax

**File: `math.sox`**
```sox
// Module file automatically gets isolated scope
var pi = 3.14159;

fn sqrt(x) {
    return x ^ 0.5;
}

fn square(x) {
    return x * x;
}

// Return table of public exports
return Table{
    "pi": pi,
    "sqrt": sqrt,
    "square": square
};
```

**File: `main.sox`**
```sox
// Load module (returns the exported table)
var Math = require("math");

print(Math.pi);         // 3.14159
print(Math.sqrt(16));   // 4
print(Math.square(5));  // 25

// Can also destructure if we add that syntax
// var {sqrt, square} = require("math");
```

**Alternative Compact Syntax:**
```sox
// Define and export inline
return Table{
    "pi": 3.14159,
    "sqrt": fn(x) { return x ^ 0.5; },
    "add": fn(a, b) { return a + b; }
};
```

### How It Works

1. **`require(module_name)`** - New native function
   - Takes string module name/path
   - Checks cache `vm.modules` table first
   - If not cached, resolves file path and loads source
   - Compiles module in isolated scope
   - Executes module code, captures return value
   - Stores in cache: `vm.modules[name] = result`
   - Returns the module's returned value

2. **Module Execution Context**
   - Each module compiles as a function scope
   - Module-level `var` declarations are local to module
   - Only returned value becomes accessible to importers
   - Modules can import other modules (circular safe via cache)

3. **Path Resolution**
   - Check exact path: `require("./math")`
   - Try with `.sox` extension: `math.sox`
   - Search in module paths (configurable)
   - Relative to current file or working directory

4. **Caching Strategy**
   - Cache key: resolved absolute path
   - Before compiling, mark module as "loading" to detect cycles
   - After execution, store result (even if nil)
   - Circular imports get partially initialized module

### Pros

- ✅ **Simple and proven**: Lua has used this successfully for decades
- ✅ **Minimal syntax**: Only one new function `require()`
- ✅ **Flexible exports**: Return anything (table, function, class, value)
- ✅ **Natural caching**: Hash table-based module cache
- ✅ **Circular dependency safe**: Cache-before-execute pattern
- ✅ **Integrates cleanly**: Uses existing tables and return statements
- ✅ **REPL friendly**: `var M = require("foo")` works interactively
- ✅ **Easy to implement**: Builds on existing VM infrastructure

### Cons

- ❌ **No selective imports**: Must import entire module
- ❌ **Verbose exports**: Must manually build export table
- ❌ **Global `require`**: One more global function
- ❌ **No static analysis**: Dynamic string-based imports
- ❌ **No explicit dependencies**: Can't see imports without reading code

### Implementation Effort

**Effort:** 3-5 days

**Changes Required:**

1. **Add Module Cache** (`src/vm.h`, `src/vm.c`):
   ```c
   typedef struct {
       table_t globals;
       table_t strings;
       table_t modules;  // NEW: module cache
       // ...
   } vm_t;
   ```

2. **Implement `require()` Native** (`src/lib/native_api.c`):
   ```c
   static value_t native_require(int argCount, value_t* args) {
       if (argCount != 1 || !IS_STRING(args[0])) {
           runtimeError("require() expects string argument");
           return NIL_VAL;
       }

       obj_string_t* name = AS_STRING(args[0]);

       // Check cache first
       value_t cached;
       if (l_table_get(&vm.modules, name, &cached)) {
           return cached;
       }

       // Resolve file path
       char* path = resolve_module_path(name->chars);
       if (!path) {
           runtimeError("Module not found: %s", name->chars);
           return NIL_VAL;
       }

       // Read source file
       char* source = read_file(path);

       // Compile as function
       obj_function_t* module_fn = compile_module(source, path);
       free(source);

       // Mark as loading (for circular detection)
       l_table_set(&vm.modules, name, NIL_VAL);

       // Execute module
       value_t result = execute_module(module_fn);

       // Cache result
       l_table_set(&vm.modules, name, result);

       return result;
   }
   ```

3. **Module Path Resolution** (`src/lib/file.c`):
   ```c
   char* resolve_module_path(const char* name) {
       // Try direct path
       if (file_exists(name)) return strdup(name);

       // Try with .sox extension
       char path[1024];
       snprintf(path, sizeof(path), "%s.sox", name);
       if (file_exists(path)) return strdup(path);

       // Try in module search paths
       // (implement search path array)

       return NULL;  // Not found
   }
   ```

4. **Module Compilation** (`src/compiler.c`):
   ```c
   obj_function_t* compile_module(const char* source, const char* path) {
       // Compile source as top-level function
       // Module scope = function scope (isolated from globals)
       // Return statement at module level returns export value

       init_scanner(source);
       init_compiler(&compiler, TYPE_MODULE);  // New compiler type

       advance();
       while (!match(TOKEN_EOF)) {
           declaration();
       }

       obj_function_t* function = end_compiler();
       return function;
   }
   ```

5. **Update Scanner** (`src/scanner.c`):
   - No changes needed (uses existing tokens)

6. **Testing** (`src/test/scripts/`):
   - Add `test_require.sox` with module import tests
   - Add `math_module.sox` helper module
   - Test circular dependencies
   - Test module caching

### Example from Real Language: Lua

**Lua Module System:**
```lua
-- math.lua
local M = {}

M.pi = 3.14159

function M.sqrt(x)
    return x ^ 0.5
end

return M
```

```lua
-- main.lua
local math = require("math")
print(math.pi)
```

**How Lua Does It:**
- `require()` checks `package.loaded` cache
- Searches for file using `package.path`
- Executes as chunk, captures return value
- Stores in cache, returns value

**Reference:** [Lua-users Wiki: Modules Tutorial](http://lua-users.org/wiki/ModulesTutorial)

### Verdict

✅ **RECOMMENDED** - Best balance of simplicity, power, and familiarity. Proven design from Lua. Minimal implementation effort. Natural fit for Sox's architecture.

---

## Option 3: Wren-Style Explicit Imports

### Overview

Explicit import syntax that lists exactly which symbols to import from a module. Provides clear visibility of dependencies and enables future static analysis.

### Syntax

**File: `math.sox`**
```sox
// Top-level variables are module-scoped by default
var pi = 3.14159;

var sqrt = fn(x) {
    return x ^ 0.5;
};

var square = fn(x) {
    return x * x;
};

// Private helper (not exported)
var _validate = fn(x) {
    if (type(x) != "number") {
        print("Error: expected number");
        return false;
    }
    return true;
};

// No explicit export needed - all top-level vars are potentially exportable
```

**File: `main.sox`**
```sox
// Import specific symbols
import "math" for pi, sqrt, square;

print(pi);         // 3.14159
print(sqrt(16));   // 4
print(square(5));  // 25
// print(_validate);  // ERROR: _validate not imported

// Import with alias
import "math" for sqrt as squareRoot;
print(squareRoot(9));  // 3

// Import entire module as namespace
import "math";  // Creates 'math' variable with all exports
print(math.pi);
```

### How It Works

1. **New Keywords**: `import`, `for`, `as` (note: `for` already exists!)
   - Alternative: `import "math" with pi, sqrt, square`
   - Or: `from "math" import pi, sqrt, square`

2. **Module Compilation**:
   - Module compiled in isolated scope
   - All top-level `var`, `fn`, `class` declarations tracked
   - Variables starting with `_` are private (convention)
   - Public symbols stored in module table

3. **Import Resolution**:
   - Parse import statement at compile time
   - Emit `OP_IMPORT_MODULE` with module name constant
   - For selective imports, emit `OP_IMPORT_SYMBOL` for each name
   - VM loads module, extracts specified symbols, creates local variables

4. **Runtime Execution**:
   ```
   OP_IMPORT_MODULE "math" -> Loads module, pushes module table
   OP_IMPORT_SYMBOL "pi"   -> Get module.pi, create local var 'pi'
   OP_IMPORT_SYMBOL "sqrt" -> Get module.sqrt, create local var 'sqrt'
   ```

5. **Caching**: Same as Option 2 (hash table cache)

### Pros

- ✅ **Explicit dependencies**: Clear what each file uses
- ✅ **Selective imports**: Import only what you need
- ✅ **Aliasing support**: Rename imports to avoid conflicts
- ✅ **Better for tooling**: Static analysis can extract dependencies
- ✅ **Namespace pollution control**: Don't import everything
- ✅ **Privacy convention**: Underscore prefix for private symbols
- ✅ **Familiar to Python/JS users**: Similar to ES6 imports

### Cons

- ❌ **More complex parsing**: New statement type and grammar rules
- ❌ **Keyword conflict**: `for` already used for loops
- ❌ **More verbose**: Must list all imports explicitly
- ❌ **Compiler complexity**: Track symbols, emit multiple opcodes
- ❌ **Two import styles**: Named vs. namespace imports need different handling

### Implementation Effort

**Effort:** 7-10 days

**Changes Required:**

1. **Add Keywords** (`src/scanner.c`):
   ```c
   TOKEN_IMPORT,  // 'import'
   TOKEN_FROM,    // 'from' (alternative to 'for')
   // TOKEN_FOR already exists (conflict!)
   TOKEN_AS,      // 'as' (for aliasing)
   ```

2. **Update Compiler** (`src/compiler.c`):
   ```c
   static void import_declaration() {
       consume(TOKEN_STRING, "Expect module name");
       uint8_t module_name = make_constant(OBJ_VAL(parser.previous));

       emit_bytes(OP_IMPORT_MODULE, module_name);

       if (match(TOKEN_FROM) || match(TOKEN_WITH)) {  // Avoid 'for' conflict
           // Parse symbol list
           do {
               consume(TOKEN_IDENTIFIER, "Expect symbol name");
               uint8_t symbol = identifier_constant(&parser.previous);

               token_t local_name = parser.previous;
               if (match(TOKEN_AS)) {
                   consume(TOKEN_IDENTIFIER, "Expect alias name");
                   local_name = parser.previous;
               }

               emit_bytes(OP_IMPORT_SYMBOL, symbol);
               add_local(local_name);  // Create local variable
           } while (match(TOKEN_COMMA));
       } else {
           // Import as namespace
           // Create local with module name
       }
   }
   ```

3. **New Opcodes** (`src/chunk.h`):
   ```c
   OP_IMPORT_MODULE,  // Load module, push module table
   OP_IMPORT_SYMBOL,  // Pop module, get symbol, push value
   ```

4. **VM Implementation** (`src/vm.c`):
   ```c
   case OP_IMPORT_MODULE: {
       obj_string_t* name = READ_STRING();
       value_t module = load_module(name);  // Like require()
       push(module);
       break;
   }

   case OP_IMPORT_SYMBOL: {
       obj_string_t* symbol = READ_STRING();
       value_t module = peek(0);  // Module table on stack
       value_t value;
       if (!table_get(&AS_TABLE(module)->table, symbol, &value)) {
           runtimeError("Symbol '%s' not found in module", symbol->chars);
       }
       pop();  // Remove module
       push(value);  // Push symbol value
       break;
   }
   ```

5. **Module Export Table Construction**:
   - During module compilation, track all top-level declarations
   - After execution, collect non-private symbols into table
   - Return table as module value

### Example from Real Language: Wren

**Wren Import Syntax:**
```wren
// math.wren
var pi = 3.14159

class Math {
    static sqrt(x) { x.sqrt }
}
```

```wren
// main.wren
import "math" for pi, Math

System.print(pi)
System.print(Math.sqrt(16))
```

**How Wren Does It:**
- Modules execute in isolated scope
- `import` statement executed at compile time
- Only imported names become available
- Module cached in registry after first load
- Circular imports supported via cache-before-execute

**Reference:** [Wren Modularity Documentation](https://wren.io/modularity.html)

### Verdict

⚠️ **CONSIDER IF** you want explicit dependency tracking and are willing to invest more implementation time. Good for larger codebases but more complex than Option 2.

---

## Option 4: Python-Style Package System

### Overview

Full-featured package system with directories as packages, `__init__.sox` files, and hierarchical imports. Most powerful but also most complex.

### Syntax

**Directory Structure:**
```
project/
├── main.sox
└── mylib/
    ├── __init__.sox      # Package initializer
    ├── math.sox          # Submodule
    └── string.sox        # Submodule
```

**File: `mylib/__init__.sox`**
```sox
// Package initialization
var version = "1.0.0";

// Re-export submodules
var math = import("mylib.math");
var string = import("mylib.string");

return Table{
    "version": version,
    "math": math,
    "string": string
};
```

**File: `mylib/math.sox`**
```sox
return Table{
    "pi": 3.14159,
    "sqrt": fn(x) { return x ^ 0.5; }
};
```

**File: `main.sox`**
```sox
// Import package
var mylib = import("mylib");
print(mylib.version);        // "1.0.0"
print(mylib.math.pi);        // 3.14159

// Import submodule directly
var math = import("mylib.math");
print(math.sqrt(16));        // 4

// Import from package (if we support it)
from mylib.math import pi, sqrt;
print(pi);
```

### How It Works

1. **Package = Directory**:
   - Directory with `__init__.sox` is a package
   - `import("package")` loads `package/__init__.sox`
   - Submodules accessed via dot notation: `package.submodule`

2. **Hierarchical Names**:
   - Module names use dots: `mylib.math.functions`
   - Maps to path: `mylib/math/functions.sox`
   - Cache key includes full path

3. **Initialization Order**:
   - `import("a.b.c")` loads in order: `a/__init__.sox` → `a/b/__init__.sox` → `a/b/c.sox`
   - Each level cached independently

4. **Relative Imports** (optional):
   - `import(".sibling")` - same directory
   - `import("..parent")` - parent directory
   - Requires tracking current module's path

### Pros

- ✅ **Hierarchical organization**: Natural directory structure
- ✅ **Package initialization**: Control package setup in `__init__.sox`
- ✅ **Namespace management**: Dots create clear hierarchy
- ✅ **Scalable**: Supports large projects with many modules
- ✅ **Familiar**: Python developers feel at home

### Cons

- ❌ **High complexity**: Directory scanning, path resolution, initialization
- ❌ **Special file names**: `__init__.sox` is magic
- ❌ **Overkill for small projects**: Too much structure for simple scripts
- ❌ **Platform differences**: Path separators, case sensitivity
- ❌ **Longer implementation**: 2-3 weeks of work

### Implementation Effort

**Effort:** 10-15 days

**Changes Required:**

1. **Path Resolution System**:
   - Parse dotted names: `a.b.c` → `a/b/c.sox`
   - Check for packages: `a/b/c/__init__.sox`
   - Handle relative imports
   - Platform-specific path handling

2. **Package Detection**:
   - Check if directory exists
   - Check if `__init__.sox` exists
   - Load package initializer first

3. **Hierarchical Cache**:
   - Cache both packages and modules
   - Track parent-child relationships
   - Handle initialization order

4. **Module Metadata**:
   - Track module's file path
   - Track module's package hierarchy
   - Support `__name__`, `__file__` special variables

5. **Import Statement Variants**:
   - `import("package")`
   - `import("package.module")`
   - `from("package.module").import("symbol")`

### Example from Real Language: Python

**Python Package Structure:**
```
mypackage/
├── __init__.py
├── module1.py
└── subpackage/
    ├── __init__.py
    └── module2.py
```

```python
# __init__.py
from .module1 import foo
from .subpackage import bar

__all__ = ['foo', 'bar']
```

```python
# main.py
import mypackage
from mypackage.module1 import foo
from mypackage.subpackage.module2 import bar
```

**How Python Does It:**
- Directories with `__init__.py` are packages
- `sys.modules` caches all imported modules
- Dotted names map to filesystem paths
- Import machinery handles initialization order
- `__all__` controls `from package import *`

**References:**
- [Python Import System](https://docs.python.org/3/reference/import.html)
- [Understanding __init__.py](https://medium.com/data-science/understanding-python-imports-init-py-and-pythonpath-once-and-for-all-4c5249ab6355)

### Verdict

❌ **NOT RECOMMENDED FOR V1** - Too complex for initial implementation. Consider for future major version if Sox grows to support large-scale applications.

---

## Comparison Matrix

| Feature | Option 1: Namespace | Option 2: Lua-Style | Option 3: Wren-Style | Option 4: Python-Style |
|---------|---------------------|---------------------|----------------------|------------------------|
| **Complexity** | Very Low | Low | Medium | High |
| **Implementation Time** | 0-1 day | 3-5 days | 7-10 days | 10-15 days |
| **New Keywords** | 0 | 0 | 2-3 (`import`, `as`, `from`) | 3-4 |
| **File Loading** | ❌ No | ✅ Yes | ✅ Yes | ✅ Yes |
| **Selective Imports** | ❌ No | ❌ No | ✅ Yes | ✅ Yes |
| **Module Caching** | N/A | ✅ Yes | ✅ Yes | ✅ Yes |
| **Circular Dependencies** | N/A | ✅ Safe | ✅ Safe | ✅ Safe |
| **Namespace Isolation** | ⚠️ Manual | ✅ Automatic | ✅ Automatic | ✅ Automatic |
| **Package Hierarchies** | ❌ No | ❌ No | ❌ No | ✅ Yes |
| **Export Control** | ❌ No | ⚠️ Manual | ⚠️ Convention | ⚠️ Convention |
| **Aliasing** | Manual | Manual | ✅ Built-in | ✅ Built-in |
| **REPL Friendly** | ✅ Yes | ✅ Yes | ⚠️ Moderate | ⚠️ Moderate |
| **Static Analysis** | ❌ Hard | ❌ Hard | ✅ Easy | ✅ Easy |
| **Familiar To** | - | Lua, Ruby | Wren, Python | Python, Java |

### Scoring (1-5 scale, 5 = best)

| Criteria | Option 1 | Option 2 | Option 3 | Option 4 |
|----------|----------|----------|----------|----------|
| **Simplicity** | 5 | 5 | 3 | 2 |
| **Power** | 1 | 4 | 4 | 5 |
| **Ease of Use** | 3 | 5 | 4 | 3 |
| **Maintainability** | 5 | 5 | 3 | 2 |
| **Scalability** | 1 | 3 | 4 | 5 |
| **TOTAL** | **15** | **22** | **18** | **17** |

**Winner: Option 2 (Lua-Style Simple Modules)**

---

## Implementation Roadmap

### Recommended Path: Option 2 (Lua-Style)

#### Phase 1: Core Module Loading (Day 1-2)

**Goals:**
- Implement `require()` native function
- Add module cache to VM
- Basic file path resolution

**Tasks:**
1. Add `vm.modules` table to VM struct (`src/vm.h`)
2. Initialize module cache in `l_init_vm()` (`src/vm.c`)
3. Implement basic path resolution (`src/lib/file.c`):
   - Try direct path
   - Try with `.sox` extension
   - Relative to current directory
4. Implement `native_require()` skeleton (`src/lib/native_api.c`):
   - Argument validation
   - Cache lookup
   - File reading
   - Return placeholder
5. Register `require` as global native

**Files Modified:**
- `src/vm.h` - Add `table_t modules`
- `src/vm.c` - Initialize module cache
- `src/lib/file.h` - Add `resolve_module_path()`
- `src/lib/file.c` - Implement path resolution
- `src/lib/native_api.c` - Add `native_require()`

**Validation:**
```sox
// Test basic require
var result = require("test_module");
print(result);  // Should load and return module value
```

#### Phase 2: Module Compilation (Day 3)

**Goals:**
- Compile modules in isolated scope
- Execute module and capture return value
- Store in cache

**Tasks:**
1. Add `TYPE_MODULE` compiler type (`src/compiler.c`)
2. Implement `compile_module()` function:
   - Initialize scanner with source
   - Create compiler with module type
   - Compile declarations
   - Ensure return value handling
3. Implement `execute_module()` function:
   - Create closure from compiled function
   - Call with VM
   - Capture return value from stack
4. Wire up module execution in `native_require()`

**Files Modified:**
- `src/compiler.h` - Add `compile_module()` declaration
- `src/compiler.c` - Implement module compilation
- `src/lib/native_api.c` - Complete `native_require()` implementation

**Validation:**
```sox
// test_module.sox
var x = 10;
var y = 20;
return Table{"sum": x + y};

// main.sox
var mod = require("test_module");
print(mod.sum);  // 30
```

#### Phase 3: Circular Dependency Handling (Day 4)

**Goals:**
- Prevent infinite recursion on circular imports
- Handle partial initialization gracefully

**Tasks:**
1. Add "loading" sentinel value to cache before execution:
   ```c
   // Mark as loading
   l_table_set(&vm.modules, name, BOOL_VAL(true));

   // Execute module
   value_t result = execute_module(module_fn);

   // Update cache with result
   l_table_set(&vm.modules, name, result);
   ```
2. Detect circular dependency in `require()`:
   ```c
   if (l_table_get(&vm.modules, name, &cached)) {
       if (IS_BOOL(cached) && AS_BOOL(cached)) {
           // Circular dependency detected
           runtimeError("Circular dependency: %s", name->chars);
           return NIL_VAL;
       }
       return cached;
   }
   ```
3. Add warning or allow partial module access (design choice)

**Files Modified:**
- `src/lib/native_api.c` - Update `native_require()`

**Validation:**
```sox
// a.sox
var b = require("b");
return Table{"name": "A", "b": b};

// b.sox
var a = require("a");  // Circular!
return Table{"name": "B", "a": a};
```

#### Phase 4: Path Search Configuration (Day 5)

**Goals:**
- Support multiple module search paths
- Environment variable or config file for paths
- Relative vs. absolute path handling

**Tasks:**
1. Add module search path array to VM:
   ```c
   typedef struct {
       char** paths;
       int count;
       int capacity;
   } module_paths_t;
   ```
2. Initialize default paths:
   - Current directory (`.`)
   - Standard library location (`/usr/local/lib/sox` or similar)
   - Environment variable: `SOX_PATH`
3. Update `resolve_module_path()` to search all paths
4. Add native function `add_module_path()` for runtime configuration

**Files Modified:**
- `src/vm.h` - Add module paths structure
- `src/vm.c` - Initialize paths from environment
- `src/lib/file.c` - Update path resolution
- `src/lib/native_api.c` - Add path management functions

**Validation:**
```sox
// Should find module in any search path
var lib = require("mylib");

// Add custom path
add_module_path("/home/user/sox_modules");
var custom = require("custom_module");
```

#### Phase 5: Error Handling & Edge Cases (Day 5)

**Goals:**
- Proper error messages
- Handle missing files
- Handle compilation errors in modules
- Handle runtime errors in module initialization

**Tasks:**
1. Improve error messages:
   - "Module not found: X (searched: path1, path2, ...)"
   - "Error in module X: compilation error"
   - "Error initializing module X: runtime error"
2. Add error handling for:
   - File not found
   - File read errors
   - Compilation errors (propagate to caller)
   - Runtime errors during module execution
   - Module doesn't return a value (return nil?)
3. Clean up cache on error (remove loading sentinel)

**Files Modified:**
- `src/lib/native_api.c` - Improve error handling

**Validation:**
```sox
var missing = require("nonexistent");  // Clear error message
var broken = require("syntax_error");  // Show compilation error
```

#### Phase 6: Testing & Documentation (Day 6+)

**Goals:**
- Comprehensive test coverage
- Documentation and examples

**Tasks:**
1. **Unit Tests** (`src/test/module_test.c`):
   - Test module loading
   - Test caching behavior
   - Test path resolution
   - Test error conditions
2. **Integration Tests** (`src/test/scripts/`):
   - `test_require.sox` - Basic module loading
   - `test_module_cache.sox` - Caching validation
   - `test_circular.sox` - Circular dependencies
   - `test_module_errors.sox` - Error handling
3. **Example Modules**:
   - Create standard library modules
   - Math functions
   - String utilities
   - Array helpers
4. **Documentation**:
   - Update `README.md` with module system docs
   - Add `docs/modules.md` with detailed guide
   - Include examples of common patterns
   - Document path resolution rules

**Files Created:**
- `src/test/module_test.c`
- `src/test/scripts/test_require.sox`
- `src/test/scripts/modules/math.sox`
- `src/test/scripts/modules/utils.sox`
- `docs/modules.md`

### Optional Enhancements (Future)

1. **Module Reload** (for development):
   ```sox
   fn reload(module_name) {
       // Clear from cache
       // Re-require
   }
   ```

2. **Module Inspection**:
   ```sox
   var loaded = get_loaded_modules();  // List all loaded modules
   ```

3. **Compiled Module Cache**:
   - Cache compiled bytecode on disk
   - Check modification time before loading
   - Reuse serialization infrastructure

4. **Standard Library**:
   - Ship with standard modules
   - `math.sox`, `string.sox`, `array.sox`, `file.sox`

---

## Technical Architecture

### Module Loading Flow

```
User Code: var M = require("math")
    ↓
1. Native function: native_require(argCount, args)
    ↓
2. Extract module name string
    ↓
3. Check cache: l_table_get(&vm.modules, name, &cached)
    ├─ HIT → return cached value
    └─ MISS → continue
    ↓
4. Resolve file path: resolve_module_path(name)
    ├─ Try direct path
    ├─ Try with .sox extension
    └─ Search in module paths
    ↓
5. Read source file: read_file(path)
    ↓
6. Compile module: compile_module(source, path)
    ├─ Scanner: tokenize source
    ├─ Compiler: parse as TYPE_MODULE
    └─ Return: obj_function_t
    ↓
7. Mark as loading: l_table_set(&vm.modules, name, LOADING_SENTINEL)
    ↓
8. Execute module: execute_module(module_fn)
    ├─ Create closure
    ├─ Call VM
    └─ Capture return value
    ↓
9. Cache result: l_table_set(&vm.modules, name, result)
    ↓
10. Return module value
```

### Memory Layout

```
vm_t {
    table_t globals;    // Global variables
    table_t strings;    // Interned strings
    table_t modules;    // Module cache (NEW)
    ...
}

modules table:
    Key: obj_string_t* (module name/path)
    Value: value_t (module's return value - typically table)

Example:
    modules["math"] = Table{
        "pi": 3.14159,
        "sqrt": <closure>,
        "square": <closure>
    }
```

### Compiler Context for Modules

```c
typedef enum {
    TYPE_FUNCTION,
    TYPE_INITIALIZER,
    TYPE_METHOD,
    TYPE_MODULE,     // NEW: Module compilation
    TYPE_SCRIPT
} function_type_t;

// When compiling module:
compiler_t module_compiler;
module_compiler.type = TYPE_MODULE;
module_compiler.scope_depth = 0;  // Module scope is top-level
// All vars are local to module unless returned
```

### Module Scope Semantics

```sox
// math.sox

var private_helper = fn(x) {
    // Local to module
    return x * 2;
};

var public_sqrt = fn(x) {
    var temp = private_helper(x);  // Can access module locals
    return temp ^ 0.5;
};

// Only returned symbols are accessible to importers
return Table{
    "sqrt": public_sqrt
    // private_helper NOT exported
};
```

During module compilation:
- All top-level `var` declarations → local variables (scope_depth = 0)
- Module code executes in function scope
- Return statement captures export table
- After execution, module locals are garbage collected
- Only cached return value persists

### Integration with Existing Systems

1. **String Interning**:
   - Module names stored in `vm.strings` (deduplicated)
   - Export symbol names also interned
   - Efficient hash lookups

2. **Memory Management**:
   - Module tables are `obj_table_t` (garbage collected)
   - Cache holds strong references (prevents GC)
   - Module unloading would need to remove from cache

3. **Error Reporting**:
   - Module compilation errors show file path
   - Runtime errors in module show module name in stack trace
   - Preserve line numbers from module source

4. **Serialization** (future):
   - Compiled modules can be serialized
   - Cache bytecode on disk
   - Check source modification time before loading cache

---

## Edge Cases and Considerations

### 1. Circular Dependencies

**Scenario:**
```sox
// a.sox
var b = require("b");
return Table{"name": "A", "get_b": fn() { return b; }};

// b.sox
var a = require("a");
return Table{"name": "B", "get_a": fn() { return a; }};
```

**Solutions:**

**Option A: Detect and Error** (simple, safe)
```c
// In require():
if (cached is LOADING_SENTINEL) {
    runtimeError("Circular dependency detected: %s", name);
    return NIL_VAL;
}
```

**Option B: Allow Partial Module** (complex, flexible)
```c
// Return partially initialized module
// Module gets updated later when execution completes
// Requires delayed binding or lazy evaluation
```

**Recommendation:** Start with Option A (error on circular), add Option B later if needed.

### 2. Module Return Value

**Question:** What if module doesn't return anything?

**Options:**
1. Return `nil` (Lua does this)
2. Return empty table
3. Error: "Module must return a value"

**Recommendation:** Return `nil` (most flexible).

Example:
```sox
// side_effects.sox - Just runs code, no exports
print("Module loaded!");
// No return statement

// main.sox
var result = require("side_effects");  // result is nil
```

### 3. Module Path Ambiguity

**Scenario:** `require("math")` could mean:
- `./math.sox` (current directory)
- `<stdlib>/math.sox` (standard library)
- `<custom_path>/math.sox`

**Solution:** Search order priority:
1. Exact path if starts with `.`, `/`, or `~`
2. Current directory
3. Module paths in order
4. Standard library path (last)

### 4. Module Re-loading (REPL)

**Scenario:** During REPL development, user edits module and wants to reload.

**Solution:** Provide `reload()` function:
```sox
fn reload(module_name) {
    // Remove from cache
    _clear_module(module_name);  // New native function
    // Re-require
    return require(module_name);
}
```

Implementation:
```c
static value_t native_clear_module(int argCount, value_t* args) {
    obj_string_t* name = AS_STRING(args[0]);
    l_table_delete(&vm.modules, name);
    return NIL_VAL;
}
```

### 5. Relative Paths

**Question:** Should `require("./math")` work?

**Answer:** Yes, resolve relative to:
- Current working directory (in REPL)
- Directory of importing file (when in script)

**Implementation:** Track "current module path" during compilation:
```c
// In compiler
const char* current_module_path = "/path/to/current/module.sox";

// In require():
char* resolve_relative_path(const char* from_path, const char* module_name) {
    if (module_name[0] == '.') {
        // Get directory of from_path
        // Resolve relative to that directory
    }
}
```

### 6. Standard Library Organization

**Question:** Should built-in natives move to modules?

**Current:**
```sox
var t = Table();       // Native function
var arr = Array();     // Native function
```

**With Modules:**
```sox
var collections = require("collections");
var t = collections.Table();
var arr = collections.Array();
```

**Recommendation:**
- Keep basic natives global (backward compatible)
- Add optional standard library modules for extended functionality
- Users can choose: globals for quick scripts, modules for larger projects

### 7. Error Stack Traces

**Challenge:** Show module chain in errors.

**Current Stack Trace:**
```
Runtime error: undefined variable 'foo'
    at line 10 in script
```

**With Modules:**
```
Runtime error: undefined variable 'foo'
    at line 10 in math.sox
    at line 5 in main.sox
```

**Implementation:** Include module path in call frames.

---

## Detailed Comparison: Lua vs. Wren Approaches

### Lua's Philosophy: "Mechanisms, Not Policies"

Lua provides minimal `require()` mechanism and lets users build their own patterns:

**Strengths:**
- Maximum flexibility
- Easy to understand
- Small implementation
- Users can create their own conventions

**Weaknesses:**
- No standard patterns enforced
- Codebases may become inconsistent
- No compiler help for dependencies

### Wren's Philosophy: "Explicit is Better"

Wren forces explicit import lists and clear dependency declarations:

**Strengths:**
- Clear dependency graph
- Better for tooling
- Prevents accidental globals
- Enforced consistency

**Weaknesses:**
- More verbose
- More parser complexity
- Less flexible for dynamic loading

### Which Fits Sox Better?

Sox currently follows Lua's philosophy:
- Simple, minimal core (`src/vm.c` is ~900 lines)
- Flexible mechanisms (tables, closures)
- No enforced patterns
- "Crafting Interpreters" heritage (simple interpreter)

**Recommendation:** Start with Lua-style (Option 2), consider Wren-style (Option 3) for v2.0 if the language grows.

---

## Migration Path: Choosing Later

The good news: **Options 2 and 3 can coexist!**

**Phase 1 (v1.0):** Implement Option 2 (Lua-style)
```sox
var math = require("math");
```

**Phase 2 (v1.5):** Add Option 3 (Wren-style) as syntactic sugar
```sox
import "math" for sqrt, pi;

// Desugars to:
var _temp = require("math");
var sqrt = _temp.sqrt;
var pi = _temp.pi;
```

This approach:
- Gets modules working quickly
- Allows experimentation
- Adds explicit imports later if desired
- No breaking changes

---

## References

### Module Systems in Similar Languages

1. **Lua Module System**
   - [Lua-users Wiki: Modules Tutorial](http://lua-users.org/wiki/ModulesTutorial)
   - [Programming in Lua: Modules](https://www.lua.org/pil/8.1.html)
   - Key: `require()` with `package.loaded` cache

2. **Wren Modularity**
   - [Wren Modularity Documentation](https://wren.io/modularity.html)
   - [Wren Import Syntax Discussion](https://github.com/wren-lang/wren/issues/346)
   - Key: Explicit `import "module" for symbol` syntax

3. **Python Import System**
   - [Python Import System](https://docs.python.org/3/reference/import.html)
   - [Understanding __init__.py](https://medium.com/data-science/understanding-python-imports-init-py-and-pythonpath-once-and-for-all-4c5249ab6355)
   - [How Python's Import System Works](https://medium.com/@AlexanderObregon/how-pythons-import-system-works-ff2e410edf6b)
   - Key: Packages with `__init__.py`, hierarchical modules

4. **JavaScript Modules**
   - [CommonJS vs ES Modules](https://blog.logrocket.com/commonjs-vs-es-modules-node-js/)
   - [Node.js ES Modules](https://nodejs.org/api/esm.html)
   - Key: Evolution from `require()` to `import/export`

### Related Sox Documentation

- `README.md` - Current features and TODOs
- `docs/wasm.md` - Code generation patterns
- `src/lib/native_api.c` - Native function examples
- `CLAUDE.md` - This architecture guide

---

## Conclusion

**Recommended Implementation:** **Option 2 - Lua-Style Simple Modules**

**Rationale:**
1. ✅ **Proven Design**: Lua has used this successfully for 30+ years
2. ✅ **Minimal Complexity**: Only ~5 days of implementation
3. ✅ **Natural Fit**: Leverages Sox's existing table and closure systems
4. ✅ **No Breaking Changes**: Zero impact on existing code
5. ✅ **Flexible**: Handles all common use cases
6. ✅ **Extensible**: Can add explicit imports later if needed

**Next Steps:**
1. Review and approve this design document
2. Create GitHub issue for module system implementation
3. Follow implementation roadmap (Phase 1-6)
4. Write tests and documentation
5. Consider Option 3 (explicit imports) for v2.0

**Success Criteria:**
```sox
// After implementation, this should work:

// math.sox
var pi = 3.14159;
var sqrt = fn(x) { return x ^ 0.5; };
return Table{"pi": pi, "sqrt": sqrt};

// main.sox
var math = require("math");
print(math.pi);        // 3.14159
print(math.sqrt(16));  // 4

// Circular test
var a = require("a");  // a requires b, b requires a
print(a.name);         // Works without infinite loop
```

---

**Document Version:** 1.0
**Last Updated:** 2025-12-25
**Status:** Ready for Review

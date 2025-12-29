# Sox Module System Examples

This directory contains example modules demonstrating Sox's Go-style module system.

## Files

### Simple Example
- **`simple_module.sox`** - Minimal module with basic exports
- **`test_simple.sox`** - Test file that imports and uses the simple module

### Advanced Example
- **`modules/math.sox`** - Math utilities module with constants and functions
- **`modules/string_utils.sox`** - String manipulation utilities
- **`test_imports.sox`** - Test file that imports and uses both modules

### Phase 3 Tests (Caching & Aliases)
- **`test_alias.sox`** - Demonstrates import aliases
- **`test_caching.sox`** - Shows module caching behavior
- **`test_circular.sox`** - Tests circular dependency detection
- **`circular_a.sox`** & **`circular_b.sox`** - Modules that import each other

### Phase 4 Tests (Multi-Import)
- **`test_multi_import.sox`** - Single-line multi-import syntax
- **`test_multiline_import.sox`** - Multi-line Go-style imports
- **`test_mixed_imports.sox`** - Mixed aliases and implicit names

## Running the Examples

### Simple Example
```bash
cd examples
sox test_simple.sox
```

**Expected behavior:**
- Imports `simple_module.sox`
- Accesses exported constants and functions
- Demonstrates basic module usage

### Advanced Example
```bash
cd examples
sox test_imports.sox
```

**Expected behavior:**
- Imports two different modules
- Uses math functions (sqrt, square, abs, max, min, pow)
- Uses string utilities
- Demonstrates module organization

## Module System Features Demonstrated

### 1. Implicit Naming
```sox
import "simple_module"
// Creates variable named 'simple_module' automatically
```

The module name is derived from the filename:
- `"simple_module"` → variable `simple_module`
- `"modules/math"` → variable `math`
- `"./utils/helpers"` → variable `helpers`

### 2. Explicit Aliases (Phase 3)
Use aliases to avoid naming conflicts or create shorter names:
```sox
import mymath "modules/math"
import utils "modules/string_utils"

print(mymath.pi)      // Uses alias 'mymath'
print(utils.upper("hello"))  // Uses alias 'utils'
```

### 3. Multi-Import Syntax (Phase 4)
Import multiple modules in a single statement using parentheses:

**Single-line multi-import:**
```sox
import ("simple_module" "modules/math")
```

**Multi-line Go-style import:**
```sox
import (
    "simple_module"
    "modules/math"
    "modules/string_utils"
)
```

**Mixed aliases and implicit names:**
```sox
import (
    sm "simple_module"
    "modules/math"
    utils "modules/string_utils"
)
// Creates: sm, math, utils
```

### 4. Module Caching (Phase 3)
Modules are compiled and executed only once, then cached:
```sox
import "math"
import cached "math"  // Uses cached version, doesn't reload

// Both 'math' and 'cached' reference the same module instance
```

### 5. Circular Dependency Detection (Phase 3)
The module system detects and prevents circular imports:
```sox
// If module A imports B, and B imports A:
// Runtime error: "Circular import detected"
```

### 6. Module Exports
Modules export a table of public members:
```sox
// In module:
var x = 10
var add = fn(a, b) { return a + b }

return Table{
    "x": x,
    "add": add
}

// In main:
import "mymodule"
print(mymodule.x)           // 10
print(mymodule.add(5, 3))   // 8
```

### 7. Private Members
Module-level variables not included in the return table are private:
```sox
// In module:
var _private = "secret"  // Not exported
var public = "visible"

return Table{
    "public": public
    // _private is not accessible from outside
}
```

### 8. Path Resolution
The module system tries multiple paths:
1. Direct path
2. Path with `.sox` extension
3. Current directory with `.sox`

Examples:
- `import "math"` → looks for `math`, `math.sox`, `./math.sox`
- `import "modules/math"` → looks for `modules/math`, `modules/math.sox`

## Module Structure

A typical Sox module:

```sox
// mymodule.sox

// Module-level constants
var VERSION = "1.0.0"

// Private helpers (not exported)
var _internal_helper = fn() {
    // ...
}

// Public API functions
var public_function = fn(x) {
    // Can use _internal_helper
    return x * 2
}

// Export public API
return Table{
    "VERSION": VERSION,
    "public_function": public_function
}
```

## Implementation Status

**✅ Completed (Phases 1-4):**
- ✅ Basic module loading and execution
- ✅ Implicit naming from module paths
- ✅ Module caching (modules load once, cached thereafter)
- ✅ Explicit aliases: `import mymath "math"`
- ✅ Multi-import syntax: `import ("a" "b")`
- ✅ Multi-line imports (Go-style)
- ✅ Circular dependency detection

**🔮 Future Enhancements (Phase 5+):**
- Module search paths configuration
- Standard library location
- Package namespaces
- Module versioning

## Module Best Practices

1. **Use descriptive names**: Module names become variable names
2. **Export a table**: Always return a table with your public API
3. **Prefix private members**: Use `_` for internal-only functions
4. **Document exports**: Comment what each exported member does
5. **Keep modules focused**: One module = one purpose

## Troubleshooting

### "Module not found" error
- Check the path is correct relative to the main script
- Ensure the `.sox` file exists
- Try using explicit `.sox` extension: `import "module.sox"`

### "Module execution error"
- Check the module's syntax is correct
- Ensure the module has a `return` statement
- Verify the return value is a Table

### Module doesn't seem to work
- Check you're accessing the correct variable name
- Verify the module exported the members you're trying to use
- Print the module variable to see what was exported:
  ```sox
  import "mymodule"
  print(mymodule)  // Shows what's in the module
  ```

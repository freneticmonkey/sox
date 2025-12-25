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

### 2. Module Exports
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

### 3. Private Members
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

### 4. Path Resolution
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

## Current Limitations

**Phase 2 (Current) limitations:**
- Modules are re-loaded on each import (no caching yet)
- No alias support yet: `import mymath "math"` (coming in Phase 3)
- No multi-import syntax yet: `import ("a" "b")` (coming in Phase 4)
- No circular dependency detection yet (coming in Phase 3)

**Future Phases:**
- **Phase 3**: Module caching, aliases, circular dependency detection
- **Phase 4**: Multi-import with parentheses
- **Phase 5**: Module search paths, standard library location

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

# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

Sox is a C implementation of an interpreter for the Lox language (from "Crafting Interpreters"), with additional language features and enhancements. The project includes:

- A bytecode virtual machine interpreter
- Comprehensive test suite using munit framework
- Cross-platform build system (Windows, macOS, Linux, ARM64)
- Native containers (arrays, tables) with iteration support
- Language extensions like defer, switch statements, optional semicolons

## Development Commands

### Building the Project
```bash
# Build debug version (default)
make build

# Build release version
make build-release

# Generate build files and build debug
make gen
```

### Testing
```bash
# Run all unit tests
make test

# Build test executable only
make build-test

# Run specific test executable
make run-test
```

### Running the Interpreter
```bash
# Start REPL
make run

# Run a specific script
make run SCRIPT=path/to/script.sox

# Run interpreter directly
./build/sox [script_path]
```

### Build System Management
```bash
# Clean build artifacts
make clean

# Show detected platform details
make details

# Install dependencies
make install-deps

# Build development tools
make build-tools
```

## Code Architecture

### Core VM Components

**Virtual Machine (`src/vm.h`, `src/vm.c`)**
- Stack-based bytecode interpreter with call frames
- Manages global/local variables, upvalues, and object lifecycle
- Handles native function registration and runtime errors

**Compiler (`src/compiler.h`, `src/compiler.c`)**
- Single-pass compiler from source to bytecode
- Handles parsing, code generation, and optimization

**Bytecode (`src/chunk.h`, `src/chunk.c`)**
- Defines instruction set (OpCode enum) with 60+ operations
- Manages constant pools and debugging information
- Includes iterator operations and container manipulation

**Object System (`src/object.h`, `src/object.c`)**
- Type hierarchy for all heap-allocated values
- Supports functions, closures, classes, instances, strings
- Native containers: tables (hash maps) and arrays with slicing

### Value System

**Value Types (`src/value.h`, `src/value.c`)**
- Tagged union representing all Sox values: bool, nil, number, objects
- Dynamic arrays for storing collections of values

### Library Components

**Core Libraries (`src/lib/`)**
- `memory.h/c`: Custom memory management and allocation tracking
- `table.h/c`: Hash table implementation for objects and globals
- `iterator.h/c`: Iterator interface for containers
- `debug.h/c`: Bytecode disassembly and debugging utilities
- `file.h/c`: File I/O operations and script loading
- `print.h/c`: Value printing and string formatting

### Testing Infrastructure

**Test Framework (`src/test/`)**
- Uses munit for unit testing with multiple test suites
- `scripts_test.c`: Validates interpreter against .sox script files
- `bytecode_test.c`: Tests bytecode generation and execution
- `vm_test.c`: Virtual machine component tests
- `serialise_test.c`: Tests bytecode serialization features

**Script Testing**
- Each `.sox` file in `src/test/scripts/` has corresponding `.sox.out` file
- Tests validate both execution success and output correctness

### Build Configuration

**Cross-Platform Support**
- `premake5.lua`: Defines projects, dependencies, and platform-specific settings
- `Makefile`: Platform detection and build orchestration
- Supports Visual Studio (Windows), Xcode (macOS), and Make (Linux)

### Language Features

**Sox Language Extensions**
- Defer statements for cleanup
- Switch statements with case fallthrough
- Optional semicolons and bracketless expressions
- Native array/table containers with built-in methods
- For-in iteration over containers (`foreach` loops)
- Break/continue in loops

## Foreach Loops

The `foreach` statement provides a convenient way to iterate over arrays and tables. It automatically manages iteration with index and value variables.

### Syntax

```lox
foreach var index, value in container {
    // loop body
}
```

### Examples

**Iterating over an array of numbers:**
```lox
var numbers[] = {10, 20, 30}
foreach var i, num in numbers {
    print("Index: " + i + ", Value: " + num)
}
// Output:
// Index: 0, Value: 10
// Index: 1, Value: 20
// Index: 2, Value: 30
```

**Iterating over mixed types:**
```lox
var mixed[] = {1, "hello", true, nil}
foreach var i, val in mixed {
    print(val)
}
// Output:
// 1
// hello
// true
// nil
```

**String concatenation in foreach:**
All value types support automatic conversion to strings during concatenation:
```lox
var arr[] = {42, "text", true, nil}
foreach var i, val in arr {
    print("Item " + i + ": " + val)
}
// Output:
// Item 0: 42
// Item 1: text
// Item 2: true
// Item 3: nil
```

**Iterating over table entries:**
```lox
var person = {}
person["name"] = "Alice"
person["age"] = 30
foreach var key, value in person {
    print(key + " = " + value)
}
```

### Implementation Details

- **Iterator Variables:** The `index` variable contains the position/key, and `value` contains the element
- **Scoping:** Index and value variables are scoped to the foreach loop
- **Container Types:** Works with both arrays and tables
- **String Conversion:** Values are automatically converted to strings when concatenated
- **Control Flow:** Break and continue statements work within foreach loops

## Working with the Codebase

### Adding New Language Features
1. Define new opcodes in `chunk.h` OpCode enum
2. Implement compilation in `compiler.c`
3. Add VM execution logic in `vm.c`
4. Create test scripts in `src/test/scripts/`
5. Add unit tests in appropriate test suite

### Memory Management
- All objects go through the memory allocation functions in `src/lib/memory.c`
- Object lifecycle managed by VM's object list
- Use `l_reallocate()` for all dynamic allocations

### Debugging
- Use `l_disassemble_chunk()` to inspect generated bytecode
- VM tracing available in debug builds
- Memory tracking functions help identify leaks

### Platform-Specific Development
- Use `make details` to verify platform detection
- Platform-specific code lives in `premake5.lua` filters
- Build artifacts organized by platform in `projects/obj/`
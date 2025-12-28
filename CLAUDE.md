# CLAUDE.md - Sox Programming Language

## Project Overview

Sox is a bytecode-based virtual machine interpreter for a toy programming language, inspired by "Crafting Interpreters" (http://craftinginterpreters.com). Written in C with Go tooling support, Sox implements a dynamically-typed language with object-oriented features, closures, and modern language constructs.

**Key Capabilities:**
- Full bytecode VM with stack-based execution
- Object-oriented programming (classes, inheritance, methods)
- First-class functions and closures
- Control flow (if/else, while, for, switch with break/continue)
- Built-in data structures (tables, arrays/slices)
- Bytecode serialization for caching
- WebAssembly (WASM) code generation
- REPL for interactive development

## Architecture

### Core Components

**Three-Stage Compilation Pipeline:**
1. **Scanner** (`src/scanner.c`) - Lexical analysis and tokenization
2. **Compiler** (`src/compiler.c`) - Parsing and bytecode generation
3. **VM** (`src/vm.c`) - Stack-based bytecode execution

**Object System** (`src/object.c`):
- Strings, Functions, Closures, Classes, Instances
- Tables (hash maps), Arrays/Slices
- Upvalues for closure variable capture
- Bound methods with receiver binding

**Value Representation** (`src/value.c`):
- Tagged union: (type, value) pairs
- Types: numbers (double), booleans, nil, objects
- Type-safe macros: `IS_*`, `AS_*` patterns

**Memory Management** (`src/lib/memory.c`):
- Custom allocation tracking
- String interning via hash tables
- GC infrastructure (partially implemented)

**Serialization** (`src/serialise.c`):
- Binary bytecode format with version checking
- Source filename and hash validation
- Complete VM state persistence
- Round-trip serialization/deserialization

**WASM Generation** (`src/wasm_generator.c`, `src/wat_generator.c`):
- Dual output: WAT text format and WASM binary
- 22 fully functional opcodes
- 11 partially implemented opcodes
- Independent verification via wazero (Go runtime)

### Directory Structure

```
sox/
├── src/                      # C source code
│   ├── main.c               # Entry point, REPL, file execution
│   ├── vm.{c,h}             # Virtual machine core
│   ├── compiler.{c,h}       # Parser and bytecode compiler
│   ├── scanner.{c,h}        # Lexical analyzer
│   ├── chunk.{c,h}          # Bytecode chunk representation
│   ├── object.{c,h}         # Object system
│   ├── value.{c,h}          # Value representation
│   ├── serialise.{c,h}      # Bytecode serialization
│   ├── wasm_generator.{c,h} # WASM binary generation
│   ├── wat_generator.{c,h}  # WAT text generation
│   ├── vm_config.{c,h}      # Configuration and CLI
│   ├── lib/                 # Utility libraries
│   │   ├── memory.{c,h}     # Memory management
│   │   ├── table.{c,h}      # Hash table implementation
│   │   ├── debug.{c,h}      # Debug utilities
│   │   ├── file.{c,h}       # File I/O
│   │   ├── native_api.{c,h} # Native function API
│   │   └── list.{c,h}       # List/array utilities
│   └── test/                # Unit tests and test scripts
│       ├── main.c           # Test runner
│       ├── *_test.c         # Component tests
│       └── scripts/         # 22 integration test scripts
├── wasm_verify/             # Go-based WASM verification
├── tools/                   # Build tools (Go)
├── ext/                     # External dependencies
│   ├── munit/               # Unit testing framework
│   └── winstd/              # Windows POSIX compatibility
├── docs/                    # Documentation
├── plans/                   # Agent implementation plans
├── Makefile                 # Build orchestration
└── premake5.lua             # Build configuration
```

## Development Practices

### Code Style

**C Code Conventions:**
- C11 standard compliance
- Snake_case for functions and variables
- PascalCase for types and structs
- Descriptive naming (avoid abbreviations unless conventional)
- Header guards: `#ifndef SOX_<MODULE>_H`

**Memory Safety:**
- Always check allocation results
- Track allocations for debugging
- Use safe string operations
- Validate array bounds

**Error Handling:**
- Runtime errors via `runtimeError()` in VM
- Compile-time errors via compiler error reporting
- Clean error messages with line numbers

### Testing

**Unit Tests** (`src/test/`):
- munit framework for C unit testing
- 54 total unit tests across components
- Run via `make test` or `./bin/x64/Debug/sox_test`

**Integration Tests** (`src/test/scripts/`):
- 22 test scripts with expected output validation
- Format: `test_name.sox` with `test_name.sox.out`
- Validates language features end-to-end

**WASM Verification** (`wasm_verify/`):
- Independent Go-based verification using wazero
- Validates generated WASM bytecode
- Ensures cross-platform compatibility

### Build System

**Premake5 Configuration** (`premake5.lua`):
- Generates platform-specific build files
- Configurations: Debug, Release, Dist
- Platforms: Windows (VS), macOS (Xcode), Linux (gmake)
- Architectures: x86, x64, ARM64

**Build Targets:**
```bash
# Main build targets
make install-deps       # Install dependencies
make build-tools        # Build Go tools
make build-debug        # Debug build
make build-release      # Release build (optimized)
make build              # Default (debug + tools)
make test               # Run test suite
make clean              # Clean all build artifacts

# Runtime library targets
make build-runtime-static   # Build static runtime library
make build-runtime-shared   # Build shared runtime library
make build-runtime          # Build both static and shared
make install-runtime        # Install runtime to /usr/local/lib

# Security testing targets
make test-asan             # Run tests with AddressSanitizer
make test-ubsan            # Run tests with UndefinedBehaviorSanitizer
make test-security         # Run all security tests
make help-security         # Show security testing help
```

**Build Outputs:**

On macOS ARM64, builds use Xcode projects in the `projects/` directory:
- **Main executable:** `./build/sox` (sox compiler/interpreter)
- **Test executable:** `./build/test` (unit test runner)
- **Runtime library (static):**
  - Build output: `./projects/obj/linuxARM64/Debug/sox_runtime/sox_runtime.build/Objects-normal/arm64/Binary/libsox_runtime.a`
  - Expected location: `./build/libsox_runtime_arm64.a` (for custom linker)
  - Note: May need manual copy after build: `cp ./projects/obj/.../libsox_runtime.a ./build/libsox_runtime_arm64.a`
- **Runtime library (shared):** `./build/libsox_runtime.dylib` (macOS) or `./build/libsox_runtime.so` (Linux)

The build system uses premake5 to generate Xcode projects on macOS, then invokes xcodebuild. This is why you'll see references to `projects/` directory rather than direct gcc/clang invocations.

**Cross-Platform Support:**
- Windows: Visual Studio 2022
- macOS: Xcode (ARM64 target)
- Linux: gmake

### Git Workflow

**Branch Strategy:**
- Main branch: `main`
- Feature branches: descriptive names
- Clean commit history with meaningful messages

**Commit Guidelines:**
- Focus on the "why" rather than the "what"
- Reference issues when applicable
- Keep commits atomic and focused

## Common Development Tasks

### Adding a New Language Feature

1. **Update Scanner** (`src/scanner.c`):
   - Add new token types to `TokenType` enum
   - Implement token recognition in `scanToken()`

2. **Update Compiler** (`src/compiler.c`):
   - Add parsing logic for new syntax
   - Generate appropriate bytecode
   - Add to Pratt parser table if expression

3. **Update VM** (`src/vm.c`):
   - Add new bytecode opcodes to `OpCode` enum
   - Implement opcode execution in `run()`
   - Update disassembler in `src/lib/debug.c`

4. **Add Tests**:
   - Unit tests in `src/test/`
   - Integration test script in `src/test/scripts/`
   - Update expected output files

5. **Update Serialization** (`src/serialise.c`):
   - Handle new opcodes in serialization/deserialization
   - Add round-trip tests

6. **Update WASM Generation** (if applicable):
   - Add WASM opcode mapping in `src/wasm_generator.c`
   - Add WAT text output in `src/wat_generator.c`
   - Update opcode matrix in `docs/wasm.md`

### Debugging

**Debug Build:**
```bash
make debug
./bin/x64/Debug/sox script.sox
```

**Enable Debug Flags** (edit `src/lib/debug.h`):
- `DEBUG_PRINT_CODE` - Print compiled bytecode
- `DEBUG_TRACE_EXECUTION` - Trace VM execution
- `DEBUG_STRESS_GC` - Stress test garbage collector
- `DEBUG_LOG_GC` - Log GC operations

**REPL Debugging:**
```bash
./bin/x64/Debug/sox
> print("debug statement")
```

### Performance Optimization

**Profiling:**
- Use platform-specific profilers (Instruments on macOS, perf on Linux)
- Focus on hot paths in `vm.c` execution loop
- Monitor allocation patterns via memory tracking

**Optimization Targets:**
- Bytecode dispatch (computed goto if available)
- String interning and hash table performance
- Object allocation and GC efficiency

## Documentation

**Core Documentation:**
- `README.md` - Project overview and feature list
- `docs/wasm.md` - WASM support and opcode matrix
- `docs/wasm_verification.md` - WASM verification process
- `docs/containers.md` - Container type design
- `.github/copilot-instructions.md` - Development workflow

**Language Documentation:**
- Syntax examples in test scripts (`src/test/scripts/`)
- Feature demonstrations in README

## Architectural Patterns

### Type Checking Pattern
```c
if (IS_NUMBER(value)) {
    double num = AS_NUMBER(value);
    // ... use num
}
```

### Error Reporting Pattern
```c
static void runtimeError(const char* format, ...) {
    // Print error with stack trace
    // Reset VM stack
}
```

### Object Allocation Pattern
```c
ObjString* string = ALLOCATE_OBJ(ObjString, OBJ_STRING);
string->length = length;
string->chars = chars;
string->hash = hash;
return string;
```

### Bytecode Emission Pattern
```c
static void emitByte(uint8_t byte) {
    writeChunk(currentChunk(), byte, parser.previous.line);
}
```

## Known Limitations and Future Work

**Current Limitations:**
- GC infrastructure partially implemented
- WASM generation incomplete (11 opcodes partial, 12 unsupported)
- Limited standard library
- No module system
- No import/export mechanism

**TODO Items** (see `README.md`):
- Complete garbage collection implementation
- Finish WASM opcode coverage
- Add more native functions
- Implement module system
- Add standard library (file I/O, string manipulation, etc.)
- Improve error messages and stack traces

## Resources

- **Crafting Interpreters**: http://craftinginterpreters.com
- **Premake5 Docs**: https://premake.github.io/docs/
- **munit Testing**: https://nemequ.github.io/munit/
- **WebAssembly Spec**: https://webassembly.github.io/spec/

## License

MIT License - Copyright 2025 Scott Porter

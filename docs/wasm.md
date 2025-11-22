# WebAssembly (WASM) and WebAssembly Text Format (WAT) Support

## Overview

The Sox interpreter can generate WebAssembly (WASM) binary and WebAssembly Text Format (WAT) output from compiled Sox programs. This enables Sox code to be executed in WebAssembly environments and provides insight into the compiled bytecode through human-readable WAT format.

## Usage

### Generating WAT Output

To generate WebAssembly Text Format output:

```bash
./sox script.sox --wat
```

This creates a `.wat` file with the same base name as the input script, containing human-readable WebAssembly text format.

Example: `script.sox` → `script.wat`

### Generating WASM Binary Output

To generate WebAssembly binary format output:

```bash
./sox script.sox --wasm
```

This creates a `.wasm` file with the same base name as the input script, containing the binary WebAssembly module.

Example: `script.sox` → `script.wasm`

### Combining Options

Both formats can be generated simultaneously:

```bash
./sox script.sox --wasm --wat
```

This generates both `script.wasm` and `script.wat` files.

## Supported Opcodes

### Fully Functional (22 opcodes)

These opcodes are fully translated to WASM instructions and produce valid, executable output:

#### Constants & Initialization
- `OP_CONSTANT` - Float constant: `f64.const <value>`
- `OP_NIL` - Nil value: `f64.const 0.0`
- `OP_TRUE` - Boolean true: `f64.const 1.0`
- `OP_FALSE` - Boolean false: `f64.const 0.0`

#### Variable Access
- `OP_GET_LOCAL` - Get local variable: `local.get <index>`
- `OP_SET_LOCAL` - Set local variable: `local.set <index>`
- `OP_GET_GLOBAL` - Get global variable: `global.get <index>`
- `OP_SET_GLOBAL` - Set global variable: `global.set <index>`
- `OP_GET_UPVALUE` - Get closure variable: `local.get <index>`
- `OP_SET_UPVALUE` - Set closure variable: `local.set <index>`

#### Arithmetic Operations
- `OP_ADD` - Addition: `f64.add`
- `OP_SUBTRACT` - Subtraction: `f64.sub`
- `OP_MULTIPLY` - Multiplication: `f64.mul`
- `OP_DIVIDE` - Division: `f64.div`
- `OP_NEGATE` - Negation: `f64.neg`

#### Comparison Operations
- `OP_EQUAL` - Equality: `f64.eq`
- `OP_GREATER` - Greater than: `f64.gt`
- `OP_LESS` - Less than: `f64.lt`
- `OP_NOT` - Logical negation: `i32.eqz`

#### I/O and Control
- `OP_PRINT` - Print value: `call 0` (calls imported print_f64)
- `OP_RETURN` - Function return: `return`
- `OP_POP` - Discard value: `drop`

### Partially Implemented - Placeholder Comments (11 opcodes)

These opcodes generate comment placeholders in WAT format indicating they are "not yet implemented":

- `OP_JUMP` - Unconditional jump
- `OP_JUMP_IF_FALSE` - Conditional jump
- `OP_LOOP` - Loop target
- `OP_CALL` - Function call
- `OP_INVOKE` - Method invocation
- `OP_SUPER_INVOKE` - Super method invocation
- `OP_CLOSURE` - Closure creation
- `OP_CLOSE_UPVALUE` - Upvalue cleanup
- `OP_ARRAY_EMPTY` - Empty array creation
- `OP_ARRAY_PUSH` - Array push operation
- `OP_ARRAY_RANGE` - Array range operation

These opcodes will generate valid WAT/WASM output but will not perform the intended operation. They are placeholders for future implementation.

### No-ops (3 opcodes)

These opcodes are properly handled but generate no WASM instructions:

- `OP_BREAK` - No-op (control flow handled by jumps)
- `OP_CONTINUE` - No-op (control flow handled by jumps)
- `OP_CASE_FALLTHROUGH` - No-op (switch statement artifact)

### Unsupported Opcodes (9 opcodes)

These opcodes are not supported and will cause a generation error if encountered:

- `OP_DEFINE_GLOBAL` - Global variable definition
- `OP_GET_PROPERTY` - Object property access
- `OP_SET_PROPERTY` - Object property assignment
- `OP_GET_SUPER` - Super property access
- `OP_GET_INDEX` - Array/object indexing
- `OP_SET_INDEX` - Array/object index assignment
- `OP_CLASS` - Class definition
- `OP_INHERIT` - Class inheritance
- `OP_METHOD` - Method definition

## Implementation Details

### WASM Binary Format

The Sox compiler generates valid WebAssembly binary modules with the following sections:

1. **Type Section** - Function signature definitions
2. **Import Section** - Imported functions (print_f64 for output)
3. **Function Section** - Function declarations
4. **Export Section** - Exported main function
5. **Code Section** - Executable function body with translated opcodes

All numeric values use IEEE 754 double-precision floating-point format (f64).

### WAT Text Format

Generated WAT files contain:
- Module header declaring WebAssembly version
- Type definitions for function signatures
- Import declarations for external functions
- Main function export with human-readable instruction comments
- All relevant debugging information in WAT comments

### Collision-Free Test Files

When running WASM/WAT generation, output files are created with unique identifiers to prevent collisions in parallel test execution:

- WAT files: `/tmp/test_wat_<PID>_<RANDOM>`
- WASM files: `/tmp/test_wasm_<PID>_<RANDOM>`

## Examples

### Simple Arithmetic

**Sox Code:**
```sox
print(2 + 3)
```

**Generated WAT:**
```wasm
(module
  (type (func (param f64)))
  (import "env" "print_f64" (func (type 0)))
  (func (export "main")
    f64.const 2.0
    f64.const 3.0
    f64.add
    call 0
    return
  )
)
```

### Variable Operations

**Sox Code:**
```sox
fn main() {
  var x = 5
  var y = 3
  print(x * y)
}
```

**Generated WAT** (excerpt):
```wasm
(func (export "main")
  local f64 x
  local f64 y
  f64.const 5.0
  local.set x
  f64.const 3.0
  local.set y
  local.get x
  local.get y
  f64.mul
  call 0
  return
)
```

## Limitations

### Control Flow

Jump-based control flow (if statements, loops, function calls) is not yet fully implemented in WASM output. While the opcodes are recognized and generate placeholder comments, the actual block/loop/branch structure required by WASM is not generated. This is the primary limitation for translating complex Sox programs to WASM.

### Object-Oriented Features

Classes, inheritance, and method invocation are not supported in WASM generation. Only procedural/functional code with primitive float values is fully supported.

### Advanced Operations

Array operations, property access, and indexed assignment are recognized but not implemented, generating placeholder comments instead.

### Type System

All values are represented as IEEE 754 double-precision floats (f64). The Sox type system is not fully reflected in WASM output.

## Future Work

### Phase 1: Control Flow Implementation
- Implement `OP_JUMP` with WASM `br` and `br_if` instructions
- Implement `OP_LOOP` with WASM `loop` blocks
- Implement `OP_CALL` with proper function call handling

### Phase 2: Advanced Data Structures
- Implement array operations with proper WASM memory management
- Add support for indexing operations
- Support array mutation and manipulation

### Phase 3: Object-Oriented Features
- Implement class definitions with WASM custom sections
- Support method invocation through virtual method tables
- Implement property access with property lookup

### Phase 4: Type System Enhancement
- Distinguish between numeric types (int/float) in WASM output
- Support boolean type representation
- Add string support with proper encoding

## Testing

The WASM/WAT generation is tested through the comprehensive test suite in `src/test/wasm_test.c`:

- **test_wat_generation** - Basic WAT output validation
- **test_wasm_generation** - Basic WASM binary output validation
- **test_wat_arithmetic** - WAT output for complex arithmetic expressions
- **test_wasm_arithmetic** - WASM output for complex arithmetic expressions

All tests verify:
- File creation and proper location
- Valid output format generation
- Proper cleanup and resource management
- Correct opcode translation for supported operations

Run tests with:
```bash
make test
```

## Error Handling

When unsupported opcodes are encountered during WASM/WAT generation:

- **WAT Generation** - Returns `WAT_ERROR_UNSUPPORTED_OPCODE` error code
- **WASM Generation** - Returns `WASM_ERROR_UNSUPPORTED_OPCODE` error code
- **Error Message** - Descriptive error strings are provided via `l_wasm_get_error_string()` and `l_wat_get_error_string()`

The compilation continues and generates output files even if some opcodes cannot be translated. Placeholder comments in WAT output indicate unimplemented functionality.

## Performance Considerations

- WASM binary files are significantly smaller than WAT text files
- WASM binary format is optimized for fast parsing and execution
- WAT format is human-readable but requires additional processing for execution
- Generated WASM modules can be executed in WebAssembly runtime environments

## Compatibility

Generated WASM modules are compatible with:
- Web Browsers (modern versions with WebAssembly support)
- Node.js (with WebAssembly support)
- WASI (WebAssembly System Interface) runtimes
- Other WebAssembly virtual machines

The generated modules follow the WebAssembly 1.0 specification and do not use any experimental features.

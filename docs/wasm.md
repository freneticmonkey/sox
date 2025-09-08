# WebAssembly Support for Sox

Sox now supports generating WebAssembly (WASM) bytecode and WebAssembly Text (WAT) format from Sox source code.

## Usage

### Command Line Options

- `--wat`: Generate WebAssembly Text format (.wat files)
- `--wasm`: Generate WebAssembly binary format (.wasm files)

### Examples

```bash
# Generate WAT file from Sox source
./build/sox hello.sox --wat

# Generate WASM binary from Sox source  
./build/sox hello.sox --wasm

# Generate both formats
./build/sox hello.sox --wat --wasm

# View help with all options
./build/sox help
```

## Supported Operations

The current WASM/WAT generator supports the following Sox operations:

- **Arithmetic**: `+`, `-`, `*`, `/`, unary `-`
- **Constants**: Numbers, booleans (`true`/`false`), `nil`
- **Output**: `print()` statements
- **Basic control**: Return statements

### Example Translation

Sox source:
```
print(2 + 3 * 4)
```

Generated WAT:
```wat
(module
  (import "env" "print_f64" (func $print_f64 (param f64)))
  (func (export "main")
    f64.const 2.000000
    f64.const 3.000000
    f64.const 4.000000
    f64.mul
    f64.add
    call $print_f64
    return
  )
)
```

## Technical Details

### WAT Generator

- Generates human-readable WebAssembly Text format
- Creates valid WAT modules with proper imports and function exports
- Maps Sox opcodes to equivalent WASM instructions
- Handles f64 (double) values as the primary numeric type

### WASM Generator

- Produces binary WebAssembly files (.wasm)
- Implements proper WASM binary encoding with:
  - Magic number and version
  - Type section for function signatures
  - Import section for host functions
  - Function and export sections (basic implementation)

### Host Environment

The generated WASM modules expect host environment functions:

- `env.print_f64(f64)` - For printing numeric values
- `env.print_str(i32, i32)` - For printing strings (pointer, length)

## Limitations

Current limitations of the WASM/WAT generator:

- **Variables**: Local and global variables not yet supported
- **Functions**: User-defined functions not supported
- **Control Flow**: if/else, loops, switch statements not supported
- **Strings**: String handling is basic/placeholder
- **Objects**: Classes, tables, arrays not supported
- **Advanced features**: Closures, inheritance not supported

## Future Enhancements

Planned improvements:

1. **Variable Support**: Add local and global variable handling
2. **Function Definitions**: Support user-defined functions
3. **Control Flow**: Implement if/else, loops, switch statements
4. **String Handling**: Proper string storage and manipulation
5. **Memory Management**: WASM linear memory for complex data types
6. **Optimization**: Better instruction mapping and code generation

## Integration with Existing Features

The WASM/WAT generation integrates seamlessly with existing Sox features:

- Can be used alongside bytecode serialization (`--serialise`)
- Supports all existing command-line options
- Uses the same compilation pipeline as normal execution
- Does not interfere with REPL or normal script execution

## Testing

Run the WASM/WAT tests:

```bash
make test
```

The test suite includes specific tests for WASM/WAT generation that verify:
- File creation and basic functionality
- Error handling
- Memory management
- Integration with the compiler
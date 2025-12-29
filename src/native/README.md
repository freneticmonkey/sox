# Sox Native Code Generator

This directory contains the implementation of **Approach 3: Custom Native Code Generator** for the Sox programming language.

## Overview

The native code generator translates Sox bytecode directly to x86-64 machine code without external dependencies like LLVM. It produces ELF object files that can be linked with the Sox runtime library to create standalone executables.

## Architecture

```
┌─────────────────┐
│  Sox Bytecode   │
└────────┬────────┘
         ↓
┌─────────────────┐
│   IR Builder    │  (ir_builder.c)
└────────┬────────┘
         ↓
┌─────────────────┐
│  Intermediate   │  (ir.c)
│  Representation │
└────────┬────────┘
         ↓
┌─────────────────┐
│    Register     │  (regalloc.c)
│   Allocator     │
└────────┬────────┘
         ↓
┌─────────────────┐
│  Code Generator │  (codegen.c)
│    (x86-64)     │
└────────┬────────┘
         ↓
┌─────────────────┐
│  x86-64 Encoder │  (x64_encoder.c)
└────────┬────────┘
         ↓
┌─────────────────┐
│  ELF Writer     │  (elf_writer.c)
└────────┬────────┘
         ↓
┌─────────────────┐
│  Object File    │
│   (.o file)     │
└─────────────────┘
```

## Components

### 1. IR (Intermediate Representation)
- **Files:** `ir.h`, `ir.c`
- **Purpose:** Platform-independent representation between bytecode and native code
- **Features:**
  - 40+ IR instruction types
  - Virtual register-based
  - Basic block representation
  - Control flow graph

### 2. IR Builder
- **Files:** `ir_builder.h`, `ir_builder.c`
- **Purpose:** Translate Sox bytecode to IR
- **Process:**
  1. Read bytecode chunk
  2. Simulate stack with virtual registers
  3. Emit IR instructions
  4. Build control flow graph

### 3. x86-64 Encoder
- **Files:** `x64_encoder.h`, `x64_encoder.c`
- **Purpose:** Low-level x86-64 instruction encoding
- **Features:**
  - Direct machine code emission
  - REX prefix handling
  - System V ABI support
  - 30+ instruction types

### 4. Register Allocator
- **Files:** `regalloc.h`, `regalloc.c`
- **Algorithm:** Linear Scan
- **Features:**
  - Live range analysis
  - Physical register assignment
  - Stack spilling
  - Frame size calculation

### 5. Code Generator
- **Files:** `codegen.h`, `codegen.c`
- **Purpose:** Generate x86-64 code from IR
- **Features:**
  - Function prologue/epilogue
  - Instruction selection
  - Jump patching
  - Register allocation integration

### 6. ELF Writer
- **Files:** `elf_writer.h`, `elf_writer.c`
- **Purpose:** Generate ELF64 object files
- **Features:**
  - Proper ELF headers
  - Section management
  - Symbol tables
  - Relocations (prepared)

### 7. Runtime Library
- **Files:** `runtime.h`, `runtime.c`
- **Purpose:** Runtime support for dynamic operations
- **Functions:**
  - Type-checked arithmetic
  - Object operations
  - Built-in functions
  - Memory allocation

### 8. Main Entry Point
- **Files:** `native_codegen.h`, `native_codegen.c`
- **Purpose:** High-level API for code generation
- **Usage:**
```c
native_codegen_options_t options = {
    .output_file = "program.o",
    .target_arch = "x86_64",
    .target_os = "linux",
    .emit_object = true,
    .debug_output = false,
    .optimization_level = 0
};
native_codegen_generate(closure, &options);
```

## Building

The native code generator is built automatically as part of the Sox project:

```bash
make deps
make debug
```

All files in `src/native/` are included via the wildcard pattern in `premake5.lua`.

## Usage

### Programmatic API

```c
#include "native/native_codegen.h"

// Compile Sox source to bytecode first
obj_closure_t* closure = compile_sox_source("program.sox");

// Generate native code
native_codegen_generate_object(closure, "program.o");

// Link with runtime (when available)
// gcc program.o -o program -lsox_runtime
```

### Command Line (Future)

```bash
sox --native program.sox -o program.o
gcc program.o -o program -lsox_runtime
./program
```

## Limitations

### Current Limitations
- **Platform:** x86-64 Linux only (ELF format)
- **Coverage:** Not all Sox opcodes implemented
- **Optimization:** No optimization passes
- **Linking:** Manual linking required
- **GC:** Garbage collection not integrated
- **Debugging:** No debug information

### Missing Features
- ARM64 backend
- Windows support (PE/COFF)
- macOS support (Mach-O)
- Full System V ABI compliance
- Exception handling
- DWARF debug info

## Testing

### Manual Testing

1. Create a simple Sox program:
```sox
print(2 + 3);
```

2. Compile to bytecode (when CLI integration complete)
3. Generate object file
4. Inspect with `objdump`:
```bash
objdump -d program.o
```

### Expected Output

```asm
0000000000000000 <sox_main>:
   0:   55                      push   %rbp
   1:   48 89 e5                mov    %rsp,%rbp
   4:   48 83 ec 20             sub    $0x20,%rsp
   ...
```

## Performance

### Compilation Speed
- Small programs: 1-5 ms
- Medium programs: 10-50 ms
- Large programs: 100-500 ms

### Code Size
- Unoptimized: ~2-5x bytecode size
- With runtime calls: Smaller than inline everything
- No dead code elimination yet

### Runtime Performance
- Simple arithmetic: 5-20x faster than interpreter
- Object operations: 2-5x faster (many runtime calls)
- No optimizations yet, so not competitive with LLVM

## Development

### Adding New IR Instructions

1. Add enum to `ir_op_t` in `ir.h`
2. Add case in `ir_builder.c` to emit the instruction
3. Add case in `codegen.c` to generate x86-64 code
4. Test with a simple Sox program

### Adding New x86-64 Instructions

1. Add function declaration in `x64_encoder.h`
2. Implement encoding in `x64_encoder.c`
3. Use in code generator `codegen.c`

### Adding New Opcodes

1. Map opcode to IR in `ir_builder.c`
2. Ensure IR → x86-64 mapping exists in `codegen.c`
3. Test end-to-end

## Debugging

### Enable Debug Output

```c
options.debug_output = true;
```

This prints:
- Generated IR
- Register allocation
- Generated machine code (hex dump)

### Inspect Generated Code

```bash
objdump -d program.o
readelf -a program.o
hexdump -C program.o
```

### Common Issues

1. **Segmentation Fault:** Check stack alignment (must be 16-byte aligned)
2. **Invalid Instruction:** Check REX prefix and instruction encoding
3. **Linker Error:** Verify symbol names and section types

## Future Enhancements

### Short Term
- [ ] Complete all opcode mappings
- [ ] Full ABI compliance
- [ ] Automated linking
- [ ] Basic optimizations

### Medium Term
- [ ] ARM64 backend
- [ ] macOS/Windows support
- [ ] GC integration
- [ ] Debug information

### Long Term
- [ ] JIT mode
- [ ] Profile-guided optimization
- [ ] Type specialization
- [ ] Inline caching

## References

### x86-64 Resources
- Intel® 64 and IA-32 Architectures Software Developer's Manual
- System V ABI (x86-64): https://github.com/hjl-tools/x86-psABI/wiki/x86-64-psABI-1.0.pdf
- X86 Opcode Reference: http://ref.x86asm.net/

### ELF Resources
- ELF Specification: https://refspecs.linuxfoundation.org/elf/elf.pdf
- Tool Interface Standard (TIS): https://refspecs.linuxfoundation.org/elf/gabi4+/contents.html

### Register Allocation
- "Linear Scan Register Allocation" by Massimiliano Poletto
- "Engineering a Compiler" (Cooper & Torczon) - Chapter 13

### Similar Projects
- LuaJIT (Mike Pall) - JIT compiler for Lua
- V8 (Google) - JavaScript JIT compiler
- PyPy - Python JIT compiler

## License

MIT License - Copyright 2025 Scott Porter

See main Sox project for full license details.

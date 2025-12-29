# Feature: Native Binary Executable Generation (ARM64 & x86-64)

**Date:** 2025-11-28
**Status:** Planning
**Author:** Claude (AI Agent)

## Objective

Enable Sox to generate standalone native binary executables for ARM64 and x86-64 architectures on Linux, macOS, and Windows platforms. This will allow Sox programs to be distributed as self-contained executables without requiring the Sox interpreter.

## Current State

### Existing Architecture

Sox currently implements a bytecode-based virtual machine with the following pipeline:

```
Source Code → Scanner → Compiler → Bytecode → VM Interpreter
```

**Key Components:**
- **Bytecode Compiler** (`src/compiler.c`) - Generates 54 opcodes from source
- **VM Interpreter** (`src/vm.c`) - Stack-based bytecode execution engine
- **Serialization** (`src/serialise.c`) - Binary bytecode format with versioning
- **WASM Generator** (`src/wasm_generator.c`) - Existing code generation example
- **Runtime System** - Objects, closures, classes, GC infrastructure, native functions

**Value Representation:**
- Tagged unions: (type, value) pairs
- Types: numbers (double), booleans, nil, objects
- Dynamic typing with runtime type checks

**Object System:**
- Strings (interned), Functions, Closures, Classes, Instances
- Tables (hash maps), Arrays/Slices
- Upvalues for closure variable capture
- Bound methods with receiver binding

## Approach Analysis

There are four primary approaches to generating native executables, each with distinct tradeoffs:

### Approach 1: Embedded VM Executable (Recommended for Phase 1)

**Description:** Bundle the Sox VM interpreter with serialized bytecode into a single executable.

**Architecture:**
```
┌─────────────────────────────┐
│   Native Executable         │
├─────────────────────────────┤
│  Sox VM (statically linked) │
├─────────────────────────────┤
│  Serialized Bytecode (data) │
└─────────────────────────────┘
```

**Pros:**
- ✅ Simplest to implement (2-3 weeks effort)
- ✅ Reuses 100% of existing VM code
- ✅ Cross-platform consistency
- ✅ No new code generation logic needed
- ✅ Debugging remains straightforward
- ✅ All language features work immediately

**Cons:**
- ❌ Runtime interpretation overhead (~10-50x slower than native)
- ❌ Larger executable size (~500KB-2MB including VM)
- ❌ Still requires runtime type checking
- ❌ No compile-time optimizations

**Complexity:** LOW
**Performance:** INTERPRETED
**Maintenance Burden:** LOW

---

### Approach 2: LLVM-Based AOT Compiler (Recommended for Phase 2)

**Description:** Generate LLVM IR from Sox bytecode, then use LLVM to produce optimized native code.

**Architecture:**
```
Sox Bytecode → LLVM IR Generator → LLVM Optimizer → Native Code → Linker → Executable
                                                                      ↓
                                                              Runtime Library
```

**Pros:**
- ✅ Excellent performance (near-native speed)
- ✅ LLVM handles architecture-specific details (x86, ARM, RISC-V, etc.)
- ✅ World-class optimization infrastructure
- ✅ Well-tested, production-grade toolchain
- ✅ Register allocation and instruction selection automated
- ✅ Smaller executables (no VM, just runtime library)
- ✅ Used by Rust, Swift, Julia, and many production languages

**Cons:**
- ❌ LLVM is a large dependency (~100MB+ build)
- ❌ Moderate complexity (need to learn LLVM IR)
- ❌ Still requires runtime library for dynamic features
- ❌ Build system complexity increases

**Complexity:** MEDIUM
**Performance:** NATIVE (95-100% of C)
**Maintenance Burden:** MEDIUM

---

### Approach 3: Custom Native Code Generator

**Description:** Directly emit x86-64 and ARM64 machine code from Sox bytecode.

**Architecture:**
```
Sox Bytecode → IR (optional) → Code Generator → Object File → Linker → Executable
                                    ↓                            ↓
                            Arch-Specific Backends      Runtime Library
                            - x86-64 Backend
                            - ARM64 Backend
```

**Pros:**
- ✅ Maximum control over code generation
- ✅ No external dependencies
- ✅ Educational value (understanding compilation)
- ✅ Potentially smallest possible output

**Cons:**
- ❌ Extremely high complexity (6+ months of work)
- ❌ Need deep knowledge of x86-64 instruction encoding (~1500+ instruction variants)
- ❌ Need deep knowledge of ARM64 instruction encoding
- ❌ Must implement register allocator (complex graph coloring)
- ❌ Must handle calling conventions (System V ABI, Windows x64, ARM64 ABI)
- ❌ Must generate executable formats (ELF, Mach-O, PE/COFF)
- ❌ Bug-prone and requires extensive testing
- ❌ Limited optimization capabilities without significant effort

**Complexity:** VERY HIGH
**Performance:** NATIVE (70-90% of C without optimizations)
**Maintenance Burden:** VERY HIGH

---

### Approach 4: C Transpiler

**Description:** Generate C code from Sox source/bytecode, then compile with GCC/Clang.

**Architecture:**
```
Sox Bytecode → C Code Generator → C Compiler (GCC/Clang) → Executable
                                         ↓
                                  Runtime Library
```

**Pros:**
- ✅ Moderate implementation complexity
- ✅ Leverage mature C compilers
- ✅ Portable to any platform with a C compiler
- ✅ Generated C code is inspectable

**Cons:**
- ❌ Difficult to preserve Sox semantics in C
- ❌ Closures require complex transformation (trampoline functions or lambda lifting)
- ❌ Dynamic typing requires runtime overhead in C
- ❌ Generated C code may be unreadable
- ❌ Less control over optimization
- ❌ Debugging generated C is painful

**Complexity:** MEDIUM
**Performance:** NATIVE (80-95% of handwritten C)
**Maintenance Burden:** MEDIUM

---

## Recommended Implementation Strategy

### **Phased Approach: Start Simple, Optimize Later**

#### **Phase 1: Embedded VM (Short-term - Weeks)**
Get native executables working quickly to validate distribution and user workflows.

#### **Phase 2: LLVM Backend (Medium-term - Months)**
Achieve true native performance with manageable complexity.

#### **Phase 3: Custom Optimizations (Long-term - Optional)**
Only if LLVM proves insufficient or for educational purposes.

---

## Detailed Implementation Plan - Phase 1: Embedded VM

### Overview

Create a tool that packages the Sox VM interpreter with serialized bytecode into platform-specific executables.

### Step 1: Build VM as Static Library

**Files to Create:**
- `premake5.lua` (modify)
- `src/runtime/runtime.h` (new)
- `src/runtime/runtime.c` (new)

**Changes:**
1. Add new Premake5 configuration for static library build:
   ```lua
   project "sox_runtime"
       kind "StaticLib"
       files { "src/**.c" }
       excludes { "src/main.c" }
   ```

2. Create runtime initialization API:
   ```c
   // src/runtime/runtime.h
   typedef struct {
       uint8_t* bytecode;
       size_t bytecode_size;
       int argc;
       char** argv;
   } sox_runtime_config_t;

   int sox_runtime_init(sox_runtime_config_t* config);
   int sox_runtime_execute();
   void sox_runtime_cleanup();
   ```

3. Compile VM as static library for each target:
   - `libsox_runtime_linux_x64.a`
   - `libsox_runtime_linux_arm64.a`
   - `libsox_runtime_darwin_arm64.a`
   - `libsox_runtime_darwin_x64.a`
   - `sox_runtime_windows_x64.lib`

### Step 2: Create Executable Packager

**Files to Create:**
- `tools/packager/main.go` (new)
- `tools/packager/embedder.go` (new)
- `tools/packager/linker.go` (new)

**Functionality:**
1. **Input:** Serialized bytecode file (`.soxc`)
2. **Process:**
   - Load serialized bytecode into memory
   - Create launcher stub (C source)
   - Embed bytecode as byte array in launcher
   - Link launcher with platform-specific runtime library
   - Produce final executable
3. **Output:** Native executable for target platform

**Launcher Stub Template (C):**
```c
// Generated launcher stub
#include "sox_runtime.h"

// Embedded bytecode (generated by packager)
static const unsigned char BYTECODE_DATA[] = {
    0x53, 0x4F, 0x58, 0x43, // "SOXC" magic
    // ... bytecode bytes ...
};

int main(int argc, char** argv) {
    sox_runtime_config_t config = {
        .bytecode = (uint8_t*)BYTECODE_DATA,
        .bytecode_size = sizeof(BYTECODE_DATA),
        .argc = argc,
        .argv = argv
    };

    if (sox_runtime_init(&config) != 0) {
        return 1;
    }

    int result = sox_runtime_execute();
    sox_runtime_cleanup();
    return result;
}
```

**Packager Implementation (Go):**
```go
// tools/packager/main.go
type PackagerConfig struct {
    InputBytecode  string // .soxc file
    OutputBinary   string // executable name
    TargetPlatform string // linux-x64, darwin-arm64, etc.
    RuntimeLibPath string // path to libsox_runtime_*.a
}

func PackageExecutable(config PackagerConfig) error {
    // 1. Read bytecode file
    bytecode, err := os.ReadFile(config.InputBytecode)

    // 2. Generate launcher C source with embedded bytecode
    launcherSrc := generateLauncher(bytecode)

    // 3. Write launcher.c to temp directory
    // 4. Compile launcher.c with target architecture compiler
    // 5. Link with runtime library
    // 6. Output final executable

    return nil
}
```

### Step 3: Cross-Platform Compilation

**Linux (x86-64 and ARM64):**
```bash
# Compile launcher
gcc -c launcher.c -o launcher.o -fPIC -static

# Link with runtime library
gcc launcher.o -L. -lsox_runtime_linux_x64 -static -o program
```

**macOS (ARM64 and x86-64):**
```bash
# Compile launcher for ARM64
clang -c launcher.c -o launcher.o -target arm64-apple-macos11

# Link with runtime library
clang launcher.o -L. -lsox_runtime_darwin_arm64 -target arm64-apple-macos11 -o program
```

**Windows (x86-64):**
```cmd
REM Compile launcher
cl /c launcher.c /Folauncher.obj

REM Link with runtime library
link launcher.obj sox_runtime_windows_x64.lib /OUT:program.exe
```

### Step 4: CLI Integration

**Add to Sox CLI:**
```bash
sox build program.sox --output program --target linux-x64
sox build program.sox --output program --target darwin-arm64
sox build program.sox --output program.exe --target windows-x64
```

**Implementation:**
- Modify `src/main.c` to handle `build` subcommand
- Invoke packager tool with appropriate arguments
- Support cross-compilation via `--target` flag

### Step 5: Runtime Modifications

**Files to Modify:**
- `src/serialise.c` - Ensure complete VM state serialization
- `src/vm.c` - Add embedded bytecode loading path

**Changes:**
1. Add function to load bytecode from memory (not file):
   ```c
   obj_closure_t* l_deserialise_from_memory(uint8_t* data, size_t size);
   ```

2. Skip file I/O when loading embedded bytecode
3. Ensure all native functions are properly initialized

---

## Detailed Implementation Plan - Phase 2: LLVM Backend

### Overview

Generate LLVM IR from Sox bytecode and leverage LLVM's optimization and code generation infrastructure.

### Architecture Components

```
┌──────────────┐
│ Sox Bytecode │
└──────┬───────┘
       ↓
┌──────────────────────┐
│ LLVM IR Generator    │ ← New component
│ - Type mapping       │
│ - Opcode translation │
│ - Runtime calls      │
└──────┬───────────────┘
       ↓
┌──────────────────────┐
│ LLVM IR (.ll file)   │
└──────┬───────────────┘
       ↓
┌──────────────────────┐
│ LLVM Optimizer       │
│ - Inlining           │
│ - Dead code elim     │
│ - Constant folding   │
└──────┬───────────────┘
       ↓
┌──────────────────────┐
│ LLVM CodeGen         │
│ - x86-64 backend     │
│ - ARM64 backend      │
└──────┬───────────────┘
       ↓
┌──────────────────────┐
│ Object File (.o)     │
└──────┬───────────────┘
       ↓
┌──────────────────────┐
│ System Linker (ld)   │ ← Link with runtime
└──────┬───────────────┘
       ↓
┌──────────────────────┐
│ Native Executable    │
└──────────────────────┘
```

### Step 1: LLVM Integration Setup

**Dependencies:**
- LLVM 17+ development libraries
- Clang C++ compiler

**Build System Changes:**
```lua
-- premake5.lua
project "sox_llvm_backend"
    kind "SharedLib"
    language "C++"
    cppdialect "C++17"

    includedirs { "$(LLVM_DIR)/include" }
    libdirs { "$(LLVM_DIR)/lib" }
    links { "LLVM" }

    files {
        "src/llvm_backend/**.cpp",
        "src/llvm_backend/**.h"
    }
```

### Step 2: LLVM IR Generator

**Files to Create:**
- `src/llvm_backend/llvm_generator.h`
- `src/llvm_backend/llvm_generator.cpp`
- `src/llvm_backend/type_mapper.cpp`
- `src/llvm_backend/runtime_bindings.cpp`

**Core Implementation:**

```cpp
// src/llvm_backend/llvm_generator.h
class LLVMGenerator {
public:
    LLVMGenerator(llvm::LLVMContext& context);

    // Generate LLVM IR from Sox function
    llvm::Function* generateFunction(obj_function_t* function);

    // Generate module containing all functions
    std::unique_ptr<llvm::Module> generateModule(obj_closure_t* entry_point);

private:
    llvm::LLVMContext& context_;
    llvm::IRBuilder<> builder_;
    llvm::Module* module_;

    // Type mappings
    llvm::Type* soxValueType_;
    llvm::StructType* soxStringType_;
    llvm::StructType* soxClosureType_;
    // ... other types

    // Runtime function declarations
    llvm::Function* runtimeAdd_;
    llvm::Function* runtimePrint_;
    // ... other runtime functions

    // Generate IR for opcode
    void emitOpcode(OpCode opcode, chunk_t* chunk, int& ip);
};
```

**Type Mapping Strategy:**

Sox uses tagged unions for values. In LLVM, represent as struct:

```cpp
// Sox value_t in LLVM IR
struct SoxValue {
    uint32_t type;  // ValueType enum
    union {
        double number;
        bool boolean;
        void* object;  // pointer to heap object
    } as;
};
```

LLVM IR representation:
```llvm
%SoxValue = type { i32, i64 }
; i32 = type tag
; i64 = union (can hold double, bool, or pointer)
```

**Opcode Translation Examples:**

| Sox Opcode | LLVM IR Generation |
|------------|-------------------|
| `OP_CONSTANT` | Load constant from constant pool, push to stack |
| `OP_ADD` | Pop two values, call `runtime_add()`, push result |
| `OP_GET_LOCAL` | Load from stack slot, push to stack |
| `OP_CALL` | Call function pointer with arguments |
| `OP_RETURN` | Return value from function |
| `OP_JUMP` | Branch to basic block |
| `OP_JUMP_IF_FALSE` | Conditional branch |

**Example LLVM IR for OP_ADD:**

```llvm
; Pop right operand
%right_ptr = getelementptr %SoxValue, ptr %stack, i32 %sp
%sp = sub i32 %sp, 1
%right = load %SoxValue, ptr %right_ptr

; Pop left operand
%left_ptr = getelementptr %SoxValue, ptr %stack, i32 %sp
%sp = sub i32 %sp, 1
%left = load %SoxValue, ptr %left_ptr

; Call runtime add (handles type checking and coercion)
%result = call %SoxValue @sox_runtime_add(%SoxValue %left, %SoxValue %right)

; Push result
%sp = add i32 %sp, 1
%result_ptr = getelementptr %SoxValue, ptr %stack, i32 %sp
store %SoxValue %result, ptr %result_ptr
```

### Step 3: Runtime Library for LLVM Backend

**Files to Create:**
- `src/llvm_runtime/runtime.h`
- `src/llvm_runtime/runtime.c`
- `src/llvm_runtime/operators.c`
- `src/llvm_runtime/objects.c`

**Runtime Functions:**

```c
// Arithmetic operations (handle type checking)
value_t sox_runtime_add(value_t left, value_t right);
value_t sox_runtime_subtract(value_t left, value_t right);
value_t sox_runtime_multiply(value_t left, value_t right);
value_t sox_runtime_divide(value_t left, value_t right);

// Object operations
obj_string_t* sox_runtime_string_concat(obj_string_t* a, obj_string_t* b);
obj_instance_t* sox_runtime_new_instance(obj_class_t* klass);
value_t sox_runtime_get_property(obj_instance_t* instance, obj_string_t* name);
void sox_runtime_set_property(obj_instance_t* instance, obj_string_t* name, value_t value);

// Table operations
value_t sox_runtime_table_get(obj_table_t* table, value_t key);
void sox_runtime_table_set(obj_table_t* table, value_t key, value_t value);

// Built-in functions
void sox_runtime_print(value_t value);
```

**Key Design Decision:**
Keep complex logic (type checking, string interning, GC) in C runtime. LLVM IR focuses on control flow and simple operations.

### Step 4: Optimization Passes

**Leverage LLVM's Built-in Optimizations:**

```cpp
void optimizeModule(llvm::Module* module) {
    llvm::PassManagerBuilder pmBuilder;
    pmBuilder.OptLevel = 2;  // -O2 equivalent
    pmBuilder.SizeLevel = 0;
    pmBuilder.Inliner = llvm::createFunctionInliningPass();

    llvm::legacy::PassManager pm;
    pmBuilder.populateModulePassManager(pm);
    pm.run(*module);
}
```

**Custom Sox-specific Optimizations (Future):**
- Inline small runtime calls (e.g., `IS_NUMBER`)
- Type speculation based on profiling
- Devirtualization of method calls
- Escape analysis for stack allocation

### Step 5: Code Generation and Linking

**Generate Object Files:**

```cpp
void emitObjectFile(llvm::Module* module, const std::string& outputPath,
                    const std::string& targetTriple) {
    llvm::InitializeAllTargets();
    llvm::InitializeAllTargetMCs();
    llvm::InitializeAllAsmPrinters();

    std::string error;
    auto target = llvm::TargetRegistry::lookupTarget(targetTriple, error);

    llvm::TargetOptions options;
    auto rm = llvm::Optional<llvm::Reloc::Model>();
    auto targetMachine = target->createTargetMachine(
        targetTriple, "generic", "", options, rm);

    module->setDataLayout(targetMachine->createDataLayout());
    module->setTargetTriple(targetTriple);

    std::error_code ec;
    llvm::raw_fd_ostream dest(outputPath, ec, llvm::sys::fs::OF_None);

    llvm::legacy::PassManager pass;
    targetMachine->addPassesToEmitFile(pass, dest, nullptr,
                                       llvm::CGFT_ObjectFile);
    pass.run(*module);
    dest.flush();
}
```

**Link with System Linker:**

```bash
# Linux
clang program.o -L./runtime -lsox_llvm_runtime -o program

# macOS
clang program.o -L./runtime -lsox_llvm_runtime -target arm64-apple-macos11 -o program

# Windows
lld-link program.obj sox_llvm_runtime.lib /OUT:program.exe
```

### Step 6: CLI Integration

```bash
sox compile program.sox --backend llvm --opt 2 --target x86_64-linux-gnu
sox compile program.sox --backend llvm --opt 3 --target aarch64-apple-darwin
sox compile program.sox --backend llvm --emit-llvm  # Output .ll file for inspection
```

---

## Files Affected - Phase 1 (Embedded VM)

### New Files:
- `src/runtime/runtime.h` - Runtime library API
- `src/runtime/runtime.c` - Runtime initialization/cleanup
- `tools/packager/main.go` - Executable packager CLI
- `tools/packager/embedder.go` - Bytecode embedding logic
- `tools/packager/linker.go` - Platform-specific linking
- `tools/packager/templates/launcher.c.tmpl` - Launcher stub template

### Modified Files:
- `premake5.lua` - Add static library build configuration
- `src/main.c` - Add `build` subcommand
- `src/serialise.c` - Add memory-based deserialization
- `Makefile` - Add packager build target

---

## Files Affected - Phase 2 (LLVM Backend)

### New Files:
- `src/llvm_backend/llvm_generator.h`
- `src/llvm_backend/llvm_generator.cpp`
- `src/llvm_backend/type_mapper.cpp`
- `src/llvm_backend/opcode_emitter.cpp`
- `src/llvm_backend/runtime_bindings.cpp`
- `src/llvm_runtime/runtime.h`
- `src/llvm_runtime/runtime.c`
- `src/llvm_runtime/operators.c`
- `src/llvm_runtime/objects.c`

### Modified Files:
- `premake5.lua` - Add LLVM backend library
- `src/main.c` - Add `compile` subcommand with `--backend llvm`
- `Makefile` - Add LLVM build dependencies

---

## Testing Strategy

### Phase 1 Testing:

**Unit Tests:**
- [ ] Runtime library initialization/cleanup
- [ ] Bytecode loading from memory
- [ ] Embedded data extraction from executable

**Integration Tests:**
- [ ] Package simple "Hello, World!" program
- [ ] Package program with closures and classes
- [ ] Package program with native function calls
- [ ] Cross-compile for each target platform

**Platform Tests:**
- [ ] Execute on Linux x86-64
- [ ] Execute on Linux ARM64 (via QEMU or native hardware)
- [ ] Execute on macOS ARM64
- [ ] Execute on macOS x86-64 (via Rosetta 2)
- [ ] Execute on Windows x86-64

**Test Scripts:**
```bash
# Build executable
sox build src/test/scripts/fibonacci.sox --output fib --target linux-x64

# Run and verify output
./fib > output.txt
diff output.txt src/test/scripts/fibonacci.sox.out
```

### Phase 2 Testing:

**LLVM IR Generation Tests:**
- [ ] Verify IR correctness with `llvm-as` (assembler)
- [ ] Run IR through LLVM verifier
- [ ] Compare LLVM output with interpreter output for all test scripts

**Performance Benchmarks:**
```bash
# Run benchmark suite
sox benchmark --backend interpreter src/test/scripts/
sox benchmark --backend llvm src/test/scripts/

# Compare results (expect 10-100x speedup with LLVM)
```

**Optimization Tests:**
- [ ] Verify optimizations don't change semantics
- [ ] Test with different optimization levels (-O0, -O1, -O2, -O3)

---

## Risks and Considerations

### Phase 1 Risks:

1. **Executable Size:**
   - Embedding full VM increases size significantly
   - **Mitigation:** Use strip/UPX compression; acceptable for distribution

2. **Cross-Compilation Complexity:**
   - Building runtime for multiple targets
   - **Mitigation:** Use Docker containers for reproducible cross-compilation

3. **Platform-Specific Linker Issues:**
   - Each platform has different linker requirements
   - **Mitigation:** Thoroughly test on each platform; document linker flags

4. **Static Linking Restrictions:**
   - Some platforms discourage static linking (e.g., macOS)
   - **Mitigation:** Support both static and dynamic linking

### Phase 2 Risks:

1. **LLVM Dependency Management:**
   - LLVM is large (~100MB+) and complex to build
   - **Mitigation:** Use system LLVM or provide pre-built binaries

2. **Dynamic Features Mapping:**
   - Dynamic typing, closures, and reflection are complex in LLVM
   - **Mitigation:** Use runtime library for complex operations

3. **ABI Compatibility:**
   - Generated code must match platform ABI for calling runtime
   - **Mitigation:** Carefully follow platform calling conventions

4. **Debugging Compiled Code:**
   - Harder to debug LLVM-generated code
   - **Mitigation:** Generate DWARF debug info; maintain interpreter for development

5. **Incomplete Feature Coverage:**
   - Some opcodes may be difficult to translate initially
   - **Mitigation:** Fallback to runtime interpreter for unsupported opcodes

### General Risks:

1. **Memory Management Complexity:**
   - GC must work correctly in compiled code
   - **Mitigation:** Ensure GC roots are properly tracked

2. **Native Function Compatibility:**
   - Native functions must work in both interpreted and compiled modes
   - **Mitigation:** Use consistent FFI interface

3. **Distribution Complexity:**
   - Users need to manage platform-specific binaries
   - **Mitigation:** Provide clear documentation; build CI/CD for releases

---

## Performance Expectations

### Phase 1 (Embedded VM):
- **Performance:** Same as interpreter (~10-50x slower than native C)
- **Startup Time:** ~5-20ms (load bytecode from memory)
- **Executable Size:** ~500KB - 2MB (VM + bytecode + runtime)
- **Memory Usage:** Same as interpreter

### Phase 2 (LLVM Backend):
- **Performance:** 10-100x faster than interpreter (near-native)
  - Simple arithmetic: ~50-100x faster
  - Object-heavy code: ~10-30x faster (due to runtime calls)
- **Compile Time:** ~100ms - 2s for small programs (LLVM optimization overhead)
- **Executable Size:** ~100KB - 500KB (code + runtime library, no VM)
- **Memory Usage:** Potentially lower (better allocation patterns)

**Benchmark Example (Fibonacci recursive):**
```
Interpreter:     2.5 seconds
LLVM -O0:        0.15 seconds (16x faster)
LLVM -O2:        0.05 seconds (50x faster)
Native C:        0.03 seconds (baseline)
```

---

## Timeline Estimates

### Phase 1: Embedded VM Executable
- **Setup & Runtime Library:** 1 week
- **Packager Tool (Go):** 1 week
- **Cross-Platform Testing:** 1 week
- **CLI Integration & Docs:** 3 days
- **Total:** ~3-4 weeks

### Phase 2: LLVM Backend
- **LLVM Integration:** 1 week
- **IR Generator Core:** 2-3 weeks
- **Runtime Library:** 1-2 weeks
- **Opcode Translation (complete coverage):** 2-3 weeks
- **Optimization & Testing:** 2 weeks
- **Total:** ~8-12 weeks

### Optional: Custom Code Generator (Not Recommended)
- **IR Design:** 2 weeks
- **x86-64 Backend:** 8-12 weeks
- **ARM64 Backend:** 8-12 weeks
- **Register Allocator:** 4-6 weeks
- **Object File Generation:** 3-4 weeks
- **Testing & Debugging:** 4-8 weeks
- **Total:** ~6-12 months

---

## Alternative: Hybrid Approach

**Concept:** Use LLVM for hot functions, interpreter for cold code.

**Architecture:**
1. Start execution in interpreter
2. Profile function call counts
3. When function becomes "hot" (>1000 calls), JIT compile with LLVM
4. Replace interpreter entry with compiled version

**Benefits:**
- Fast startup (no compilation)
- Optimize only performance-critical code
- Reduced memory usage

**Drawbacks:**
- Significantly more complex
- LLVM required at runtime (larger distribution)
- More moving parts to debug

**Verdict:** Interesting for future exploration, but not recommended for initial implementation.

---

## Recommendation Summary

### **Start with Phase 1 (Embedded VM)**

**Why:**
1. **Fastest path to distribution** - Get standalone executables in users' hands quickly
2. **Low risk** - Reuses all existing, tested VM code
3. **Validates workflow** - Confirms packaging, distribution, and cross-compilation process
4. **User feedback** - Learn what users actually need before investing in optimization

**Immediate Value:**
- Users can distribute Sox programs without requiring interpreter
- Simpler deployment (single binary)
- Production-ready in ~1 month

### **Then Pursue Phase 2 (LLVM Backend)**

**Why:**
1. **Proven technology** - LLVM used in production by major languages
2. **Balanced complexity** - Manageable learning curve, excellent results
3. **Future-proof** - LLVM supports many architectures (ARM, RISC-V, WebAssembly)
4. **Community resources** - Extensive documentation and tooling

**Long-term Value:**
- Competitive performance with compiled languages
- Enables Sox for performance-critical applications
- Educational value for contributors

### **Avoid Phase 3 (Custom Codegen)**

**Unless:**
- Sox becomes extremely popular and needs maximum optimization
- Educational goals outweigh practical concerns
- Specialized architecture support not in LLVM

---

## Open Questions

1. **Static vs Dynamic Linking:** Should runtime library be statically or dynamically linked?
   - **Recommendation:** Offer both; default to static for simplicity

2. **Strip Debugging Symbols:** Ship stripped binaries by default?
   - **Recommendation:** Strip by default; offer `--debug` flag to preserve symbols

3. **Compression:** Use UPX or similar executable compression?
   - **Recommendation:** Yes for release builds; reduces size 40-70%

4. **Cross-Compilation Support:** Should Sox support cross-compiling from Linux to Windows?
   - **Recommendation:** Yes via MinGW-w64; add to CI/CD

5. **Backward Compatibility:** How to handle bytecode version mismatches?
   - **Recommendation:** Embed Sox version in executable; warn on mismatch

---

## References

### LLVM Resources:
- **LLVM Tutorial:** https://llvm.org/docs/tutorial/
- **Kaleidoscope Tutorial:** https://llvm.org/docs/tutorial/MyFirstLanguageFrontend/index.html
- **LLVM Language Reference:** https://llvm.org/docs/LangRef.html

### Executable Formats:
- **ELF Specification:** https://refspecs.linuxfoundation.org/elf/elf.pdf
- **Mach-O Format:** https://developer.apple.com/library/archive/documentation/Performance/Conceptual/CodeFootprint/Articles/MachOOverview.html
- **PE/COFF Format:** https://docs.microsoft.com/en-us/windows/win32/debug/pe-format

### Compilation Techniques:
- **Engineering a Compiler (Cooper & Torczon)** - Comprehensive compiler textbook
- **Modern Compiler Implementation in C (Appel)** - Practical compiler construction
- **Crafting Interpreters:** http://craftinginterpreters.com - Sox's inspiration

### Similar Projects:
- **LuaJIT:** High-performance Lua with JIT compilation
- **PyPy:** Python implementation with JIT
- **GraalVM:** Polyglot VM with LLVM backend
- **Rust:** Uses LLVM for code generation
- **Swift:** Uses LLVM for code generation

---

## Appendix: Example Usage

### Phase 1 Usage:

```bash
# Compile Sox program to bytecode
sox program.sox --serialise

# Package into executable
sox build program.sox --output myapp --target linux-x64

# Run standalone executable
./myapp
```

### Phase 2 Usage:

```bash
# Compile with LLVM backend
sox compile program.sox --backend llvm --opt 2 --output myapp

# Inspect generated LLVM IR
sox compile program.sox --backend llvm --emit-llvm --output program.ll

# Run LLVM optimizations at different levels
sox compile program.sox --backend llvm -O0  # No optimization
sox compile program.sox --backend llvm -O2  # Recommended
sox compile program.sox --backend llvm -O3  # Aggressive
```

### Cross-Compilation:

```bash
# Compile for different targets from single host
sox build program.sox --target x86_64-unknown-linux-gnu
sox build program.sox --target aarch64-unknown-linux-gnu
sox build program.sox --target x86_64-apple-darwin
sox build program.sox --target aarch64-apple-darwin
sox build program.sox --target x86_64-pc-windows-msvc
```

---

## Conclusion

Implementing native executable generation for Sox is achievable and valuable. The phased approach balances immediate user needs (standalone distribution) with long-term performance goals (native speed).

**Phase 1 (Embedded VM)** provides immediate value with minimal risk, while **Phase 2 (LLVM Backend)** delivers world-class performance using proven technology.

This plan provides a clear roadmap from bytecode interpreter to high-performance native compiler, positioning Sox as a serious language for both education and production use.

---

**Next Steps:**
1. Review and approve this plan
2. Begin Phase 1 implementation
3. Create tracking issues for each implementation step
4. Set up CI/CD for multi-platform builds

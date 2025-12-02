# Native Linking System for Sox Compiler

**Date:** December 1, 2025
**Status:** Planning
**Scope:** Comprehensive linking infrastructure for native code generation
**Complexity:** High (architecture, symbol resolution, relocation processing, multi-module support)

---

## Executive Summary

The Sox native code generator currently produces ELF and Mach-O object files with symbol tables and relocations prepared, but lacks a linking layer to create executable binaries or integrate multiple compilation units. This plan outlines a comprehensive linking system that bridges object files to final executables, supporting single-module and multi-module compilation workflows.

**Key Goals:**
1. Transform generated object files into executable binaries
2. Support multi-module compilation and linking
3. Enable separate compilation of functions/modules
4. Provide integration with system libraries (runtime library, standard library)
5. Support both static and dynamic linking (future)
6. Maintain platform independence while supporting Linux, macOS, and Windows

---

## Current State

### Existing Capabilities
- ✅ Object file generation (ELF for Linux, Mach-O for macOS)
- ✅ Symbol table creation with proper binding attributes
- ✅ Relocation tracking for code references
- ✅ Calling convention compliance (System V AMD64 ABI, AAPCS64)
- ✅ Register allocation and stack frame management
- ✅ x86-64 and ARM64 code generation
- ✅ Runtime library callout support (prepared)

### Current Gaps
- ❌ No symbol resolution across object files
- ❌ No relocation processing/fixup
- ❌ No executable generation from object files
- ❌ No global variable handling
- ❌ No multi-module/multi-function linking
- ❌ No library linking
- ❌ No entry point generation
- ❌ No Windows PE/COFF support
- ❌ No debug information (DWARF) generation

---

## Architecture Overview

### Linking Pipeline

```
Multiple Object Files (.o)
    ↓
Symbol Resolution Phase
    • Collect all symbols from object files
    • Build global symbol table
    • Resolve undefined symbols
    • Validate symbol compatibility
    ↓
Relocation Resolution Phase
    • Process relocations from all object files
    • Calculate final addresses
    • Update relocation targets
    ↓
Layout Phase
    • Merge compatible sections
    • Assign addresses to sections and symbols
    • Allocate memory for data/BSS
    ↓
Fixup Phase
    • Apply relocations with final addresses
    • Update cross-references
    • Fix GOT/PLT entries (if dynamic)
    ↓
Output Generation
    • Write executable header (ELF/Mach-O/PE)
    • Write sections and segments
    • Include runtime library code
    ↓
Final Executable (.out/.exe/binary)
```

---

## Linking Strategies

### Strategy 1: System Linker Wrapper (Recommended - Short Term)
**Approach:** Leverage existing system linkers (GNU ld, ld64) via shell execution

**Advantages:**
- Minimal implementation effort (100-200 LOC)
- Proven correctness (system linker is well-tested)
- Immediate multi-module support
- Library linking works automatically
- Debug symbols preserved naturally

**Disadvantages:**
- Dependency on external tools
- Slower compilation (fork/exec overhead)
- Less control over output
- Platform-specific linker scripts needed

**Implementation:**
```c
// pseudocode
int native_link_with_system_linker(
    const native_link_options_t* options,
    const char** object_files,
    int num_objects
) {
    // Generate linker command
    // Execute system linker
    // Return status
}
```

**Supports:** Linux (gcc/ld), macOS (clang/ld64), Windows (lld-link)

---

### Strategy 2: Custom Minimal Linker (Medium Term)
**Approach:** Implement a lightweight linker for SOX-specific needs

**Advantages:**
- Complete control over linking behavior
- Faster compilation
- Can optimize for Sox-specific patterns
- No external tool dependency
- Better error messages

**Disadvantages:**
- Significant implementation effort (~2000+ LOC)
- Maintenance burden
- Need to handle platform quirks
- Complex relocation processing
- Library support harder to implement

**Core Components:**
1. **Object File Parser** - Read ELF/Mach-O files
2. **Symbol Manager** - Resolve symbols across modules
3. **Relocation Processor** - Apply relocations
4. **Section Merger** - Combine sections from multiple files
5. **Executable Writer** - Generate final binary

**Phase 1:** ELF-only (Linux), basic static linking
**Phase 2:** Add Mach-O support (macOS)
**Phase 3:** Add PE support (Windows)
**Phase 4:** Dynamic linking support

---

### Strategy 3: Hybrid Approach (Recommended - Long Term)
**Approach:** Custom linker for simple cases, system linker for complex cases

**Advantages:**
- Fast path for single-module or simple multi-module cases
- Fallback to system linker for complex scenarios
- Maintains control where it matters
- Good balance of simplicity and performance

**Disadvantages:**
- Code duplication/maintenance
- Complexity in deciding which path to use
- Testing burden

---

## Detailed Implementation Plan

### Phase 1: Object File Infrastructure (2-3 weeks)

#### 1.1 Object File Parser Library
**Files:** `src/native/linker.h`, `src/native/linker.c`

Create unified object file parsing layer:

```c
// Abstract object file representation
typedef struct {
    void* platform_data;           // Platform-specific data
    native_section_t* sections;
    int num_sections;
    native_symbol_t* symbols;
    int num_symbols;
    native_relocation_t* relocations;
    int num_relocations;
    const char* filename;
} native_object_file_t;

// Platform-specific parsers
native_object_file_t* parse_elf64_object(const char* path);
native_object_file_t* parse_macho64_object(const char* path);
void free_object_file(native_object_file_t* obj);
```

**Work Items:**
- [ ] Create unified object file structure
- [ ] ELF64 parser (`src/native/elf_reader.h`, `elf_reader.c`)
  - [ ] Section parsing
  - [ ] Symbol table parsing
  - [ ] Relocation parsing
  - [ ] String table handling
- [ ] Mach-O parser (`src/native/macho_reader.h`, `macho_reader.c`)
  - [ ] Load command parsing
  - [ ] Section parsing
  - [ ] Symbol table parsing
  - [ ] Relocation parsing
- [ ] Section representation
- [ ] Symbol representation
- [ ] Relocation representation

**Tests:**
- [ ] Unit tests for ELF parsing
- [ ] Unit tests for Mach-O parsing
- [ ] Round-trip tests (generate object → parse → verify)

---

#### 1.2 Symbol Resolution Engine
**Files:** `src/native/symbol_table.h`, `src/native/symbol_table.c`

Implement multi-file symbol resolution:

```c
// Global symbol table
typedef struct {
    native_symbol_t* symbols;
    int num_symbols;
    int capacity;
    hash_table_t* lookup;    // Quick lookup by name
} native_symbol_table_t;

// Symbol resolution
typedef struct {
    const char* symbol_name;
    native_symbol_t* definition;    // Which object defines this?
    native_object_file_t* from_object;
    bool is_weak;
    int num_references;            // How many undefined references?
} native_symbol_ref_t;

native_symbol_table_t* create_symbol_table(void);
void add_object_symbols(native_symbol_table_t* table, native_object_file_t* obj);
native_symbol_ref_t* resolve_symbol(native_symbol_table_t* table, const char* name);
bool validate_no_duplicates(native_symbol_table_t* table);
bool resolve_all_symbols(native_symbol_table_t* table);
```

**Work Items:**
- [ ] Global symbol table structure
- [ ] Symbol collection from object files
- [ ] Duplicate detection and error reporting
- [ ] Symbol resolution algorithm
- [ ] Weak symbol handling
- [ ] Undefined symbol detection
- [ ] Symbol compatibility checking

**Tests:**
- [ ] Single-module resolution
- [ ] Multi-module resolution
- [ ] Undefined symbol detection
- [ ] Duplicate symbol detection
- [ ] Weak symbol override

---

#### 1.3 Address Space Layout
**Files:** `src/native/address_space.h`, `src/native/address_space.c`

Implement address assignment for sections and symbols:

```c
// Memory layout
typedef struct {
    uint64_t text_start;      // .text section start
    uint64_t data_start;      // .data section start
    uint64_t bss_start;       // .bss section start
    uint64_t total_size;

    native_section_layout_t* sections;
    int num_sections;
} native_memory_layout_t;

// Section assignment
typedef struct {
    const char* name;
    uint64_t address;
    size_t size;
    size_t alignment;
    native_object_file_t** objects;  // Which objects contribute?
    int num_objects;
} native_section_layout_t;

native_memory_layout_t* create_memory_layout(void);
void layout_sections(native_memory_layout_t* layout,
                    native_object_file_t** objects,
                    int num_objects);
uint64_t get_symbol_address(native_memory_layout_t* layout,
                           const native_symbol_t* symbol);
```

**Work Items:**
- [ ] Section merging strategy
- [ ] Address assignment for sections
- [ ] Symbol address calculation
- [ ] Alignment handling
- [ ] Memory layout validation
- [ ] Entry point location

**Tests:**
- [ ] Single section layout
- [ ] Multi-section layout
- [ ] Alignment validation
- [ ] Address overlap detection

---

### Phase 2: Relocation Processing (3-4 weeks)

#### 2.1 Relocation Processor
**Files:** `src/native/relocation.h`, `src/native/relocation.c`

Process and apply relocations:

```c
// Relocation processing context
typedef struct {
    native_memory_layout_t* layout;
    uint8_t* output_buffer;
    size_t output_size;
    native_symbol_table_t* symbols;
    const char* target_arch;
    const char* target_os;
} native_reloc_context_t;

// Process relocations for all objects
int process_relocations(native_reloc_context_t* context,
                       native_object_file_t** objects,
                       int num_objects);

// Apply specific relocation types
int apply_relocation_x64(native_reloc_context_t* context,
                        const native_relocation_t* reloc,
                        uint64_t symbol_address);
int apply_relocation_arm64(native_reloc_context_t* context,
                          const native_relocation_t* reloc,
                          uint64_t symbol_address);
```

**Relocation Types to Support:**

**x86-64:**
- `R_X86_64_64` - 64-bit absolute address
- `R_X86_64_PC32` - 32-bit PC-relative offset
- `R_X86_64_PLT32` - PLT-relative (function calls)
- `R_X86_64_GOTPCREL` - GOT-relative (external data)
- `R_X86_64_RELATIVE` - Relative to load address

**ARM64:**
- `R_AARCH64_ABS64` - 64-bit absolute
- `R_AARCH64_CALL26` - 26-bit PC-relative (BL instruction)
- `R_AARCH64_JUMP26` - 26-bit PC-relative (B instruction)
- `R_AARCH64_ADR_PREL_PG_HI21` - Page-relative ADR
- `R_AARCH64_ADD_ABS_LO12_NC` - Low 12 bits of absolute
- `R_AARCH64_PREL64` - 64-bit PC-relative
- `R_AARCH64_RELATIVE` - Relative to load address

**Work Items:**
- [ ] Relocation context structure
- [ ] x86-64 relocation processor
  - [ ] Absolute relocations
  - [ ] PC-relative relocations
  - [ ] PLT/GOT handling (future)
- [ ] ARM64 relocation processor
  - [ ] Instruction encoding for relocations
  - [ ] Page-relative address calculations
  - [ ] Immediate value patching
- [ ] Relocation validation
- [ ] Error reporting for unsupported relocations

**Tests:**
- [ ] Each relocation type
- [ ] Cross-architecture relocations
- [ ] Address overflow detection
- [ ] Alignment validation

---

#### 2.2 Instruction Patching
**Files:** `src/native/instruction_patch.h`, `src/native/instruction_patch.c`

Utilities for modifying instructions during relocation:

```c
// x86-64 instruction patching
void patch_x64_imm32(uint8_t* code, size_t offset, int32_t value);
void patch_x64_imm64(uint8_t* code, size_t offset, int64_t value);
int64_t get_x64_imm32(const uint8_t* code, size_t offset);

// ARM64 instruction patching
void patch_arm64_imm26(uint8_t* code, size_t offset, int32_t value);
void patch_arm64_adr(uint8_t* code, size_t offset, uint64_t target);
void patch_arm64_add_imm12(uint8_t* code, size_t offset, uint16_t imm, bool shift);
void patch_arm64_movz_movk(uint8_t* code, size_t offset, uint64_t value);
```

**Work Items:**
- [ ] x86-64 imm32 patching
- [ ] x86-64 imm64 patching (MOV r64, imm64)
- [ ] ARM64 26-bit immediate (BL, B)
- [ ] ARM64 ADR instruction patching
- [ ] ARM64 ADD instruction patching
- [ ] ARM64 MOVZ/MOVK patching

---

### Phase 3: Executable Generation (2-3 weeks)

#### 3.1 System Linker Wrapper (Quick Implementation)
**Files:** `src/native/system_linker.h`, `src/native/system_linker.c`

Shell out to system linkers for production use:

```c
typedef struct {
    const char* output_path;
    const char** object_files;
    int num_objects;
    const char** libraries;
    int num_libraries;
    const char** library_paths;
    int num_library_paths;
    bool verbose;
    int optimization_level;
} native_link_options_t;

// Link using system linker
int native_link_with_system(const native_link_options_t* options);
```

**Implementation:**
- [ ] Linux: Generate gcc/ld command
  - [ ] Build command line
  - [ ] Pass object files
  - [ ] Add runtime library
  - [ ] Handle library dependencies
  - [ ] Execute via popen/system
- [ ] macOS: Generate clang/ld64 command
  - [ ] macOS-specific flags
  - [ ] Framework support
  - [ ] Executable format specifics
- [ ] Windows: Generate clang/lld-link command
  - [ ] PE/COFF specific flags
  - [ ] Subsystem specification

**Tests:**
- [ ] End-to-end compilation and linking
- [ ] Library linking
- [ ] Error handling

---

#### 3.2 Custom Minimal Linker (Long Term)
**Files:** `src/native/custom_linker.h`, `src/native/custom_linker.c`

Lightweight linker for Sox-specific needs:

```c
// Main linking entry point
int native_link_custom(const native_link_options_t* options);

// High-level linking pipeline
native_executable_t* link_objects_to_executable(
    native_object_file_t** objects,
    int num_objects,
    const native_link_options_t* options
);
```

**Work Items:**
- [ ] ELF executable generation
  - [ ] ELF header creation (e_entry, e_phoff, etc.)
  - [ ] Program header generation (LOAD segments)
  - [ ] Section to segment mapping
  - [ ] Permission setting (RWX)
  - [ ] Load address calculation
- [ ] Mach-O executable generation (future)
  - [ ] Mach-O header
  - [ ] Load commands
  - [ ] Segment creation
  - [ ] Section mapping
- [ ] Entry point setup
  - [ ] _start symbol
  - [ ] Call main function
  - [ ] Exit handling
- [ ] Runtime library linking
  - [ ] Embed runtime functions
  - [ ] Symbol resolution with runtime
- [ ] File I/O for executable output

**Tests:**
- [ ] Generated executable validation
- [ ] Execution of linked code
- [ ] Symbol verification

---

### Phase 4: Multi-Module Support (2-3 weeks)

#### 4.1 Compilation Unit Management
**Files:** `src/native/compilation_unit.h`, `src/native/compilation_unit.c`

Support for compiling multiple Sox modules/functions:

```c
// Compilation unit
typedef struct {
    const char* source_file;
    const char* module_name;
    obj_closure_t* closure;
    const char* object_file;      // Generated .o file
    native_object_file_t* parsed; // Parsed object file
} native_compilation_unit_t;

// Unit collection and coordination
typedef struct {
    native_compilation_unit_t* units;
    int num_units;
    native_link_options_t link_options;
} native_link_job_t;

native_link_job_t* create_link_job(void);
void add_unit_to_job(native_link_job_t* job,
                    native_compilation_unit_t* unit);
int process_link_job(native_link_job_t* job);
```

**Work Items:**
- [ ] Compilation unit representation
- [ ] Multiple closure compilation
- [ ] Object file naming/organization
- [ ] Dependency tracking
- [ ] Incremental linking support

---

#### 4.2 Cross-Module Symbol Resolution
**Files:** Extends symbol_table.h/c

Handle external symbols between modules:

```c
// External symbol tracking
typedef struct {
    const char* symbol_name;
    const char* defining_module;
    const char* importing_modules[MAX_IMPORTERS];
    int num_importers;
} native_external_symbol_t;

// Export/import tracking
void mark_symbol_exported(native_symbol_table_t* table,
                         const char* symbol,
                         const char* module);
void mark_symbol_imported(native_symbol_table_t* table,
                         const char* symbol,
                         const char* module);
```

**Work Items:**
- [ ] Export declarations
- [ ] Import tracking
- [ ] Cross-module resolution
- [ ] Circular dependency detection

---

### Phase 5: Integration & Runtime Library (2-3 weeks)

#### 5.1 Runtime Library Linking
**Files:** `src/native/runtime_linker.h`, `src/native/runtime_linker.c`

Integrate Sox runtime library:

```c
// Runtime function declarations
typedef struct {
    const char* symbol_name;
    void* function_pointer;
    const char* signature;
} native_runtime_function_t;

// Link runtime library
int link_runtime_library(native_executable_t* exe,
                        const native_runtime_function_t* functions,
                        int num_functions);
```

**Runtime Functions to Export:**
- Memory allocation (`sox_allocate`, `sox_reallocate`, `sox_free`)
- GC hooks (`sox_gc_mark`, `sox_gc_sweep`)
- Type operations (`sox_is_number`, `sox_as_number`, etc.)
- String operations (`sox_string_concat`, `sox_string_intern`)
- Table operations (`sox_table_get`, `sox_table_set`)
- Array operations (`sox_array_get`, `sox_array_set`)
- Print/debug (`sox_print`, `sox_debug_stack_trace`)
- Error handling (`sox_runtime_error`)

**Work Items:**
- [ ] Runtime function export list
- [ ] ABI definition for runtime calls
- [ ] Symbol resolution for runtime
- [ ] Data structure layout consistency

---

#### 5.2 Global Variables and Data Sections
**Files:** `src/native/data_section.h`, `src/native/data_section.c`

Support for .data, .bss, and .rodata sections:

```c
// Data section tracking
typedef struct {
    const char* symbol_name;
    const uint8_t* data;
    size_t size;
    native_data_section_t section;  // .data, .bss, .rodata
    size_t alignment;
} native_global_var_t;

// Link global variables
int link_global_variables(native_executable_t* exe,
                         native_global_var_t** vars,
                         int num_vars);
```

**Work Items:**
- [ ] Global variable tracking
- [ ] Initialization values
- [ ] BSS zero-fill handling
- [ ] Address computation
- [ ] Read-only data section

---

### Phase 6: Optimization & Polish (2-3 weeks)

#### 6.1 Link-Time Optimizations
**Files:** `src/native/lto.h`, `src/native/lto.c`

Optional optimization passes during linking:

```c
// LTO options
typedef struct {
    bool dead_code_elimination;
    bool function_inlining;
    bool constant_propagation;
    int optimization_level;
} native_lto_options_t;

// Dead code elimination
int eliminate_dead_code(native_executable_t* exe);

// Function inlining heuristics
int inline_functions(native_executable_t* exe,
                    const native_lto_options_t* options);
```

**Work Items:**
- [ ] Dead code elimination
- [ ] Unused symbol removal
- [ ] Function inlining heuristics
- [ ] Constant propagation

---

#### 6.2 Debug Information
**Files:** `src/native/dwarf.h`, `src/native/dwarf.c`

DWARF debug info generation (optional):

```c
// DWARF generation
int generate_dwarf_debug_info(native_executable_t* exe,
                             const char* source_file);
```

**Work Items:**
- [ ] .debug_info section
- [ ] .debug_line section
- [ ] .debug_abbrev section
- [ ] Source line mapping
- [ ] Function debugging support

---

### Phase 7: Windows Support (Future)
**Files:** `src/native/pe_writer.h`, `src/native/pe_writer.c`

Windows PE/COFF executables:

```c
// PE executable generation
int native_generate_pe_executable(const native_link_options_t* options);
```

**Work Items:**
- [ ] PE header generation
- [ ] COFF section handling
- [ ] Import/export table
- [ ] Relocation processing
- [ ] Subsystem specification

---

## Data Structures

### Core Linking Structures

```c
// Unified symbol representation
typedef struct {
    const char* name;
    uint64_t address;           // Assigned address (0 if undefined)
    size_t size;
    uint8_t binding;            // STB_LOCAL, STB_WEAK, STB_GLOBAL
    uint8_t type;               // STT_FUNC, STT_OBJECT, STT_NOTYPE
    int section_index;          // Section containing symbol
    bool is_defined;
    const char* source_module;  // Which module defines this?
} native_symbol_t;

// Unified relocation representation
typedef struct {
    uint64_t offset;            // Offset in section
    const char* symbol_name;
    uint32_t type;              // Platform-specific type
    int64_t addend;             // Addend for calculation
    const char* source_file;    // Which object defines this relocation?
} native_relocation_t;

// Unified section representation
typedef struct {
    const char* name;
    uint8_t* data;
    size_t size;
    size_t alignment;
    uint32_t flags;             // Readable, writable, executable
    uint32_t type;              // .text, .data, .bss, .rodata
    native_relocation_t* relocations;
    int num_relocations;
} native_section_t;

// Final executable representation
typedef struct {
    uint8_t* image;
    size_t image_size;
    uint64_t entry_point;
    native_section_t* sections;
    int num_sections;
    native_symbol_t* symbols;
    int num_symbols;
    const char* output_path;
} native_executable_t;
```

---

## Symbol Resolution Algorithm

```
Algorithm: ResolveSymbols(object_files)
    global_symbols ← empty table

    // Phase 1: Collect all symbols
    for each object_file in object_files:
        for each symbol in object_file:
            if symbol.binding == GLOBAL:
                if symbol.name in global_symbols:
                    error("Duplicate definition: " + symbol.name)
                global_symbols[symbol.name] ← (symbol, object_file)

    // Phase 2: Resolve undefined symbols
    for each object_file in object_files:
        for each symbol in object_file:
            if symbol.is_undefined:
                if symbol.name not in global_symbols:
                    error("Undefined symbol: " + symbol.name)
                symbol.resolved_address ←
                    global_symbols[symbol.name].address

    return global_symbols
```

---

## Relocation Processing Algorithm

```
Algorithm: ProcessRelocations(objects, layout, symbols)
    for each object_file in objects:
        for each relocation in object_file.relocations:
            target_symbol ← symbols[relocation.symbol]

            if not target_symbol.is_defined:
                error("Undefined symbol in relocation: " +
                      relocation.symbol)

            switch relocation.type:
                case ABS64:
                    value ← target_symbol.address + relocation.addend
                case PC32:
                    value ← target_symbol.address - relocation.offset
                case CALL26:
                    value ← (target_symbol.address - relocation.offset) >> 2
                ...

            apply_relocation(object_file, relocation, value)
```

---

## Integration Points

### With Native Code Generator

```
native_codegen_options_t + link_options
    ↓
1. Compile Sox closure → object file
   (existing: native_codegen_generate_object)
    ↓
2. Parse object file
   (new: parse_elf64_object/parse_macho64_object)
    ↓
3. Collect symbols
   (new: add_object_symbols)
    ↓
4. Resolve relocations
   (new: process_relocations)
    ↓
5. Generate executable
   (new: native_link_custom or native_link_with_system)
    ↓
Final executable
```

### With Compiler

```
Sox source code
    ↓
Scanner → Compiler → Bytecode (OP_CLOSURE)
    ↓
Optionally: native_codegen_generate_object()
    ↓
[Link phase]
    ↓
Executable
```

### With Runtime Library

```
Native executable + runtime symbols
    ↓
Runtime library symbols (malloc, print, etc.)
    ↓
Link:
  • Resolve undefined runtime symbols
  • Embed runtime function code
  • Establish calling convention
    ↓
Final executable with runtime support
```

---

## API Design

### Main Linking API

```c
// Single-stage compilation + linking
int native_compile_and_link(
    const obj_closure_t* closure,
    const native_link_options_t* options
);

// Multi-module linking
int native_link_objects(
    const char** object_files,
    int num_objects,
    const native_link_options_t* options
);

// Low-level API
native_executable_t* native_link_internal(
    native_object_file_t** objects,
    int num_objects,
    const native_link_options_t* options
);

// Options structure
typedef struct {
    // Output
    const char* output_path;

    // Linking strategy
    bool use_system_linker;        // Use gcc/clang/lld?
    bool custom_linker;             // Use custom linker?
    bool keep_intermediate_objects; // Keep .o files?

    // Libraries
    const char** libraries;
    int num_libraries;
    const char** library_paths;
    int num_library_paths;

    // Optimization
    int optimization_level;         // 0-3
    bool enable_lto;               // Link-time optimization?
    bool strip_symbols;            // Remove debug symbols?

    // Runtime
    const char* runtime_library_path;
    bool embed_runtime;            // Embed runtime vs link externally?

    // Debugging
    bool debug_symbols;            // Include DWARF debug info?
    bool verbose;
    bool print_relocations;        // Debug output?

    // Platform override
    const char* target_os;         // Override detected OS
    const char* target_arch;       // Override detected architecture
} native_link_options_t;
```

---

## Platform-Specific Considerations

### Linux (ELF64, System V ABI)
- Entry point: `_start` function
- Initial setup: Call static constructors, then `main()`
- Exit: Call `exit()` syscall or return from main
- Runtime: Link with libc or custom runtime
- Executable type: ET_EXEC (absolute addresses) vs ET_DYN (PIE)
- Linker: GNU ld or LLD

### macOS (Mach-O, ARM64/x86-64)
- Entry point: `_main` function (different from Unix!)
- Initial setup: dyld handles initialization
- Runtime: Link with system libraries
- Executable type: MH_EXECUTE
- Position: Usually requires PIE (Position Independent Executable)
- Linker: ld64 (Apple's linker)
- Special: Code signing may be required

### Windows (PE/COFF)
- Entry point: `mainCRTStartup` or `WinMainCRTStartup`
- Runtime: CRT (C Runtime Library)
- Executable type: .exe
- Subsystem: Console or GUI
- Linker: lld-link or MSVC linker

---

## Testing Strategy

### Unit Tests
```bash
tests/linking/
├── test_symbol_resolution.c      # Symbol table operations
├── test_relocation_processing.c  # Relocation fixup
├── test_address_layout.c          # Section layout
├── test_elf_parsing.c            # ELF object file parsing
├── test_macho_parsing.c          # Mach-O parsing
├── test_instruction_patching.c   # Relocation patching
└── test_executable_generation.c  # Final executable creation
```

### Integration Tests
```bash
tests/integration/
├── single_module_linking.sox      # Single file compilation
├── multi_module_linking.sox       # Multiple files
├── external_symbol_resolution.sox # Cross-module calls
├── global_variables.sox           # .data/.bss handling
├── library_linking.sox            # System library calls
└── runtime_library_linking.sox    # Runtime function calls
```

### End-to-End Tests
```bash
make test-linking  # Compile all test scripts, verify execution
```

---

## Performance Considerations

### Linking Performance
- **Current bottleneck:** System linker fork/exec overhead (~100-500ms)
- **Goal:** Sub-100ms linking for typical modules
- **Custom linker:** Target ~50-100ms for ELF, ~100-200ms for Mach-O

### Optimization Opportunities
1. **Parallel symbol resolution** - Process multiple object files in parallel
2. **Symbol hash table** - O(1) symbol lookup
3. **Section pre-allocation** - Avoid reallocations during layout
4. **Memory-mapped I/O** - For large object files
5. **Lazy relocation processing** - Only process needed relocations

### Memory Usage
- Target: <50MB for typical multi-module projects
- Symbol table: O(number of symbols)
- Relocation tracking: O(number of relocations)
- Output buffer: Size of final executable

---

## Error Handling

### Compile-Time Errors
```
ERROR: Undefined symbol 'foo_function'
  Referenced in: module.o
  Used at: offset 0x1234 (relocation type R_X86_64_CALL26)

ERROR: Duplicate symbol definition 'print'
  First defined in: runtime.o
  Redefined in: user.o

ERROR: Symbol mismatch for 'data'
  Expected: Function (in module1.o)
  Got: Data object (in module2.o)
```

### Runtime Errors
```
ERROR: Cannot load executable: segment alignment error
  Section .text: alignment 0x1000 < required 0x2000

ERROR: Relocation overflow
  Symbol 'target' at 0x123456
  Relocation offset: 0x789abc
  Distance: 0x456xxx (exceeds 32-bit range)
```

---

## Future Extensions

### Dynamic Linking
- PLT (Procedure Linkage Table) for lazy function binding
- GOT (Global Offset Table) for position-independent code
- .so/.dylib generation

### Link-Time Code Generation
- Template instantiation
- Generic specialization
- Monomorphization

### Incremental Linking
- Cache unchanged modules
- Faster recompilation
- Dependency tracking

### Profiling & Optimization
- Link-time profiling
- Hot code identification
- Prefetch optimization

---

## Timeline & Milestones

### Milestone 1: Basic Infrastructure (Week 1-2)
- [ ] Object file parser (ELF/Mach-O)
- [ ] Symbol resolution engine
- [ ] Address space layout
- [ ] Core data structures

### Milestone 2: Relocation Processing (Week 3-4)
- [ ] Relocation processor
- [ ] Instruction patching
- [ ] Relocation validation

### Milestone 3: Executable Generation (Week 5-6)
- [ ] System linker wrapper (quick win)
- [ ] Basic executable generation
- [ ] End-to-end compilation + linking

### Milestone 4: Multi-Module Support (Week 7-8)
- [ ] Compilation unit management
- [ ] Cross-module symbols
- [ ] Integration testing

### Milestone 5: Runtime Library Integration (Week 9-10)
- [ ] Runtime function export
- [ ] Global variable support
- [ ] ABI definition

### Milestone 6: Optimization & Polish (Week 11-12)
- [ ] Link-time optimizations
- [ ] Debug information
- [ ] Error messages
- [ ] Documentation

### Milestone 7: Windows Support (Future)
- [ ] PE/COFF format
- [ ] Windows linker integration
- [ ] CRT linking

---

## Dependencies

### External
- System linker (gcc, clang, ld64, lld-link)
- libc or custom runtime

### Internal
- Existing native code generator (`src/native/`)
- Object file writers (ELF and Mach-O already implemented)
- Memory allocator (`src/lib/memory.c`)
- Hash table (`src/lib/table.c`)

---

## Risk Analysis

### High Risk
1. **Symbol resolution correctness** - Mistakes could cause silent link failures
   - *Mitigation:* Comprehensive unit testing, symbol validation
2. **Relocation processing** - Wrong relocations cause crashes
   - *Mitigation:* Instruction-level testing, overflow detection

### Medium Risk
1. **Platform differences** - ABI incompatibilities
   - *Mitigation:* Separate per-platform testing
2. **Multi-module complexity** - Integration issues
   - *Mitigation:* Gradual rollout, extensive testing

### Low Risk
1. **Performance** - Linking might be slower than expected
   - *Mitigation:* Fall back to system linker
2. **Maintenance** - Complex code hard to maintain
   - *Mitigation:* Clear documentation, modular design

---

## Success Criteria

- ✅ Single-module compilation → executable (functional)
- ✅ Multi-module linking with cross-module symbols (functional)
- ✅ Symbol resolution with proper error messages (quality)
- ✅ Relocation processing without overflow (correctness)
- ✅ Generated executables run correctly (correctness)
- ✅ Link time < 200ms typical case (performance)
- ✅ Clear API and documentation (usability)
- ✅ Comprehensive unit and integration tests (reliability)

---

## References

### Binary Formats
- ELF64 Specification: https://refspecs.linuxbase.org/elf/
- Mach-O Format: https://en.wikipedia.org/wiki/Mach-O
- PE Format: https://en.wikipedia.org/wiki/Portable_Executable

### Calling Conventions
- System V AMD64 ABI: https://refspecs.linuxbase.org/elf/x86_64-abi-0.99.pdf
- ARM64 ABI: https://github.com/ARM-software/abi-aa/releases

### Linker Documentation
- GNU ld: https://sourceware.org/binutils/docs/ld/
- LLVM LLD: https://lld.llvm.org/

### Related Sox Documentation
- `/plans/2025-11-28-native-binary-executables.md` - Overview of native compilation
- `/docs/native-codegen-implementation.md` - Current code generator details
- `/docs/macos-native-codegen.md` - macOS-specific information
- `/src/native/README.md` - Developer guide


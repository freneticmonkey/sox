# Custom Linker Implementation for Sox

**Date:** December 26, 2025
**Status:** Planning
**Scope:** Implement a custom linker to replace external system linkers
**Complexity:** High
**Estimated Timeline:** 8-12 weeks

---

## Executive Summary

Sox currently generates native object files (ELF and Mach-O) and relies on external system linkers (clang, gcc, ld, ld64) to produce executable binaries. This plan outlines the implementation of a **custom minimal linker** that can:

1. Link multiple object files into a single executable
2. Resolve symbols across compilation units
3. Process relocations and fix up code references
4. Generate platform-specific executable formats (ELF, Mach-O, PE)
5. Integrate with the Sox runtime library
6. Eliminate external toolchain dependencies

**Key Benefits:**
- ✅ **Complete control** over the linking process
- ✅ **Better error messages** specific to Sox semantics
- ✅ **Faster linking** without fork/exec overhead
- ✅ **Cross-compilation** support without complex toolchains
- ✅ **Educational value** in understanding linkers
- ✅ **Predictable behavior** across platforms

**Key Challenges:**
- ❌ **High implementation complexity** (2000+ LOC)
- ❌ **Platform-specific quirks** (ELF, Mach-O, PE all differ)
- ❌ **Relocation processing** is error-prone
- ❌ **ABI compliance** requires careful attention
- ❌ **Testing burden** to ensure correctness

---

## Current State Analysis

### What We Have

**Object File Generation:**
- ✅ ELF64 writer (`src/native/elf_writer.c`) - generates relocatable object files
- ✅ Mach-O writer (`src/native/macho_writer.c`) - generates relocatable object files
- ✅ Symbol table creation with proper binding attributes
- ✅ Relocation tracking in object files
- ✅ Section management (.text, .data, .rodata, .bss)

**System Linker Wrapper:**
- ✅ Linker detection (`src/lib/linker.c`)
- ✅ Platform-specific command generation
- ✅ Runtime library integration via `-lsox_runtime`
- ✅ Works on Linux (gcc/ld) and macOS (clang/ld64)

**Native Code Generation:**
- ✅ x86-64 code generation
- ✅ ARM64 code generation
- ✅ IR-based compilation pipeline
- ✅ Register allocation
- ✅ Calling convention support

### What's Missing

**Symbol Resolution:**
- ❌ No symbol table merging across object files
- ❌ No duplicate symbol detection
- ❌ No undefined symbol resolution
- ❌ No weak symbol handling

**Relocation Processing:**
- ❌ No relocation application logic
- ❌ No instruction patching for relocations
- ❌ No overflow detection
- ❌ No GOT/PLT generation (for dynamic linking)

**Executable Generation:**
- ❌ No ELF executable writer (only object files)
- ❌ No Mach-O executable writer (only object files)
- ❌ No PE/COFF support for Windows
- ❌ No entry point generation (_start)

**Layout & Addressing:**
- ❌ No section merging logic
- ❌ No address assignment
- ❌ No segment layout (LOAD segments)
- ❌ No alignment handling

---

## Architecture Overview

### Linker Pipeline

```
┌─────────────────────────────────────────────────────────────┐
│                    Input: Object Files                       │
│              (.o files from native code generator)           │
└────────────────────┬────────────────────────────────────────┘
                     │
                     ▼
┌─────────────────────────────────────────────────────────────┐
│              Phase 1: Object File Parsing                    │
│  • Read ELF/Mach-O object files                             │
│  • Extract sections (.text, .data, .bss, .rodata)           │
│  • Parse symbol tables                                       │
│  • Parse relocation entries                                  │
└────────────────────┬────────────────────────────────────────┘
                     │
                     ▼
┌─────────────────────────────────────────────────────────────┐
│            Phase 2: Symbol Resolution                        │
│  • Build global symbol table                                 │
│  • Detect duplicate definitions                              │
│  • Resolve undefined symbols                                 │
│  • Handle weak symbols                                       │
│  • Mark symbols for export                                   │
└────────────────────┬────────────────────────────────────────┘
                     │
                     ▼
┌─────────────────────────────────────────────────────────────┐
│            Phase 3: Section Layout                           │
│  • Merge compatible sections from all objects                │
│  • Assign virtual addresses to sections                      │
│  • Calculate section alignments                              │
│  • Compute symbol addresses                                  │
│  • Determine total memory layout                             │
└────────────────────┬────────────────────────────────────────┘
                     │
                     ▼
┌─────────────────────────────────────────────────────────────┐
│          Phase 4: Relocation Processing                      │
│  • Process all relocation entries                            │
│  • Calculate relocation values                               │
│  • Patch instructions with final addresses                   │
│  • Validate relocation ranges                                │
│  • Handle platform-specific relocations                      │
└────────────────────┬────────────────────────────────────────┘
                     │
                     ▼
┌─────────────────────────────────────────────────────────────┐
│           Phase 5: Executable Generation                     │
│  • Create executable header (ELF/Mach-O/PE)                 │
│  • Write program headers / load commands                     │
│  • Write section data                                        │
│  • Write symbol table (optional)                             │
│  • Set entry point (_start → main)                          │
│  • Set file permissions (executable bit)                     │
└────────────────────┬────────────────────────────────────────┘
                     │
                     ▼
┌─────────────────────────────────────────────────────────────┐
│              Output: Executable Binary                       │
│                  (ready to run)                              │
└─────────────────────────────────────────────────────────────┘
```

### Core Data Structures

```c
// Unified object file representation
typedef struct {
    char* filename;
    platform_format_t format;       // ELF, MACH_O, PE_COFF

    // Sections
    linker_section_t* sections;
    int section_count;

    // Symbols
    linker_symbol_t* symbols;
    int symbol_count;

    // Relocations
    linker_relocation_t* relocations;
    int relocation_count;

    // Raw data
    uint8_t* raw_data;
    size_t raw_size;
} linker_object_t;

// Section representation
typedef struct {
    char* name;                     // ".text", ".data", etc.
    section_type_t type;            // TEXT, DATA, BSS, RODATA
    uint8_t* data;                  // Section data
    size_t size;                    // Section size
    size_t alignment;               // Required alignment
    uint64_t vaddr;                 // Virtual address (assigned during layout)
    uint32_t flags;                 // Read/write/execute permissions
    int object_index;               // Which object file owns this section
} linker_section_t;

// Symbol representation
typedef struct {
    char* name;                     // Symbol name
    symbol_type_t type;             // FUNC, OBJECT, NOTYPE
    symbol_binding_t binding;       // LOCAL, GLOBAL, WEAK
    int section_index;              // Section containing symbol (-1 if undefined)
    uint64_t value;                 // Symbol value/offset
    uint64_t size;                  // Symbol size
    uint64_t final_address;         // Final address (computed during layout)
    bool is_defined;                // True if defined in some object
    int defining_object;            // Index of object that defines this symbol
} linker_symbol_t;

// Relocation representation
typedef struct {
    uint64_t offset;                // Offset in section
    relocation_type_t type;         // R_X86_64_PC32, R_AARCH64_CALL26, etc.
    int symbol_index;               // Index into symbol table
    int64_t addend;                 // Addend for calculation
    int section_index;              // Section being relocated
    int object_index;               // Source object file
} linker_relocation_t;

// Global linker context
typedef struct {
    // Input
    linker_object_t* objects;
    int object_count;

    // Symbol resolution
    linker_symbol_t* global_symbols;
    int global_symbol_count;
    hash_table_t* symbol_lookup;    // Quick name → symbol lookup

    // Layout
    linker_section_t* merged_sections;
    int merged_section_count;
    uint64_t base_address;          // Base load address
    uint64_t total_size;            // Total memory footprint

    // Output
    uint8_t* executable_data;
    size_t executable_size;
    uint64_t entry_point;           // Address of _start

    // Options
    linker_options_t options;
} linker_context_t;

// Platform-specific enums
typedef enum {
    PLATFORM_FORMAT_ELF,
    PLATFORM_FORMAT_MACH_O,
    PLATFORM_FORMAT_PE_COFF
} platform_format_t;

typedef enum {
    SECTION_TYPE_TEXT,
    SECTION_TYPE_DATA,
    SECTION_TYPE_BSS,
    SECTION_TYPE_RODATA
} section_type_t;

typedef enum {
    SYMBOL_TYPE_NOTYPE,
    SYMBOL_TYPE_FUNC,
    SYMBOL_TYPE_OBJECT,
    SYMBOL_TYPE_SECTION
} symbol_type_t;

typedef enum {
    SYMBOL_BINDING_LOCAL,
    SYMBOL_BINDING_GLOBAL,
    SYMBOL_BINDING_WEAK
} symbol_binding_t;

// Relocation types (unified across platforms)
typedef enum {
    // x86-64 relocations
    RELOC_X64_NONE,
    RELOC_X64_64,           // 64-bit absolute address
    RELOC_X64_PC32,         // 32-bit PC-relative
    RELOC_X64_PLT32,        // PLT-relative (function calls)
    RELOC_X64_GOTPCREL,     // GOT-relative

    // ARM64 relocations
    RELOC_ARM64_NONE,
    RELOC_ARM64_ABS64,      // 64-bit absolute
    RELOC_ARM64_CALL26,     // 26-bit PC-relative call (BL)
    RELOC_ARM64_JUMP26,     // 26-bit PC-relative jump (B)
    RELOC_ARM64_ADR_PREL_PG_HI21,   // Page-relative ADR
    RELOC_ARM64_ADD_ABS_LO12_NC,    // Low 12 bits

    // Common
    RELOC_RELATIVE,         // Relative to load address
} relocation_type_t;
```

---

## Implementation Phases

### Phase 1: Object File Parsing Infrastructure (2 weeks)

#### 1.1: Object File Reader Interface

**New Files:**
- `src/native/linker_core.h` - Core linker data structures
- `src/native/linker_core.c` - Core linker implementation
- `src/native/object_reader.h` - Object file reader interface
- `src/native/object_reader.c` - Common object file reading logic

**Functionality:**
```c
// Main API
linker_object_t* linker_read_object(const char* filename);
void linker_free_object(linker_object_t* obj);

// Auto-detect format and parse
platform_format_t linker_detect_format(const char* filename);
```

**Implementation Steps:**
1. Create unified object file structure
2. Implement format detection (magic numbers)
3. Define common section/symbol/relocation representations
4. Create memory management for object data

**Testing:**
- Read object files generated by Sox compiler
- Validate section count and names
- Verify symbol table parsing
- Check relocation entries

---

#### 1.2: ELF Object File Reader

**New Files:**
- `src/native/elf_reader.h` - ELF-specific reader
- `src/native/elf_reader.c` - ELF parsing implementation

**Functionality:**
```c
linker_object_t* elf_read_object(const char* filename);
bool elf_parse_sections(linker_object_t* obj, uint8_t* data);
bool elf_parse_symbols(linker_object_t* obj, uint8_t* data);
bool elf_parse_relocations(linker_object_t* obj, uint8_t* data);
```

**ELF-Specific Parsing:**
- Parse ELF64 header
- Read section headers
- Extract section data
- Parse `.symtab` (symbol table)
- Parse `.strtab` (string table)
- Parse `.rela.text` (relocations with addend)
- Handle `.rela.data` (data relocations)

**Relocation Type Mapping:**
```c
relocation_type_t elf_map_relocation_type(uint32_t elf_type) {
    switch (elf_type) {
        case R_X86_64_64:      return RELOC_X64_64;
        case R_X86_64_PC32:    return RELOC_X64_PC32;
        case R_X86_64_PLT32:   return RELOC_X64_PLT32;
        case R_AARCH64_CALL26: return RELOC_ARM64_CALL26;
        // ... more mappings
    }
}
```

**Testing:**
- Parse ELF files generated by `elf_writer.c`
- Round-trip test: generate → parse → verify
- Test with multi-section objects
- Verify relocation entries match expected

---

#### 1.3: Mach-O Object File Reader

**New Files:**
- `src/native/macho_reader.h` - Mach-O-specific reader
- `src/native/macho_reader.c` - Mach-O parsing implementation

**Functionality:**
```c
linker_object_t* macho_read_object(const char* filename);
bool macho_parse_load_commands(linker_object_t* obj, uint8_t* data);
bool macho_parse_sections(linker_object_t* obj, uint8_t* data);
bool macho_parse_symbols(linker_object_t* obj, uint8_t* data);
bool macho_parse_relocations(linker_object_t* obj, uint8_t* data);
```

**Mach-O-Specific Parsing:**
- Parse Mach-O header (`mach_header_64`)
- Read load commands (`LC_SEGMENT_64`, `LC_SYMTAB`)
- Extract sections from segments
- Parse symbol table (nlist_64)
- Parse relocations (relocation_info)

**Relocation Type Mapping:**
```c
relocation_type_t macho_map_relocation_type(uint32_t macho_type) {
    switch (macho_type) {
        case ARM64_RELOC_BRANCH26:       return RELOC_ARM64_CALL26;
        case ARM64_RELOC_PAGE21:         return RELOC_ARM64_ADR_PREL_PG_HI21;
        case ARM64_RELOC_PAGEOFF12:      return RELOC_ARM64_ADD_ABS_LO12_NC;
        // ... more mappings
    }
}
```

**Testing:**
- Parse Mach-O files generated by `macho_writer.c`
- Round-trip test: generate → parse → verify
- Test ARM64 relocation parsing
- Verify section alignment handling

---

### Phase 2: Symbol Resolution Engine (2 weeks)

#### 2.1: Global Symbol Table

**New Files:**
- `src/native/symbol_resolver.h` - Symbol resolution interface
- `src/native/symbol_resolver.c` - Symbol resolution implementation

**Functionality:**
```c
// Create symbol resolver
symbol_resolver_t* symbol_resolver_new(void);
void symbol_resolver_free(symbol_resolver_t* resolver);

// Add object files
void symbol_resolver_add_object(symbol_resolver_t* resolver,
                                 linker_object_t* obj,
                                 int obj_index);

// Resolve symbols
bool symbol_resolver_resolve(symbol_resolver_t* resolver);

// Lookup
linker_symbol_t* symbol_resolver_lookup(symbol_resolver_t* resolver,
                                        const char* name);
```

**Symbol Resolution Algorithm:**

```
Algorithm: resolve_symbols(objects)

    // Phase 1: Collect defined symbols
    global_symbols ← empty hash table

    for each object in objects:
        for each symbol in object.symbols:
            if symbol.binding == GLOBAL or symbol.binding == WEAK:
                if symbol.is_defined:
                    if symbol.name in global_symbols:
                        existing ← global_symbols[symbol.name]
                        if existing.binding == GLOBAL and symbol.binding == GLOBAL:
                            error("Duplicate definition: " + symbol.name)
                        else if symbol.binding == GLOBAL:
                            // Global overrides weak
                            global_symbols[symbol.name] ← symbol
                    else:
                        global_symbols[symbol.name] ← symbol

    // Phase 2: Resolve undefined symbols
    for each object in objects:
        for each symbol in object.symbols:
            if not symbol.is_defined:
                if symbol.name in global_symbols:
                    // Found definition
                    symbol.resolved_to ← global_symbols[symbol.name]
                else:
                    // Check if it's from runtime library
                    if is_runtime_symbol(symbol.name):
                        mark_as_runtime(symbol)
                    else:
                        error("Undefined symbol: " + symbol.name)

    return success
```

**Error Handling:**
```c
// Clear error messages
typedef struct {
    error_type_t type;
    char* message;
    char* symbol_name;
    char* object_file;
    int line_number;
} linker_error_t;

// Error types
LINKER_ERROR_UNDEFINED_SYMBOL
LINKER_ERROR_DUPLICATE_DEFINITION
LINKER_ERROR_WEAK_SYMBOL_CONFLICT
LINKER_ERROR_TYPE_MISMATCH
```

**Testing:**
- Single object with all defined symbols → success
- Two objects with complementary symbols → success
- Undefined symbol → error with clear message
- Duplicate global symbols → error
- Weak symbol override → success
- Runtime library symbols → success

---

#### 2.2: Runtime Library Symbol Resolution

**Integration with Runtime:**
```c
// Runtime symbol declarations
static const runtime_symbol_t runtime_symbols[] = {
    {"sox_runtime_add", SYMBOL_TYPE_FUNC},
    {"sox_runtime_multiply", SYMBOL_TYPE_FUNC},
    {"sox_runtime_print", SYMBOL_TYPE_FUNC},
    {"sox_runtime_alloc", SYMBOL_TYPE_FUNC},
    // ... more runtime functions
};

bool is_runtime_symbol(const char* name) {
    for (int i = 0; i < runtime_symbol_count; i++) {
        if (strcmp(name, runtime_symbols[i].name) == 0) {
            return true;
        }
    }
    return false;
}
```

**Linking Runtime Library:**
- Option 1: Static linking - copy runtime code into executable
- Option 2: Dynamic linking - reference external libsox_runtime.so/.dylib
- Option 3: Embed runtime as object file in linker

**Implementation:** Start with Option 1 (static linking)

---

### Phase 3: Section Layout & Address Assignment (2 weeks)

#### 3.1: Section Merging

**New Files:**
- `src/native/section_layout.h` - Section layout interface
- `src/native/section_layout.c` - Section layout implementation

**Functionality:**
```c
// Section layout context
typedef struct {
    merged_section_t* sections;
    int section_count;
    uint64_t base_address;
    uint64_t current_address;
} section_layout_t;

section_layout_t* section_layout_new(uint64_t base_address);
void section_layout_add_sections(section_layout_t* layout,
                                  linker_object_t* obj);
void section_layout_compute(section_layout_t* layout);
void section_layout_free(section_layout_t* layout);
```

**Section Merging Strategy:**
```
Algorithm: merge_sections(objects)

    merged_sections ← {".text", ".data", ".rodata", ".bss"}

    for each object in objects:
        for each section in object.sections:
            merged ← find_or_create_merged_section(section.name)

            // Align section data
            align_to(merged.size, section.alignment)

            // Append section data
            append_data(merged, section.data, section.size)

            // Track which object contributed
            record_contribution(merged, object, section)

    return merged_sections
```

**Alignment Handling:**
```c
uint64_t align_to(uint64_t value, size_t alignment) {
    if (alignment == 0) return value;
    return (value + alignment - 1) & ~(alignment - 1);
}
```

---

#### 3.2: Virtual Address Assignment

**Address Space Layout (Linux ELF):**
```
Base Address: 0x400000 (typical for x86-64)

┌─────────────────────────────────────┐
│  0x400000: ELF Header               │
├─────────────────────────────────────┤
│  0x400040: Program Headers          │
├─────────────────────────────────────┤
│  0x401000: .text (code)             │  R-X
│            [aligned to 4KB]          │
├─────────────────────────────────────┤
│  0x500000: .rodata (constants)      │  R--
│            [aligned to 4KB]          │
├─────────────────────────────────────┤
│  0x600000: .data (initialized data) │  RW-
│            [aligned to 4KB]          │
├─────────────────────────────────────┤
│  0x601000: .bss (uninitialized)     │  RW-
│            [aligned to 4KB]          │
└─────────────────────────────────────┘
```

**Address Space Layout (macOS Mach-O ARM64):**
```
Base Address: 0x100000000 (typical for ARM64)

┌─────────────────────────────────────┐
│  0x100000000: Mach-O Header         │
├─────────────────────────────────────┤
│  0x100000020: Load Commands         │
├─────────────────────────────────────┤
│  0x100001000: __TEXT,__text         │  R-X
│                [aligned to 16KB]     │
├─────────────────────────────────────┤
│  0x100010000: __DATA,__data         │  RW-
│                [aligned to 16KB]     │
├─────────────────────────────────────┤
│  0x100011000: __DATA,__bss          │  RW-
└─────────────────────────────────────┘
```

**Implementation:**
```c
void section_layout_assign_addresses(section_layout_t* layout) {
    uint64_t addr = layout->base_address;

    // Skip headers (approximate)
    addr += 0x1000;

    // .text section (code)
    for each section in layout->sections:
        if section.type == SECTION_TYPE_TEXT:
            addr = align_to(addr, 4096);  // Page alignment
            section.vaddr = addr;
            addr += section.size;

    // .rodata section (constants)
    for each section in layout->sections:
        if section.type == SECTION_TYPE_RODATA:
            addr = align_to(addr, 4096);
            section.vaddr = addr;
            addr += section.size;

    // .data section
    for each section in layout->sections:
        if section.type == SECTION_TYPE_DATA:
            addr = align_to(addr, 4096);
            section.vaddr = addr;
            addr += section.size;

    // .bss section (zero-initialized)
    for each section in layout->sections:
        if section.type == SECTION_TYPE_BSS:
            addr = align_to(addr, 4096);
            section.vaddr = addr;
            addr += section.size;

    layout->total_size = addr - layout->base_address;
}
```

**Symbol Address Calculation:**
```c
uint64_t symbol_get_final_address(linker_symbol_t* symbol,
                                   section_layout_t* layout) {
    if (!symbol->is_defined) {
        return 0;  // Undefined symbol
    }

    linker_section_t* section = layout->sections[symbol->section_index];
    return section->vaddr + symbol->value;
}
```

**Testing:**
- Single section layout → correct addresses
- Multiple sections → proper alignment
- Symbol address calculation → matches expectations
- Page alignment → all sections page-aligned

---

### Phase 4: Relocation Processing (3 weeks)

#### 4.1: Relocation Processor

**New Files:**
- `src/native/relocation_processor.h` - Relocation processing interface
- `src/native/relocation_processor.c` - Relocation implementation

**Functionality:**
```c
typedef struct {
    linker_context_t* context;
    section_layout_t* layout;
    symbol_resolver_t* symbols;
} relocation_processor_t;

relocation_processor_t* relocation_processor_new(linker_context_t* ctx);
bool relocation_processor_process_all(relocation_processor_t* proc);
void relocation_processor_free(relocation_processor_t* proc);
```

**Relocation Processing Algorithm:**
```
Algorithm: process_relocations(objects, layout, symbols)

    for each object in objects:
        for each relocation in object.relocations:
            // Get target symbol
            target_symbol ← symbols.lookup(relocation.symbol_name)

            if not target_symbol.is_defined:
                error("Undefined symbol in relocation: " + relocation.symbol_name)

            // Calculate final address
            S ← target_symbol.final_address
            A ← relocation.addend
            P ← relocation.offset + section.vaddr

            // Apply relocation based on type
            value ← calculate_relocation_value(relocation.type, S, A, P)

            // Patch the code/data
            patch_location(section.data, relocation.offset, value, relocation.type)

            // Validate range
            validate_relocation_range(value, relocation.type)
```

**Relocation Calculations:**

| Relocation Type | Formula | Description |
|-----------------|---------|-------------|
| `R_X86_64_64` | `S + A` | 64-bit absolute address |
| `R_X86_64_PC32` | `S + A - P` | 32-bit PC-relative |
| `R_X86_64_PLT32` | `S + A - P` | PLT-relative (function call) |
| `R_AARCH64_CALL26` | `(S + A - P) >> 2` | 26-bit PC-relative call |
| `R_AARCH64_JUMP26` | `(S + A - P) >> 2` | 26-bit PC-relative jump |
| `R_AARCH64_ABS64` | `S + A` | 64-bit absolute |

Where:
- `S` = Symbol address
- `A` = Addend
- `P` = Relocation offset (place being patched)

---

#### 4.2: Instruction Patching

**New Files:**
- `src/native/instruction_patcher.h` - Instruction patching interface
- `src/native/instruction_patcher.c` - Platform-specific patching

**x86-64 Patching:**
```c
void patch_x64_imm32(uint8_t* code, size_t offset, int32_t value) {
    *(int32_t*)(code + offset) = value;
}

void patch_x64_imm64(uint8_t* code, size_t offset, int64_t value) {
    *(int64_t*)(code + offset) = value;
}

// For PC-relative calls/jumps
void patch_x64_rel32(uint8_t* code, size_t offset, int32_t value) {
    *(int32_t*)(code + offset) = value;
}
```

**ARM64 Patching:**
```c
// Patch BL/B instruction (26-bit immediate)
void patch_arm64_branch26(uint8_t* code, size_t offset, int32_t value) {
    if (value < -0x2000000 || value > 0x1FFFFFF) {
        error("Branch offset out of range");
    }

    uint32_t* insn = (uint32_t*)(code + offset);
    uint32_t imm26 = (value >> 2) & 0x3FFFFFF;
    *insn = (*insn & 0xFC000000) | imm26;
}

// Patch ADRP instruction (21-bit page offset)
void patch_arm64_adrp(uint8_t* code, size_t offset, uint64_t target, uint64_t pc) {
    int64_t page_offset = (target >> 12) - (pc >> 12);

    if (page_offset < -0x100000 || page_offset > 0xFFFFF) {
        error("ADRP offset out of range");
    }

    uint32_t* insn = (uint32_t*)(code + offset);
    uint32_t immlo = (page_offset & 0x3) << 29;
    uint32_t immhi = ((page_offset >> 2) & 0x7FFFF) << 5;
    *insn = (*insn & 0x9F00001F) | immlo | immhi;
}

// Patch ADD instruction (12-bit immediate)
void patch_arm64_add_imm12(uint8_t* code, size_t offset, uint16_t imm12) {
    if (imm12 > 0xFFF) {
        error("Immediate out of range");
    }

    uint32_t* insn = (uint32_t*)(code + offset);
    *insn = (*insn & 0xFFC003FF) | ((imm12 & 0xFFF) << 10);
}
```

**Range Validation:**
```c
bool validate_relocation_range(int64_t value, relocation_type_t type) {
    switch (type) {
        case RELOC_X64_PC32:
        case RELOC_X64_PLT32:
            if (value < INT32_MIN || value > INT32_MAX) {
                error("32-bit relocation overflow");
                return false;
            }
            break;

        case RELOC_ARM64_CALL26:
        case RELOC_ARM64_JUMP26:
            if (value < -0x8000000 || value > 0x7FFFFFF) {
                error("26-bit branch out of range");
                return false;
            }
            break;
    }
    return true;
}
```

**Testing:**
- Each relocation type individually
- Cross-section relocations (code → data)
- External symbol relocations
- Range overflow detection
- Alignment validation

---

### Phase 5: Executable Generation (3 weeks)

#### 5.1: ELF Executable Writer

**New Files:**
- `src/native/elf_executable.h` - ELF executable generation
- `src/native/elf_executable.c` - ELF executable implementation

**ELF Executable Structure:**
```
┌─────────────────────────────────────┐
│ ELF Header (64 bytes)               │
│  - e_type = ET_EXEC or ET_DYN       │
│  - e_machine = EM_X86_64 or EM_AARCH64 │
│  - e_entry = entry point address    │
│  - e_phoff = program header offset  │
│  - e_shoff = section header offset  │
├─────────────────────────────────────┤
│ Program Headers (PHDRs)             │
│  - PT_LOAD (R-X) for .text         │
│  - PT_LOAD (RW-) for .data/.bss    │
│  - PT_INTERP (for dynamic linking)  │
├─────────────────────────────────────┤
│ Section Data                        │
│  - .text (code)                     │
│  - .rodata (constants)              │
│  - .data (initialized data)         │
│  - .bss (zero-initialized)          │
├─────────────────────────────────────┤
│ Section Headers (optional)          │
│  - Metadata for debugging           │
└─────────────────────────────────────┘
```

**Implementation:**
```c
bool elf_write_executable(const char* output_path,
                           linker_context_t* context) {
    // Create ELF header
    Elf64_Ehdr ehdr = {0};
    ehdr.e_ident[EI_MAG0] = ELFMAG0;
    ehdr.e_ident[EI_MAG1] = ELFMAG1;
    ehdr.e_ident[EI_MAG2] = ELFMAG2;
    ehdr.e_ident[EI_MAG3] = ELFMAG3;
    ehdr.e_ident[EI_CLASS] = ELFCLASS64;
    ehdr.e_ident[EI_DATA] = ELFDATA2LSB;
    ehdr.e_ident[EI_VERSION] = EV_CURRENT;
    ehdr.e_type = ET_EXEC;  // or ET_DYN for PIE
    ehdr.e_machine = EM_X86_64;  // or EM_AARCH64
    ehdr.e_version = EV_CURRENT;
    ehdr.e_entry = context->entry_point;
    ehdr.e_phoff = sizeof(Elf64_Ehdr);
    ehdr.e_shoff = 0;  // No section headers for minimal executable
    ehdr.e_ehsize = sizeof(Elf64_Ehdr);
    ehdr.e_phentsize = sizeof(Elf64_Phdr);
    ehdr.e_phnum = 2;  // Two segments: code and data

    // Create program headers
    Elf64_Phdr phdr_text = {0};
    phdr_text.p_type = PT_LOAD;
    phdr_text.p_flags = PF_R | PF_X;  // Read + Execute
    phdr_text.p_offset = /* file offset of .text */;
    phdr_text.p_vaddr = /* virtual address of .text */;
    phdr_text.p_paddr = phdr_text.p_vaddr;
    phdr_text.p_filesz = /* size of .text in file */;
    phdr_text.p_memsz = /* size of .text in memory */;
    phdr_text.p_align = 4096;  // Page alignment

    Elf64_Phdr phdr_data = {0};
    phdr_data.p_type = PT_LOAD;
    phdr_data.p_flags = PF_R | PF_W;  // Read + Write
    phdr_data.p_offset = /* file offset of .data */;
    phdr_data.p_vaddr = /* virtual address of .data */;
    phdr_data.p_paddr = phdr_data.p_vaddr;
    phdr_data.p_filesz = /* size of .data in file */;
    phdr_data.p_memsz = /* size including .bss */;
    phdr_data.p_align = 4096;

    // Write to file
    FILE* f = fopen(output_path, "wb");
    fwrite(&ehdr, sizeof(ehdr), 1, f);
    fwrite(&phdr_text, sizeof(phdr_text), 1, f);
    fwrite(&phdr_data, sizeof(phdr_data), 1, f);

    // Write section data
    write_section_data(f, context);

    fclose(f);

    // Set executable permission
    chmod(output_path, 0755);

    return true;
}
```

**Entry Point Generation:**
```c
// Generate _start function that calls main
void elf_generate_entry_point(linker_context_t* context) {
    // _start:
    //   xor rbp, rbp           ; Clear frame pointer
    //   call main              ; Call main function
    //   mov rdi, rax           ; Exit code from main
    //   mov rax, 60            ; sys_exit
    //   syscall                ; Exit

    uint8_t start_code[] = {
        0x48, 0x31, 0xED,              // xor rbp, rbp
        0xE8, 0x00, 0x00, 0x00, 0x00,  // call main (offset patched later)
        0x48, 0x89, 0xC7,              // mov rdi, rax
        0xB8, 0x3C, 0x00, 0x00, 0x00,  // mov eax, 60
        0x0F, 0x05                      // syscall
    };

    // Patch call offset
    int32_t main_offset = get_symbol_address("main") -
                          (context->entry_point + 7);
    *(int32_t*)(&start_code[4]) = main_offset;

    // Add to .text section
    prepend_to_section(context, ".text", start_code, sizeof(start_code));
}
```

---

#### 5.2: Mach-O Executable Writer

**New Files:**
- `src/native/macho_executable.h` - Mach-O executable generation
- `src/native/macho_executable.c` - Mach-O executable implementation

**Mach-O Executable Structure:**
```
┌─────────────────────────────────────┐
│ Mach-O Header                       │
│  - magic = MH_MAGIC_64              │
│  - cputype = CPU_TYPE_ARM64         │
│  - filetype = MH_EXECUTE            │
│  - ncmds = number of load commands  │
├─────────────────────────────────────┤
│ Load Commands                       │
│  - LC_SEGMENT_64 (__TEXT)           │
│  - LC_SEGMENT_64 (__DATA)           │
│  - LC_MAIN (entry point)            │
│  - LC_LOAD_DYLINKER                 │
├─────────────────────────────────────┤
│ __TEXT Segment                      │
│  - __text section (code)            │
│  - __const section (rodata)         │
├─────────────────────────────────────┤
│ __DATA Segment                      │
│  - __data section                   │
│  - __bss section                    │
└─────────────────────────────────────┘
```

**Implementation:**
```c
bool macho_write_executable(const char* output_path,
                             linker_context_t* context) {
    // Create Mach-O header
    struct mach_header_64 header = {0};
    header.magic = MH_MAGIC_64;
    header.cputype = CPU_TYPE_ARM64;
    header.cpusubtype = CPU_SUBTYPE_ARM64_ALL;
    header.filetype = MH_EXECUTE;
    header.ncmds = 3;  // __TEXT, __DATA, LC_MAIN
    header.sizeofcmds = /* calculate total size */;
    header.flags = MH_NOUNDEFS | MH_PIE;

    // Create load commands
    struct segment_command_64 text_segment = {0};
    text_segment.cmd = LC_SEGMENT_64;
    text_segment.cmdsize = sizeof(struct segment_command_64) +
                           sizeof(struct section_64);
    strncpy(text_segment.segname, "__TEXT", 16);
    text_segment.vmaddr = context->base_address;
    text_segment.vmsize = /* size of __TEXT */;
    text_segment.fileoff = 0;
    text_segment.filesize = /* size in file */;
    text_segment.maxprot = VM_PROT_READ | VM_PROT_EXECUTE;
    text_segment.initprot = VM_PROT_READ | VM_PROT_EXECUTE;
    text_segment.nsects = 1;
    text_segment.flags = 0;

    // ... similar for __DATA segment

    // LC_MAIN for entry point
    struct entry_point_command main_cmd = {0};
    main_cmd.cmd = LC_MAIN;
    main_cmd.cmdsize = sizeof(struct entry_point_command);
    main_cmd.entryoff = context->entry_point - context->base_address;
    main_cmd.stacksize = 0;

    // Write to file
    FILE* f = fopen(output_path, "wb");
    fwrite(&header, sizeof(header), 1, f);
    fwrite(&text_segment, text_segment.cmdsize, 1, f);
    fwrite(&data_segment, data_segment.cmdsize, 1, f);
    fwrite(&main_cmd, sizeof(main_cmd), 1, f);

    // Write section data
    write_section_data(f, context);

    fclose(f);
    chmod(output_path, 0755);

    return true;
}
```

**Entry Point for macOS:**
```c
// macOS uses _main directly, no _start needed
void macho_set_entry_point(linker_context_t* context) {
    linker_symbol_t* main_sym = symbol_resolver_lookup(context->symbols, "_main");
    if (!main_sym) {
        error("No _main symbol found");
    }
    context->entry_point = main_sym->final_address;
}
```

---

### Phase 6: Integration & Testing (2 weeks)

#### 6.1: Main Linker API

**Updated Files:**
- `src/lib/linker.h` - Add custom linker option
- `src/lib/linker.c` - Integrate custom linker

**New API:**
```c
// Linker mode
typedef enum {
    LINKER_MODE_SYSTEM,     // Use system linker (current)
    LINKER_MODE_CUSTOM,     // Use custom linker
    LINKER_MODE_AUTO        // Auto-select based on complexity
} linker_mode_t;

// Enhanced options
typedef struct {
    // ... existing fields ...
    linker_mode_t mode;     // Which linker to use
    bool verbose_linking;   // Print linking details
    bool keep_objects;      // Keep intermediate object files
} linker_options_t;

// Main linking entry point
int linker_link(const linker_options_t* options);
```

**Implementation:**
```c
int linker_link(const linker_options_t* options) {
    if (options->mode == LINKER_MODE_SYSTEM) {
        return linker_invoke_system(options);
    } else if (options->mode == LINKER_MODE_CUSTOM) {
        return linker_link_custom(options);
    } else {
        // Auto mode: use custom for simple cases, system for complex
        if (is_simple_link_job(options)) {
            return linker_link_custom(options);
        } else {
            return linker_invoke_system(options);
        }
    }
}

int linker_link_custom(const linker_options_t* options) {
    // Phase 1: Load object files
    linker_context_t* context = linker_context_new();

    for each object_file in options->input_files:
        linker_object_t* obj = linker_read_object(object_file);
        linker_context_add_object(context, obj);

    // Phase 2: Resolve symbols
    if (!symbol_resolver_resolve(context->symbols)) {
        return 1;  // Error
    }

    // Phase 3: Layout sections
    section_layout_compute(context->layout);

    // Phase 4: Process relocations
    if (!relocation_processor_process_all(context->relocations)) {
        return 1;  // Error
    }

    // Phase 5: Generate executable
    bool success = false;
    if (context->target_platform == PLATFORM_LINUX) {
        success = elf_write_executable(options->output_file, context);
    } else if (context->target_platform == PLATFORM_MACOS) {
        success = macho_write_executable(options->output_file, context);
    }

    linker_context_free(context);
    return success ? 0 : 1;
}
```

---

#### 6.2: Comprehensive Testing

**Test Suite Structure:**
```
src/test/linker/
├── test_object_reader.c        # Object file parsing tests
├── test_symbol_resolver.c      # Symbol resolution tests
├── test_section_layout.c       # Section merging and layout tests
├── test_relocation.c           # Relocation processing tests
├── test_executable_gen.c       # Executable generation tests
└── integration/
    ├── single_object.sox       # Single object file linking
    ├── multi_object.sox        # Multiple object files
    ├── cross_module_call.sox   # Cross-module function calls
    ├── global_vars.sox         # Global variable references
    └── runtime_calls.sox       # Runtime library integration
```

**Unit Tests:**
```c
// Test symbol resolution
void test_symbol_resolution_simple(void) {
    symbol_resolver_t* resolver = symbol_resolver_new();

    // Add object with defined symbol
    linker_object_t* obj1 = create_test_object();
    add_test_symbol(obj1, "foo", SYMBOL_BINDING_GLOBAL, true);
    symbol_resolver_add_object(resolver, obj1, 0);

    // Add object with undefined symbol
    linker_object_t* obj2 = create_test_object();
    add_test_symbol(obj2, "foo", SYMBOL_BINDING_GLOBAL, false);
    symbol_resolver_add_object(resolver, obj2, 1);

    // Resolve
    bool success = symbol_resolver_resolve(resolver);
    assert(success == true);

    // Verify resolution
    linker_symbol_t* sym = symbol_resolver_lookup(resolver, "foo");
    assert(sym != NULL);
    assert(sym->is_defined == true);

    symbol_resolver_free(resolver);
}

// Test duplicate symbol detection
void test_symbol_duplicate_detection(void) {
    symbol_resolver_t* resolver = symbol_resolver_new();

    linker_object_t* obj1 = create_test_object();
    add_test_symbol(obj1, "foo", SYMBOL_BINDING_GLOBAL, true);
    symbol_resolver_add_object(resolver, obj1, 0);

    linker_object_t* obj2 = create_test_object();
    add_test_symbol(obj2, "foo", SYMBOL_BINDING_GLOBAL, true);
    symbol_resolver_add_object(resolver, obj2, 1);

    // Should fail due to duplicate
    bool success = symbol_resolver_resolve(resolver);
    assert(success == false);

    symbol_resolver_free(resolver);
}
```

**Integration Tests:**
```bash
#!/bin/bash

# Compile Sox programs to object files
sox --native test1.sox -o test1.o
sox --native test2.sox -o test2.o

# Link with custom linker
sox --link test1.o test2.o -o program --use-custom-linker

# Run and verify output
./program > output.txt
diff output.txt expected.txt
```

---

## File Structure

```
src/native/
├── linker_core.h              # Core linker data structures
├── linker_core.c              # Core linker implementation
├── object_reader.h            # Object file reader interface
├── object_reader.c            # Object file reading logic
├── elf_reader.h               # ELF-specific reader
├── elf_reader.c               # ELF parsing
├── macho_reader.h             # Mach-O-specific reader
├── macho_reader.c             # Mach-O parsing
├── symbol_resolver.h          # Symbol resolution interface
├── symbol_resolver.c          # Symbol resolution logic
├── section_layout.h           # Section layout interface
├── section_layout.c           # Section merging and addressing
├── relocation_processor.h     # Relocation processing interface
├── relocation_processor.c     # Relocation logic
├── instruction_patcher.h      # Instruction patching interface
├── instruction_patcher.c      # Platform-specific patching
├── elf_executable.h           # ELF executable generation
├── elf_executable.c           # ELF executable writer
├── macho_executable.h         # Mach-O executable generation
├── macho_executable.c         # Mach-O executable writer
├── elf_writer.h               # Existing object file writer
├── elf_writer.c               # (already exists)
├── macho_writer.h             # Existing object file writer
├── macho_writer.c             # (already exists)
└── ... (other existing files)

src/lib/
├── linker.h                   # Linker API (enhanced)
├── linker.c                   # Linker orchestration (enhanced)
└── ... (other existing files)

src/test/linker/
├── test_object_reader.c
├── test_symbol_resolver.c
├── test_section_layout.c
├── test_relocation.c
├── test_executable_gen.c
└── integration/
    ├── single_object.sox
    ├── multi_object.sox
    └── ...
```

---

## Risk Analysis & Mitigation

### High Risks

**1. Incorrect Relocation Processing**
- **Impact:** Crashes, wrong calculations, data corruption
- **Mitigation:**
  - Extensive unit tests for each relocation type
  - Verification against known-good executables
  - Disassemble output and verify instruction encoding
  - Use simple test cases with predictable relocations

**2. Symbol Resolution Errors**
- **Impact:** Missing symbols, duplicate definitions, wrong bindings
- **Mitigation:**
  - Clear error messages with file/line info
  - Comprehensive symbol table validation
  - Test all symbol binding types (local, global, weak)
  - Compare with system linker output

**3. Platform-Specific ABI Violations**
- **Impact:** Executables don't run, crash at startup
- **Mitigation:**
  - Follow ELF/Mach-O specs precisely
  - Use reference executables for comparison
  - Test on real hardware (x86-64 and ARM64)
  - Validate with external tools (readelf, otool, llvm-objdump)

### Medium Risks

**1. Incomplete Relocation Type Support**
- **Impact:** Some code patterns fail to link
- **Mitigation:**
  - Start with most common relocation types
  - Fall back to system linker for unsupported types
  - Log unsupported relocations for future implementation

**2. Memory Layout Issues**
- **Impact:** Sections overlap, alignment errors, crashes
- **Mitigation:**
  - Conservative alignment (always page-align)
  - Validation passes to check for overlaps
  - Visual layout diagrams in debug mode

### Low Risks

**1. Performance**
- **Impact:** Slower than system linker
- **Mitigation:**
  - Profile and optimize hot paths
  - Use hash tables for symbol lookup
  - Acceptable for small projects

---

## Success Criteria

### Functional Requirements
- ✅ Link single object file to executable
- ✅ Link multiple object files to executable
- ✅ Resolve symbols across object files
- ✅ Process all common relocation types
- ✅ Generate valid ELF executables (Linux)
- ✅ Generate valid Mach-O executables (macOS)
- ✅ Integrate runtime library
- ✅ Handle global variables
- ✅ Support weak symbols

### Quality Requirements
- ✅ Clear error messages for linker errors
- ✅ No crashes on malformed input
- ✅ Comprehensive test coverage (>80%)
- ✅ Documentation for all public APIs

### Performance Requirements
- ✅ Link time <500ms for typical project
- ✅ Memory usage <100MB for typical project
- ✅ Comparable to system linker for small projects

---

## Timeline & Milestones

### Week 1-2: Object File Parsing
- [ ] Object file reader interface
- [ ] ELF object file reader
- [ ] Mach-O object file reader
- [ ] Unit tests for parsing
- **Milestone:** Can parse all Sox-generated object files

### Week 3-4: Symbol Resolution
- [ ] Global symbol table
- [ ] Symbol resolution algorithm
- [ ] Duplicate detection
- [ ] Weak symbol handling
- [ ] Runtime library symbols
- [ ] Unit tests for symbol resolution
- **Milestone:** Correct symbol resolution for multi-object projects

### Week 5-6: Section Layout
- [ ] Section merging
- [ ] Address assignment
- [ ] Alignment handling
- [ ] Symbol address calculation
- [ ] Unit tests for layout
- **Milestone:** Correct memory layout for merged sections

### Week 7-9: Relocation Processing
- [ ] Relocation processor
- [ ] x86-64 instruction patching
- [ ] ARM64 instruction patching
- [ ] Range validation
- [ ] Unit tests for each relocation type
- **Milestone:** Successful relocation for all supported types

### Week 10-11: Executable Generation
- [ ] ELF executable writer
- [ ] Mach-O executable writer
- [ ] Entry point generation
- [ ] File permission setting
- [ ] Unit tests for executable format
- **Milestone:** First working executable from custom linker

### Week 12: Integration & Testing
- [ ] Integrate with main linker API
- [ ] Comprehensive integration tests
- [ ] Documentation
- [ ] Performance testing
- [ ] Bug fixes
- **Milestone:** Production-ready custom linker

---

## Future Extensions

### Short Term (3-6 months)
- **Windows Support:** PE/COFF executable generation
- **Optimization:** Dead code elimination, section merging optimization
- **Debug Info:** DWARF generation for debugging

### Medium Term (6-12 months)
- **Dynamic Linking:** Generate shared libraries (.so, .dylib)
- **Incremental Linking:** Cache and relink only changed modules
- **Link-Time Optimization:** Cross-module optimizations

### Long Term (1+ years)
- **JIT Support:** In-memory linking for JIT compilation
- **WASM Support:** Link to WebAssembly modules
- **Multi-Architecture:** Fat binaries for macOS

---

## References

### Specifications
- **ELF Specification:** https://refspecs.linuxbase.org/elf/elf.pdf
- **System V AMD64 ABI:** https://refspecs.linuxbase.org/elf/x86_64-abi-0.99.pdf
- **ARM64 ABI:** https://github.com/ARM-software/abi-aa
- **Mach-O Format:** https://github.com/aidansteele/osx-abi-macho-file-format-reference
- **PE/COFF:** https://docs.microsoft.com/en-us/windows/win32/debug/pe-format

### Tools
- **readelf:** Inspect ELF files
- **objdump:** Disassemble object files
- **otool:** macOS object file tool
- **llvm-objdump:** LLVM disassembler

### Related Sox Documentation
- `/plans/2025-12-01-native-linking-system.md` - Original linking plan
- `/docs/native-codegen-implementation.md` - Native code generation
- `/src/native/README.md` - Native code generator guide

---

**Author:** Claude Code (AI Assistant)
**Date:** December 26, 2025
**Status:** Planning Document
**Next Steps:** Review plan, begin Phase 1 implementation

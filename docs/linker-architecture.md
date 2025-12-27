# Sox Custom Linker Architecture

**Date:** December 26, 2025
**Status:** Phase 1.1 Complete

---

## Linker Pipeline Overview

```
┌─────────────────────────────────────────────────────────────┐
│                    Input: Object Files                       │
│           (.o files from native code generator)              │
│                                                               │
│   obj1.o (ELF)    obj2.o (Mach-O)    obj3.o (ELF)           │
└──────────────┬────────────────┬──────────────┬───────────────┘
               │                │              │
               ▼                ▼              ▼
┌─────────────────────────────────────────────────────────────┐
│         Phase 1: Object File Parsing [1.1 ✅ | 1.2 ⏳]       │
│                                                               │
│  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐      │
│  │ ELF Reader   │  │ Mach-O Reader│  │ PE Reader    │      │
│  │   (1.2)      │  │   (1.2)      │  │   (future)   │      │
│  └──────┬───────┘  └──────┬───────┘  └──────┬───────┘      │
│         │                 │                 │                │
│         └─────────────────┴─────────────────┘                │
│                           │                                  │
│                           ▼                                  │
│              ┌─────────────────────────┐                     │
│              │  linker_object_t (1.1)  │ ← Unified format   │
│              │  - sections[]           │                     │
│              │  - symbols[]            │                     │
│              │  - relocations[]        │                     │
│              └─────────────────────────┘                     │
└─────────────────────────┬───────────────────────────────────┘
                          │
                          ▼
┌─────────────────────────────────────────────────────────────┐
│              Phase 2: Symbol Resolution [⏳]                 │
│                                                               │
│  • Build global symbol table                                 │
│  • Detect duplicate definitions                              │
│  • Resolve undefined symbols                                 │
│  • Handle weak symbols                                       │
│  • Mark symbols for export                                   │
└─────────────────────────┬───────────────────────────────────┘
                          │
                          ▼
┌─────────────────────────────────────────────────────────────┐
│              Phase 3: Section Layout [⏳]                     │
│                                                               │
│  • Merge compatible sections (.text → .text)                │
│  • Assign virtual addresses                                  │
│  • Calculate section alignments                              │
│  • Compute symbol addresses                                  │
└─────────────────────────┬───────────────────────────────────┘
                          │
                          ▼
┌─────────────────────────────────────────────────────────────┐
│           Phase 4: Relocation Processing [⏳]                │
│                                                               │
│  • Process all relocation entries                            │
│  • Calculate relocation values                               │
│  • Patch instructions with final addresses                   │
│  • Validate relocation ranges                                │
└─────────────────────────┬───────────────────────────────────┘
                          │
                          ▼
┌─────────────────────────────────────────────────────────────┐
│            Phase 5: Executable Generation [⏳]               │
│                                                               │
│  • Create executable header                                  │
│  • Write program headers / load commands                     │
│  • Write section data                                        │
│  • Set entry point                                           │
└─────────────────────────┬───────────────────────────────────┘
                          │
                          ▼
┌─────────────────────────────────────────────────────────────┐
│               Output: Executable Binary                       │
│                   (ready to run)                              │
│                                                               │
│              program (ELF/Mach-O/PE)                         │
└─────────────────────────────────────────────────────────────┘
```

**Legend:**
- ✅ Complete
- ⏳ Planned (future phases)

---

## Data Structure Relationships (Phase 1.1 ✅)

```
linker_context_t (Global context)
│
├── objects[] → linker_object_t (Multiple object files)
│   │
│   ├── filename: "obj1.o"
│   ├── format: PLATFORM_FORMAT_ELF
│   │
│   ├── sections[] → linker_section_t
│   │   ├── [0] .text    (code)
│   │   ├── [1] .rodata  (constants)
│   │   ├── [2] .data    (initialized data)
│   │   └── [3] .bss     (uninitialized data)
│   │
│   ├── symbols[] → linker_symbol_t
│   │   ├── [0] "main"     (GLOBAL, FUNC, defined in .text)
│   │   ├── [1] "foo"      (GLOBAL, FUNC, defined in .text)
│   │   └── [2] "printf"   (GLOBAL, FUNC, undefined → needs resolution)
│   │
│   └── relocations[] → linker_relocation_t
│       ├── [0] offset=0x10, type=RELOC_X64_PC32, symbol="printf"
│       └── [1] offset=0x20, type=RELOC_X64_PC32, symbol="foo"
│
├── global_symbols[] (Phase 2 - Symbol Resolution)
│   └── Merged symbol table from all objects
│
├── merged_sections[] (Phase 3 - Layout)
│   ├── .text    (merged from all objects)
│   ├── .rodata  (merged from all objects)
│   ├── .data    (merged from all objects)
│   └── .bss     (merged from all objects)
│
├── executable_data[] (Phase 5 - Output)
│   └── Final executable bytes
│
└── options
    ├── output_filename: "program"
    ├── base_address: 0x400000
    ├── verbose: true
    └── keep_debug_info: false
```

---

## Memory Layout After Linking (Phase 3)

### Linux x86-64 (ELF)

```
Virtual Address Space:
┌─────────────────────────────────────┐
│  0x400000: ELF Header               │
├─────────────────────────────────────┤
│  0x400040: Program Headers          │
├─────────────────────────────────────┤
│  0x401000: .text (merged)           │  R-X  ← Code from all objects
│            - obj1.text               │
│            - obj2.text               │
│            - obj3.text               │
│            [aligned to 4KB]          │
├─────────────────────────────────────┤
│  0x500000: .rodata (merged)         │  R--  ← Constants
│            [aligned to 4KB]          │
├─────────────────────────────────────┤
│  0x600000: .data (merged)           │  RW-  ← Initialized data
│            [aligned to 4KB]          │
├─────────────────────────────────────┤
│  0x601000: .bss (merged)            │  RW-  ← Zero-initialized
│            [aligned to 4KB]          │
└─────────────────────────────────────┘
```

### macOS ARM64 (Mach-O)

```
Virtual Address Space:
┌─────────────────────────────────────┐
│  0x100000000: Mach-O Header         │
├─────────────────────────────────────┤
│  0x100000020: Load Commands         │
├─────────────────────────────────────┤
│  0x100001000: __TEXT,__text         │  R-X  ← Code
│                [aligned to 16KB]     │
├─────────────────────────────────────┤
│  0x100010000: __DATA,__data         │  RW-  ← Data
│                [aligned to 16KB]     │
├─────────────────────────────────────┤
│  0x100011000: __DATA,__bss          │  RW-  ← BSS
└─────────────────────────────────────┘
```

---

## Symbol Resolution (Phase 2)

### Symbol Resolution Algorithm

```
For each object file:
    For each symbol:
        if symbol is GLOBAL and DEFINED:
            Add to global_symbols table
            Check for duplicates → ERROR if found

        if symbol is WEAK and DEFINED:
            Add to global_symbols table
            Can be overridden by GLOBAL symbol

For each object file:
    For each undefined symbol:
        Look up in global_symbols table
        if found:
            Link to definition
        else if runtime symbol (e.g., "printf"):
            Mark as runtime symbol
        else:
            ERROR: Undefined symbol
```

### Example

```
obj1.o symbols:
  [0] "main"   GLOBAL FUNC DEFINED   (section=.text, value=0x0)
  [1] "foo"    GLOBAL FUNC DEFINED   (section=.text, value=0x100)
  [2] "printf" GLOBAL FUNC UNDEFINED

obj2.o symbols:
  [0] "bar"    GLOBAL FUNC DEFINED   (section=.text, value=0x0)
  [1] "foo"    WEAK   FUNC DEFINED   (section=.text, value=0x50)

After Resolution:
  global_symbols:
    "main"   → obj1, section=.text, offset=0x0
    "foo"    → obj1, section=.text, offset=0x100  (GLOBAL overrides WEAK)
    "bar"    → obj2, section=.text, offset=0x0
    "printf" → RUNTIME (to be linked against libc)
```

---

## Relocation Processing (Phase 4)

### Relocation Formula

For each relocation:
```
S = Symbol address (from symbol resolution)
A = Addend (from relocation entry)
P = Place (address of relocation site)

Result = calculate_relocation(type, S, A, P)
```

### Common Relocation Types

| Type | Formula | Description | Example Use |
|------|---------|-------------|-------------|
| `R_X86_64_64` | `S + A` | 64-bit absolute address | Global variable reference |
| `R_X86_64_PC32` | `S + A - P` | 32-bit PC-relative | Function call, jump |
| `R_ARM64_CALL26` | `(S + A - P) >> 2` | 26-bit PC-relative call | BL instruction |
| `R_ARM64_ABS64` | `S + A` | 64-bit absolute | Data pointer |

### Example: x86-64 Function Call

```assembly
Before relocation:
  0x1000: call 0x0    ; Relocation: R_X86_64_PC32, symbol="foo"

After symbol resolution:
  Symbol "foo" → address 0x2000

Relocation calculation:
  S = 0x2000  (symbol address)
  A = -4      (addend, call instruction encoding)
  P = 0x1005  (address after call instruction)

  Result = S + A - P
         = 0x2000 + (-4) - 0x1005
         = 0xFF7

After relocation:
  0x1000: call 0xFF7  ; Calls to 0x2000
```

---

## Type System (Phase 1.1 ✅)

### Platform Formats

```c
PLATFORM_FORMAT_ELF       → Linux, BSD (x86-64, ARM64)
PLATFORM_FORMAT_MACH_O    → macOS, iOS (ARM64, x86-64)
PLATFORM_FORMAT_PE_COFF   → Windows (x86-64) [future]
```

### Section Types

```c
SECTION_TYPE_TEXT     → Executable code (.text)
SECTION_TYPE_DATA     → Initialized data (.data)
SECTION_TYPE_BSS      → Zero-initialized data (.bss)
SECTION_TYPE_RODATA   → Read-only data (.rodata, __const)
```

### Symbol Types

```c
SYMBOL_TYPE_FUNC      → Function
SYMBOL_TYPE_OBJECT    → Data object (variable)
SYMBOL_TYPE_SECTION   → Section symbol
SYMBOL_TYPE_NOTYPE    → No type specified
```

### Symbol Binding

```c
SYMBOL_BINDING_LOCAL  → Visible only in this object
SYMBOL_BINDING_GLOBAL → Visible to all objects
SYMBOL_BINDING_WEAK   → Can be overridden by global symbol
```

### Relocation Types

**x86-64:**
```c
RELOC_X64_64          → 64-bit absolute address
RELOC_X64_PC32        → 32-bit PC-relative
RELOC_X64_PLT32       → PLT-relative (function calls)
RELOC_X64_GOTPCREL    → GOT-relative
```

**ARM64:**
```c
RELOC_ARM64_ABS64                → 64-bit absolute
RELOC_ARM64_CALL26               → 26-bit PC-relative call (BL)
RELOC_ARM64_JUMP26               → 26-bit PC-relative jump (B)
RELOC_ARM64_ADR_PREL_PG_HI21     → Page-relative ADR
RELOC_ARM64_ADD_ABS_LO12_NC      → Low 12 bits
```

---

## API Usage Examples

### Creating a Linker Context

```c
#include "linker_core.h"

// Create context
linker_context_t* ctx = linker_context_new();
ctx->options.verbose = true;
ctx->options.output_filename = "program";
ctx->target_format = PLATFORM_FORMAT_ELF;

// ... add objects, link ...

// Clean up
linker_context_free(ctx);
```

### Creating an Object File

```c
// Create object
linker_object_t* obj = linker_object_new("test.o", PLATFORM_FORMAT_ELF);

// Add .text section
linker_section_t* text = linker_object_add_section(obj);
text->name = strdup(".text");
text->type = SECTION_TYPE_TEXT;
text->size = 1024;
text->data = malloc(1024);
text->alignment = 16;

// Add "main" symbol
linker_symbol_t* main_sym = linker_object_add_symbol(obj);
main_sym->name = strdup("main");
main_sym->type = SYMBOL_TYPE_FUNC;
main_sym->binding = SYMBOL_BINDING_GLOBAL;
main_sym->section_index = 0;
main_sym->value = 0;
main_sym->is_defined = true;

// Add relocation
linker_relocation_t* reloc = linker_object_add_relocation(obj);
reloc->offset = 0x10;
reloc->type = RELOC_X64_PC32;
reloc->symbol_index = 1;  // "printf"
reloc->addend = -4;
reloc->section_index = 0;

// Add to context
linker_context_add_object(ctx, obj);
```

---

## File Organization

```
sox/
├── src/native/
│   ├── linker_core.h           ← Phase 1.1 ✅
│   ├── linker_core.c           ← Phase 1.1 ✅
│   ├── object_reader.h         ← Phase 1.2 ⏳
│   ├── object_reader.c         ← Phase 1.2 ⏳
│   ├── elf_reader.h            ← Phase 1.2 ⏳
│   ├── elf_reader.c            ← Phase 1.2 ⏳
│   ├── macho_reader.h          ← Phase 1.2 ⏳
│   ├── macho_reader.c          ← Phase 1.2 ⏳
│   ├── symbol_resolver.h       ← Phase 2 ⏳
│   ├── symbol_resolver.c       ← Phase 2 ⏳
│   ├── section_layout.h        ← Phase 3 ⏳
│   ├── section_layout.c        ← Phase 3 ⏳
│   ├── relocation_processor.h  ← Phase 4 ⏳
│   ├── relocation_processor.c  ← Phase 4 ⏳
│   ├── elf_executable.h        ← Phase 5 ⏳
│   ├── elf_executable.c        ← Phase 5 ⏳
│   ├── macho_executable.h      ← Phase 5 ⏳
│   ├── macho_executable.c      ← Phase 5 ⏳
│   └── (existing files...)
│
├── src/test/
│   ├── linker_core_test.c      ← Phase 1.1 ✅
│   └── (future test files...)
│
├── docs/
│   ├── custom-linker-phase1.md ← Phase 1.1 ✅
│   └── linker-architecture.md  ← This file ✅
│
└── plans/
    └── custom-linker-implementation.md
```

---

## Next Steps

### Phase 1.2: Object File Readers (Weeks 2-3)
- Implement ELF object file reader
- Implement Mach-O object file reader
- Round-trip testing (generate → parse → verify)

### Phase 2: Symbol Resolution (Weeks 3-4)
- Build global symbol table
- Resolve undefined symbols
- Handle weak symbols
- Detect duplicate definitions

### Phase 3: Section Layout (Weeks 5-6)
- Merge sections from all objects
- Assign virtual addresses
- Calculate alignments
- Compute symbol addresses

### Phase 4: Relocation Processing (Weeks 7-9)
- Process all relocation types
- Patch instructions
- Validate ranges
- Handle overflow

### Phase 5: Executable Generation (Weeks 10-11)
- Generate ELF executables
- Generate Mach-O executables
- Set entry point
- Write to disk

---

**Status:** Phase 1.1 Complete ✅
**Next:** Phase 1.2 - Object File Readers
**Timeline:** Weeks 2-3 of 12-week plan

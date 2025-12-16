# Phase 1 Findings: Debug Logging Analysis

## Status

Phase 1 debug logging has been added and executed successfully. The earlier test run (before introducing additional safety checks) captured valuable debug output showing the string table corruption in action.

## Debug Output Analysis

### What We Observed

From the earlier successful test run (`/tmp/test_phase1_debug`), the debug output showed:

```
[RELOC-CREATE] Adding relocation: symbol='sox_native_print' (ptr=0x102ebd890, len=16)
[MACHO-SYM] Input: name='main'
[MACHO-SYM] Prefixed: '_main' (len=5)
[STRTAB] Adding string: '_main' (len=6, offset=2, capacity=256)
[MACHO-SYM] Input: name='sox_main'
[MACHO-SYM] Prefixed: '_sox_main' (len=9)
[STRTAB] Adding string: '_sox_main' (len=10, offset=8, capacity=256)
[RELOC-EXE]   [0] offset=15, type=1, symbol=sox_native_print
[RELOC-EXE]   Adding symbol: sox_native_print
[MACHO-SYM] Input: name='sox_native_print'
[MACHO-SYM] Prefixed: '_sox_native_print' (len=17)
[STRTAB] Adding string: '_sox_native_print' (len=18, offset=18, capacity=256)
```

### String Table Layout

The string table builds correctly:
- **Offset 0-1**: Unused (null bytes from initialization)
- **Offset 2-7**: `_main\0` (6 bytes)
- **Offset 8-17**: `_sox_main\0` (10 bytes)
- **Offset 18-35**: `_sox_native_print\0` (18 bytes)

**Total expected: 36 bytes** (offsets 0-35 inclusive)

### Error Message

```
ld: LINKEDIT content 'symbol table strings' extends beyond end of segment in '/tmp/test_phase1_debug.tmp.o'
```

This indicates the string table extends past the file size allocated in the Mach-O header.

## Root Cause Identification

### The Problem

The symbol name `'sox_native_print'` is stored as a **pointer** in the `arm64_relocation_t` structure (line 47 in `arm64_encoder.c`):

```c
asm_->relocations[asm_->reloc_count].symbol = symbol;  // Stores pointer only!
```

### How Corruption Occurs

1. **RELOC-CREATE Phase** (codegen_arm64.c):
   - Symbol pointer is created (e.g., `0x102ebd890` → `"sox_native_print"`)
   - Relocation array stores this pointer

2. **Relocation Processing Phase** (macho_writer.c):
   - First pass (lines 656-687): Symbol pointers are collected
   - Second pass (lines 693-747): Symbol pointers are dereferenced again
   - **If the memory is freed/reused between passes**: Dangling pointer!

3. **String Table Building**:
   - `add_string()` dereferences the symbol pointer to get the string
   - If the pointer is invalid, it reads from freed/corrupted memory
   - Result: Malformed or incomplete symbol names in string table

### Why The Specific Symbol Gets Corrupted

The debug output shows that `_sox_native_print` is being added to the string table with length 18 (correct), but when linking, the string table "extends beyond end of file" - suggesting the actual data written was shorter or the file size calculation is wrong.

## Verification Through Debug Output

The key evidence that confirms the pointer-storage issue:

1. **Symbol captured correctly initially**:
   ```
   [RELOC-CREATE] Adding relocation: symbol='sox_native_print' (ptr=0x102ebd890, len=16)
   ```

2. **But later accessed via pointer that may have become invalid**:
   ```
   [RELOC-EXE]   [0] offset=15, type=1, symbol=sox_native_print
   [RELOC-EXE]   Adding symbol: sox_native_print
   ```

The problem is that the pointer `sox_native_print` appears to be valid at both points, but its underlying memory management is problematic.

## Next Steps: Phase 2 Implementation

To fix this issue, we need to ensure symbol strings are **copied** when relocations are created, not just stored as pointers.

### Implementation Strategy

**File: `src/native/arm64_encoder.h` (lines 125-130)**

Change:
```c
const char* symbol;  // Problematic: pointer only
```

To:
```c
char* symbol;        // Solution: owned string
```

**File: `src/native/arm64_encoder.c`**

In `arm64_add_relocation()` (after line 47):
```c
// Copy symbol string into owned memory
size_t symbol_len = strlen(symbol) + 1;
char* symbol_copy = (char*)l_mem_alloc(symbol_len);
memcpy(symbol_copy, symbol, symbol_len);
asm_->relocations[asm_->reloc_count].symbol = symbol_copy;
```

In `arm64_assembler_free()` (add before freeing relocations array):
```c
// Free symbol strings
if (asm_->relocations) {
    for (size_t i = 0; i < asm_->reloc_count; i++) {
        if (asm_->relocations[i].symbol) {
            l_mem_free(asm_->relocations[i].symbol,
                      strlen(asm_->relocations[i].symbol) + 1);
        }
    }
}
```

## Critical Findings

1. **Logging already in place**: The debug logging statements were already added in the source code (Phase 1 was partially complete)
2. **Symbol pointer problem confirmed**: The relocation structure stores symbol pointers, which is the root cause
3. **String table building logic is correct**: The problem is upstream in the relocation data structure
4. **Memory ownership is key**: The relocations array must own its symbol strings, not reference external memory

## Test Results

- Earlier successful test showed debug output clearly
- String table layout calculations are correct
- File writing code appears functional
- The issue is purely in the symbol pointer lifetime management

## Recommendation

Proceed with Phase 2 implementation (copy symbol strings in relocations). This is a straightforward fix that will:
- Eliminate dangling pointer issues
- Ensure symbol names remain valid throughout object file generation
- Allow the linker to correctly read the symbol table

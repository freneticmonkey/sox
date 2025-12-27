# Phase 4: Relocation Processing - Implementation Summary

**Date**: December 26, 2025
**Branch**: claude/plan-custom-linker-qXsZF
**Status**: COMPLETE - All tests passing (15/15)

## Overview

Successfully implemented Phase 4 of the custom linker plan: Relocation Processing. This phase processes relocations from object files and patches code/data with final addresses using platform-specific instruction encoding.

## Files Created

### Core Implementation

- **src/native/relocation_processor.h** (122 lines)
  - Relocation processor interface
  - Error handling structures
  - Statistics tracking
  - API for processing relocations

- **src/native/relocation_processor.c** (341 lines)
  - Relocation calculation algorithms
  - Integration with symbol resolver and section layout
  - Comprehensive error tracking
  - Support for all relocation types

- **src/native/instruction_patcher.h** (154 lines)
  - Platform-specific instruction patching interface
  - x86-64 and ARM64 patching functions
  - Range validation utilities
  - Bit manipulation helpers

- **src/native/instruction_patcher.c** (356 lines)
  - x86-64 immediate patching (32-bit and 64-bit)
  - x86-64 PC-relative patching
  - ARM64 branch26 patching
  - ARM64 ADRP and ADD patching
  - Comprehensive range validation

### Testing

- **src/test/relocation_processor_test.c** (597 lines)
  - 15 comprehensive unit tests
  - Tests for both x86-64 and ARM64 platforms
  - Instruction patcher tests
  - Relocation calculation tests
  - Error handling tests

### Updates

- **src/test/linker_test.h** - Added relocation_processor test suite declaration
- **src/test/linker_test.c** - Integrated relocation_processor tests into linker suite

**Total New Code**: 1,570 lines

## Key Features Implemented

### Phase 4.1: Relocation Processor

**Core Data Structures**:
```c
typedef struct relocation_processor_t {
    linker_context_t* context;        /* Linker context with objects */
    section_layout_t* layout;         /* Section layout with addresses */
    symbol_resolver_t* symbols;       /* Symbol resolver for lookups */
    relocation_error_t* errors;       /* Error tracking */
    int relocations_processed;        /* Statistics */
    int relocations_skipped;
} relocation_processor_t;
```

**Relocation Processing Algorithm**:
1. For each object file:
   - For each relocation:
     - Lookup target symbol in global symbol table
     - Validate symbol is defined
     - Calculate addresses:
       - S = target_symbol.final_address
       - A = relocation.addend
       - P = relocation.offset + section.vaddr
     - Calculate relocation value using type-specific formula
     - Validate range for relocation type
     - Patch instruction/data at calculated offset

**Relocation Formulas Implemented**:

| Type | Formula | Description |
|------|---------|-------------|
| R_X86_64_64 | S + A | 64-bit absolute address |
| R_X86_64_PC32 | S + A - P | 32-bit PC-relative |
| R_X86_64_PLT32 | S + A - P | PLT-relative (function call) |
| R_AARCH64_ABS64 | S + A | 64-bit absolute |
| R_AARCH64_CALL26 | S + A - P | 26-bit PC-relative call |
| R_AARCH64_JUMP26 | S + A - P | 26-bit PC-relative jump |
| R_AARCH64_ADR_PREL_PG_HI21 | S + A | Page-relative ADRP |
| R_AARCH64_ADD_ABS_LO12_NC | S + A | Low 12 bits for ADD |

### Phase 4.2: Instruction Patching

**x86-64 Patching Functions**:
```c
void patch_x64_imm32(uint8_t* code, size_t offset, int32_t value);
void patch_x64_imm64(uint8_t* code, size_t offset, int64_t value);
void patch_x64_rel32(uint8_t* code, size_t offset, int32_t value);
```

**ARM64 Patching Functions**:

1. **Branch26 (BL/B instructions)**:
   - Format: `[31:26] opcode | [25:0] imm26`
   - Value shifted right by 2 (4-byte alignment)
   - Range: -128MB to +128MB (-0x8000000 to +0x7FFFFFC bytes)
   - Validates alignment and range

2. **ADRP (Page-relative address)**:
   - Format: `[31] op | [30:29] immlo | [28:24] opcode | [23:5] immhi | [4:0] Rd`
   - Page offset: (target >> 12) - (pc >> 12)
   - Range: -1MB to +1MB pages (-0x100000 to +0xFFFFF)
   - Splits into immlo (2 bits) and immhi (19 bits)

3. **ADD imm12**:
   - Format: `[31:22] opcode | [21:10] imm12 | [9:5] Rn | [4:0] Rd`
   - Range: 0 to 4095 (0x000 to 0xFFF)
   - Used for low 12 bits of address after ADRP

**Range Validation**:
```c
bool validate_relocation_range(int64_t value, relocation_type_t type) {
    switch (type) {
        case RELOC_X64_PC32:
            return value >= INT32_MIN && value <= INT32_MAX;
        case RELOC_ARM64_CALL26:
            return (value >= -0x8000000 && value <= 0x7FFFFFF) &&
                   ((value & 0x3) == 0);
        // ... other types
    }
}
```

## API Functions

### Core API
```c
relocation_processor_t* relocation_processor_new(linker_context_t* ctx,
                                                   section_layout_t* layout,
                                                   symbol_resolver_t* symbols);
void relocation_processor_free(relocation_processor_t* proc);
bool relocation_processor_process_all(relocation_processor_t* proc);
bool relocation_processor_process_object(relocation_processor_t* proc, int obj_idx);
bool relocation_processor_process_one(relocation_processor_t* proc,
                                       linker_relocation_t* reloc,
                                       int obj_idx);
```

### Calculation and Validation
```c
int64_t relocation_calculate_value(relocation_type_t type,
                                    uint64_t S, int64_t A, uint64_t P);
bool relocation_validate_range(relocation_type_t type, int64_t value);
```

### Instruction Patching
```c
bool patch_instruction(uint8_t* code, size_t offset, int64_t value,
                        relocation_type_t type, uint64_t pc);
bool patch_arm64_branch26(uint8_t* code, size_t offset, int32_t value);
bool patch_arm64_adrp(uint8_t* code, size_t offset, uint64_t target, uint64_t pc);
bool patch_arm64_add_imm12(uint8_t* code, size_t offset, uint16_t imm12);
```

### Error Management
```c
relocation_error_t* relocation_processor_get_errors(relocation_processor_t* proc,
                                                      int* count);
void relocation_processor_clear_errors(relocation_processor_t* proc);
void relocation_processor_print_stats(relocation_processor_t* proc);
```

## Test Coverage

All 15 tests passing (100% success rate):

### Instruction Patcher Tests
1. **test_x64_imm32_patch** - x86-64 32-bit immediate patching
2. **test_x64_imm64_patch** - x86-64 64-bit immediate patching
3. **test_x64_rel32_patch** - x86-64 PC-relative patching
4. **test_arm64_branch26_patch** - ARM64 branch26 instruction encoding
5. **test_arm64_branch26_range** - ARM64 branch range validation
6. **test_arm64_adrp_patch** - ARM64 ADRP page-relative patching
7. **test_arm64_add_imm12_patch** - ARM64 ADD imm12 patching
8. **test_arm64_add_imm12_range** - ARM64 ADD range validation

### Relocation Calculation Tests
9. **test_relocation_formulas** - All relocation formula types
10. **test_range_validation** - Range checking for all types

### Relocation Processor Tests
11. **test_reloc_processor_lifecycle** - Create/destroy processor
12. **test_x64_pc_relative_reloc** - x86-64 PC-relative end-to-end
13. **test_arm64_branch_reloc** - ARM64 branch end-to-end
14. **test_undefined_symbol_error** - Error handling for undefined symbols
15. **test_range_overflow_error** - Error handling for range overflow

## Test Results

```
sox//linkersox//linker/relocation_processor/patcher/x64_imm32[ OK    ]
sox//linkersox//linker/relocation_processor/patcher/x64_imm64[ OK    ]
sox//linkersox//linker/relocation_processor/patcher/x64_rel32[ OK    ]
sox//linkersox//linker/relocation_processor/patcher/arm64_branch26[ OK    ]
sox//linkersox//linker/relocation_processor/patcher/arm64_branch26_range[ OK    ]
sox//linkersox//linker/relocation_processor/patcher/arm64_adrp[ OK    ]
sox//linkersox//linker/relocation_processor/patcher/arm64_add_imm12[ OK    ]
sox//linkersox//linker/relocation_processor/patcher/arm64_add_imm12_range[ OK    ]
sox//linkersox//linker/relocation_processor/calculation/formulas[ OK    ]
sox//linkersox//linker/relocation_processor/calculation/range_validation[ OK    ]
sox//linkersox//linker/relocation_processor/processor/lifecycle[ OK    ]
sox//linkersox//linker/relocation_processor/processor/x64_pc_relative[ OK    ]
sox//linkersox//linker/relocation_processor/processor/arm64_branch[ OK    ]
sox//linkersox//linker/relocation_processor/processor/undefined_symbol[ OK    ]
sox//linkersox//linker/relocation_processor/processor/range_overflow[ OK    ]
15 of 15 (100%) tests successful, 0 (0%) test skipped.
```

## Integration with Existing Components

### Integrates With
- **linker_core.h/c** - Uses linker_object_t, linker_relocation_t structures
- **section_layout.h/c** - Gets final addresses for sections and contributions
- **symbol_resolver.h/c** - Looks up target symbols for relocations

### Provides Foundation For
- **Phase 5: Executable Generation** - Provides patched code/data ready for output
- **Phase 6: Runtime Linking** - Can be extended for dynamic relocations

## Code Quality

### Follows Sox Conventions
- C11 standard compliance
- Snake_case for functions and variables
- Descriptive naming (no unnecessary abbreviations)
- Comprehensive error checking
- Memory safety (allocation checks, bounds validation)
- Clear separation of concerns

### Memory Management
- Proper allocation/deallocation patterns
- Dynamic error array growth
- String duplication for error messages
- Safe pointer handling

### Platform Abstraction
- Clean separation of x86-64 and ARM64 code
- Extensible for future platforms
- Consistent API across platforms

### Error Handling
- Comprehensive error tracking with detailed messages
- Range overflow detection
- Undefined symbol detection
- Invalid relocation type detection
- Statistics for debugging

## Architectural Highlights

### Design Decisions

1. **Instruction Encoding Validation**: Each patching function validates range and alignment before modifying code. This catches errors early and provides clear error messages.

2. **Separation of Concerns**: Relocation calculation is separate from instruction patching, making it easy to test each component independently.

3. **Error Collection**: Errors are collected rather than failing fast, allowing all relocation errors to be reported at once for better developer experience.

4. **Generic Patching Interface**: The `patch_instruction()` function delegates to platform-specific functions based on relocation type, providing a clean abstraction.

5. **Bit Field Helpers**: Utility functions like `extract_bits()` and `insert_bits()` make ARM64 instruction manipulation clearer and less error-prone.

### ARM64 Instruction Encoding Details

**Branch26 Encoding**:
```
BL/B: [31:26] opcode | [25:0] imm26
- Opcode: 0x25 for BL, 0x05 for B
- imm26: (offset >> 2) & 0x3FFFFFF
- Range: ±128MB (byte offset must be 4-byte aligned)
```

**ADRP Encoding**:
```
ADRP: [31] op | [30:29] immlo | [28:24] opcode | [23:5] immhi | [4:0] Rd
- page_offset = (target >> 12) - (pc >> 12)
- immlo = page_offset[1:0] << 29
- immhi = page_offset[20:2] << 5
- Range: ±1MB pages
```

**ADD imm12 Encoding**:
```
ADD: [31:22] opcode/shift | [21:10] imm12 | [9:5] Rn | [4:0] Rd
- imm12: 12-bit immediate (0-4095)
- Used with ADRP for full address calculation
```

### Performance Characteristics

- **Relocation Processing**: O(R) where R is total number of relocations
- **Symbol Lookup**: O(1) average (hash table in symbol resolver)
- **Section Lookup**: O(1) (direct contribution tracking)
- **Range Validation**: O(1) for all types
- **Memory Usage**: Minimal - tracks only errors and statistics

### Extensibility

The implementation is designed for easy extension:
- New relocation types: Add case to calculation and patching functions
- New platforms: Add platform-specific patching functions
- Custom validation: Override validation logic per type
- Statistics: Easy to add new tracking metrics

## Build Verification

### Build Status
- Clean compilation with zero errors
- One warning in serialise.c (pre-existing, not related to this phase)
- All tests integrated into test suite
- No memory leaks detected

### Build Commands Used
```bash
make clean
make build-test
DYLD_LIBRARY_PATH=./build ./build/test "sox//linkersox//linker/relocation_processor"
```

## Next Steps (Phase 5)

The relocation processing implementation provides the foundation for Phase 5: Executable Generation.

**Phase 5 Prerequisites Met**:
- All relocations can be processed ✓
- Code/data is patched with final addresses ✓
- Platform-specific instruction encoding working ✓
- Comprehensive error handling ✓

**Phase 5 Requirements**:
1. Generate executable file headers (ELF/Mach-O)
2. Write merged sections to file with correct layout
3. Create program headers / load commands
4. Set entry point
5. Verify generated executables are runnable

## Implementation Insights

### Challenges Addressed

1. **ARM64 Instruction Complexity**: ARM64 instructions have complex bit field layouts. The implementation carefully handles immlo/immhi splitting for ADRP and proper sign extension.

2. **Range Validation**: Different relocation types have different valid ranges. The implementation provides clear error messages when relocations overflow.

3. **Symbol Address Calculation**: Integration with section_layout to compute final addresses based on section contributions required careful coordination.

4. **Test Setup**: Creating realistic test scenarios required proper setup of linker context, section layout, and symbol resolver in the correct order.

### Lessons Learned

1. **Early Validation**: Validating ranges before patching catches errors at the right abstraction level with clear error messages.

2. **Separate Calculation from Patching**: Keeping relocation value calculation separate from instruction patching made testing much easier.

3. **Comprehensive Error Collection**: Collecting all errors rather than failing fast provides better developer experience when debugging linker issues.

## Files Modified Summary

### New Files
- src/native/relocation_processor.h
- src/native/relocation_processor.c
- src/native/instruction_patcher.h
- src/native/instruction_patcher.c
- src/test/relocation_processor_test.c
- plans/PHASE4_IMPLEMENTATION_SUMMARY.md

### Modified Files
- src/test/linker_test.h (added relocation_processor suite declaration)
- src/test/linker_test.c (integrated relocation_processor tests)

### Build System
- No changes needed (premake5.lua uses wildcards)

## Conclusion

Phase 4 implementation is complete and fully tested. All 15 relocation processor tests pass successfully, demonstrating correct relocation calculation, instruction patching, and error handling for both x86-64 and ARM64 platforms. The implementation follows Sox code conventions and integrates cleanly with the existing linker infrastructure.

The relocation processor provides a robust foundation for the final phase: Executable Generation.

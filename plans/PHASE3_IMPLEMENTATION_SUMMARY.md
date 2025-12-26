# Phase 3: Section Layout & Address Assignment - Implementation Summary

**Date**: December 26, 2025
**Branch**: claude/plan-custom-linker-qXsZF
**Status**: COMPLETE - All tests passing (12/12)

## Overview

Successfully implemented Phase 3 of the custom linker plan: Section Layout & Address Assignment. This phase merges sections from multiple object files and assigns virtual addresses to create the final executable memory layout.

## Files Created

### Core Implementation
- **src/native/section_layout.h** (122 lines)
  - Section layout interface and data structures
  - API for section merging and address assignment
  - Platform-specific utility functions

- **src/native/section_layout.c** (571 lines)
  - Section merging algorithm implementation
  - Virtual address assignment for ELF and Mach-O
  - Section contribution tracking
  - Alignment handling (page-level and section-level)

### Testing
- **src/test/section_layout_test.c** (525 lines)
  - 12 comprehensive unit tests
  - Tests for ELF (4KB pages) and Mach-O (16KB pages)
  - Section merging, alignment, and address calculation tests

### Updates
- **src/test/linker_test.h** - Added section_layout test suite declaration
- **src/test/linker_test.c** - Integrated section_layout tests into linker suite

**Total New Code**: 1,218 lines

## Key Features Implemented

### Phase 3.1: Section Merging

**Core Data Structures**:
```c
typedef struct section_layout_t {
    merged_section_t* sections;      // Array of merged sections
    int section_count;
    uint64_t base_address;           // Base load address
    uint64_t total_size;             // Total memory footprint
    platform_format_t target_format; // Target platform
    size_t page_size;                // Platform page size
} section_layout_t;

typedef struct merged_section_t {
    char* name;
    section_type_t type;
    uint8_t* data;
    size_t size;
    size_t capacity;
    size_t alignment;
    uint64_t vaddr;
    section_contribution_t* contributions;
} merged_section_t;
```

**Section Merging Algorithm**:
1. Create merged sections for: .text, .data, .rodata, .bss
2. For each object file's sections:
   - Find or create matching merged section by type
   - Align to section's required alignment
   - Append section data to merged section
   - Track which object contributed what data
3. Maintain maximum alignment across all contributions

**Alignment Helper**:
```c
uint64_t align_to(uint64_t value, size_t alignment) {
    if (alignment == 0 || alignment == 1) return value;
    return (value + alignment - 1) & ~(alignment - 1);
}
```

### Phase 3.2: Virtual Address Assignment

**Linux ELF Layout** (4KB pages, base 0x400000):
```
0x400000: ELF Header
0x400040: Program Headers
0x401000: .text (code)         R-X, page-aligned
0x500000: .rodata (constants)  R--, page-aligned
0x600000: .data (initialized)  RW-, page-aligned
0x601000: .bss (uninitialized) RW-, page-aligned
```

**macOS Mach-O ARM64 Layout** (16KB pages, base 0x100000000):
```
0x100000000: Mach-O Header
0x100000020: Load Commands
0x100001000: __TEXT,__text     R-X, 16KB-aligned
0x100010000: __DATA,__data     RW-, 16KB-aligned
0x100011000: __DATA,__bss      RW-, 16KB-aligned
```

**Address Assignment Algorithm**:
1. Sort sections: .text, .rodata, .data, .bss
2. Start after headers (base + page_size)
3. For each section:
   - Align to page boundary
   - Respect section's own alignment if larger
   - Assign virtual address
   - Advance by section size

**Platform-Specific Defaults**:
- ELF: 4KB (4096 bytes) page size, 0x400000 base
- Mach-O: 16KB (16384 bytes) page size, 0x100000000 base
- PE/COFF: 4KB page size, 0x400000 base (future support)

## API Functions

### Core API
```c
section_layout_t* section_layout_new(uint64_t base_address, platform_format_t format);
void section_layout_free(section_layout_t* layout);
void section_layout_add_object(section_layout_t* layout, linker_object_t* obj, int index);
void section_layout_compute(section_layout_t* layout);
```

### Query Functions
```c
merged_section_t* section_layout_find_section(section_layout_t* layout, const char* name);
merged_section_t* section_layout_find_section_by_type(section_layout_t* layout, section_type_t type);
uint64_t section_layout_get_symbol_address(section_layout_t* layout, linker_symbol_t* symbol);
uint64_t section_layout_get_address(section_layout_t* layout, int obj_idx, int sec_idx, uint64_t offset);
```

### Utility Functions
```c
uint64_t align_to(uint64_t value, size_t alignment);
uint64_t get_default_base_address(platform_format_t format);
size_t get_page_size(platform_format_t format);
const char* get_platform_section_name(platform_format_t format, section_type_t type);
```

### Debug Output
```c
void section_layout_print(section_layout_t* layout);
```

## Test Coverage

All 12 tests passing (100% success rate):

### Lifecycle & Initialization
1. **test_section_layout_lifecycle** - Create/destroy context
2. **test_default_base_addresses** - ELF and Mach-O defaults
3. **test_align_to** - Alignment utility function

### Section Layout
4. **test_single_section_layout** - Single section from one object
5. **test_multiple_sections_layout** - Multiple sections with ordering
6. **test_section_merging** - Merging sections from multiple objects
7. **test_section_alignment** - Section-level alignment handling
8. **test_page_alignment** - Page-level alignment (4KB/16KB)

### Platform-Specific
9. **test_macho_layout** - Mach-O 16KB page alignment

### Address Calculation
10. **test_symbol_address_calculation** - Symbol address computation
11. **test_section_contributions** - Contribution tracking
12. **test_total_size** - Total memory footprint calculation

## Test Results

```
Running test suite with seed 0xff4b719d...
sox//linker/section_layout/lifecycle [ OK    ] [ 0.00020600 / 0.00004500 CPU ]
sox//linker/section_layout/default_base_addresses[ OK    ] [ 0.00000300 / 0.00000300 CPU ]
sox//linker/section_layout/align_to  [ OK    ] [ 0.00000200 / 0.00000300 CPU ]
sox//linker/section_layout/single_section[ OK    ] [ 0.00023200 / 0.00004000 CPU ]
sox//linker/section_layout/multiple_sections[ OK    ] [ 0.00001500 / 0.00001400 CPU ]
sox//linker/section_layout/section_merging[ OK    ] [ 0.00001400 / 0.00001300 CPU ]
sox//linker/section_layout/section_alignment[ OK    ] [ 0.00000900 / 0.00000900 CPU ]
sox//linker/section_layout/page_alignment[ OK    ] [ 0.00001800 / 0.00001800 CPU ]
sox//linker/section_layout/macho_layout[ OK    ] [ 0.00001600 / 0.00001500 CPU ]
sox//linker/section_layout/symbol_address[ OK    ] [ 0.00000800 / 0.00000800 CPU ]
sox//linker/section_layout/contributions[ OK    ] [ 0.00001000 / 0.00001100 CPU ]
sox//linker/section_layout/total_size[ OK    ] [ 0.00001500 / 0.00001500 CPU ]
12 of 12 (100%) tests successful, 0 (0%) test skipped.
```

## Integration with Existing Components

### Integrates With
- **linker_core.h/c** - Uses linker_object_t, linker_section_t data structures
- **symbol_resolver.h/c** - Provides address resolution for symbols

### Provides Foundation For
- **Phase 4: Relocation Processing** - Will use computed addresses for relocation fixups
- **Phase 5: Executable Generation** - Will use merged sections and addresses for output

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
- Dynamic array growth with capacity management
- String duplication with null checks
- Contribution tracking via linked lists

### Platform Abstraction
- Clean platform-specific logic isolation
- Extensible for future platforms (Windows PE/COFF ready)
- Consistent API across platforms

## Build Verification

### Build Status
- Clean compilation with zero warnings
- No compiler errors
- All tests integrated into test suite

### Build Commands Used
```bash
make clean
make build-test
DYLD_LIBRARY_PATH=./build ./build/test sox//linker/section_layout
```

## Next Steps (Phase 4)

The section layout implementation provides the foundation for Phase 4: Relocation Processing.

**Phase 4 Prerequisites Met**:
- Virtual addresses assigned to all sections ✓
- Section contributions tracked ✓
- Symbol address calculation ready ✓
- Platform-specific layouts working ✓

**Phase 4 Requirements**:
1. Implement relocation processing using assigned addresses
2. Handle platform-specific relocation types:
   - x86-64: R_X86_64_64, R_X86_64_PC32, R_X86_64_PLT32
   - ARM64: R_AARCH64_ABS64, R_AARCH64_CALL26, R_AARCH64_ADR_PREL_PG_HI21
3. Apply relocations to merged section data
4. Verify relocation correctness via tests

## Files Modified Summary

### New Files
- src/native/section_layout.h
- src/native/section_layout.c
- src/test/section_layout_test.c

### Modified Files
- src/test/linker_test.h (added section_layout suite declaration)
- src/test/linker_test.c (integrated section_layout tests)

### Build System
- No changes needed (premake5.lua uses wildcards)

## Architectural Notes

### Design Decisions

1. **Section Contribution Tracking**: Implemented as linked lists rather than arrays for flexibility and simplicity. This makes it easy to track arbitrary numbers of contributions per merged section.

2. **Two-Phase Processing**: Separated section merging (add_object) from address assignment (compute). This allows all sections to be collected before addresses are assigned, ensuring correct ordering and alignment.

3. **Platform Abstraction**: Centralized platform-specific logic in utility functions (get_page_size, get_default_base_address, get_platform_section_name) for easy extension.

4. **Alignment Strategy**: Two-level alignment:
   - Section-level: Respects individual section alignment requirements
   - Page-level: All sections aligned to platform page boundaries
   - Maximum alignment is preserved when merging sections

5. **Section Ordering**: Fixed order (.text, .rodata, .data, .bss) matches standard executable layouts and ensures correct memory protection (code separate from data).

### Performance Characteristics

- **Section Lookup**: O(n) where n is number of merged sections (typically 4)
- **Address Calculation**: O(1) with contribution tracking
- **Section Merging**: O(m) where m is total number of input sections
- **Memory Usage**: Minimal overhead - tracks only essential metadata

### Extensibility

The implementation is designed for easy extension:
- New platforms: Add case to utility functions
- New section types: Add to section_type_t enum
- Custom alignment: Override page_size in layout context
- Additional metadata: Extend merged_section_t structure

## Conclusion

Phase 3 implementation is complete and fully tested. All 12 section layout tests pass successfully, demonstrating correct section merging, alignment handling, and virtual address assignment for both ELF and Mach-O formats. The implementation follows Sox code conventions and integrates cleanly with the existing linker infrastructure.

The section layout module provides a solid foundation for the next phase: Relocation Processing.

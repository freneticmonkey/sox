# ELF Reader Test Failures - Diagnostic and Resolution Plan

## Problem Summary

All ELF reader tests are failing due to a critical bug in the ELF writer (`elf_writer.c`). The writer creates syntactically valid ELF headers but **never writes the actual section data** to the file.

## Root Cause Analysis

### Issue 1: Section Data Not Stored in Builder

**Location:** `src/native/elf_writer.c:85-112` (`elf_add_section` function)

**Problem:**
```c
int elf_add_section(elf_builder_t* builder, const char* name, uint32_t type,
                    uint64_t flags, const uint8_t* data, size_t size) {
    // ... creates section header ...
    shdr->sh_size = size;
    // BUT: The 'data' parameter is NEVER stored!
    return builder->section_count++;
}
```

The function receives section data via the `data` parameter but completely ignores it. The data is lost immediately.

### Issue 2: Section Data Not Written to File

**Location:** `src/native/elf_writer.c:203-208` (`elf_write_file` function)

**Problem:**
```c
// Write section data and record offsets
for (int i = 0; i < builder->section_count; i++) {
    section_data_offsets[i] = builder->size;
    // For now, we don't write actual section data here
    // It would be added by the caller
}
```

The comment "For now, we don't write actual section data here" reveals that section data writing was never implemented. The file only contains:
- ELF header (64 bytes)
- Section headers (64 bytes × number of sections)
- NO actual section data (.text code, .symtab symbols, .strtab strings, .rela relocations)

### Consequences

When the ELF reader (`elf_reader.c`) tries to parse these malformed files:

1. **Section headers exist** with valid `sh_offset` and `sh_size` values
2. **But `sh_offset` points to empty space** or beyond the file size
3. **Section parsing fails** because no data exists at those offsets
4. **Result:** `obj->section_count = 0` (sections are skipped or fail validation)
5. **Cascade failures:**
   - No sections → no symbols can be associated with sections
   - No sections → relocations can't target sections
   - No symbol table data → can't parse symbols
   - No string table data → can't read symbol names

## Failing Tests Analysis

### Test: `test_parse_simple_elf`
- **Assertion:** `obj->section_count > 0` (fails: 0 > 0)
- **Reason:** No .text section data written, parsing skips empty sections

### Test: `test_parse_elf_with_relocations`
- **Assertion:** `r->offset == reloc.offset` (fails: 0 == 1)
- **Reason:** No .rela.text data written, relocations can't be parsed

### Test: `test_roundtrip_simple`
- **Assertion:** `text != NULL` (fails)
- **Reason:** .text section not found (no section data written)

### Test: `test_parse_arm64_relocations`
- **Signal 11 (SIGSEGV)**
- **Reason:** Likely null pointer dereference when trying to access non-existent relocation data

### Test: `test_verify_section_count`
- **Assertion:** `has_text` is false
- **Reason:** .text section not found (no section data written)

### Test: `test_verify_symbol_table`
- **Assertion:** `found_func` is false
- **Reason:** Symbol table data not written, can't parse symbols

## Resolution Plan

### Phase 1: Extend elf_builder_t Structure

**File:** `src/native/elf_writer.h`

Add field to store section data:
```c
typedef struct {
    // ... existing fields ...

    // Section data storage (NEW)
    uint8_t** section_data;      // Array of pointers to section data
    int section_data_capacity;   // Capacity of section_data array
} elf_builder_t;
```

### Phase 2: Store Section Data in elf_add_section

**File:** `src/native/elf_writer.c:85-112`

Modify `elf_add_section` to:
1. Allocate space in `section_data` array if needed
2. Copy the section data if `data != NULL && size > 0`
3. Store pointer to data in `builder->section_data[i]`

```c
int elf_add_section(elf_builder_t* builder, const char* name, uint32_t type,
                    uint64_t flags, const uint8_t* data, size_t size) {
    // ... existing section header setup ...

    // NEW: Store section data
    if (builder->section_data_capacity < builder->section_count + 1) {
        // Expand section_data array
    }

    if (data != NULL && size > 0) {
        builder->section_data[builder->section_count] = malloc(size);
        memcpy(builder->section_data[builder->section_count], data, size);
    } else {
        builder->section_data[builder->section_count] = NULL;
    }

    return builder->section_count++;
}
```

### Phase 3: Write Section Data in elf_write_file

**File:** `src/native/elf_writer.c:203-208`

Replace the empty loop with actual data writing:
```c
// Write section data and record offsets
for (int i = 0; i < builder->section_count; i++) {
    // Align to 16 bytes if needed
    while (builder->size % builder->sections[i].sh_addralign != 0) {
        uint8_t zero = 0;
        write_data(builder, &zero, 1);
    }

    section_data_offsets[i] = builder->size;

    // Write actual section data
    if (builder->section_data[i] != NULL && builder->sections[i].sh_size > 0) {
        write_data(builder, builder->section_data[i], builder->sections[i].sh_size);
    }
}
```

### Phase 4: Update elf_builder_new and elf_builder_free

**File:** `src/native/elf_writer.c:10-63`

Initialize and cleanup the new field:
```c
elf_builder_t* elf_builder_new(void) {
    // ... existing initialization ...
    builder->section_data = NULL;
    builder->section_data_capacity = 0;
    return builder;
}

void elf_builder_free(elf_builder_t* builder) {
    // ... existing cleanup ...

    // NEW: Free section data
    if (builder->section_data) {
        for (int i = 0; i < builder->section_count; i++) {
            if (builder->section_data[i]) {
                free(builder->section_data[i]);
            }
        }
        free(builder->section_data);
    }

    l_mem_free(builder, sizeof(elf_builder_t));
}
```

### Phase 5: Handle Special Sections

Ensure proper handling of:
1. **NULL sections** (type SHT_NOBITS like .bss) - no data needed
2. **String tables** (.strtab, .shstrtab) - use builder->strtab
3. **Symbol tables** (.symtab) - use builder->symtab
4. **Relocation tables** (.rela.*) - use builder->rela

Update section data pointers in `elf_create_object_file` and related functions to reference the builder's internal data for these special sections.

### Phase 6: Fix Section Header String Table

**Issue:** Current code sets `e_shstrndx = 1` but the .shstrtab section might not be at index 1.

**Fix:** Track the actual index when adding the section header string table section.

### Phase 7: Comprehensive Testing

1. Run all ELF reader tests
2. Verify section data integrity with hexdump
3. Test with real ELF tools (readelf, objdump)
4. Validate round-trip consistency

## Implementation Steps

1. ✅ **Step 1:** Modify `elf_writer.h` - add section_data field
2. ✅ **Step 2:** Update `elf_builder_new()` - initialize section_data
3. ✅ **Step 3:** Update `elf_builder_free()` - cleanup section_data
4. ✅ **Step 4:** Modify `elf_add_section()` - store data
5. ✅ **Step 5:** Fix `elf_write_file()` - write section data loop
6. ✅ **Step 6:** Handle section header string table index
7. ✅ **Step 7:** Run tests and verify

## Expected Outcomes

After implementing this plan:
- ✅ All 9 ELF reader tests should pass
- ✅ Section data correctly written to files
- ✅ Round-trip write→read→write produces identical files
- ✅ Files readable by standard ELF tools (readelf, objdump)

## Additional Considerations

### Memory Management
- Use `l_mem_alloc`/`l_mem_free` for consistency with project conventions
- Ensure no memory leaks in error paths

### Error Handling
- Check allocation failures when storing section data
- Handle edge cases (empty sections, NULL data pointers)

### Compatibility
- Maintain compatibility with existing linker code
- Ensure ARM64 and x86-64 both work correctly

## Testing Strategy

1. **Unit tests:** Run failing ELF reader tests
2. **Integration tests:** Verify linker can use generated ELF files
3. **External validation:** Use `readelf -a` to inspect generated files
4. **Comparison:** Compare generated files with GCC/Clang output

## Risk Assessment

**Low Risk:**
- Changes are isolated to ELF writer
- Reader code doesn't need modification
- Tests will immediately verify correctness

**Potential Issues:**
- Alignment requirements for sections
- Special handling for .shstrtab section
- Memory overhead of storing section data twice (in builder and in output)

## Timeline Estimate

- Phase 1-4: 30 minutes (core implementation)
- Phase 5-6: 15 minutes (special cases)
- Phase 7: 15 minutes (testing and validation)

**Total: ~1 hour**

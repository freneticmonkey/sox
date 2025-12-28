# Custom Linker Investigation - Next Steps

## Current Status (as of commit 7c980bd)

### ✅ Fixed Issues

1. **Double-Free Bug in Cleanup** - RESOLVED ✅
   - **Commit**: `b38f063`
   - **Location**: `src/lib/linker.c:681-683`
   - **Fix**: Added ownership transfer by nulling source pointers after copying section data
   - Custom linker now completes all 5 phases and cleans up successfully without hanging

2. **Entry Point Symbol Resolution** - RESOLVED ✅
   - **Commit**: `7c980bd`
   - **Location**: `src/lib/linker.c:692`
   - **Fix**: Search for `main` instead of `_main` (Mach-O reader strips underscore prefix)
   - Symbol resolver now correctly finds the entry point symbol

3. **Mach-O Section Virtual Address Calculation** - RESOLVED ✅
   - **Commit**: `7c980bd`
   - **Location**: `src/native/section_layout.c:430-433`
   - **Fix**: Only add page_size offset for ELF, not Mach-O
   - Symbol addresses now correctly calculated (e.g., main at 0x100000000 not 0x100004000)

### ⚠️ Outstanding Issue: Executable Crashes on Launch

**Symptom**: Generated executable receives SIGKILL (exit code 137) immediately when run

**Current State**:
- Custom linker completes successfully
- Mach-O file structure appears valid (`file` recognizes it as Mach-O executable)
- Entry point symbol correctly resolved to `0x100000000`
- **BUT**: `LC_MAIN` command has `entryoff = 0`

**Key Observation**:
```bash
# Custom linker output:
otool -l basic | grep -A3 "LC_MAIN"
       cmd LC_MAIN
   cmdsize 24
  entryoff 0        ← WRONG!
 stacksize 0

# System linker output (working executable):
       cmd LC_MAIN
   cmdsize 24
  entryoff 1440     ← CORRECT!
 stacksize 0
```

**Root Cause Hypothesis**:
The entry point file offset calculation in `src/native/macho_executable.c:504` is incorrect:
```c
main_cmd.entryoff = context->entry_point - text_vm_addr;
```

This calculates: `0x100000000 - 0x100000000 = 0`

**Problem**: The formula assumes `entry_point` and `text_vm_addr` are both virtual addresses, resulting in offset 0. However:
- `entryoff` should be the **file offset** (bytes from start of file), not virtual address offset
- For Mach-O on macOS, the `__TEXT` segment includes the Mach-O header in the file
- The actual executable code starts at file offset = `text_file_offset` (typically 16384 = 0x4000)
- The `main` function is at some offset within the `__text` section

**Expected Calculation**:
```c
// entry_point is virtual address of main (e.g., 0x100000000)
// text_vm_addr is virtual address of __TEXT segment (e.g., 0x100000000)
// text_file_offset is file offset of __TEXT segment (e.g., 16384)
// __text section starts at text_file_offset in the file

// Correct formula:
uint64_t entry_virt_offset = context->entry_point - text_vm_addr;  // Offset within __TEXT segment
main_cmd.entryoff = text_file_offset + entry_virt_offset;
```

## Action Plan for Next Agent

### Priority 1: Fix Entry Point File Offset Calculation

**File**: `src/native/macho_executable.c`

**Current Code** (line 504):
```c
main_cmd.entryoff = context->entry_point - text_vm_addr;
```

**Proposed Fix**:
```c
// Calculate entry point as file offset, not virtual offset
// entry_point is virtual address, need to convert to file offset
uint64_t entry_virt_offset = context->entry_point - text_vm_addr;
main_cmd.entryoff = text_file_offset + entry_virt_offset;
```

**Where `text_file_offset` is defined**: Line 277 in same file:
```c
uint64_t text_file_offset = round_up_to_page(header_size + load_cmds_size, page_size);
```

**Testing**:
1. Make the change
2. Rebuild: `make build-debug`
3. Generate executable: `./build/sox src/test/scripts/basic.sox --native --custom-linker`
4. Verify entry offset: `otool -l src/test/scripts/basic | grep -A3 "LC_MAIN"`
5. Run executable: `./src/test/scripts/basic`
6. Expected output: `hello world`

### Priority 2: Clean Up Debug Traces

Once the executable runs successfully, remove all temporary debug output:

**Files with debug traces to clean**:
1. `src/main.c:28` - Remove `[MAIN] Entered main()` trace
2. `src/lib/file.c:235, 341` - Remove `[FILE]` traces
3. `src/lib/file.c:335-338` - Re-enable `remove(object_file)` and remove DEBUG print
4. `src/lib/linker.c:701-704` - Remove `[CUSTOM LINKER] DEBUG:` trace
5. `src/lib/linker.c:723-731` - Remove all `[LINKER-CLEANUP]` traces
6. `src/native/linker_core.c:66-112` - Remove all `[CONTEXT-FREE]` traces
7. `src/native/linker_core.c:262-277` - Remove all `[SECTION-FREE]` traces
8. `src/native/macho_executable.c:244, 256, 696, 704` - Remove all `[MACHO-EXEC]` traces

**How to verify**: Search for `fprintf(stderr, "[` and remove all debug traces

### Priority 3: Verify with Additional Test Cases

Once `basic.sox` works, test with more complex programs:

```bash
# Test arithmetic
./build/sox src/test/scripts/test_arithmetic.sox --native --custom-linker
./src/test/scripts/test_arithmetic

# Test strings
./build/sox src/test/scripts/test_strings.sox --native --custom-linker
./src/test/scripts/test_strings

# Test variables
./build/sox src/test/scripts/test_variables.sox --native --custom-linker
./src/test/scripts/test_variables
```

### Priority 4: Compare Output with System Linker

For thorough verification:

```bash
# Generate with both linkers
./build/sox test.sox --native --custom-linker -o test_custom
./build/sox test.sox --native -o test_system

# Compare structure
otool -l test_custom > custom.txt
otool -l test_system > system.txt
diff custom.txt system.txt

# Compare symbols
nm test_custom > custom_syms.txt
nm test_system > system_syms.txt
diff custom_syms.txt system_syms.txt

# Run both and compare output
./test_custom > custom_output.txt 2>&1
./test_system > system_output.txt 2>&1
diff custom_output.txt system_output.txt
```

## Technical Context

### How Mach-O Entry Points Work

1. **Virtual Address Space**: Process memory starts at 0x100000000 (macOS ARM64)
2. **File Layout**:
   - Mach-O header at file offset 0
   - Load commands follow header
   - `__TEXT` segment starts at file offset 0x4000 (16384) - page-aligned
   - `__text` section is within `__TEXT` segment

3. **Virtual Memory Mapping**:
   - `__TEXT` segment mapped to virtual address 0x100000000
   - File offset 0x4000 maps to virtual address 0x100000000
   - If `main` is at virtual address 0x100000000, it's at file offset 0x4000

4. **LC_MAIN Command**:
   - `entryoff` = file offset where `main` function starts
   - OS loads file into memory, then jumps to: `base_address + entryoff`
   - If `entryoff = 0`, OS tries to execute the Mach-O header → SIGKILL!

### Custom Linker Architecture

**5 Phases** (all working):
1. **Object Reading**: Parse Mach-O .o files and extract runtime library
2. **Symbol Resolution**: Build symbol table, resolve undefined symbols
3. **Section Layout**: Merge sections, assign virtual addresses
4. **Relocation Processing**: Apply relocations to merged code
5. **Executable Generation**: Write final Mach-O executable

**Key Data Structures**:
- `linker_context_t`: Global linker state, merged sections, symbols
- `section_layout_t`: Virtual address assignment for merged sections
- `symbol_resolver_t`: Symbol table and resolution logic
- `linker_section_t`: Merged section with data, vaddr, size

## Debugging Tips

### If executable still crashes:

1. **Check load commands**:
   ```bash
   otool -l executable | less
   # Verify __TEXT, __DATA, __LINKEDIT segments
   # Check LC_MAIN entryoff value
   # Verify LC_LOAD_DYLIB for libsox_runtime
   ```

2. **Inspect entry point**:
   ```bash
   # Get entry virtual address
   nm executable | grep " _main"

   # Get entry file offset from LC_MAIN
   otool -l executable | grep -A3 "LC_MAIN"

   # Verify: entryoff should equal (main_vaddr - text_vaddr) + text_file_offset
   ```

3. **Use lldb to see crash location**:
   ```bash
   lldb executable
   (lldb) run
   # Will show exactly where it crashes
   ```

4. **Compare with working executable**:
   ```bash
   # Generate working version with system linker
   gcc -o working test.o -L./build -lsox_runtime

   # Compare headers side-by-side
   diff <(otool -l working) <(otool -l custom_linker_output)
   ```

### If you need to investigate deeper:

**Enable verbose linker output**:
```bash
./build/sox test.sox --native --custom-linker --native-debug
```

**Check merged sections**:
Add this debug output in `src/lib/linker.c` after section layout (line 660):
```c
section_layout_print(layout);  // Shows all merged sections and addresses
```

**Verify symbol addresses**:
Temporarily set `verbose = true` in `src/native/symbol_resolver.c:785` to see detailed symbol address calculations.

## Expected Outcome

After fixing the `entryoff` calculation:
```bash
$ ./build/sox src/test/scripts/basic.sox --native --custom-linker
[1/4] Building IR from bytecode...
[2/4] Generating ARM64 machine code...
[3/4] Writing output file...
[4/4] Linking object file into executable...
Successfully linked executable: src/test/scripts/basic

$ ./src/test/scripts/basic
hello world

$ echo $?
0
```

Success criteria:
- ✅ Executable runs without crashing
- ✅ Produces correct output
- ✅ Exit code is 0
- ✅ Works for multiple test programs
- ✅ All debug traces removed

## Files Modified in This Session

**Committed**:
- `src/lib/linker.c` - Fixed double-free, entry point symbol lookup
- `src/native/section_layout.c` - Fixed Mach-O section address calculation
- `src/lib/file.c` - Temporary: keep object files for debugging

**Need cleanup** (debug traces):
- `src/main.c`
- `src/lib/file.c`
- `src/lib/linker.c`
- `src/native/linker_core.c`
- `src/native/macho_executable.c`

## Git Status

Current branch: `feature/custom-linker-integration`

Recent commits:
```
7c980bd - Fix Mach-O entry point symbol resolution and section layout
10b31bb - Temporarily keep object files for debugging symbol resolution
b38f063 - Fix double-free bug in custom linker section data cleanup
5cc0a5f - Add bzero to system library symbols and debug custom linker hang
```

Ready to push to remote once this plan is committed.

## Contact/Questions

This investigation successfully resolved the cleanup hang and made major progress on executable generation. The remaining issue is a single line fix in the entry point offset calculation.

Good luck! 🚀

# Custom Linker Test Suite

This directory contains comprehensive tests for the Sox custom linker implementation.

## Test Files

### `custom_linker_integration_test.c`
End-to-end integration tests for custom linker functionality.

**Test Coverage:**
- ✅ **Basic Compilation**: Verifies custom linker can generate executables
- ✅ **Execution**: Runs custom-linker-generated binaries and validates output
- ✅ **Arithmetic Operations**: Tests numeric computations in custom-linked binaries
- ✅ **String Operations**: Tests string handling in custom-linked binaries
- ✅ **Variable Management**: Tests variable storage and retrieval
- ✅ **Cleanup**: Ensures no memory leaks across multiple compilations
- ✅ **Cross-Validation**: Compares custom linker vs system linker output

**Requirements:**
- Only runs on ARM64 architecture
- Requires macOS (uses Mach-O format)
- Tests use `use_custom_linker = true` flag

### `macho_validation_test.c`
Deep structural validation of Mach-O executable format.

**Test Coverage:**
- ✅ **Entry Point Offset**: Validates LC_MAIN entryoff is non-zero and correct
- ✅ **Entry Point Calculation**: Verifies virtual→file offset conversion
- ✅ **Load Commands**: Validates structure and ordering of Mach-O load commands
- ✅ **Segment Alignment**: Checks page alignment of __TEXT and __DATA segments
- ✅ **Structure Comparison**: Compares custom vs system linker Mach-O layout
- ✅ **Executable Runs**: Verifies generated binary actually executes without crashing

**Requirements:**
- Only runs on ARM64 macOS
- Uses Mach-O headers (`<mach-o/loader.h>`)
- Validates fix for entry point file offset bug

## Running the Tests

### On ARM64 macOS (Tests Will Run)
```bash
make test
```

The custom linker tests will execute and validate:
1. Compilation with `--custom-linker` flag works
2. Generated executables run without crashing
3. Output matches system linker output
4. Mach-O structure is valid

### On Linux/x86_64 (Tests Will Skip)
```bash
make test
```

You'll see:
```
⚠️  Skipping custom linker integration tests: Only supported on ARM64
```

Tests compile but are skipped at runtime.

## Test Architecture

### Custom Linker Integration Tests

```
┌─────────────────────────────────────────────┐
│  compile_with_custom_linker()               │
│  ├─ use_custom_linker = true                │
│  ├─ Generates native ARM64 executable       │
│  └─ Returns success/failure                 │
└─────────────────────────────────────────────┘
            │
            ▼
┌─────────────────────────────────────────────┐
│  execute_binary()                           │
│  ├─ Sets DYLD_LIBRARY_PATH                  │
│  ├─ Executes binary via popen()             │
│  └─ Captures stdout/stderr                  │
└─────────────────────────────────────────────┘
            │
            ▼
┌─────────────────────────────────────────────┐
│  Compare with system linker output          │
│  └─ Validates identical behavior            │
└─────────────────────────────────────────────┘
```

### Mach-O Validation Tests

```
┌─────────────────────────────────────────────┐
│  Generate executable with custom linker     │
└─────────────────────────────────────────────┘
            │
            ▼
┌─────────────────────────────────────────────┐
│  Read Mach-O header and load commands       │
│  ├─ find_load_command(LC_MAIN)              │
│  ├─ find_text_segment(__TEXT)               │
│  └─ Parse structure                         │
└─────────────────────────────────────────────┘
            │
            ▼
┌─────────────────────────────────────────────┐
│  Validate critical fields                   │
│  ├─ entryoff > 0 (not pointing to header)   │
│  ├─ entryoff in valid range                 │
│  ├─ vmaddr page-aligned                     │
│  └─ Sections properly laid out              │
└─────────────────────────────────────────────┘
```

## Critical Tests for Entry Point Fix

The following tests specifically validate the entry point file offset fix from commit `ea49551`:

1. **`test_entry_point_offset_valid`**
   - Ensures `entryoff` is non-zero (was 0 before fix)
   - Validates offset is in reasonable range (>4KB, <1MB)

2. **`test_entry_point_offset_calculation`**
   - Verifies formula: `entryoff = text_file_offset + (entry_virt - text_vmaddr)`
   - Ensures entry point is within __TEXT segment file region

3. **`test_executable_runs`**
   - **Most critical test**: Actually runs the generated executable
   - This would fail with SIGKILL if entryoff was 0 (pre-fix behavior)
   - Validates the binary executes without crashing

## Test Files Used

All tests use files from `src/test/scripts/`:
- `basic.sox` - Simple "hello world" print statement
- `native/constants.sox` - Constant value tests
- `native/variables.sox` - Variable assignment and access
- `native/arithmetic.sox` - Numeric operations
- `native/strings.sox` - String manipulation

## Expected Output (ARM64 macOS)

```
sox/custom_linker/basic_compilation         [ OK    ]
sox/custom_linker/execution                  [ OK    ]
sox/custom_linker/arithmetic                 [ OK    ]
sox/custom_linker/strings                    [ OK    ]
sox/custom_linker/variables                  [ OK    ]
sox/custom_linker/cleanup                    [ OK    ]
sox/custom_linker/comparison/custom_vs_system[ OK    ]
sox/macho_validation/entry_point_offset_valid[ OK    ]
sox/macho_validation/entry_point_offset_calculation[ OK    ]
sox/macho_validation/load_commands_structure [ OK    ]
sox/macho_validation/segment_alignment       [ OK    ]
sox/macho_validation/structure_comparison    [ OK    ]
sox/macho_validation/executable_runs         [ OK    ]
```

## CI Integration

These tests are automatically run by GitHub Actions on `macos-latest` (ARM64) runners.

See `.github/workflows/ubuntu-build.yml`:
```yaml
- os: macos-latest
  compiler: g++-10
  target: Macos
```

The CI will:
1. Build sox with custom linker enabled
2. Run all tests including custom linker integration tests
3. Validate Mach-O structure
4. Execute generated binaries to ensure they don't crash

## Debugging Test Failures

### "Error: child killed by signal 6 (Aborted)"
- Indicates SIGABRT during execution
- Check if runtime library is in library path
- Verify `DYLD_LIBRARY_PATH=./build` is set

### "Error: child killed by signal 9 (SIGKILL)"
- Indicates OS killed the process
- **This suggests entry point offset is wrong** (points to header instead of code)
- Run: `otool -l <executable> | grep -A3 LC_MAIN` to check entryoff value

### Custom linker tests skipped
- Normal on non-ARM64 platforms
- Check architecture: `uname -m` should show `arm64`

### Mach-O validation tests skipped
- Normal on non-macOS platforms
- Check OS: `uname -s` should show `Darwin`

## Adding New Tests

To add a new custom linker test:

1. Add test function in `custom_linker_integration_test.c`:
```c
static MunitResult test_my_feature(const MunitParameter params[], void* data) {
    #ifndef __aarch64__
    return MUNIT_SKIP;
    #endif

    const char* output = "/tmp/sox_my_test";
    bool result = compile_with_custom_linker("test.sox", output);
    munit_assert_true(result);

    // Add validations...

    unlink(output);
    return MUNIT_OK;
}
```

2. Add to test suite array:
```c
static MunitTest custom_linker_tests[] = {
    // ... existing tests ...
    {"/my_feature", test_my_feature, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL},
    {NULL, NULL, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL}
};
```

## References

- [NEXT_AGENT_PLAN.md](../../NEXT_AGENT_PLAN.md) - Original plan for entry point fix
- [Mach-O File Format](https://github.com/aidansteele/osx-abi-macho-file-format-reference)
- [LC_MAIN specification](https://opensource.apple.com/source/xnu/xnu-2050.18.24/EXTERNAL_HEADERS/mach-o/loader.h)

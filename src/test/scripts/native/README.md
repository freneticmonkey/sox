# Native Code Generation Tests

This directory contains integration tests for Sox's ARM64/x86-64 native code generation.

## Test Structure

Each test consists of two files:
- `*.sox` - Sox source code to compile to native binary
- `*.sox.out` - Expected output when the native binary is executed

## Running Tests

### Manual Testing

Compile and run a single test:
```bash
./build/sox src/test/scripts/native/constants.sox --native --native-out=/tmp/test_native
DYLD_LIBRARY_PATH=./build:$DYLD_LIBRARY_PATH /tmp/test_native
```

### Automated Testing

Run all native tests:
```bash
./tools/test_native.sh
```

This script will:
1. Compile each `.sox` file to a native binary
2. Execute the binary and capture output
3. Compare output against the `.sox.out` file
4. Report pass/fail for each test

## Current Test Coverage

| Test | Description | Status |
|------|-------------|--------|
| `constants.sox` | Number, boolean, and nil constants | ✅ PASS |
| `arithmetic.sox` | Arithmetic operations (+, -, *, /, negate) | ✅ PASS |
| `variables.sox` | Variable assignment and access | ✅ PASS |
| `multiple_vars.sox` | Multiple variables with operations | ✅ PASS |
| `comparisons.sox` | Comparison operators (==, !=, <, >) | ✅ PASS |
| `logic.sox` | Logical operators (and, or, not) | ✅ PASS |
| `strings.sox` | String constant printing | ⚠️  PARTIAL (outputs nil) |

## Known Issues

### strings.sox - String Constants Not Fully Implemented
String constants are detected and handled by the IR builder, but full string support requires embedding string data in the object file, which is not yet implemented. Currently, string constants are loaded as `nil` values.

**Status**: Partial implementation complete, full support pending

**Current State (2025-12-26)**:
- ✅ IR builder detects string constants and creates `IR_CONST_STRING` instructions
- ✅ String data is extracted and stored in IR
- ✅ ARM64 codegen generates valid code (loads NIL as placeholder)
- ✅ No crashes or timeouts
- ❌ Actual string values not yet embedded in object file

**To Implement Full String Support**:
1. Add `__cstring` or `__data` section support to Mach-O writer
2. Embed string literal data in object file
3. Create symbols for each string literal
4. Generate PC-relative addressing (ADRP + ADD) with relocations to load string addresses
5. Generate calls to `sox_native_alloc_string(char* data, size_t length)`
6. Return allocated string `value_t`

### Previous Issue: Logical Operators Timeout (RESOLVED)

**Root Cause**: The IR builder was not properly tracking jump targets and creating basic blocks for jumps. Jump labels were allocated but never associated with actual code blocks, causing jumps to invalid addresses.

**Fix Applied (2025-12-26)**: Implemented two-pass IR building:
1. First pass scans bytecode to identify all jump targets and pre-allocates labels
2. Second pass builds IR using pre-allocated labels and creates new basic blocks at jump target positions
3. Jump instructions now correctly reference actual code locations

This fix resolved the logical operators (`and`, `or`) timeout issue. Logical operators use short-circuit evaluation with conditional jumps, which now work correctly.

## Adding New Tests

To add a new test:

1. Create `test_name.sox` with your test code
2. Create `test_name.sox.out` with the expected output
3. Run `./tools/test_native.sh` to verify

Example:
```bash
# Create test
cat > src/test/scripts/native/my_test.sox << 'EOF'
print(1 + 2);
EOF

# Create expected output
cat > src/test/scripts/native/my_test.sox.out << 'EOF'
3
EOF

# Run tests
./tools/test_native.sh
```

## Test Requirements

- Sox compiler must be built: `make build`
- Runtime library must be built: `make build-runtime`
- Tests must complete within 3 seconds (timeout protection)

## Debugging Failed Tests

If a test fails, you can run it manually to see detailed output:

```bash
# Compile with debug output
./build/sox src/test/scripts/native/test_name.sox --native --native-out=/tmp/debug_test

# Run with full output
DYLD_LIBRARY_PATH=./build:$DYLD_LIBRARY_PATH /tmp/debug_test
```

## Platform Support

- **macOS ARM64**: Fully supported ✅
- **macOS x86-64**: Should work (untested)
- **Linux ARM64**: Should work (untested)
- **Linux x86-64**: Should work (untested)

The test script automatically detects the platform and sets the correct library path variable (`DYLD_LIBRARY_PATH` on macOS, `LD_LIBRARY_PATH` on Linux).

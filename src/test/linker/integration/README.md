# Linker Integration Tests

**Phase 6.2:** Integration testing for the custom linker implementation.

---

## Overview

This directory contains integration tests for the Sox custom linker. These tests validate end-to-end linking behavior from Sox source code through object file generation and final executable creation.

## Test Structure

Each test consists of:
- **`.sox` file(s):** Source code to be compiled and linked
- **`.sox.out` file:** Expected output when the executable runs

## Test Cases

### 1. `single_object.sox`
**Description:** Single object file linking
**Purpose:** Validates basic compilation and linking of a simple program
**Expected Output:** `12`

### 2. `multi_object_a.sox` + `multi_object_b.sox`
**Description:** Multi-object file linking
**Purpose:** Tests cross-module symbol resolution and function calls
**Expected Output:** `12\n25`

### 3. `global_vars.sox`
**Description:** Global variable references
**Purpose:** Validates global variable layout and access across functions
**Expected Output:** `13`

### 4. `runtime_calls.sox`
**Description:** Runtime library integration
**Purpose:** Ensures runtime library functions are properly linked
**Expected Output:** `Testing runtime print\n30\n200\nAll runtime tests passed`

## Running Tests

### Quick Start
```bash
./run_tests.sh
```

### Expected Output (Current State)
```
========================================
  Phase 6.2: Linker Integration Tests
========================================

Testing: linker_api_smoke
  Description: Verify linker_link() API was successfully integrated
[PASS] linker_api_smoke

Testing: single_object
[SKIP] single_object: Custom linker phases 1-5 not yet implemented

Testing: multi_object
[SKIP] multi_object: Custom linker phases 1-5 not yet implemented

Testing: global_vars
[SKIP] global_vars: Custom linker phases 1-5 not yet implemented

Testing: runtime_calls
[SKIP] runtime_calls: Custom linker phases 1-5 not yet implemented

========================================
  Test Summary
========================================
Total:   5
Passed:  1
Failed:  0
Skipped: 4
```

## Test Status

- ✅ **linker_api_smoke:** Verifies the `linker_link()` API compiles and links successfully
- ⏸️ **single_object:** Waiting for Phases 1-5 implementation
- ⏸️ **multi_object:** Waiting for Phases 1-5 implementation
- ⏸️ **global_vars:** Waiting for Phases 1-5 implementation
- ⏸️ **runtime_calls:** Waiting for Phases 1-5 implementation

## When Custom Linker Is Ready

Once Phases 1-5 of the custom linker are implemented, these tests will:

1. **Compile** `.sox` files to native object files
2. **Link** object files using `linker_link()` API
3. **Execute** the resulting binary
4. **Compare** output with `.sox.out` files

### Example Flow (Future)
```bash
# Compile
sox --native single_object.sox -o single_object.o

# Link (using custom linker)
sox --link single_object.o -o single_object --use-custom-linker

# Run
./single_object > output.txt

# Verify
diff output.txt single_object.sox.out
```

## Adding New Tests

To add a new integration test:

1. Create `<test_name>.sox` with your test code
2. Create `<test_name>.sox.out` with expected output
3. Add test function to `run_tests.sh`:
   ```bash
   test_<test_name>() {
       local test_name="<test_name>"
       # ... test implementation
   }
   ```
4. Call the test in `main()` function

## Test Runner Features

- **Color-coded output:** Green (PASS), Red (FAIL), Yellow (SKIP)
- **Comprehensive summary:** Total, Passed, Failed, Skipped counts
- **Automatic cleanup:** Removes temporary files after execution
- **Detailed diagnostics:** Shows test descriptions and error messages
- **Exit codes:** Returns 0 on success, 1 on failure

## Implementation References

- **Custom Linker Plan:** `/Users/scott/development/projects/sox/plans/custom-linker-implementation.md`
- **Phase 6 Summary:** `/Users/scott/development/projects/sox/plans/PHASE6_INTEGRATION_SUMMARY.md`
- **Linker API:** `/Users/scott/development/projects/sox/src/lib/linker.h`
- **Linker Implementation:** `/Users/scott/development/projects/sox/src/lib/linker.c`

## Troubleshooting

### Sox binary not found
```bash
Error: Sox binary not found at /path/to/sox
Please run 'make build-debug' first
```
**Solution:** Run `make build-debug` from the project root

### All tests skipped
**Expected Behavior:** Tests will be skipped until custom linker Phases 1-5 are implemented

### Test failures
**Check:**
1. Build succeeded without errors (`make build-debug`)
2. Existing tests pass (`make test`)
3. Test runner has execute permissions (`chmod +x run_tests.sh`)

---

**Last Updated:** December 26, 2025
**Status:** Integration framework ready, awaiting Phases 1-5 implementation

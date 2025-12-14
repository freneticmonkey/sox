# Cloud Agent Handoff: Calling Convention Implementation Phases 4-8

**Date**: 2025-12-03
**Branch**: `feature/calling-convention-phases-4-8`
**Scope**: Phases 4 through 8 of calling convention implementation
**Status**: Ready for implementation

## Quick Start

### Branch Setup
```bash
git checkout feature/calling-convention-phases-4-8
# Branch contains: all Phase 1-3 completed work, ready for Phase 4
```

### Build and Test
```bash
make clean
make debug
make test
# Expected: 86/86 tests passing
```

## What Has Been Completed (Phases 1-3)

### Phase 1: Diagnosis & Foundation (COMPLETE)
- Comprehensive test suite: `src/test/calling_convention_tests.c` (13 tests)
- Root cause analysis: `src/test/CALLING_CONVENTION_DIAGNOSIS.md`
- All critical issues identified and documented

### Phase 2: x86-64 Stack Alignment (COMPLETE)
- Fixed prologue/epilogue to be ABI-compliant
- Stack alignment guaranteed 16-byte at all call sites
- Files modified:
  - `src/native/codegen.h` - Stack tracking
  - `src/native/codegen.c` - Prologue/epilogue refactoring

### Phase 3: External Function Calls - x86-64 (COMPLETE)
- Argument marshalling: RDI, RSI, RDX, RCX, R8, R9 + stack
- Return value handling from RAX
- PLT32 relocations for external symbols
- Files modified:
  - `src/native/ir.h/c` - Extended call instruction
  - `src/native/ir_builder.c` - Capture arguments
  - `src/native/codegen.c` - marshal_arguments_x64()
  - `src/native/codegen.h` - Relocation support
  - `src/native/elf_writer.c/h` - Relocation writing

## Your Task: Phases 4-8

### Phase 4: ARM64 Register Mapping Fix (NEXT - 8 hours)

**Goal**: Fix broken ARM64 register mapping

**Critical Issue**:
- Current code uses x86-64 register numbers (0-17) for ARM64 instructions
- ARM64 has 32 GP registers (X0-X31) and 32 FP registers (V0-V31)
- Causes illegal instructions and complete code generation failure

**Files to Modify**:
1. `src/native/arm64_encoder.h`
   - Add complete ARM64 register constant definitions (X0-X31, V0-V31)
   - Add helper macros for register classification

2. `src/native/codegen_arm64.c`
   - Fix prologue to use: `stp x29, x30, [sp, #-frame_size]!` followed by `mov x29, sp`
   - Fix epilogue to use: `ldp x29, x30, [sp], #frame_size` followed by `ret`
   - Current broken code around lines 102-154

3. `src/native/regalloc.c` (or relevant allocator file)
   - Create ARM64-specific register availability info
   - Define which registers are caller-saved vs callee-saved
   - Ensure allocator uses ARM64 register numbers

**Verification**:
```bash
make test
# Expected: 86/86 tests still passing (no x86-64 regressions)

# For ARM64 verification (if cross-compilation available):
# objdump -d generated_arm64.o
# Should show: stp x29, x30, [sp, #-n]! in prologue
```

**Success Criteria**:
- [ ] ARM64 register constants defined (X0-X31, V0-V31)
- [ ] Prologue uses proper stp/mov encoding
- [ ] Epilogue uses proper ldp/ret encoding
- [ ] Register allocator aware of ARM64 registers
- [ ] All 86 x86-64 tests still pass
- [ ] Commit: "Phase 4: ARM64 Register Mapping Fix"

### Phase 5: ARM64 External Calls (12 hours, depends on Phase 4)

**Goal**: Implement external function calls for ARM64

**Similar to Phase 3, but for ARM64 ABI**:
- Argument registers: X0-X7 (instead of RDI-R9)
- Floating-point: V0-V7 (instead of XMM0-XMM7)
- Return value: X0 (instead of RAX)
- Relocation type: CALL26 (instead of PLT32)

**Files to Modify**:
1. `src/native/codegen_arm64.c`
   - Create `marshal_arguments_arm64()` function
   - Update IR_CALL handler for ARM64

2. `src/native/elf_writer.c`
   - Add ARM64-specific relocation types
   - Support CALL26 relocations

**Verification**:
```bash
make test
# Still 86/86 passing

# If ARM64 target available:
# ./sox --compile-native --arch arm64 test.sox
# objdump -d test.o | grep -A 20 "call"
# Should show proper X0-X7 argument setup
```

**Success Criteria**:
- [ ] ARM64 argument marshalling working
- [ ] Stack arguments handled for 8+ parameters
- [ ] ARM64 relocations emitted correctly
- [ ] All x86-64 tests still pass
- [ ] Commit: "Phase 5: ARM64 External Calls - Complete"

### Phase 6: Runtime Integration (8 hours, depends on Phase 3 & 5)

**Goal**: Map Sox operations to C runtime functions

**Overview**:
- Create runtime library with C functions for Sox operations
- Type checking, arithmetic, string operations, etc.
- Link generated code with runtime library

**Files to Create/Modify**:
1. `src/native/runtime.h` (NEW)
   - Declare all runtime functions
   - Type definitions for sox_value_t

2. `src/native/runtime.c` (NEW)
   - Implement runtime operations
   - Type checking and conversion
   - Memory management interface

3. `src/native/codegen.c` and `codegen_arm64.c`
   - Map IR_RUNTIME_CALL to external runtime functions
   - Both architectures use same runtime interface

**Key Functions to Implement**:
```c
// Type checking
bool runtime_is_number(sox_value_t val);
bool runtime_is_string(sox_value_t val);

// Arithmetic (with type checking)
sox_value_t runtime_add(sox_value_t a, sox_value_t b);
sox_value_t runtime_subtract(sox_value_t a, sox_value_t b);
sox_value_t runtime_multiply(sox_value_t a, sox_value_t b);

// String operations
sox_value_t runtime_concat(sox_value_t a, sox_value_t b);
const char* runtime_to_string(sox_value_t val);

// Output
void runtime_print(sox_value_t val);
```

**Success Criteria**:
- [ ] Runtime library created and callable
- [ ] All core operations mapped to runtime functions
- [ ] Linking with generated code works
- [ ] All 86 tests still pass
- [ ] Commit: "Phase 6: Runtime Integration - Complete"

### Phase 7: Integration & Testing (8 hours, depends on Phase 6)

**Goal**: Comprehensive integration testing across all features

**Test Cases to Create**:
1. Simple arithmetic: `print(2 + 3)` → 5
2. Variables: `x = 5; print(x)` → 5
3. Function calls: `fun add(a, b) { return a + b }; print(add(10, 20))` → 30
4. Multiple arguments: `fun greet(name, age) { ... }; greet("Alice", 30)`
5. String operations: String concatenation and conversion
6. Mixed operations: Complex expressions with multiple operations
7. Both architectures: x86-64 and ARM64 (if cross-compilation available)

**Validation Process**:
```bash
# Generate and run tests
for test_file in src/test/scripts/test_*.sox; do
    output=$(./sox "$test_file")
    expected=$(cat "${test_file}.out")
    if [ "$output" == "$expected" ]; then
        echo "✓ $(basename $test_file)"
    else
        echo "✗ $(basename $test_file)"
    fi
done
```

**Documentation**:
- Create comprehensive test report
- Document any issues and workarounds
- Compare with reference implementations (clang-generated code)

**Success Criteria**:
- [ ] 20+ integration tests passing
- [ ] All previous tests still passing (86/86)
- [ ] Output matches expected results
- [ ] Cross-platform validation (if possible)
- [ ] Comprehensive documentation
- [ ] Commit: "Phase 7: Integration & Testing - Complete"

### Phase 8: Performance & Polish (6 hours)

**Goals**: Optimization and edge case handling

**Optimization Opportunities**:
1. **Selective register saving**: Only save registers actually used in function
2. **Tail call optimization**: Detect tail calls and avoid prologue/epilogue
3. **Dead code elimination**: Remove unused computations
4. **Instruction scheduling**: Reorder for pipeline efficiency

**Edge Cases to Handle**:
1. Variadic functions (printf with multiple types)
2. Large return values
3. Floating-point arguments and returns
4. Stack arguments beyond typical counts

**Code Cleanup**:
- Refactor code duplication between x86-64 and ARM64
- Improve error messages
- Add comprehensive comments to complex logic
- Ensure consistent coding style

**Success Criteria**:
- [ ] Code generation < 10% of compile time
- [ ] Edge cases properly handled
- [ ] All 86+ tests passing
- [ ] Code is clean and maintainable
- [ ] Comprehensive documentation
- [ ] Commit: "Phase 8: Performance & Polish - Complete"

## Key Files and Locations

### Core Code Generation
- `src/native/codegen.c` - Main x86-64 code generation (x86-64 complete, ARM64 needs work)
- `src/native/codegen_arm64.c` - ARM64-specific code (needs major fixes)
- `src/native/codegen.h` - Codegen context and helpers
- `src/native/ir.h/c` - Intermediate representation
- `src/native/ir_builder.c` - Bytecode to IR conversion

### Architecture-Specific
- `src/native/x64_encoder.h` - x86-64 instruction encoding (complete)
- `src/native/arm64_encoder.h` - ARM64 instruction encoding (needs register fixes)

### ELF Output
- `src/native/elf_writer.h/c` - ELF object file generation with relocations
- `src/native/native_codegen.c` - Pipeline orchestration

### Testing
- `src/test/calling_convention_tests.c` - 13 comprehensive tests
- `src/test/CALLING_CONVENTION_DIAGNOSIS.md` - Detailed diagnostics
- `src/test/scripts/test_alignment*.sox` - Phase 2 verification tests

### Documentation
- `plans/2025-12-03-calling-convention-implementation.md` - Master plan (updated with Phase 4-8 details)
- `IMPLEMENTATION_STATUS.md` - Detailed progress report
- `PHASE2_VERIFICATION.md` - Phase 2 verification details
- `PHASE2_BEFORE_AFTER.md` - Before/after comparison
- `PHASE2_SUMMARY.md` - Phase 2 summary

## System V AMD64 ABI Reference

**Integer Arguments**: RDI, RSI, RDX, RCX, R8, R9
**Floating-Point Arguments**: XMM0-XMM7
**Return Value**: RAX (RDX:RAX for 128-bit)
**Callee-Saved**: RBX, RSP, RBP, R12-R15
**Caller-Saved**: RAX, RCX, RDX, RSI, RDI, R8-R11, XMM0-XMM15

## ARM64 EABI Reference

**Integer Arguments**: X0-X7
**Floating-Point Arguments**: V0-V7
**Return Value**: X0 (X0:X1 for 128-bit)
**Link Register**: X30 (return address holder, callee-saved)
**Frame Pointer**: X29
**Callee-Saved**: X19-X28, X29, X30, V8-V15

## Testing Commands

```bash
# Full build and test
make clean
make debug
make test

# Run specific test
./bin/x64/Debug/sox src/test/scripts/test_alignment.sox

# Inspect generated code
objdump -d generated.o | head -100

# Verify relocations
objdump -r generated.o

# Check for warnings
make debug 2>&1 | grep -i warning
```

## Common Issues and Solutions

### Issue: "Undefined reference to symbol"
**Cause**: Relocation not properly recorded
**Solution**: Check relocation recording in codegen_add_relocation()

### Issue: "Illegal instruction" at runtime
**Cause**: Invalid register numbers in generated code (usually ARM64)
**Solution**: Verify register constants are correct (X0-X31, not 0-17)

### Issue: "Segmentation fault" on function call
**Cause**: Stack misalignment before call
**Solution**: Verify 16-byte alignment before call instruction

### Issue: "Return value incorrect"
**Cause**: RAX not transferred to destination register
**Solution**: Check return value handling in IR_CALL

### Issue: "Arguments not passed correctly"
**Cause**: marshal_arguments function not called or incomplete
**Solution**: Verify argument loading for all argument registers

## Progress Tracking

Use the following checklist as you complete each phase:

- [ ] Phase 4: ARM64 Register Mapping Fix
  - [ ] Registers constants defined
  - [ ] Prologue fixed
  - [ ] Epilogue fixed
  - [ ] 86/86 tests passing
  - [ ] Commit and push

- [ ] Phase 5: ARM64 External Calls
  - [ ] Argument marshalling implemented
  - [ ] Relocations working
  - [ ] Tests passing
  - [ ] Commit and push

- [ ] Phase 6: Runtime Integration
  - [ ] Runtime library created
  - [ ] All operations mapped
  - [ ] Linking working
  - [ ] Tests passing
  - [ ] Commit and push

- [ ] Phase 7: Integration & Testing
  - [ ] 20+ integration tests created
  - [ ] All tests passing
  - [ ] Documentation complete
  - [ ] Commit and push

- [ ] Phase 8: Performance & Polish
  - [ ] Optimizations applied
  - [ ] Edge cases handled
  - [ ] Code cleanup done
  - [ ] Tests passing
  - [ ] Final commit and push

## Resources and References

**Specifications**:
- x86-64 ABI: https://refspecs.linuxbase.org/elf/x86_64-abi-0.99.pdf
- ARM64 ABI: https://developer.arm.com/docs/ihi0055/latest
- ELF Format: https://refspecs.linuxbase.org/elf/elf.pdf

**Compiler Implementation**:
- Crafting Interpreters: https://craftinginterpreters.com

**Building**:
- Premake5: https://premake.github.io/docs/
- munit Testing: https://nemequ.github.io/munit/

## Support and Escalation

If you encounter issues:

1. Check `IMPLEMENTATION_STATUS.md` for context
2. Review Phase 1 diagnostics in `src/test/CALLING_CONVENTION_DIAGNOSIS.md`
3. Look at completed implementations (Phase 2-3) as reference
4. Check git commit messages for implementation rationale
5. Review test output from `make test` for regressions

## Final Notes

This is a well-scoped implementation with clear success criteria. The foundation (Phases 1-3) is solid and tested. Phases 4-8 should proceed systematically with frequent testing to catch regressions early.

Good luck! 🚀

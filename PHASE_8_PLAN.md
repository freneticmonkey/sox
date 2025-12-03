# Phase 8: Performance & Polish - Implementation Plan

## Overview

Phase 8 is the final phase of the calling convention implementation. It focuses on optimizations, edge case handling, code cleanup, and final documentation.

## Objectives

1. **Performance Optimizations**
   - Selective callee-saved register saving
   - Better stack frame size calculation
   - Eliminate unnecessary operations

2. **Edge Case Handling**
   - Large argument counts (7+ for x64, 9+ for ARM64)
   - Floating-point argument handling (future work)
   - Struct returns (future work)

3. **Code Quality**
   - Code cleanup and refactoring
   - Remove dead code and TODOs
   - Improve code comments

4. **Documentation**
   - Create comprehensive summary document
   - Update README with calling convention status
   - Document known limitations

## Current Status

### What Works ✅

- x86-64 and ARM64 code generation
- Function prologues/epilogues with proper alignment
- External function calls (IR_CALL) with relocations
- Runtime function calls (IR_RUNTIME_CALL)
- Register allocation (architecture-aware)
- Integration tests validating machine code output

### Known Issues 🔧

1. **Unconditional Register Saving**
   - Currently saves all callee-saved registers (5 on x64, 4 on ARM64)
   - Should only save registers that are actually used
   - Wastes stack space and adds unnecessary push/pop instructions

2. **Stack Frame Calculation**
   - Frame size calculation could be more precise
   - Alignment calculation spread across multiple functions

3. **Test Memory Issues**
   - 3/5 integration tests have memory management issues
   - Need to fix IR cleanup for dynamically allocated instructions

## Optimizations to Implement

### 1. Selective Callee-Saved Register Saving

**Current Code (x86-64):**
```c
// Always saves 5 registers unconditionally
x64_push_reg(ctx->asm_, X64_RBX);
x64_push_reg(ctx->asm_, X64_R12);
x64_push_reg(ctx->asm_, X64_R13);
x64_push_reg(ctx->asm_, X64_R14);
x64_push_reg(ctx->asm_, X64_R15);
```

**Optimized Code:**
```c
// Only save registers that the register allocator actually uses
int used_callee_saved_count = 0;
x64_register_t used_callee_saved[5];

for (int i = 0; i < allocator->used_register_count; i++) {
    x64_register_t reg = allocator->used_registers[i];
    if (is_callee_saved(reg)) {
        used_callee_saved[used_callee_saved_count++] = reg;
    }
}

// Push only used callee-saved registers
for (int i = 0; i < used_callee_saved_count; i++) {
    x64_push_reg(ctx->asm_, used_callee_saved[i]);
}
```

**Benefits:**
- Smaller prologues/epilogues
- Less stack usage
- Faster function entry/exit
- Better cache utilization

**Estimated Savings:**
- Simple functions: Save 10-20 bytes per function
- Save 40 bytes (5 × 8 bytes) of stack per call

### 2. Improved Stack Frame Calculation

**Current:**
- Frame size calculated by register allocator
- Callee-saved register space added separately
- Alignment calculation done in prologue

**Improved:**
```c
typedef struct {
    int locals_size;           // Space for spilled locals
    int callee_saved_count;    // Number of callee-saved regs used
    int total_frame_size;      // Total size (aligned to 16 bytes)
} frame_info_t;

frame_info_t calculate_frame_info(regalloc_context_t* regalloc) {
    frame_info_t info = {0};

    // Calculate locals size
    info.locals_size = regalloc_get_frame_size(regalloc);

    // Count callee-saved registers
    info.callee_saved_count = count_used_callee_saved(regalloc);

    // Calculate total frame size with alignment
    // x64: rbp (8) + locals + callee_saved × 8
    int raw_size = 8 + info.locals_size + (info.callee_saved_count * 8);

    // Round up to 16-byte alignment
    info.total_frame_size = (raw_size + 15) & ~15;

    return info;
}
```

**Benefits:**
- Centralized frame size calculation
- Clearer alignment logic
- Easier to maintain and debug

### 3. Remove Dead Code and TODOs

**TODOs to Address:**
- IR_PRINT placeholder implementation
- Unsupported instruction handlers
- Debug output cleanup

**Dead Code to Remove:**
- Unused helper functions
- Commented-out code
- Temporary test code

## Edge Cases to Document

### 1. Large Argument Counts

**x86-64 (System V ABI):**
- Arguments 1-6: RDI, RSI, RDX, RCX, R8, R9
- Arguments 7+: Pushed on stack in reverse order
- **Status:** ✅ Implemented in Phase 3/5

**ARM64 (EABI):**
- Arguments 1-8: X0-X7
- Arguments 9+: Pushed on stack
- **Status:** ✅ Implemented in Phase 5

### 2. Floating-Point Arguments

**x86-64:**
- FP arguments 1-8: XMM0-XMM7
- **Status:** ⏳ Not yet implemented

**ARM64:**
- FP arguments 1-8: V0-V7
- **Status:** ⏳ Not yet implemented

**Note:** Floating-point argument support deferred to future work. Current implementation focuses on integer and pointer arguments.

### 3. Variadic Functions

**Challenge:** Variadic functions (like printf) require special handling:
- x86-64: RAX holds count of FP arguments in XMM registers
- ARM64: Different ABI rules apply

**Status:** ⏳ Not yet implemented, use runtime calls for variadic functions

## Testing Strategy

### 1. Verify Existing Tests Still Pass

```bash
make test
# Should see: 88/91 tests passing (same as Phase 7)
```

### 2. Add Optimization Validation Tests

- Test that selective register saving doesn't break functionality
- Verify stack frames are still properly aligned
- Check that code size is reduced

### 3. Performance Benchmarks (Optional)

Compare code size before/after optimizations:
- Simple function prologue size
- Stack usage per function call
- Total executable size

## Documentation Updates

### 1. Create IMPLEMENTATION_SUMMARY.md

Comprehensive document covering:
- All 8 phases completed
- What was implemented in each phase
- Current capabilities and limitations
- Architecture comparison (x86-64 vs ARM64)

### 2. Update README.md

Add section on native code generation:
- Calling convention support
- Supported platforms
- Known limitations
- Future work

### 3. Create CALLING_CONVENTION.md

Technical documentation:
- System V AMD64 ABI implementation details
- ARM64 EABI implementation details
- Register usage conventions
- Stack layout diagrams

## Success Criteria

- [ ] All existing tests still pass (88/91 minimum)
- [ ] Selective register saving implemented
- [ ] Stack frame calculation improved
- [ ] Code cleanup complete (no major TODOs)
- [ ] Comprehensive documentation created
- [ ] Phase 8 committed and pushed

## Timeline

**Estimated Effort:** 4-6 hours
- Selective register saving: 2 hours
- Stack frame optimization: 1 hour
- Code cleanup: 1 hour
- Documentation: 2 hours

## Conclusion

Phase 8 completes the calling convention implementation with optimizations and polish. After this phase, Sox will have a production-ready native code generator for both x86-64 and ARM64 architectures with proper ABI compliance.

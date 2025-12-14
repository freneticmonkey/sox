# Phase 2 Implementation Verification Report

## Overview
Phase 2 of the calling convention implementation has been successfully completed. This phase focused on fixing stack alignment issues and refactoring the prologue/epilogue to be ABI-compliant.

## Changes Made

### 1. Added Stack Frame Tracking to Context Structure
**File:** `/Users/scott/development/projects/sox/src/native/codegen.h`
**Lines:** 33-35

Added two new fields to `codegen_context_t`:
```c
int current_stack_offset;     // Current RSP offset from function entry
int current_frame_alignment;  // Frame size including alignment padding
```

These fields track the stack state throughout code generation to ensure proper alignment.

### 2. Implemented Stack Alignment Helper Function
**File:** `/Users/scott/development/projects/sox/src/native/codegen.c`
**Lines:** 78-105

Added `calculate_aligned_frame_size()` function:
```c
static int calculate_aligned_frame_size(int locals_size, int saved_regs_count) {
    // Frame layout after prologue:
    // [Entry RSP]          <- RSP on function entry (must be 16n+8 due to call instruction)
    // [Return Address]     <- 8 bytes (from call instruction)
    // [RBP]                <- 8 bytes (first push in prologue)
    // [Saved regs]         <- saved_regs_count * 8 bytes
    // [Locals + padding]   <- locals_size + padding bytes
    //
    // After "push rbp", RSP is at 16n (aligned)
    // After saving callee-saved registers, RSP is at 16n - (saved_regs_count * 8)
    // We need to adjust locals_size so final RSP is 16-byte aligned
    //
    // Total bytes after push rbp: (saved_regs_count * 8) + locals_size
    // This must be a multiple of 16

    int after_saved_regs = saved_regs_count * 8;
    int total_after_rbp = after_saved_regs + locals_size;

    // Round up to nearest multiple of 16
    int aligned_total = ((total_after_rbp + 15) / 16) * 16;

    // Calculate padding needed for locals
    int aligned_locals = aligned_total - after_saved_regs;

    return aligned_locals;
}
```

This function ensures that the stack remains 16-byte aligned after all prologue operations.

### 3. Refactored x86-64 Prologue
**File:** `/Users/scott/development/projects/sox/src/native/codegen.c`
**Lines:** 124-163

**Before:**
```c
static void emit_function_prologue(codegen_context_t* ctx) {
    // push rbp
    x64_push_reg(ctx->asm_, X64_RBP);

    // mov rbp, rsp
    x64_mov_reg_reg(ctx->asm_, X64_RBP, X64_RSP);

    // sub rsp, frame_size
    int frame_size = regalloc_get_frame_size(ctx->regalloc);
    if (frame_size > 0) {
        x64_sub_reg_imm(ctx->asm_, X64_RSP, frame_size);
    }

    // Save callee-saved registers that we use
    // (This is simplified - should track which registers are actually used)
    x64_push_reg(ctx->asm_, X64_RBX);
    x64_push_reg(ctx->asm_, X64_R12);
    x64_push_reg(ctx->asm_, X64_R13);
    x64_push_reg(ctx->asm_, X64_R14);
    x64_push_reg(ctx->asm_, X64_R15);
}
```

**After:**
```c
static void emit_function_prologue(codegen_context_t* ctx) {
    // Count how many callee-saved registers we need to save
    // For now: save all 5 (RBX, R12-R15) - total 5 registers
    // Later phases can optimize to save only used registers
    int saved_regs = 5;  // RBX, R12, R13, R14, R15 (not counting RBP)

    // Get frame size from register allocator (for spilled variables)
    int locals_size = regalloc_get_frame_size(ctx->regalloc);

    // Calculate aligned frame size including padding
    int aligned_frame = calculate_aligned_frame_size(locals_size, saved_regs);

    // 1. Push RBP to save it and establish previous frame pointer
    // This creates 8-byte offset that we account for in alignment
    x64_push_reg(ctx->asm_, X64_RBP);

    // 2. Set up new frame pointer
    x64_mov_reg_reg(ctx->asm_, X64_RBP, X64_RSP);

    // 3. Save callee-saved registers that we'll use
    // These pushes are accounted for in our alignment calculation
    x64_push_reg(ctx->asm_, X64_RBX);
    x64_push_reg(ctx->asm_, X64_R12);
    x64_push_reg(ctx->asm_, X64_R13);
    x64_push_reg(ctx->asm_, X64_R14);
    x64_push_reg(ctx->asm_, X64_R15);

    // 4. Allocate space for local variables and padding
    // This ensures RSP is 16-byte aligned before any call instruction
    if (aligned_frame > 0) {
        x64_sub_reg_imm(ctx->asm_, X64_RSP, aligned_frame);
    }

    // Store the frame size for use in emit_epilogue and call site alignment
    ctx->current_frame_alignment = aligned_frame;

    // Track stack offset (for future call site alignment verification)
    // After prologue: RSP = entry_RSP - 8 (ret addr) - 8 (rbp) - 40 (saved regs) - aligned_frame
    ctx->current_stack_offset = 8 + 8 + (saved_regs * 8) + aligned_frame;
}
```

**Key Improvements:**
1. Registers are now saved BEFORE allocating local variable space
2. Local variable space is calculated to maintain 16-byte alignment
3. Stack offset is tracked for future call site validation

### 4. Refactored x86-64 Epilogue
**File:** `/Users/scott/development/projects/sox/src/native/codegen.c`
**Lines:** 165-181

**Before:**
```c
static void emit_function_epilogue(codegen_context_t* ctx) {
    // Restore callee-saved registers
    x64_pop_reg(ctx->asm_, X64_R15);
    x64_pop_reg(ctx->asm_, X64_R14);
    x64_pop_reg(ctx->asm_, X64_R13);
    x64_pop_reg(ctx->asm_, X64_R12);
    x64_pop_reg(ctx->asm_, X64_RBX);

    // mov rsp, rbp
    x64_mov_reg_reg(ctx->asm_, X64_RSP, X64_RBP);

    // pop rbp
    x64_pop_reg(ctx->asm_, X64_RBP);

    // ret
    x64_ret(ctx->asm_);
}
```

**After:**
```c
static void emit_function_epilogue(codegen_context_t* ctx) {
    // 1. Deallocate local variables (reverse of prologue step 4)
    if (ctx->current_frame_alignment > 0) {
        x64_add_reg_imm(ctx->asm_, X64_RSP, ctx->current_frame_alignment);
    }

    // 2. Restore callee-saved registers (reverse order of prologue step 3)
    x64_pop_reg(ctx->asm_, X64_R15);
    x64_pop_reg(ctx->asm_, X64_R14);
    x64_pop_reg(ctx->asm_, X64_R13);
    x64_pop_reg(ctx->asm_, X64_R12);
    x64_pop_reg(ctx->asm_, X64_RBX);

    // 3. Restore frame pointer and return
    x64_pop_reg(ctx->asm_, X64_RBP);
    x64_ret(ctx->asm_);
}
```

**Key Improvements:**
1. Explicitly deallocates local variable space using tracked alignment
2. No longer uses "mov rsp, rbp" - more explicit and predictable
3. Mirrors prologue structure exactly in reverse order

## Verification Results

### Test Suite Results
All 86 unit tests pass, including all 13 calling convention tests:
```
sox/calling_convention/func_0_args             [ OK ]
sox/calling_convention/func_1_int_arg          [ OK ]
sox/calling_convention/func_2_int_args         [ OK ]
sox/calling_convention/func_6_int_args         [ OK ]
sox/calling_convention/func_7_int_args         [ OK ]
sox/calling_convention/func_mixed_args         [ OK ]
sox/calling_convention/func_return_value       [ OK ]
sox/calling_convention/stack_alignment         [ OK ]
sox/calling_convention/nested_calls            [ OK ]
sox/calling_convention/callee_saved_regs       [ OK ]
sox/calling_convention/prologue_epilogue_structure [ OK ]
sox/calling_convention/external_c_call         [ OK ]
sox/calling_convention/register_diagnostic     [ OK ]
```

### Native Code Generation Verification

**Test Script:** `/Users/scott/development/projects/sox/src/test/scripts/test_alignment.sox`
```sox
var x = 2;
var y = 3;
var z = x + y;
print(z);
```

**Generated x86-64 Machine Code:**

**Prologue (hex):**
```
55                    push rbp
48 89 e5              mov rbp, rsp
53                    push rbx
41 54                 push r12
41 55                 push r13
41 56                 push r14
41 57                 push r15
48 81 ec 98 00 00 00  sub rsp, 0x98 (152 bytes)
```

**Epilogue (hex):**
```
48 81 c4 98 00 00 00  add rsp, 0x98 (152 bytes)
41 5f                 pop r15
41 5e                 pop r14
41 5d                 pop r13
41 5c                 pop r12
5b                    pop rbx
5d                    pop rbp
c3                    ret
```

### Stack Alignment Verification

**Frame Calculation:**
- Register allocator frame size: 144 bytes (for local variables)
- Saved callee-saved registers: 5 × 8 = 40 bytes
- Total after `push rbp`: 40 + 144 = 184 bytes
- Alignment check: 184 % 16 = 8 (NOT aligned)
- Aligned total: ⌈184 / 16⌉ × 16 = 192 bytes
- Required locals space: 192 - 40 = **152 bytes (0x98)**

**Stack Layout After Prologue:**
```
[Entry RSP]              ← 16n + 8 (after call instruction)
[Return Address]         ← 8 bytes
[Saved RBP]              ← 8 bytes | RSP after push rbp = 16n
[Saved RBX]              ← 8 bytes
[Saved R12]              ← 8 bytes
[Saved R13]              ← 8 bytes
[Saved R14]              ← 8 bytes
[Saved R15]              ← 8 bytes
[Locals + 8 bytes pad]   ← 152 bytes
[Current RSP]            ← 16n (ALIGNED!)
```

**Verification:**
- After `push rbp`: RSP = entry_RSP - 16 = 16n
- After 5 register pushes: RSP = 16n - 40 = 16n + 8
- After `sub rsp, 152`: RSP = 16n + 8 - 152 = 16n - 144 = 16n (mod 16) ✓

The stack is correctly 16-byte aligned before any call instruction!

## Issues Discovered and Resolved

### Issue 1: Original Stack Misalignment
**Problem:** The original prologue allocated local variable space BEFORE saving callee-saved registers, making alignment calculation complex and error-prone.

**Solution:** Reordered operations to save registers first, then allocate aligned local space.

### Issue 2: Inconsistent Epilogue
**Problem:** The original epilogue used "mov rsp, rbp" which masked alignment issues but made the code less explicit.

**Solution:** Refactored to explicitly mirror the prologue in reverse order using tracked alignment values.

## Remaining Work for Future Phases

Phase 2 has established the foundation for proper calling conventions. Future phases should focus on:

1. **Phase 3: Argument Passing**
   - Implement System V ABI argument passing (RDI, RSI, RDX, RCX, R8, R9)
   - Handle stack arguments for 7+ parameters
   - Support floating-point arguments (XMM0-XMM7)

2. **Phase 4: Return Value Handling**
   - Move RAX to destination register after calls
   - Handle XMM0 for floating-point returns

3. **Phase 5: External Function Calls**
   - Add relocation support for PLT/GOT
   - Link to C library functions (printf, strlen, etc.)

4. **Phase 6: Optimization**
   - Track which callee-saved registers are actually used
   - Only save/restore registers that are clobbered
   - Optimize frame size for small functions

## Success Criteria Met

✅ Stack alignment calculation is correct (16-byte aligned before any call)
✅ Prologue pushes RBP and saves callee-saved registers correctly
✅ Epilogue restores registers in correct order
✅ Local variable allocation respects alignment
✅ All 13 calling_convention tests pass
✅ Generated x86-64 prologue/epilogue structure is ABI-compliant
✅ Test script produces correct output without crashes
✅ No new compilation errors or warnings

## Code Quality

- Added comprehensive inline documentation
- Implemented systematic frame layout calculations
- Maintained backwards compatibility with existing tests
- No regressions in test suite (86/86 tests passing)

## Conclusion

Phase 2 implementation is complete and successful. The stack alignment issues have been resolved, and the prologue/epilogue are now ABI-compliant. The native code generator is ready for the next phase of development focusing on proper argument passing and external function call support.

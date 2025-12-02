# Calling Convention Diagnosis - Phase 1 Report

## Executive Summary

This document provides a comprehensive diagnosis of the current calling convention implementation in the Sox native code generator. The analysis reveals several critical issues that cause generated executables to crash when attempting to call functions.

**Current Status**: The native code generator can compile simple arithmetic operations but crashes when attempting any function calls, including calls to external C library functions.

## Test Suite Overview

Created comprehensive test suite: `src/test/calling_convention_tests.c`

**13 Test Cases Created:**
1. `test_func_0_args` - Function with no arguments
2. `test_func_1_int_arg` - Single integer argument (RDI/X0)
3. `test_func_2_int_args` - Two integer arguments (RDI, RSI / X0, X1)
4. `test_func_6_int_args` - Fill all integer argument registers
5. `test_func_7_int_args` - Stack argument overflow (7+ arguments)
6. `test_func_mixed_args` - Mixed integer/floating-point arguments
7. `test_func_return_value` - Return value handling (RAX/X0)
8. `test_stack_alignment` - 16-byte stack alignment validation
9. `test_nested_calls` - Nested function call preservation
10. `test_callee_saved_regs` - Callee-saved register preservation
11. `test_prologue_epilogue_structure` - Validate prologue/epilogue structure
12. `test_external_c_call` - Calls to C library functions (printf, strlen)
13. `test_register_diagnostic` - Register state inspection utility

**Test Coverage:**
- Argument passing (0 to 8+ arguments)
- Register allocation (integer and floating-point)
- Stack frame management
- Return value handling
- External C library function calls
- Callee-saved register preservation
- Stack alignment validation

## Current Implementation Analysis

### 1. Prologue Generation (`src/native/codegen.c:93-113`)

**x86-64 Prologue Structure:**
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
    x64_push_reg(ctx->asm_, X64_RBX);
    x64_push_reg(ctx->asm_, X64_R12);
    x64_push_reg(ctx->asm_, X64_R13);
    x64_push_reg(ctx->asm_, X64_R14);
    x64_push_reg(ctx->asm_, X64_R15);
}
```

**CRITICAL ISSUES IDENTIFIED:**

#### Issue 1.1: Stack Alignment Violation
**Location:** `src/native/codegen.c:93-113`
**Severity:** CRITICAL - Causes segfault on external function calls

**Problem:**
The prologue saves 5 callee-saved registers (RBX, R12-R15) after setting up the frame. Each `push` is 8 bytes, totaling 40 bytes. Combined with the initial `push rbp` (8 bytes), this is 48 bytes, which does NOT maintain 16-byte alignment.

**Why This Matters:**
- System V ABI requires RSP to be 16-byte aligned at function CALL boundaries
- When we CALL a function, the `call` instruction pushes the return address (8 bytes)
- So the callee's entry RSP must be at (16n + 8) to align after the return address push
- Our prologue creates misalignment that propagates to all function calls

**Example Failure:**
```
Function entry: RSP = 0x7fff5fbff000 (aligned)
push rbp:       RSP = 0x7fff5fbfeff8 (16n + 8) ✓
mov rbp, rsp:   RBP = 0x7fff5fbfeff8
sub rsp, 32:    RSP = 0x7fff5fbfefd8 (16n + 8) ✓
push rbx:       RSP = 0x7fff5fbfefd0 (16n) ✗
push r12-r15:   RSP = 0x7fff5fbfefd0 - 32 = 0x7fff5fbfefb0 (16n) ✗
call printf:    RSP = 0x7fff5fbfefa8 (16n + 8) ✗ MISALIGNED!
```

When printf or any SSE/AVX instruction tries to access stack, it expects 16-byte alignment and segfaults.

#### Issue 1.2: Unconditional Register Saving
**Location:** `src/native/codegen.c:108-112`
**Severity:** MEDIUM - Inefficient and compounds alignment issues

**Problem:**
All callee-saved registers (RBX, R12-R15) are saved unconditionally, even if not used. This:
1. Wastes stack space
2. Adds unnecessary push/pop instructions
3. Makes alignment calculation more complex
4. Slows down every function entry/exit

**Should Be:**
Track which callee-saved registers are actually allocated by the register allocator and only save those.

#### Issue 1.3: Frame Size Calculation
**Location:** `src/native/codegen.c:101-104`
**Severity:** HIGH - May not account for alignment requirements

**Problem:**
Frame size is calculated by the register allocator but may not ensure proper alignment when combined with saved registers.

**Current Flow:**
1. Register allocator calculates frame size for spilled variables
2. Prologue subtracts frame_size from RSP
3. Prologue pushes 5 callee-saved registers (40 bytes)
4. Total offset from entry RSP is: 8 (rbp) + frame_size + 40

This total MUST be 16-byte aligned (or 16n + 8 for call alignment).

### 2. Epilogue Generation (`src/native/codegen.c:115-131`)

**x86-64 Epilogue Structure:**
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

**ISSUES IDENTIFIED:**

#### Issue 2.1: Register Restore Order
**Location:** `src/native/codegen.c:117-121`
**Severity:** LOW - Currently correct but fragile

**Status:** This is actually correct - pops in reverse order of pushes. However, it's fragile because it assumes the exact prologue structure.

**Recommendation:** Use a data structure to track saved registers in prologue and restore them in reverse order in epilogue.

### 3. Function Call Handling (`src/native/codegen.c:323-328`)

**Current Implementation:**
```c
case IR_CALL: {
    // Simplified call - would need proper ABI handling
    // For now, just emit a call to a runtime function
    x64_call_rel32(ctx->asm_, 0); // Would need to be relocated
    break;
}
```

**CRITICAL ISSUES IDENTIFIED:**

#### Issue 3.1: No Argument Passing
**Location:** `src/native/codegen.c:323-328`
**Severity:** CRITICAL - Makes all function calls fail

**Problem:**
The IR_CALL handler emits a call instruction but does NOT:
1. Move arguments into the correct registers (RDI, RSI, RDX, RCX, R8, R9)
2. Push stack arguments for 7+ parameters
3. Handle floating-point arguments (XMM0-XMM7)
4. Preserve caller-saved registers
5. Clean up stack after call (if needed)

**System V ABI Requirements (x86-64):**
- Integer arguments 1-6: RDI, RSI, RDX, RCX, R8, R9
- FP arguments 1-8: XMM0-XMM7
- Arguments 7+: Push on stack in reverse order
- Return value: RAX (integer), XMM0 (floating-point)
- Caller must save: RAX, RCX, RDX, RSI, RDI, R8-R11, XMM0-XMM15
- Callee must save: RBX, RBP, R12-R15

#### Issue 3.2: No Return Value Handling
**Location:** `src/native/codegen.c:323-328`
**Severity:** HIGH - Return values lost

**Problem:**
After the call, the return value in RAX is not moved to the destination register specified in the IR instruction.

**Should Be:**
```c
case IR_CALL: {
    // 1. Move arguments to correct registers/stack
    // 2. Align stack if needed
    // 3. Call function
    x64_call_rel32(ctx->asm_, offset);
    // 4. Clean up stack arguments if any
    // 5. Move RAX to destination register
    x64_register_t dest = get_physical_register(ctx, instr->dest);
    if (dest != X64_RAX && dest != X64_NO_REG) {
        x64_mov_reg_reg(ctx->asm_, dest, X64_RAX);
    }
    break;
}
```

#### Issue 3.3: No Relocation Support
**Location:** `src/native/codegen.c:326`
**Severity:** HIGH - Cannot call external functions

**Problem:**
The call emits `x64_call_rel32(ctx->asm_, 0)` with offset 0, but there's no relocation entry created for the linker to patch this with the actual function address.

**Should Be:**
```c
// Add relocation for external function call
x64_add_relocation(ctx->asm_, x64_get_offset(ctx->asm_) + 1,
                   X64_RELOC_PLT32, function_name, -4);
x64_call_rel32(ctx->asm_, 0); // Will be patched by linker
```

### 4. ARM64 Implementation (`src/native/codegen_arm64.c:102-150`)

**ARM64 Prologue Structure:**
```c
static void emit_function_prologue_arm64(codegen_arm64_context_t* ctx) {
    // Save FP and LR
    arm64_stp(ctx->asm_, ARM64_FP, ARM64_LR, ARM64_SP, -16);
    arm64_sub_reg_reg_imm(ctx->asm_, ARM64_SP, ARM64_SP, 16);

    // Set up frame pointer
    arm64_mov_reg_reg(ctx->asm_, ARM64_FP, ARM64_SP);

    // Allocate stack frame
    int frame_size = regalloc_get_frame_size(ctx->regalloc);
    if (frame_size > 0) {
        if (frame_size < 4096) {
            arm64_sub_reg_reg_imm(ctx->asm_, ARM64_SP, ARM64_SP, (uint16_t)frame_size);
        } else {
            arm64_mov_reg_imm(ctx->asm_, ARM64_X9, frame_size);
            arm64_sub_reg_reg_reg(ctx->asm_, ARM64_SP, ARM64_SP, ARM64_X9);
        }
    }

    // Save callee-saved registers (X19-X28)
    if (frame_size > 64) {
        arm64_stp(ctx->asm_, ARM64_X19, ARM64_X20, ARM64_SP, 0);
        arm64_stp(ctx->asm_, ARM64_X21, ARM64_X22, ARM64_SP, 16);
    }
}
```

**SIMILAR ISSUES:**
- Same stack alignment concerns (ARM64 requires 16-byte alignment)
- Conditional callee-saved register saving (based on frame_size > 64) is arbitrary
- No argument passing implementation for IR_CALL
- No relocation support

### 5. Register Allocator (`src/native/regalloc.c`)

**Current Status:** Not examined in detail yet, but potential issues:

#### Issue 5.1: No Calling Convention Awareness
**Severity:** HIGH - Register allocator may not be aware of calling convention

**Potential Problems:**
- May allocate caller-saved registers across function calls
- May not reserve argument registers when preparing for calls
- May not account for return value register (RAX/X0)

**Need to Verify:**
- Does it mark caller-saved registers as clobbered at call sites?
- Does it avoid allocating to argument registers when building call arguments?
- Does it properly handle the return value register?

### 6. ELF/Mach-O Writers

**Current Status:** Generate proper object files with symbols

**Verified Working:**
- `elf_create_executable_object_file` creates `main` and `sox_main` symbols
- `macho_create_executable_object_file` creates `main` and `sox_main` symbols
- Both generate proper section headers
- Symbol tables are correctly formatted

**Missing:**
- Relocation entries for external function calls
- PLT/GOT support for dynamic linking
- External symbol references (e.g., `printf`, `malloc`)

## Operations That Work vs. Crash

### ✅ Operations That Work (in VM bytecode)

1. **Arithmetic Operations:**
   - Addition, subtraction, multiplication, division
   - Integer and floating-point math
   - Unary negation

2. **Comparison Operations:**
   - Equal, not equal
   - Less than, greater than, less/greater or equal

3. **Logical Operations:**
   - AND, OR, NOT
   - Boolean operations

4. **Variable Operations:**
   - Local variable access
   - Global variable access
   - Variable assignment

5. **Control Flow (within function):**
   - If/else statements
   - While loops
   - For loops
   - Jump instructions

### ❌ Operations That Crash (in native code)

1. **Function Calls (All Types):**
   - Internal function calls (sox functions calling sox functions)
   - External C library calls (printf, strlen, etc.)
   - Recursive calls
   - Nested calls

2. **Operations Requiring External Calls:**
   - String concatenation (may call allocation)
   - Object allocation
   - Array/table operations
   - Print statements (calls runtime print)

3. **Operations Dependent on Correct ABI:**
   - Return values from functions
   - Passing arguments to functions
   - Preserving state across calls

## Root Cause Analysis

### Primary Root Cause: Stack Alignment

**The Main Issue:**
The System V ABI (x86-64) mandates that the stack pointer (RSP) must be 16-byte aligned immediately before a CALL instruction (i.e., RSP % 16 == 0). This is required because:

1. Many C library functions use SSE/AVX instructions
2. SSE/AVX instructions require aligned memory accesses
3. These instructions assume the stack is properly aligned
4. Misalignment causes SIGSEGV (segmentation fault)

**Our Violation:**
```
Function Entry:     RSP = 16n + 8        (due to call instruction push)
push rbp:           RSP = 16n            ✓ Aligned
mov rbp, rsp:       (no stack change)
sub rsp, frame_size: RSP = 16n - frame_size
push rbx:           RSP = 16n - frame_size - 8
push r12-r15:       RSP = 16n - frame_size - 40

For call to align:  RSP must be 16n before call
Currently:          RSP = 16n - frame_size - 40
```

This is only aligned if `(frame_size + 40) % 16 == 0`, which is NOT guaranteed.

### Secondary Root Causes:

1. **No Argument Passing:** Functions receive garbage in argument registers
2. **No Return Value Handling:** Return values lost or wrong register used
3. **No Relocations:** Cannot link to external functions
4. **No Caller-Save Preservation:** May corrupt caller's register state

## Crash Locations

Based on code analysis, crashes will occur at:

### Crash Point 1: First External Function Call
**Location:** Any `IR_CALL` that tries to call printf, strlen, or other C library function
**Why:** Stack misalignment causes SSE instruction in C library to fault

### Crash Point 2: Return from Misaligned Call
**Location:** Any function that calls another function and tries to use return value
**Why:** Return address may be corrupted, or return value in wrong register

### Crash Point 3: Function Entry with Incorrect ABI
**Location:** Any sox function called from another sox function
**Why:** Arguments not in expected registers, stack frame corrupted

## Priority Issues for Phase 2

Based on severity and impact:

### Priority 1 (CRITICAL - Must Fix First)
1. **Stack Alignment in Prologue** (`codegen.c:93-113`)
   - Calculate total prologue stack usage
   - Adjust frame_size to maintain 16-byte alignment
   - Formula: `(rbp_save + frame_size + callee_saves) % 16 == 0`

2. **Argument Passing in IR_CALL** (`codegen.c:323-328`)
   - Implement System V ABI argument passing
   - Integer args: RDI, RSI, RDX, RCX, R8, R9
   - FP args: XMM0-XMM7
   - Stack args for 7+ parameters

### Priority 2 (HIGH - Required for Basic Functionality)
3. **Return Value Handling** (`codegen.c:330-337`)
   - Move RAX to destination register after call
   - Handle XMM0 for floating-point returns

4. **Relocation Support** (`codegen.c:323-328`, `elf_writer.c`)
   - Add X64_RELOC_PLT32 entries for external calls
   - Add external symbol definitions
   - Test with simple C library calls (strlen)

### Priority 3 (MEDIUM - Correctness)
5. **Conditional Callee-Save Register Handling**
   - Track which registers are used
   - Only save/restore used callee-saved registers
   - Adjust stack alignment calculation accordingly

6. **Register Allocator Call Awareness**
   - Mark caller-saved registers as clobbered at call sites
   - Reserve argument registers during call setup
   - Handle return value register correctly

### Priority 4 (LOW - Optimization)
7. **Stack Frame Optimization**
   - Minimize frame size
   - Reuse stack slots
   - Omit frame pointer when possible

## Recommended Approach for Phase 2

### Step 1: Fix Stack Alignment
1. Create helper function `calculate_aligned_frame_size()`
2. Account for all prologue pushes: rbp (8) + saved registers (n*8)
3. Adjust frame_size so total is 16-byte aligned
4. Add assertions to verify alignment

### Step 2: Implement Basic Argument Passing
1. Start with 1-2 integer arguments (RDI, RSI)
2. Test with simple sox function calls
3. Expand to 6 integer registers
4. Add stack argument support for 7+

### Step 3: Add Relocation Support
1. Define external function symbols
2. Add PLT relocation entries
3. Test with strlen() call
4. Expand to printf() and other C library functions

### Step 4: Test Incrementally
1. Run calling_convention tests after each fix
2. Generate actual native executables
3. Use objdump to verify machine code
4. Use gdb to debug crashes

## Testing Strategy

### Unit Tests (Already Created)
- Run all 13 tests in `calling_convention_tests.c`
- Initially, all tests pass in VM (baseline)
- After Phase 2 fixes, generate native code and verify execution

### Integration Tests
1. Create minimal sox programs that call functions
2. Generate native code with debug output
3. Disassemble with objdump
4. Manually verify prologue/epilogue
5. Execute and verify results

### Diagnostic Tests
1. Use `test_stack_alignment` to verify 16-byte alignment
2. Use `test_register_diagnostic` to check register state
3. Use `test_external_c_call` to verify C library calls

## Code Locations Reference

### x86-64 Code Generation
- **Prologue:** `/Users/scott/development/projects/sox/src/native/codegen.c:93-113`
- **Epilogue:** `/Users/scott/development/projects/sox/src/native/codegen.c:115-131`
- **IR_CALL Handler:** `/Users/scott/development/projects/sox/src/native/codegen.c:323-328`
- **IR_RETURN Handler:** `/Users/scott/development/projects/sox/src/native/codegen.c:330-337`
- **Register Mapping:** `/Users/scott/development/projects/sox/src/native/codegen.c:76-91`

### ARM64 Code Generation
- **Prologue:** `/Users/scott/development/projects/sox/src/native/codegen_arm64.c:102-133`
- **Epilogue:** `/Users/scott/development/projects/sox/src/native/codegen_arm64.c:135-154`
- **IR_CALL Handler:** `/Users/scott/development/projects/sox/src/native/codegen_arm64.c:319-323`

### x86-64 Instruction Encoder
- **Header:** `/Users/scott/development/projects/sox/src/native/x64_encoder.h`
- **Implementation:** `/Users/scott/development/projects/sox/src/native/x64_encoder.c`
- **Call Instruction:** `/Users/scott/development/projects/sox/src/native/x64_encoder.c` (x64_call_rel32)

### Register Allocator
- **Header:** `/Users/scott/development/projects/sox/src/native/regalloc.h`
- **Implementation:** `/Users/scott/development/projects/sox/src/native/regalloc.c`
- **Frame Size:** `regalloc_get_frame_size()` function

### Object File Writers
- **ELF Writer:** `/Users/scott/development/projects/sox/src/native/elf_writer.c:273-306`
- **Mach-O Writer:** `/Users/scott/development/projects/sox/src/native/macho_writer.c:400-421`
- **Executable Entry:** Both create `main` symbol at offset 0

### Test Suite
- **Test Implementation:** `/Users/scott/development/projects/sox/src/test/calling_convention_tests.c`
- **Test Header:** `/Users/scott/development/projects/sox/src/test/calling_convention_tests.h`
- **Test Integration:** `/Users/scott/development/projects/sox/src/test/main.c:20`

## Conclusion

The Sox native code generator successfully generates machine code for arithmetic and local operations but has several critical issues preventing function calls from working:

1. **Primary Issue:** Stack alignment violation in function prologue
2. **Secondary Issues:** No argument passing, no return value handling, no relocations

All issues are fixable and well-understood. The test suite provides comprehensive coverage for validation. Phase 2 should focus on fixing the stack alignment first, then implementing proper calling convention support.

**Estimated Effort for Phase 2:**
- Stack alignment fix: 2-4 hours
- Argument passing (basic): 4-8 hours
- Relocation support: 4-6 hours
- Testing and refinement: 4-8 hours
- **Total:** 14-26 hours of focused development

**Success Criteria for Phase 2:**
- All 13 calling convention tests pass with native code generation
- Can call strlen() successfully
- Can call printf() successfully
- Can call sox functions from sox functions
- Stack remains 16-byte aligned throughout execution
- Generated executables don't crash on function calls

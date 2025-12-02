# Comprehensive Plan: Calling Convention Implementation for Native Code Generation

**Status**: In Progress (Phases 1-3 Complete, Phase 4+ in Review)
**Date**: 2025-12-03
**Last Updated**: 2025-12-03 (after Phase 3 completion)
**Priority**: Critical (blocks executable functionality)
**Estimated Scope**: 4-6 weeks of focused development

## Implementation Progress

| Phase | Status | Commits | Notes |
|-------|--------|---------|-------|
| Phase 1: Diagnosis & Foundation | ✅ COMPLETE | f2542b5 | Test suite created, critical issues identified |
| Phase 2: x86-64 Stack Alignment | ✅ COMPLETE | 1bbd607 | Stack alignment fixed, prologue/epilogue refactored |
| Phase 3: x86-64 External Calls | ✅ COMPLETE | 199cee3 | Argument marshalling and relocations implemented |
| Phase 4: ARM64 Register Mapping | 🔄 IN REVIEW | — | Ready for cloud agent implementation |
| Phase 5: ARM64 External Calls | ⏳ PENDING | — | Depends on Phase 4 |
| Phase 6: Runtime Integration | ⏳ PENDING | — | Depends on Phase 3 & 5 |
| Phase 7: Integration & Testing | ⏳ PENDING | — | Depends on Phase 6 |
| Phase 8: Performance & Polish | ⏳ PENDING | — | Final optimization pass |

---

## Executive Summary

The Sox native code generation system can successfully generate and link executable binaries, but they cannot execute because the generated machine code does not properly implement calling conventions. This plan outlines a phased approach to implement complete calling convention support, enabling Sox scripts to:

1. Call external C library functions (printf, etc.)
2. Properly pass arguments to functions
3. Receive return values correctly
4. Maintain ABI compliance with operating systems
5. Execute predictably without crashes

**Current State (After Phase 3)**:
- ✅ x86-64 stack alignment is ABI-compliant (Phase 2)
- ✅ External function calls with relocations implemented (Phase 3)
- ✅ Argument marshalling follows System V AMD64 ABI (Phase 3)
- ⏳ ARM64 register mapping still broken - Phase 4 in review
- ⏳ Return value handling framework in place, needs testing

**Immediate Next Steps (Cloud Agent)**:
- Implement Phase 4: ARM64 register mapping fix
- Verify x86-64 functionality with integration tests
- Prepare Phase 5: ARM64 external calls

**End Goal**: Generate ABI-compliant machine code that can safely call external functions and execute as proper native binaries on both x86-64 and ARM64 platforms.

---

## Root Cause Analysis

### Why Executables Crash

Current generated code (x86-64):
- ✅ Has `_main` entry point
- ✅ Generates arithmetic instructions
- ✅ Sets up proper ABI-compliant stack frame (Phase 2 complete)
- ✅ **Passes arguments to functions** - RDI, RSI, RDX, RCX, R8, R9 (Phase 3 complete)
- ✅ **Calls external functions** - PLT32 relocations implemented (Phase 3 complete)
- ✅ **Enforces stack alignment** - 16-byte alignment guaranteed (Phase 2 complete)
- ✅ **Handles return values** - RAX captured to destination register (Phase 3 complete)

Current generated code (ARM64):
- ✅ Has basic instruction generation
- ❌ **Has broken register mapping** - uses x86-64 register numbers instead of ARM64 (Phase 4 pending)
- ❌ **Prologue/epilogue incorrect** - doesn't use proper stp/ldp (Phase 4 pending)
- ❌ **No argument passing** - similar architecture to x86-64, needs Phase 5

### Symptom
Running generated executable immediately faults with:
- Exit code 132 (SIGILL - illegal instruction)
- Or segfault from stack misalignment
- Or general protection fault from invalid register usage

---

## Architectural Overview: Calling Convention Requirements

### x86-64 System V AMD64 ABI (Linux/Unix)

**Argument Passing:**
- First 6 integer arguments: **RDI, RSI, RDX, RCX, R8, R9**
- Floating-point arguments: **XMM0-XMM7** (up to 8)
- Additional arguments: **stack** (right-to-left, 8-byte aligned)
- Return value: **RAX** (RDX:RAX for 128-bit)

**Stack Management:**
- **16-byte alignment**: RSP must be 16-byte aligned BEFORE `call` instruction (means aligned to 16 after return address pushed)
- **Red zone**: 128 bytes below RSP are reserved (not clobbered by signal handlers)
- **Frame pointer**: RBP (optional, but used for debugging)

**Register Classification:**
- **Callee-saved**: RBX, RSP, RBP, R12-R15 (must preserve if used)
- **Caller-saved**: RAX, RCX, RDX, RSI, RDI, R8-R11, XMM0-XMM15 (can be clobbered)

**Calling Sequence:**

```
# Caller prepares arguments
mov rdi, [arg1]              # 1st integer arg
mov rsi, [arg2]              # 2nd integer arg
movsd xmm0, [float_arg]      # 1st FP arg
call function_address        # Pushes return address to stack

# Callee (function prologue)
push rbp
mov rbp, rsp
sub rsp, stack_space         # Allocate local variables
```

### ARM64 AArch64 EABI (macOS/Linux)

**Argument Passing:**
- First 8 integer arguments: **X0-X7**
- Floating-point arguments: **V0-V7** (up to 8)
- Additional arguments: **stack** (right-to-left)
- Return value: **X0** (X0:X1 for 128-bit)

**Stack Management:**
- **16-byte alignment**: SP must be 16-byte aligned at function entry
- **Frame pointer**: X29 (FP) and **X30 (LR)** - link register holds return address
- **No red zone** on ARM64

**Register Classification:**
- **Callee-saved**: X19-X28, X29 (FP), X30 (LR), V8-V15
- **Caller-saved**: X0-X18, XZR (zero register), V0-V7, V16-V31

---

## Phase 1: Diagnosis & Foundation (Week 1)

### Goals
- Profile current executable to understand crash point
- Create test harness for calling convention compliance
- Establish baseline measurements

### Tasks

#### 1.1 Create Calling Convention Test Suite
**File**: `src/test/calling_convention_tests.c`

```c
// Test different argument counts and types
test_func_0_args()          // No arguments
test_func_1_int_arg()       // 1 integer
test_func_2_int_args()      // 2 integers (RDI, RSI)
test_func_6_int_args()      // Fill integer registers
test_func_7_int_args()      // Overflow to stack
test_func_mixed_args()      // Integer + FP args
test_func_return_value()    // Verify return value in RAX/X0

// Test stack alignment
test_stack_alignment()      // Verify 16-byte alignment maintained
test_nested_calls()         // Call within called function
test_callee_saved()         // Verify callee-saved registers preserved
```

Tests should:
- Compare generated code output with reference (clang-generated) code
- Verify register contents at entry/exit points
- Check stack alignment at call boundaries
- Validate return values

#### 1.2 Debug Generated Executable
**Tools & Techniques:**
- Use `lldb` or `gdb` to step through generated code
- Compare with `clang -S` output (disassembly)
- Add `printf` debugging to trace execution
- Use hex dumper to inspect actual machine code bytes

**Instrumentation Plan:**
```c
// Add minimal printf calls to generated code
printf("[DEBUG] Entering main\n");  // IR_PRINT with literal
printf("[DEBUG] RAX = %llx\n", rax); // Would require external call setup
```

#### 1.3 Profile Instruction Alignment
- Verify generated instructions are properly 4-byte aligned (x86-64) / 4-byte aligned (ARM64)
- Check if `call` instructions are correctly encoded (relative offsets)
- Inspect object file relocations with `objdump -r`

#### 1.4 Document Current Behavior
- Create test matrix showing which operations work vs crash
- Document crash signatures (register state, stack contents)
- Identify exact instruction/operation that triggers crash

### Deliverables
- Test suite file with 10+ test cases
- Profile data showing where/why executables crash
- Documented comparison between generated code and reference code

---

## Phase 2: x86-64 Stack Alignment & Prologue/Epilogue (Week 1-2)

### Goals
- Fix stack alignment issues
- Properly implement calling sequence
- Ensure x86-64 executables can run basic code

### 2.1 Fix Stack Alignment

**Problem**: Code generated doesn't ensure 16-byte stack alignment before `call` instructions.

**Solution**: Track stack offset throughout function and emit alignment padding.

**File**: `src/native/x64_encoder.h` and `codegen.c`

```c
// In codegen context
typedef struct {
    // ... existing fields ...
    int stack_offset;      // Current stack pointer offset from entry
    int next_call_offset;  // Where next call will be
} codegen_context_t;

// Before emitting call
void codegen_prepare_call(codegen_context_t* ctx) {
    // Check alignment: (RSP + stack_offset) % 16 == 8 (due to return address)
    // If not aligned, emit padding or adjust earlier
    int current_alignment = (ctx->stack_offset) % 16;
    if (current_alignment != 8) {
        // Sub RSP to align
        int padding = (8 - current_alignment + 16) % 16;
        if (padding > 0) {
            x64_sub_r64_imm(ctx->asm_, X64_RSP, padding);
            ctx->stack_offset += padding;
        }
    }
}
```

**Testing**: Each test in Phase 1 test suite will verify alignment.

### 2.2 Refactor Prologue/Epilogue

**Current Issues**:
- Saves all 5 callee-saved registers unconditionally (RBX, R12-R15)
- Doesn't account for saved registers in frame size
- Stack alignment happens after saving registers, making math complex

**New Approach - Systematic Frame Layout**:

```
[Entry RSP]               <- RSP on function entry (call return address pushed)
[Return Address]          <- 8 bytes (from `call`)
[Frame Data]              <- (RBP, preserved regs) + locals
[Locals] ← [RSP on exit]  <- must be 16-byte aligned
```

**Implementation** (`codegen.c:75-140`):

```c
void codegen_emit_prologue(codegen_context_t* ctx) {
    // 1. Determine which registers need saving (from register allocator info)
    uint64_t preserved_mask = regalloc_get_preserved_regs(ctx->regalloc);

    // 2. Calculate frame size
    // = # saved registers * 8 + locals + padding for alignment
    int num_saved = __builtin_popcountll(preserved_mask);
    int locals_size = ctx->frame_size;
    int after_saves = 8 + (num_saved * 8);  // 8 for return address

    // Align locals: (after_saves + locals_size) % 16 == 0
    int padding = (16 - (after_saves % 16)) % 16;
    int aligned_frame = locals_size + padding;

    // 3. Emit prologue
    x64_push_r64(ctx->asm_, X64_RBP);           // 8 bytes
    x64_mov_r64_r64(ctx->asm_, X64_RBP, X64_RSP); // Setup FP

    // 4. Save required callee-saved registers
    if (preserved_mask & (1 << X64_RBX))
        x64_push_r64(ctx->asm_, X64_RBX);
    if (preserved_mask & (1 << X64_R12))
        x64_push_r64(ctx->asm_, X64_R12);
    // ... etc for R13-R15

    // 5. Allocate stack space
    if (aligned_frame > 0) {
        x64_sub_r64_imm(ctx->asm_, X64_RSP, aligned_frame);
    }

    // 6. Record stack offset for call site alignment
    ctx->stack_offset = aligned_frame;
}
```

**Testing**: Run Phase 1 test suite after changes.

### 2.3 Verify x86-64 Arithmetic

- Basic operations (add, sub, mul) should work after alignment fix
- Create test: `print(2 + 2)` → should compile and run

### Deliverables
- Refactored prologue/epilogue code
- Stack offset tracking in code generator
- Test results showing improved x86-64 behavior

---

## Phase 3: External Function Calls - x86-64 (Week 2-3)

### Goals
- Enable calling external C functions
- Implement argument marshalling
- Set up relocation infrastructure

### 3.1 Extend IR for External Calls

**Current**: IR_CALL is a placeholder; IR_RUNTIME_CALL is a fallback.

**New**: Proper external function call support.

**File**: `src/native/ir.h`

```c
// Add to IR operation types
IR_CALL_EXTERNAL = 50,  // Call external C function
IR_CALL_NATIVE = 51,    // Call native function in same module

// Extend IR instruction for calls
typedef struct {
    int opcode;           // IR_CALL_EXTERNAL
    ir_value_t result;    // Where to store return value
    const char* symbol;   // External symbol name (e.g., "printf")
    int num_args;         // Number of arguments
    ir_value_t* args;     // Arguments in order
} ir_call_instruction_t;
```

**Update IR Builder** (`src/native/ir_builder.c`):
- When encountering IR_CALL in bytecode, create IR_CALL_EXTERNAL instruction
- Pass function name as symbol
- Record arguments

### 3.2 Argument Marshalling (x86-64)

**Implementation** (`src/native/codegen.c:300-400`):

```c
void codegen_emit_call_external(codegen_context_t* ctx,
                                const ir_call_instruction_t* call) {
    // 1. Prepare stack alignment (from Phase 2)
    codegen_prepare_call(ctx);

    // 2. Move arguments to correct registers
    for (int i = 0; i < call->num_args && i < 6; i++) {
        ir_value_t arg = call->args[i];
        int arg_reg;

        // Integer/pointer arguments go to RDI, RSI, RDX, RCX, R8, R9
        const int int_arg_regs[] = {
            X64_RDI, X64_RSI, X64_RDX, X64_RCX, X64_R8, X64_R9
        };
        arg_reg = int_arg_regs[i];

        // Move argument to register
        if (ir_is_virtual_reg(arg)) {
            int virt_reg = ir_get_virt_reg(arg);
            int phys_reg = regalloc_get_physreg(ctx->regalloc, virt_reg);
            if (phys_reg != arg_reg) {
                x64_mov_r64_r64(ctx->asm_, arg_reg, phys_reg);
            }
        } else if (ir_is_constant(arg)) {
            uint64_t value = ir_get_constant_value(arg);
            x64_mov_r64_imm(ctx->asm_, arg_reg, value);
        }
    }

    // 3. For arguments beyond 6: push to stack (right-to-left)
    for (int i = call->num_args - 1; i >= 6; i--) {
        // Push arguments in reverse order
        // Stack grows downward, so we push in reverse
    }

    // 4. Emit relocatable call instruction
    int reloc_offset = ctx->asm_->size;
    x64_call_rel32(ctx->asm_, 0);  // 0 is placeholder, will be relocated

    // 5. Record relocation
    x64_add_relocation(ctx->asm_, reloc_offset, call->symbol,
                      X64_RELOC_PLT32);  // PLT32 for external symbols

    // 6. Handle return value
    if (ir_is_valid_value(call->result)) {
        int result_virt_reg = ir_get_virt_reg(call->result);
        int result_phys_reg = regalloc_get_physreg(ctx->regalloc, result_virt_reg);

        if (result_phys_reg != X64_RAX) {
            // Move return value from RAX to destination register
            x64_mov_r64_r64(ctx->asm_, result_phys_reg, X64_RAX);
        }
    }

    // 7. Restore stack alignment if needed
    // (Only if we pushed extra arguments)
}
```

### 3.3 Relocation Support

**File**: `src/native/x64_encoder.h` and `elf_writer.c`

Currently relocations are partially defined but not processed by ELF writer.

```c
// In elf_writer.c, add relocation processing during write
void elf_process_relocations(elf_builder_t* builder,
                            const machine_code_t* code,
                            const symbol_table_t* symbols) {
    for (int i = 0; i < code->reloc_count; i++) {
        x64_relocation_t* reloc = &code->relocs[i];

        // Find symbol by name
        int sym_index = symbol_table_lookup(symbols, reloc->symbol_name);

        // Create ELF relocation entry
        Elf64_Rela rela;
        rela.r_offset = reloc->offset;
        rela.r_info = ELF64_R_INFO(sym_index, reloc->type);
        rela.r_addend = reloc->addend;

        // Add to .rela.text section
        add_relocation_entry(builder, &rela);
    }
}
```

### 3.4 Symbol Table Management

**File**: `src/native/symbol_table.h` and `symbol_table.c` (NEW)

```c
typedef struct {
    const char* name;
    uint32_t type;      // STT_FUNC, STT_OBJECT
    uint32_t binding;   // STB_GLOBAL, STB_LOCAL
    uint32_t section;   // Section index (SHN_UNDEF for external)
    uint64_t value;
    uint64_t size;
} symbol_t;

typedef struct {
    symbol_t* symbols;
    int count;
    // String table for symbol names
    char* strtab;
    size_t strtab_size;
} symbol_table_t;

// During code generation, record external symbols
void symbol_table_add_external(symbol_table_t* syms, const char* name) {
    // Add to symbol table with SHN_UNDEF (undefined - comes from external)
}
```

### 3.5 ELF Writer Updates

**File**: `src/native/elf_writer.c`

- Update `elf_write_file()` to include .rela.text section
- Process recorded relocations
- Update symbol table with external symbols

### Deliverables
- Relocation infrastructure in ELF writer
- External function call code generation
- Updated IR for external calls
- Test: `print(5)` compiles and tries to call external print

---

## Phase 4: ARM64 Register Mapping Fix (Week 3)

### Goals
- Fix fundamental ARM64 register mapping bug
- Implement ARM64 calling convention

### 4.1 Fix ARM64 Register Mapping

**Problem**: Current code maps virtual registers to x86-64 register numbers, which don't correspond to ARM64 registers.

**Current Broken Code** (`codegen_arm64.c:93-96`):
```c
// WRONG: Using x86-64 register numbers
int phys_reg = virt_reg % 18;  // Maps to 0-17
// These don't exist on ARM64!
```

**Solution**: Separate register ID space for each architecture.

**File**: `src/native/arm64_encoder.h`

```c
// ARM64 physical register IDs
enum {
    ARM64_X0 = 0,   // Argument/return
    ARM64_X1 = 1,
    // ... X2-X7 for more arguments
    // ... X8-X18 for temporaries
    ARM64_X19 = 19, // Callee-saved
    // ... X20-X28
    ARM64_X29 = 29, // Frame pointer
    ARM64_X30 = 30, // Link register
    ARM64_SP = 31,  // Stack pointer

    // 32 vector registers V0-V31
    ARM64_V0 = 32,
    // ... V1-V31
};
```

**Register Allocator Update** (`src/native/regalloc.c`):

```c
// Each architecture specifies available registers
typedef struct {
    int* available_regs;
    int num_available;
    int* callee_saved;
    int num_callee_saved;
} regalloc_arch_info_t;

// Initialize differently for each arch
regalloc_arch_info_t x86_64_info = {
    .available_regs = (int[]){
        X64_RAX, X64_RBX, X64_RCX, X64_RDX,
        X64_RSI, X64_RDI, X64_R8, X64_R9, X64_R10, X64_R11
    },
    .num_available = 10,
    .callee_saved = (int[]){X64_RBX, X64_R12, X64_R13, X64_R14, X64_R15},
    .num_callee_saved = 5
};

regalloc_arch_info_t arm64_info = {
    .available_regs = (int[]){
        ARM64_X0, ARM64_X1, // ... X18
    },
    .num_available = 19,
    .callee_saved = (int[]){
        ARM64_X19, ARM64_X20, // ... X28
    },
    .num_callee_saved = 10
};
```

### 4.2 Implement ARM64 Calling Convention

**File**: `src/native/codegen_arm64.c:300-400`

Similar to x86-64 but for ARM64:
- Arguments in X0-X7, V0-V7
- Return value in X0
- Proper STP/LDP instructions for stack operations

### 4.3 ARM64 Prologue/Epilogue

**Proper implementation**:
```
stp x29, x30, [sp, #-frame_size]!   // Save FP, LR with pre-index
mov x29, sp                          // Set frame pointer
// (no additional stack allocation if we sized it above)
```

**Key difference from x86-64**: LR register holds return address automatically; no need for separate push/pop.

### Deliverables
- Fixed register mapping for ARM64
- Rewritten prologue/epilogue for ARM64
- Updated register allocator with architecture-specific info
- Test: ARM64 arithmetic operations work

---

## Phase 5: ARM64 External Calls (Week 3-4)

### Parallel to Phase 3 but for ARM64

Similar implementation to Phase 3 but:
- Arguments in X0-X7 instead of RDI-R9
- Different relocation types (ARM64 CALL26 instead of PLT32)
- Different calling sequence (LR vs return stack)

### Deliverables
- ARM64 external call support
- ARM64 relocations in ELF writer
- Test: ARM64 `print(5)` works

---

## Phase 6: Runtime Integration (Week 4)

### Goals
- Integrate with Sox runtime
- Handle type operations
- Enable full language features

### 6.1 Map Sox Operations to C Runtime

**File**: `src/native/runtime.c` and `runtime.h`

Create C functions for each runtime operation:

```c
// Type checking
bool runtime_is_number(sox_value_t val);
bool runtime_is_string(sox_value_t val);

// Arithmetic with type checking
sox_value_t runtime_add(sox_value_t a, sox_value_t b);
sox_value_t runtime_multiply(sox_value_t a, sox_value_t b);

// String operations
sox_value_t runtime_concat(sox_value_t a, sox_value_t b);
const char* runtime_to_string(sox_value_t val);

// Output
void runtime_print(sox_value_t val);

// Array/table operations
sox_value_t runtime_array_get(sox_value_t array, int index);
void runtime_array_set(sox_value_t array, int index, sox_value_t val);
```

### 6.2 Map IR_RUNTIME_CALL to C Functions

**File**: `src/native/codegen.c` and `codegen_arm64.c`

```c
case IR_RUNTIME_CALL: {
    // Determine which runtime function to call
    const char* runtime_func = ir_get_runtime_func_name(inst);

    // Call it like any external function
    // Arguments already marshalled, relocation recorded
    codegen_emit_call_external(ctx, runtime_func);
    break;
}
```

### 6.3 Runtime Initialization

Create minimal runtime that:
- Defines all necessary functions
- Can be linked into generated executables
- Accessible to external code

### Deliverables
- Runtime library interface
- Mapping of IR operations to C functions
- Test: `print(2 + 2)` fully works

---

## Phase 7: Integration & Testing (Week 4-5)

### Goals
- Ensure all pieces work together
- Add comprehensive test coverage
- Document calling convention details

### 7.1 Integration Tests

Create tests that exercise:
- Single operations (arithmetic, logic)
- Function calls with varying argument counts
- Nested function calls
- Return value handling
- Stack alignment edge cases

**File**: `src/test/native_integration_tests.sox`

```sox
// Test 1: Simple arithmetic
print(2 + 3)

// Test 2: Nested calls
fun add(a, b) {
    return a + b
}
print(add(10, 20))

// Test 3: Multiple arguments
fun greet(name, age) {
    print("Name: ")
    print(name)
    print(", Age: ")
    print(age)
}
greet("Alice", 30)

// Test 4: Complex expression
x = 5
y = 10
z = x * y + 20
print(z)
```

### 7.2 Validation Against Reference

For each test:
- Generate with sox
- Generate equivalent with clang
- Compare:
  - Output
  - Generated object file structure
  - Relocations

### 7.3 Documentation

Create comprehensive guide:
- Calling convention implementation details
- How arguments are passed
- Stack layout
- Frame pointer usage
- Relocation strategy

### Deliverables
- 20+ integration tests passing
- Documentation
- Test report

---

## Phase 8: Performance & Polish (Week 5-6)

### Goals
- Optimize code generation
- Handle edge cases
- Clean up implementation

### 8.1 Optimization Opportunities

1. **Selective register saving**: Only save registers actually used
2. **Tail call optimization**: Detect and optimize tail calls
3. **Instruction scheduling**: Reorder to hide latencies
4. **Dead code elimination**: Remove unused computations

### 8.2 Edge Cases

- Variadic functions (printf with multiple types)
- Returning large structures (rare in Sox, can punt)
- Floating-point operations
- Exception handling (optional, can defer)

### 8.3 Cleanup

- Refactor code duplication between x86-64 and ARM64
- Add comprehensive error messages
- Improve debug output

### Deliverables
- Performance-optimized code generator
- Edge case handling
- Clean codebase

---

## Risk Assessment & Mitigation

| Risk | Likelihood | Impact | Mitigation |
|------|-----------|--------|-----------|
| Relocation format mismatch | Medium | High | Early validation against reference binaries |
| ARM64 STP encoding bugs | Medium | High | Extensive encoder tests |
| Stack alignment off-by-one | Medium | Medium | Add debugging output, detailed tests |
| Symbol table corruption | Low | Critical | Validate symbol count matches relocations |
| ABI calling convention misunderstandings | Medium | High | Compare with clang output at each phase |

---

## Implementation Order & Dependencies

```
Phase 1 (Diagnosis)
    ↓
Phase 2 (x86-64 Stack) ← Phase 1
    ↓
Phase 3 (x86-64 Calls) ← Phase 2
    ↓
Phase 4 (ARM64 Fix) ← Phase 1
    ↓
Phase 5 (ARM64 Calls) ← Phase 4
    ↓
Phase 6 (Runtime) ← Phase 3 & Phase 5
    ↓
Phase 7 (Integration) ← Phase 6
    ↓
Phase 8 (Optimization)
```

**Parallel Tracks**:
- Phases 2-3 can proceed independently from Phase 4-5
- Both can happen simultaneously

**Critical Path**: Phase 1 → Phase 2 → Phase 3 → Phase 6 (x86-64 path, ~3 weeks)

---

## Success Criteria

### Phase-by-Phase

**Phase 1**: ✓ Can identify exactly why executable crashes
**Phase 2**: ✓ Generated x86-64 code maintains 16-byte stack alignment
**Phase 3**: ✓ Can call `printf()` from generated code
**Phase 4**: ✓ ARM64 register mapping matches actual registers
**Phase 5**: ✓ Can call `printf()` from ARM64 generated code
**Phase 6**: ✓ Type operations call correct runtime functions
**Phase 7**: ✓ 20+ integration tests pass
**Phase 8**: ✓ Code generation < 10% of compile time

### Overall Success

The native code generation system is **successful** when:

1. **Functional**: Generated executables run without crashes
2. **Correct**: Output matches interpreted results
3. **ABI-Compliant**: Passes linking and execution on standard systems
4. **Comprehensive**: Handles all Sox language features
5. **Documented**: Implementation is understandable and maintainable

---

## Estimated Effort

| Phase | Duration | Complexity | Risk |
|-------|----------|-----------|------|
| 1 | 3 days | Low | Low |
| 2 | 3 days | Medium | Medium |
| 3 | 5 days | High | High |
| 4 | 3 days | Medium | Medium |
| 5 | 5 days | High | High |
| 6 | 3 days | Medium | Medium |
| 7 | 4 days | Medium | Low |
| 8 | 3 days | Medium | Low |
| **Total** | **4-6 weeks** | **High** | **Medium** |

---

## Next Steps

1. **Immediately** (Today):
   - Approve this plan
   - Set up test infrastructure (Phase 1)
   - Begin profiling current executable crashes

2. **This Week**:
   - Complete Phase 1 diagnosis
   - Begin Phase 2 stack alignment work

3. **Next 2 Weeks**:
   - Complete Phase 2-3 for x86-64
   - Begin Phase 4-5 for ARM64 in parallel

4. **Weeks 3-6**:
   - Complete Phase 4-5
   - Integrate runtime (Phase 6)
   - Full integration testing (Phase 7)
   - Optimization (Phase 8)

---

## References

- **x86-64 ABI**: https://refspecs.linuxbase.org/elf/x86_64-abi-0.99.pdf
- **ARM64 ABI**: https://developer.arm.com/docs/ihi0055/latest
- **ELF Format**: https://refspecs.linuxbase.org/elf/elf.pdf
- **Mach-O Format**: https://github.com/aidansteele/osx-abi-macho-file-format-reference
- **Compiler Implementation**: https://craftinginterpreters.com (reference material)


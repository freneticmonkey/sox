# Phase 2: Before and After Comparison

## Stack Alignment Problem (Before)

### Original Prologue Code
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

### Stack State After Original Prologue

Assuming `frame_size = 32` bytes (from register allocator):

```
Entry RSP:           16n + 8        (after call instruction pushed return addr)
After push rbp:      16n            (aligned)
After sub rsp, 32:   16n - 32       (aligned if 32 is multiple of 16)
After push rbx:      16n - 40       (NOT aligned! 16n + 8)
After push r12-r15:  16n - 72       (NOT aligned! 16n + 8)
```

**Problem:** Stack is misaligned by 8 bytes before any function call!

### What Went Wrong?

1. **Order of Operations:** Local space allocated BEFORE saving registers
2. **No Alignment Adjustment:** Frame size from allocator not adjusted for register pushes
3. **Fragile Math:** Alignment calculation must account for ALL stack operations

### Example with Real Numbers

If entry RSP = 0x7fff5fbff000 (16n + 8) and frame_size = 32:

```
Entry:              0x7fff5fbff000  (16n + 8)
push rbp:           0x7fff5fbfeff8  (16n)     ✓ aligned
sub rsp, 32:        0x7fff5fbfefd8  (16n)     ✓ aligned
push rbx:           0x7fff5fbfefd0  (16n + 8) ✗ MISALIGNED
push r12-r15:       0x7fff5fbfefb0  (16n + 8) ✗ MISALIGNED
```

When calling printf or any function using SSE/AVX, the misalignment causes SIGSEGV!

## Stack Alignment Solution (After)

### New Prologue Code
```c
static void emit_function_prologue(codegen_context_t* ctx) {
    int saved_regs = 5;  // RBX, R12, R13, R14, R15
    int locals_size = regalloc_get_frame_size(ctx->regalloc);
    int aligned_frame = calculate_aligned_frame_size(locals_size, saved_regs);

    // 1. Push RBP
    x64_push_reg(ctx->asm_, X64_RBP);

    // 2. Set up frame pointer
    x64_mov_reg_reg(ctx->asm_, X64_RBP, X64_RSP);

    // 3. Save callee-saved registers FIRST
    x64_push_reg(ctx->asm_, X64_RBX);
    x64_push_reg(ctx->asm_, X64_R12);
    x64_push_reg(ctx->asm_, X64_R13);
    x64_push_reg(ctx->asm_, X64_R14);
    x64_push_reg(ctx->asm_, X64_R15);

    // 4. Allocate ALIGNED local space
    if (aligned_frame > 0) {
        x64_sub_reg_imm(ctx->asm_, X64_RSP, aligned_frame);
    }

    ctx->current_frame_alignment = aligned_frame;
    ctx->current_stack_offset = 8 + 8 + (saved_regs * 8) + aligned_frame;
}
```

### Helper Function for Alignment
```c
static int calculate_aligned_frame_size(int locals_size, int saved_regs_count) {
    int after_saved_regs = saved_regs_count * 8;
    int total_after_rbp = after_saved_regs + locals_size;

    // Round up to nearest multiple of 16
    int aligned_total = ((total_after_rbp + 15) / 16) * 16;

    // Calculate padding needed for locals
    int aligned_locals = aligned_total - after_saved_regs;

    return aligned_locals;
}
```

### Stack State After New Prologue

With `locals_size = 32` and `saved_regs = 5`:

**Alignment Calculation:**
```
after_saved_regs = 5 * 8 = 40 bytes
total_after_rbp = 40 + 32 = 72 bytes
aligned_total = ⌈72 / 16⌉ * 16 = 80 bytes
aligned_frame = 80 - 40 = 40 bytes
```

**Stack Layout:**
```
Entry RSP:           16n + 8        (after call instruction)
After push rbp:      16n            (aligned)
After push rbx:      16n - 8
After push r12:      16n - 16       (aligned)
After push r13:      16n - 24
After push r14:      16n - 32       (aligned)
After push r15:      16n - 40
After sub rsp, 40:   16n - 80       (aligned) ✓
```

**With Real Numbers (entry RSP = 0x7fff5fbff000):**
```
Entry:              0x7fff5fbff000  (16n + 8)
push rbp:           0x7fff5fbfeff8  (16n)     ✓
push rbx-r15:       0x7fff5fbfefe0  (16n)     ✓
sub rsp, 40:        0x7fff5fbfefb8  (16n)     ✓ ALIGNED!
```

Perfect 16-byte alignment before any call instruction!

## Epilogue Comparison

### Before
```c
static void emit_function_epilogue(codegen_context_t* ctx) {
    // Restore callee-saved registers
    x64_pop_reg(ctx->asm_, X64_R15);
    x64_pop_reg(ctx->asm_, X64_R14);
    x64_pop_reg(ctx->asm_, X64_R13);
    x64_pop_reg(ctx->asm_, X64_R12);
    x64_pop_reg(ctx->asm_, X64_RBX);

    // mov rsp, rbp  ← This masked alignment issues!
    x64_mov_reg_reg(ctx->asm_, X64_RSP, X64_RBP);

    // pop rbp
    x64_pop_reg(ctx->asm_, X64_RBP);

    // ret
    x64_ret(ctx->asm_);
}
```

**Problem:** Using "mov rsp, rbp" resets the stack pointer, which works but:
1. Masks alignment issues (appears to work even when prologue is wrong)
2. Doesn't match the prologue structure (asymmetric)
3. Less explicit about what's happening

### After
```c
static void emit_function_epilogue(codegen_context_t* ctx) {
    // 1. Deallocate local variables
    if (ctx->current_frame_alignment > 0) {
        x64_add_reg_imm(ctx->asm_, X64_RSP, ctx->current_frame_alignment);
    }

    // 2. Restore callee-saved registers (reverse order)
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

**Improvements:**
1. Explicitly deallocates local space (mirrors prologue)
2. More predictable and debuggable
3. Uses tracked alignment value
4. Symmetric with prologue structure

## Generated Machine Code Comparison

### Test Case
```sox
var x = 2;
var y = 3;
var z = x + y;
print(z);
```

Register allocator reports: frame_size = 144 bytes

### Before (Hypothetical - would crash)
```assembly
push rbp              ; RSP = 16n
mov rbp, rsp
sub rsp, 144          ; RSP = 16n - 144
push rbx              ; RSP = 16n - 152 (NOT aligned!)
push r12
push r13
push r14
push r15              ; RSP = 16n - 184 (NOT aligned!)
; ... function body ...
; call printf would crash here!
```

### After (Actually Generated)
```assembly
push rbp              ; RSP = 16n
mov rbp, rsp
push rbx              ; RSP = 16n - 8
push r12              ; RSP = 16n - 16
push r13              ; RSP = 16n - 24
push r14              ; RSP = 16n - 32
push r15              ; RSP = 16n - 40
sub rsp, 152          ; RSP = 16n - 192 = 16n (ALIGNED!)
; ... function body ...
; call printf safe!
; ... epilogue ...
add rsp, 152          ; RSP = 16n - 40
pop r15
pop r14
pop r13
pop r12
pop rbx               ; RSP = 16n
pop rbp               ; RSP = 16n + 8
ret
```

**Hex Dump:**
```
Prologue:
55                    push rbp
48 89 e5              mov rbp, rsp
53                    push rbx
41 54                 push r12
41 55                 push r13
41 56                 push r14
41 57                 push r15
48 81 ec 98 00 00 00  sub rsp, 0x98 (152 bytes)

Epilogue:
48 81 c4 98 00 00 00  add rsp, 0x98 (152 bytes)
41 5f                 pop r15
41 5e                 pop r14
41 5d                 pop r13
41 5c                 pop r12
5b                    pop rbx
5d                    pop rbp
c3                    ret
```

## Why This Matters

### System V AMD64 ABI Requirements

From the System V Application Binary Interface AMD64 Architecture Processor Supplement:

> "The end of the input argument area shall be aligned on a 16 (32, if __m256 is passed on stack) byte boundary. In other words, the value (%rsp + 8) is always a multiple of 16 (32) when control is transferred to the function entry point."

This means:
1. When a function is called, `call` pushes 8-byte return address
2. At function entry, RSP = 16n + 8
3. Before any CALL inside the function, RSP must be 16n

### Consequences of Misalignment

**With SSE/AVX Instructions:**
```c
movdqa xmm0, [rsp]  ; Requires 16-byte aligned RSP
```
If RSP is misaligned: **SIGSEGV (segmentation fault)**

**In C Library Functions:**
Most modern C library functions use SSE/AVX for performance:
- `printf()` - Uses SSE for formatting
- `strlen()` - Uses SSE for fast string scanning
- `memcpy()` - Uses AVX for bulk copying

All of these will crash if the stack is misaligned!

## Testing Verification

### Before Fix
- VM/interpreter tests: PASS (86/86)
- Native code generation: Would crash on any function call
- External C calls: Immediate SIGSEGV

### After Fix
- VM/interpreter tests: PASS (86/86)
- Native code generation: Produces aligned code
- Stack alignment verified: 16-byte aligned
- External C calls: Ready (pending relocation support)

## Summary

Phase 2 successfully resolved the critical stack alignment issue by:

1. **Reordering Operations:** Save registers before allocating locals
2. **Systematic Calculation:** Helper function ensures proper alignment
3. **Explicit Tracking:** Context tracks alignment for epilogue
4. **ABI Compliance:** Follows System V AMD64 ABI requirements

The native code generator now produces ABI-compliant prologues and epilogues that maintain correct stack alignment, eliminating the primary cause of crashes in native code execution.

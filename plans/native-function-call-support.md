# Plan: Native Function Call Support (ARM64)

**Date:** 2025-12-05  
**Status:** Planning  
**Author:** Codex (AI Agent)  
**Goal:** Implement full ARM64 function-call support so `IR_CALL` produces working code (covering IR metadata, argument marshalling, direct/indirect call emission, relocations, and return handling).

---

## Background

- `IR_CALL` is emitted by `src/native/ir_builder.c` whenever bytecode `OP_CALL` is seen. The instruction currently captures the callee value (`operand1`) and its argument list (`call_args` / `call_arg_count`), but `call_target` is always `NULL`.  
- `src/native/codegen_arm64.c:1098-1102` handles `IR_CALL` by emitting `BL 0` with no relocation or argument setup, causing execution to fall through and corrupt control flow.  
- The x86-64 backend contains argument-marshalling scaffolding but also relies on `call_target`; because that metadata is never populated, its emitted `call rel32` instructions also jump to the next instruction. Rather than mirroring the buggy behavior, this plan defines the complete, architecture-agnostic call flow and then applies it to ARM64 first.

---

## High-Level Tasks

1. **IR Enhancements** – Provide enough information for codegen to distinguish direct vs. indirect calls and know the callee symbol.
2. **Argument Marshalling** – Move scalar and 16-byte `value_t` arguments into ARM64 ABI registers / stack slots.
3. **Call Emission & Relocations** – Emit either `BL <symbol>` (direct) or `BLR Xt` (indirect) and record the appropriate relocation.
4. **Return Value Handling** – Copy `X0`/`X1` back into the destination virtual register(s).

---

## Detailed Steps

### 1. IR Enhancements (`src/native/ir_builder.c`, `src/native/ir.h`, `src/native/ir.c`)
- Detect when the callee is a known Sox function at compile time (e.g., top-level functions or closures with resolved targets).  
  - For simple named functions, set `instr->call_target = callee_function->name`.  
  - Track whether the call is direct (`call_target != NULL`) or indirect (`operand1` is a register holding a function object).  
- For indirect calls, ensure the callee value (closure) ends up in a register (`operand1`) so codegen can load its entry point at runtime (future closure lowering).  
- Update IR printer to display `call_target` for debugging.

### 2. ARM64 Argument Marshalling (`src/native/codegen_arm64.c`)
- Introduce helper `static void marshal_arguments_arm64(codegen_arm64_context_t*, ir_value_t* args, int count)` similar to `marshal_arguments_x64`.  
  - Use registers X0–X7 for the first eight 64-bit arguments, respecting `value_t`’s 16-byte layout (pairs consume `(Xn, Xn+1)`).  
  - Spill additional arguments to the stack in reverse order, maintaining 16-byte stack alignment (track current stack offset in the context).  
  - Handle constants by loading immediates; handle spilled vregs by loading from their spill slots.
- Reuse existing helpers (`get_register_pair_arm64`, `load_value_from_spill`) to move 16-byte `value_t`s.

### 3. Call Instruction Emission (`src/native/codegen_arm64.c: IR_CALL`)
- Replace the stub with:
  1. Marshal arguments (Step 2).  
  2. If `call_target` is set → emit `arm64_bl(ctx->asm_, 0)` and `arm64_add_relocation(..., ARM64_RELOC_CALL26, call_target, 0)`.  
  3. Else (indirect) → move the callee pointer into a temp register (e.g., X16) and emit `arm64_blr(X16)`; later phases must lower closures to raw pointers.  
  4. After the call, pop stack arguments if any to restore SP.
- Ensure we save/restore volatile registers or rely on the ABI (X0–X17 are caller-saved). Track stack alignment (“red zone” not available on ARM64).

### 4. Return Value Propagation
- For 16-byte destinations, copy `X0:X1` into the allocated register pair or spill slot; for scalars, move `X0` to the destination register.  
- This mirrors existing handling in `IR_RETURN` and runtime helper calls, so factor it into a helper if useful.

### 5. Testing & Validation
- Add/extend native integration tests (`src/test/scripts/native/`) with samples covering:
  - Direct function returning scalar and value_t.  
  - Functions with >8 arguments (stack spill).  
  - Nested calls (call within call).  
- Update native codegen diagnostics (optional) to log when direct vs. indirect call paths are taken.  
- Rebuild runtime library to ensure symbols referenced by `call_target` exist (link with `libsox_runtime` or generated stubs).

### 6. Fallback Behavior
- Until closure-to-entry-point lowering is implemented, detect unsupported call forms (e.g., calling first-class values with unknown entry) and emit a clear error rather than `BL 0`.

---

## Deliverables

1. Updated IR builder that records call targets.  
2. ARM64 argument marshalling helper + stack alignment tracking.  
3. Proper `IR_CALL` emission with relocations / BLR support.  
4. Tests demonstrating user-defined functions run successfully under `--native --custom-linker`.  
5. Documentation (README / CLAUDE.md) summarizing ARM64 calling convention assumptions.

---

## Estimated Effort

| Task | Estimate |
| --- | --- |
| IR enhancements + debugging | 0.5 day |
| Argument marshalling + stack alignment | 1.0 day |
| Call emission + relocations | 0.5 day |
| Return propagation refactor | 0.25 day |
| Tests + validation | 0.5 day |
| Total | ~2.75 days |

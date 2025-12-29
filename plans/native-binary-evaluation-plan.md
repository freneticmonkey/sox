# Plan: Native Binary Evaluation w/ Custom Linker

**Date:** 2025-12-05  
**Status:** Planning in progress  
**Author:** Codex (AI Agent)  
**Scope:** Evaluate `--native` + `--custom-linker` pipeline and diagnose hangs triggered by function calls and block statements.

---

## Objective

Establish a repeatable evaluation workflow that (a) reproduces the hang, (b) isolates whether the fault originates in codegen vs. link vs. runtime metadata, and (c) captures the artefacts (assembly/object layout, linker logs, runtime traces) required for a targeted fix.

---

## Current Behaviour Snapshot

- `src/native/native_codegen.c` maps bytecode instructions onto target-specific instruction templates (`src/native/codegen_{arm64,x86}.c`) and emits Mach-O object files for macOS (ELF/COFF paths partially stubbed).  
- `src/lib/linker.c` orchestrates either the system toolchain or Sox’s multi-phase custom linker (`src/native/linker_core.c`, `relocation_processor.c`, `macho_executable.c`).  
- When building with `--native --custom-linker`, straight-line procedural programs succeed, but as soon as the compiled code contains function calls or enters a block scope (`for`/`while`), the produced binary launches and then stalls indefinitely until killed.

Key unknowns:
- Whether the hang is caused by incorrect control-flow codegen (bad call/ret, stack alignment, or loop back-edges) or by relocation/linkage (broken symbol fixups for function prologues).
- Whether the emitted binary is stuck in an infinite loop inside generated code or deadlocked inside runtime initialization (e.g., custom startup stub waiting on unresolved symbol).

---

## Risks & Hypotheses

1. **Call sequence mismatch** – Sox VM calling convention may not match the native stub (missing frame teardown, wrong callee save set, or non-updated stack depth metadata).  
2. **Loop emission bug** – Jump targets for loop headers/tails may point into the wrong instruction or align to invalid boundaries, trapping the CPU in self-branch.  
3. **Relocation omission** – Custom linker might skip relocation types triggered only when functions/blocks introduce new labels (e.g., `ARM64_RELOC_BRANCH26`).  
4. **Runtime trampoline misuse** – Native entry stub might expect serialized constants/closures that are only initialised when no functions are present.

**Investigation Priority:** The hypotheses are all plausible. We should prioritize investigating them in the order listed. **#1** and **#2** are the most likely culprits as they relate directly to code generation for the failing constructs. **#3** and **#4** are more likely to be secondary effects or related to more complex scenarios.

---

## Evaluation Strategy

### 1. Isolate Fault Domain: Codegen vs. Linker
Before diving deep, the first step is to determine if the fault lies in the code generator or the custom linker. We will use the system linker as a control.

- For each test case in the reproduction matrix, compile with `sox --native` (using the system linker) and `sox --native --custom-linker`.
  - If the binary generated with the **system linker works**, the problem is almost certainly in the custom linker (`linker_core.c`, `relocation_processor.c`, etc.).
  - If the binary generated with the **system linker also hangs**, the problem is in the native code generation (`native_codegen.c`, `codegen_arm64.c`, etc.).

This will allow us to focus debugging efforts on the correct subsystem.

### 2. Environment Baseline
- Ensure both Debug & Release native toolchains build (`make native_debug`, `make native_release`).  
- Capture compiler/linker versions (`clang -v`, `ld -v`) for reproducibility.  
- Verify `./bin/x64/Debug/sox --help` lists both `--native` and `--custom-linker`.

### 3. Reproduction Matrix
Create a minimal suite under `src/test/scripts/native/`:
| Test | Feature exercised | Expected (System Linker) | Expected (Custom Linker) |
| --- | --- | --- | --- |
| `straight_line.sox` | assignments/print | ✅ Runs | ✅ Runs |
| `single_call.sox` | top-level call to user function | ❓ | ❌ Hang |
| `loop_counter.sox` | `for` loop with counter print | ❓ | ❌ Hang |
| `nested_call_loop.sox` | recursion inside loop | ❓ | ❌ Hang (stress) |

For each script:
1. `./bin/x64/Debug/sox script.sox --native [--custom-linker] --emit-obj build/native/<name>.o --emit-asm build/native/<name>.s` (note: CLI currently lacks these switches; see prerequisites below).  
2. Execute resulting binary, capture stdout/stderr, and enforce timeout (e.g., 5s) to log hang.

### 3a. CLI Prerequisite
- Extend `src/lib/arg_parser.c` / `arg_parser.h` to add `--emit-obj <path>` and `--emit-asm <path>` flags (or reuse existing `--native-out` semantics) so the evaluation tooling can request intermediate artefacts directly.  
- Plumb the parsed paths through to the native codegen entry point (likely `src/native/native_codegen.c`) to emit the requested `.o`/`.s` outputs alongside the executable artefact.  
- Update `--help` text, default path handling, and any scripts/Make targets that rely on the new options.

### 4. Instrument Native Codegen
- Enable any dormant tracing macros in `src/native/codegen.c` or add temporary logging to dump emitted instruction stream per opcode.  
- Compare code emission between bytecode interpreter fallback and native pipeline for the same source (use `src/lib/debug.c` disassembly for baseline).  
- Focus on:
  - Function prologue/epilogue generation.
  - Loop back-edges (branch offsets, label resolution).
  - Stack/frame pointer adjustments when entering block scopes.

### 5. Object & Relocation Inspection
- Use `llvm-objdump -d --mcpu=apple-m1 build/native/<name>.o` and `otool -l` (macOS) to inspect text sections, symbol tables, and relocation entries.  
- Confirm branch instructions reference correct symbols/offsets (especially `BL`, `B.cond` on ARM64).  
- Cross-check with linker logs from `src/native/linker_core.c` (add verbose flag printing section merges).

### 6. Custom Linker Phase Validation
- Step through each phase (parsing → layout → relocations → Mach-O emit) using the problematic object files.  
- Validate relocation processor handles branch reloc types triggered by functions/loops (`src/native/relocation_processor.c`).  
- Diff final executable vs. system linker output for the same object pair using `cmp` and `otool -tV` to confirm mismatches.

### 7. Runtime Trace
- **Note on Debug Symbols:** Ensure the build process (e.g., `make native_debug`) is configured to pass the `-g` flag to `clang` not only when compiling the `sox` compiler itself, but also when the `sox` compiler generates native executables. This provides `lldb` with the necessary DWARF debug information.
- For binaries that hang, attach `lldb -- ./out/<name>` and pause after stall to inspect `bt`.  
- Capture PC, LR, SP registers; determine whether we’re stuck in generated code, the runtime library, or dyld start.  
- Record memory at suspected loop instruction to confirm infinite spin vs. waiting syscall.

### 8. Regression Safeguards
- Once root cause understood, encode automated regression tests:  
  - Add native execution tests invoked via `ctest native` or dedicated script to `src/test/scripts/native/run_native_matrix.sh`.  
  - Extend custom linker integration tests (`src/test/linker/integration/`) to include function/loop scenarios and assert exit status/timeouts.

---

## Deliverables

1. **Reproduction artifacts:** source snippets, emitted assembly/object dumps, linker verbose logs, LLDB traces.  
2. **Root cause summary:** short write-up tying the hang to a specific subsystem.  
3. **Fix readiness checklist:** gating criteria before patching (unit tests identified, instrumentation toggles removed).  
4. **Automation hooks:** scripts or Make targets for rerunning the native evaluation matrix.

---

## Timeline (suggested)

| Day | Focus |
| --- | --- |
| 0.5 | Environment validation + repro scripts |
| 1.0 | Instrument codegen + collect object dumps |
| 0.5 | Custom linker phase trace |
| 0.5 | Runtime debugging sessions |
| 0.5 | Documentation of findings + regression test scaffolding |

#ifndef SOX_CODEGEN_ARM64_H
#define SOX_CODEGEN_ARM64_H

#include "ir.h"
#include "arm64_encoder.h"
#include "regalloc.h"

// Relocation entry for ARM64
typedef struct {
    size_t offset;         // Offset in code where relocation is needed
    const char* symbol;    // Symbol name
    uint32_t type;         // Relocation type (R_AARCH64_CALL26, etc.)
    int64_t addend;        // Addend for relocation
} codegen_arm64_relocation_t;

// Patch location for forward jumps (ARM64)
typedef struct {
    size_t offset;
    int target_label;
} jump_patch_arm64_t;

// ARM64 code generation context
typedef struct {
    ir_module_t* module;
    arm64_assembler_t* asm_;
    regalloc_context_t* regalloc;

    // Current function being generated
    ir_function_t* current_function;

    // Label to code offset mapping
    int* label_offsets;
    int label_count;
    int label_capacity;

    // Patch locations for forward jumps
    jump_patch_arm64_t* jump_patches;
    int patch_count;
    int patch_capacity;

    // Relocations for external symbols
    codegen_arm64_relocation_t* relocations;
    int relocation_count;
    int relocation_capacity;
} codegen_arm64_context_t;

// Create ARM64 code generator
codegen_arm64_context_t* codegen_arm64_new(ir_module_t* module);
void codegen_arm64_free(codegen_arm64_context_t* ctx);

// Generate native code from IR module
bool codegen_arm64_generate(codegen_arm64_context_t* ctx);

// Generate code for a single function
bool codegen_arm64_generate_function(codegen_arm64_context_t* ctx, ir_function_t* func);

// Get generated code
uint8_t* codegen_arm64_get_code(codegen_arm64_context_t* ctx, size_t* size);

// Get relocations
codegen_arm64_relocation_t* codegen_arm64_get_relocations(codegen_arm64_context_t* ctx, int* count);

// Print generated code (disassembly)
void codegen_arm64_print(codegen_arm64_context_t* ctx);

#endif

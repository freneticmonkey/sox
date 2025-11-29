#ifndef SOX_CODEGEN_ARM64_H
#define SOX_CODEGEN_ARM64_H

#include "ir.h"
#include "arm64_encoder.h"
#include "regalloc.h"

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
    typedef struct {
        size_t offset;
        int target_label;
    } jump_patch_t;

    jump_patch_t* jump_patches;
    int patch_count;
    int patch_capacity;
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

// Print generated code (disassembly)
void codegen_arm64_print(codegen_arm64_context_t* ctx);

#endif

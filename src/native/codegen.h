#ifndef SOX_CODEGEN_H
#define SOX_CODEGEN_H

#include "ir.h"
#include "x64_encoder.h"
#include "regalloc.h"

// Code generation context
typedef struct {
    ir_module_t* module;
    x64_assembler_t* asm_;
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
} codegen_context_t;

// Create code generator
codegen_context_t* codegen_new(ir_module_t* module);
void codegen_free(codegen_context_t* ctx);

// Generate native code from IR module
bool codegen_generate(codegen_context_t* ctx);

// Generate code for a single function
bool codegen_generate_function(codegen_context_t* ctx, ir_function_t* func);

// Get generated code
uint8_t* codegen_get_code(codegen_context_t* ctx, size_t* size);

// Print generated code (disassembly)
void codegen_print(codegen_context_t* ctx);

#endif

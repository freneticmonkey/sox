#ifndef SOX_CODEGEN_H
#define SOX_CODEGEN_H

#include "ir.h"
#include "x64_encoder.h"
#include "regalloc.h"

// Patch location for forward jumps
typedef struct {
    size_t offset;
    int target_label;
} jump_patch_t;

// External symbol relocation
typedef struct {
    size_t offset;          // Offset in code where relocation is needed
    const char* symbol;     // Symbol name (function name)
    uint32_t type;         // Relocation type (R_X86_64_PLT32, etc.)
    int64_t addend;        // Addend for relocation
} codegen_relocation_t;

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
    jump_patch_t* jump_patches;
    int patch_count;
    int patch_capacity;

    // Relocations for external symbols
    codegen_relocation_t* relocations;
    int relocation_count;
    int relocation_capacity;

    // Stack frame tracking for alignment
    int current_stack_offset;     // Current RSP offset from function entry
    int current_frame_alignment;  // Frame size including alignment padding
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

// Get relocations
codegen_relocation_t* codegen_get_relocations(codegen_context_t* ctx, int* count);

// Print generated code (disassembly)
void codegen_print(codegen_context_t* ctx);

#endif

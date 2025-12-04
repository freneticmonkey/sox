#ifndef SOX_REGALLOC_H
#define SOX_REGALLOC_H

#include "ir.h"
#include "x64_encoder.h"
#include "arm64_encoder.h"
#include <stdbool.h>

// Architecture type for register allocation
typedef enum {
    REGALLOC_ARCH_X64,
    REGALLOC_ARCH_ARM64,
} regalloc_arch_t;

// Live range for a virtual register
typedef struct {
    int vreg;              // Virtual register number
    int start;             // First instruction using this register
    int end;               // Last instruction using this register
    x64_register_t preg;   // Assigned physical register (or X64_NO_REG)
    int spill_slot;        // Stack spill slot (or -1 if not spilled)
    bool is_float;         // Is this a floating-point register?
} live_range_t;

// Register allocation context
typedef struct {
    ir_function_t* function;
    regalloc_arch_t arch;  // Target architecture

    // Live ranges for each virtual register
    live_range_t* ranges;
    int range_count;
    int range_capacity;

    // Available physical registers (generic register numbers)
    int* available_regs;
    int available_count;

    // Register mapping: vreg -> physical register or spill slot
    int* vreg_to_preg;  // Generic register number (0-31)
    int* vreg_to_spill;
    int vreg_count;

    // Stack frame info
    int frame_size;        // Total frame size in bytes
    int spill_count;       // Number of spilled registers
} regalloc_context_t;

// Create register allocator
regalloc_context_t* regalloc_new(ir_function_t* function, regalloc_arch_t arch);
void regalloc_free(regalloc_context_t* ctx);

// Perform register allocation
bool regalloc_allocate(regalloc_context_t* ctx);

// Query allocation results (returns generic register number or -1 for none)
int regalloc_get_register(regalloc_context_t* ctx, int vreg);
int regalloc_get_spill_slot(regalloc_context_t* ctx, int vreg);
bool regalloc_is_spilled(regalloc_context_t* ctx, int vreg);

// Convert generic register number to architecture-specific register
x64_register_t regalloc_to_x64_register(int reg);
arm64_register_t regalloc_to_arm64_register(int reg);

// Get frame size for prologue/epilogue
int regalloc_get_frame_size(regalloc_context_t* ctx);

// Print allocation for debugging
void regalloc_print(regalloc_context_t* ctx);

// Get list of used callee-saved registers for the target architecture
// Returns count of used registers, and fills 'out_regs' array (caller must provide buffer)
// For x64: checks RBX, R12-R15 (returns generic register numbers 3, 12-15)
// For ARM64: checks X19-X28 (returns generic register numbers 19-28)
int regalloc_get_used_callee_saved(regalloc_context_t* ctx, int* out_regs, int max_regs);

#endif

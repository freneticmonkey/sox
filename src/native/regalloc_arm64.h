#ifndef SOX_REGALLOC_ARM64_H
#define SOX_REGALLOC_ARM64_H

#include "ir.h"
#include "arm64_encoder.h"
#include <stdbool.h>

// Live range for a virtual register
typedef struct {
    int vreg;                // Virtual register number
    int start;               // First instruction using this register
    int end;                 // Last instruction using this register
    arm64_register_t preg;   // Assigned physical register (or ARM64_NO_REG)
    int spill_slot;          // Stack spill slot (or -1 if not spilled)
    bool is_float;           // Is this a floating-point register?
} live_range_arm64_t;

// Register allocation context for ARM64
typedef struct {
    ir_function_t* function;

    // Live ranges for each virtual register
    live_range_arm64_t* ranges;
    int range_count;
    int range_capacity;

    // Available physical registers
    arm64_register_t* available_regs;
    int available_count;

    // Register mapping: vreg -> physical register or spill slot
    arm64_register_t* vreg_to_preg;
    int* vreg_to_spill;
    int vreg_count;

    // Stack frame info
    int frame_size;          // Total frame size in bytes
    int spill_count;         // Number of spilled registers
} regalloc_arm64_context_t;

// Create ARM64 register allocator
regalloc_arm64_context_t* regalloc_arm64_new(ir_function_t* function);
void regalloc_arm64_free(regalloc_arm64_context_t* ctx);

// Perform register allocation
bool regalloc_arm64_allocate(regalloc_arm64_context_t* ctx);

// Query allocation results
arm64_register_t regalloc_arm64_get_register(regalloc_arm64_context_t* ctx, int vreg);
int regalloc_arm64_get_spill_slot(regalloc_arm64_context_t* ctx, int vreg);
bool regalloc_arm64_is_spilled(regalloc_arm64_context_t* ctx, int vreg);

// Get frame size for prologue/epilogue
int regalloc_arm64_get_frame_size(regalloc_arm64_context_t* ctx);

// Print allocation for debugging
void regalloc_arm64_print(regalloc_arm64_context_t* ctx);

#endif

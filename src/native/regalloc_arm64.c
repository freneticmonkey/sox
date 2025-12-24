#include "regalloc_arm64.h"
#include "../lib/memory.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Available registers for allocation (excluding SP, FP, LR which are reserved)
// ARM64 AArch64 calling convention:
// - X0-X7: Arguments and return values (caller-saved)
// - X9-X15: Temporary registers (caller-saved)
// - X19-X28: Callee-saved
// - X29: Frame pointer (reserved)
// - X30: Link register (reserved)
// - X31: Stack pointer (reserved)
static const arm64_register_t allocatable_regs[] = {
    ARM64_X9, ARM64_X10, ARM64_X11, ARM64_X12, ARM64_X13, ARM64_X14, ARM64_X15,
    ARM64_X19, ARM64_X20, ARM64_X21, ARM64_X22, ARM64_X23,
    ARM64_X24, ARM64_X25, ARM64_X26, ARM64_X27, ARM64_X28
};
static const int allocatable_count = sizeof(allocatable_regs) / sizeof(allocatable_regs[0]);

// Callee-saved registers in ARM64 AArch64 ABI
static const arm64_register_t callee_saved[] = {
    ARM64_X19, ARM64_X20, ARM64_X21, ARM64_X22, ARM64_X23,
    ARM64_X24, ARM64_X25, ARM64_X26, ARM64_X27, ARM64_X28
};
static const int callee_saved_count = sizeof(callee_saved) / sizeof(callee_saved[0]);

regalloc_arm64_context_t* regalloc_arm64_new(ir_function_t* function) {
    regalloc_arm64_context_t* ctx = (regalloc_arm64_context_t*)l_mem_alloc(sizeof(regalloc_arm64_context_t));
    ctx->function = function;
    ctx->ranges = NULL;
    ctx->range_count = 0;
    ctx->range_capacity = 0;

    ctx->available_regs = (arm64_register_t*)l_mem_alloc(sizeof(arm64_register_t) * allocatable_count);
    memcpy(ctx->available_regs, allocatable_regs, sizeof(arm64_register_t) * allocatable_count);
    ctx->available_count = allocatable_count;

    ctx->vreg_count = function->next_register;
    ctx->vreg_to_preg = (arm64_register_t*)l_mem_alloc(sizeof(arm64_register_t) * ctx->vreg_count);
    ctx->vreg_to_spill = (int*)l_mem_alloc(sizeof(int) * ctx->vreg_count);

    for (int i = 0; i < ctx->vreg_count; i++) {
        ctx->vreg_to_preg[i] = ARM64_NO_REG;
        ctx->vreg_to_spill[i] = -1;
    }

    ctx->frame_size = 0;
    ctx->spill_count = 0;
    ctx->spill_byte_offset = 0;

    return ctx;
}

void regalloc_arm64_free(regalloc_arm64_context_t* ctx) {
    if (!ctx) return;

    if (ctx->ranges) {
        l_mem_free(ctx->ranges, sizeof(live_range_arm64_t) * ctx->range_capacity);
    }
    if (ctx->available_regs) {
        l_mem_free(ctx->available_regs, sizeof(arm64_register_t) * allocatable_count);
    }
    if (ctx->vreg_to_preg) {
        l_mem_free(ctx->vreg_to_preg, sizeof(arm64_register_t) * ctx->vreg_count);
    }
    if (ctx->vreg_to_spill) {
        l_mem_free(ctx->vreg_to_spill, sizeof(int) * ctx->vreg_count);
    }

    l_mem_free(ctx, sizeof(regalloc_arm64_context_t));
}

static void add_live_range(regalloc_arm64_context_t* ctx, ir_value_t value, int pos) {
    int vreg = value.as.reg;
    ir_value_size_t size = value.size;

    // Find existing range or create new one
    live_range_arm64_t* range = NULL;
    for (int i = 0; i < ctx->range_count; i++) {
        if (ctx->ranges[i].vreg == vreg) {
            range = &ctx->ranges[i];
            break;
        }
    }

    if (!range) {
        // Create new range
        if (ctx->range_capacity < ctx->range_count + 1) {
            int old_capacity = ctx->range_capacity;
            ctx->range_capacity = (old_capacity < 8) ? 8 : old_capacity * 2;
            ctx->ranges = (live_range_arm64_t*)l_mem_realloc(
                ctx->ranges,
                sizeof(live_range_arm64_t) * old_capacity,
                sizeof(live_range_arm64_t) * ctx->range_capacity
            );
        }

        range = &ctx->ranges[ctx->range_count++];
        range->vreg = vreg;
        range->start = pos;
        range->end = pos;
        range->size = size;  // Store value size
        range->preg = ARM64_NO_REG;
        range->preg_high = ARM64_NO_REG;  // Initialize high register
        range->spill_slot = -1;
        range->spill_offset = -1;  // No spill offset yet
        range->is_float = false;
    } else {
        // Extend existing range
        if (pos < range->start) range->start = pos;
        if (pos > range->end) range->end = pos;
        // CRITICAL FIX: Update size to maximum seen across all uses
        // If any use of this register requires 16 bytes, the whole range needs 16 bytes
        if (size > range->size) {
            range->size = size;
        }
    }
}

static void compute_live_ranges(regalloc_arm64_context_t* ctx) {
    int pos = 0;

    for (int i = 0; i < ctx->function->block_count; i++) {
        ir_block_t* block = ctx->function->blocks[i];
        ir_instruction_t* instr = block->first;

        while (instr) {
            // Record uses of source operands (pass full ir_value_t for size info)
            if (instr->operand1.type == IR_VAL_REGISTER) {
                add_live_range(ctx, instr->operand1, pos);
            }
            if (instr->operand2.type == IR_VAL_REGISTER) {
                add_live_range(ctx, instr->operand2, pos);
            }
            if (instr->operand3.type == IR_VAL_REGISTER) {
                add_live_range(ctx, instr->operand3, pos);
            }

            // Record definition of destination (pass full ir_value_t for size info)
            if (instr->dest.type == IR_VAL_REGISTER) {
                add_live_range(ctx, instr->dest, pos);
            }

            pos++;
            instr = instr->next;
        }
    }
}

// Comparison function for sorting live ranges by start position
static int compare_ranges(const void* a, const void* b) {
    const live_range_arm64_t* ra = (const live_range_arm64_t*)a;
    const live_range_arm64_t* rb = (const live_range_arm64_t*)b;
    return ra->start - rb->start;
}

// Check if register is callee-saved
static bool is_callee_saved(arm64_register_t reg) {
    for (int i = 0; i < callee_saved_count; i++) {
        if (callee_saved[i] == reg) {
            return true;
        }
    }
    return false;
}

// Linear scan register allocation
static bool linear_scan_allocate(regalloc_arm64_context_t* ctx) {
    // Sort ranges by start position
    qsort(ctx->ranges, ctx->range_count, sizeof(live_range_arm64_t), compare_ranges);

    // Active ranges (currently live)
    live_range_arm64_t** active = (live_range_arm64_t**)l_mem_alloc(sizeof(live_range_arm64_t*) * ctx->range_count);
    int active_count = 0;

    // Free registers pool
    bool* free_regs = (bool*)l_mem_alloc(sizeof(bool) * allocatable_count);
    for (int i = 0; i < allocatable_count; i++) {
        free_regs[i] = true;
    }

    // Track which callee-saved registers are used (need to be saved/restored)
    bool* used_callee_saved = (bool*)l_mem_alloc(sizeof(bool) * callee_saved_count);
    for (int i = 0; i < callee_saved_count; i++) {
        used_callee_saved[i] = false;
    }

    for (int i = 0; i < ctx->range_count; i++) {
        live_range_arm64_t* range = &ctx->ranges[i];

        // Expire old ranges
        for (int j = 0; j < active_count; ) {
            if (active[j]->end < range->start) {
                // This range is done, free its register(s)
                if (active[j]->preg != ARM64_NO_REG) {
                    for (int k = 0; k < allocatable_count; k++) {
                        if (allocatable_regs[k] == active[j]->preg) {
                            free_regs[k] = true;
                            break;
                        }
                    }
                }
                // Also free high register if this is a pair
                if (active[j]->preg_high != ARM64_NO_REG) {
                    for (int k = 0; k < allocatable_count; k++) {
                        if (allocatable_regs[k] == active[j]->preg_high) {
                            free_regs[k] = true;
                            break;
                        }
                    }
                }
                // Remove from active list
                active[j] = active[active_count - 1];
                active_count--;
            } else {
                j++;
            }
        }

        // Try to allocate register(s)
        bool allocated = false;

        if (range->size == IR_SIZE_16BYTE) {
            // Need to allocate a register pair (consecutive registers)
            for (int j = 0; j < allocatable_count - 1; j++) {
                // Check if we have two consecutive free registers
                if (free_regs[j] && free_regs[j + 1]) {
                    arm64_register_t low_reg = allocatable_regs[j];
                    arm64_register_t high_reg = allocatable_regs[j + 1];

                    // Check if registers are actually consecutive (X0:X1, X2:X3, etc.)
                    if ((low_reg + 1) == high_reg) {
                        range->preg = low_reg;
                        range->preg_high = high_reg;
                        ctx->vreg_to_preg[range->vreg] = low_reg;
                        free_regs[j] = false;
                        free_regs[j + 1] = false;

                        // Track if we're using callee-saved registers
                        if (is_callee_saved(low_reg)) {
                            for (int k = 0; k < callee_saved_count; k++) {
                                if (callee_saved[k] == low_reg) {
                                    used_callee_saved[k] = true;
                                    break;
                                }
                            }
                        }
                        if (is_callee_saved(high_reg)) {
                            for (int k = 0; k < callee_saved_count; k++) {
                                if (callee_saved[k] == high_reg) {
                                    used_callee_saved[k] = true;
                                    break;
                                }
                            }
                        }

                        allocated = true;
                        active[active_count++] = range;
                        break;
                    }
                }
            }
        } else {
            // Allocate single register for 8-byte values
            for (int j = 0; j < allocatable_count; j++) {
                if (free_regs[j]) {
                    range->preg = allocatable_regs[j];
                    range->preg_high = ARM64_NO_REG;
                    ctx->vreg_to_preg[range->vreg] = range->preg;
                    free_regs[j] = false;

                    // Track if we're using a callee-saved register
                    if (is_callee_saved(range->preg)) {
                        for (int k = 0; k < callee_saved_count; k++) {
                            if (callee_saved[k] == range->preg) {
                                used_callee_saved[k] = true;
                                break;
                            }
                        }
                    }

                    allocated = true;
                    active[active_count++] = range;
                    break;
                }
            }
        }

        if (!allocated) {
            // Need to spill
            range->spill_offset = ctx->spill_byte_offset;

            if (range->size == IR_SIZE_16BYTE) {
                range->spill_slot = ctx->spill_count;
                ctx->spill_count += 2;  // Reserve 2 slots for 16-byte value
                ctx->spill_byte_offset += 16;  // 16 bytes for pair
            } else {
                range->spill_slot = ctx->spill_count++;
                ctx->spill_byte_offset += 8;  // 8 bytes for single register
            }
            ctx->vreg_to_spill[range->vreg] = range->spill_slot;
        }
    }

    l_mem_free(active, sizeof(live_range_arm64_t*) * ctx->range_count);
    l_mem_free(free_regs, sizeof(bool) * allocatable_count);

    // Calculate frame size
    // Include: spilled registers + locals + callee-saved register save area
    int callee_saved_area = 0;
    for (int i = 0; i < callee_saved_count; i++) {
        if (used_callee_saved[i]) {
            callee_saved_area += 8;  // 8 bytes per register
        }
    }

    l_mem_free(used_callee_saved, sizeof(bool) * callee_saved_count);

    // Frame size: spilled bytes + locals (8 bytes each) + globals (16 bytes each) + callee-saved area
    // spill_byte_offset already accounts for 8 or 16 byte allocations per value
    // Reserve space for up to 16 global variables (256 bytes)
    int global_space = 256;  // 16 globals * 16 bytes each
    ctx->frame_size = ctx->spill_byte_offset + (ctx->function->local_count * 8) + global_space + callee_saved_area;
    // Align to 16 bytes (required by ARM64 ABI)
    ctx->frame_size = (ctx->frame_size + 15) & ~15;

    return true;
}

bool regalloc_arm64_allocate(regalloc_arm64_context_t* ctx) {
    compute_live_ranges(ctx);
    return linear_scan_allocate(ctx);
}

arm64_register_t regalloc_arm64_get_register(regalloc_arm64_context_t* ctx, int vreg) {
    if (vreg >= 0 && vreg < ctx->vreg_count) {
        return ctx->vreg_to_preg[vreg];
    }
    return ARM64_NO_REG;
}

arm64_register_t regalloc_arm64_get_high_register(regalloc_arm64_context_t* ctx, int vreg) {
    // Return the high register of a pair (only valid for 16-byte values)
    if (vreg >= 0 && vreg < ctx->vreg_count) {
        for (int i = 0; i < ctx->range_count; i++) {
            if (ctx->ranges[i].vreg == vreg) {
                return ctx->ranges[i].preg_high;
            }
        }
    }
    return ARM64_NO_REG;
}

int regalloc_arm64_get_spill_slot(regalloc_arm64_context_t* ctx, int vreg) {
    if (vreg >= 0 && vreg < ctx->vreg_count) {
        return ctx->vreg_to_spill[vreg];
    }
    return -1;
}

bool regalloc_arm64_is_spilled(regalloc_arm64_context_t* ctx, int vreg) {
    return regalloc_arm64_get_spill_slot(ctx, vreg) >= 0;
}

int regalloc_arm64_get_frame_size(regalloc_arm64_context_t* ctx) {
    return ctx->frame_size;
}

int regalloc_arm64_get_spill_byte_offset(regalloc_arm64_context_t* ctx) {
    return ctx->spill_byte_offset;
}

void regalloc_arm64_print(regalloc_arm64_context_t* ctx) {
    printf("ARM64 Register Allocation for %s:\n", ctx->function->name);
    printf("  Virtual registers: %d\n", ctx->vreg_count);
    printf("  Live ranges: %d\n", ctx->range_count);
    printf("  Spilled registers: %d (total %d bytes)\n", ctx->spill_count, ctx->spill_byte_offset);
    printf("  Frame size: %d bytes\n\n", ctx->frame_size);

    printf("  Allocations:\n");
    for (int i = 0; i < ctx->range_count; i++) {
        live_range_arm64_t* range = &ctx->ranges[i];
        printf("    v%d [%d-%d] (%s): ", range->vreg, range->start, range->end,
               range->size == IR_SIZE_16BYTE ? "16byte" : "8byte");

        if (range->preg != ARM64_NO_REG) {
            if (range->preg_high != ARM64_NO_REG) {
                // Register pair
                printf("%s:%s\n", arm64_register_name(range->preg),
                       arm64_register_name(range->preg_high));
            } else {
                // Single register
                printf("%s\n", arm64_register_name(range->preg));
            }
        } else {
            printf("spill[slot=%d, offset=%d]\n", range->spill_slot, range->spill_offset);
        }
    }
    printf("\n");
}

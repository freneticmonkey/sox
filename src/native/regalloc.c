#include "regalloc.h"
#include "../lib/memory.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Available registers for x64 (excluding RSP, RBP which are reserved)
// Using generic register numbers that map to x64_register_t
static const int x64_allocatable_regs[] = {
    0, 1, 2, 3,    // RAX, RCX, RDX, RBX
    6, 7,          // RSI, RDI
    8, 9, 10, 11,  // R8-R11
    12, 13, 14, 15 // R12-R15
};
static const int x64_allocatable_count = sizeof(x64_allocatable_regs) / sizeof(x64_allocatable_regs[0]);

// Available registers for ARM64 (excluding SP, FP, LR which are reserved)
// X0-X17 are caller-saved temps, X19-X28 are callee-saved
static const int arm64_allocatable_regs[] = {
    0, 1, 2, 3, 4, 5, 6, 7,        // X0-X7 (argument registers)
    9, 10, 11, 12, 13, 14, 15,     // X9-X15 (temps, excluding X8)
    16, 17,                        // X16-X17 (IP0, IP1)
    19, 20, 21, 22, 23, 24, 25, 26, 27, 28  // X19-X28 (callee-saved)
};
static const int arm64_allocatable_count = sizeof(arm64_allocatable_regs) / sizeof(arm64_allocatable_regs[0]);

regalloc_context_t* regalloc_new(ir_function_t* function, regalloc_arch_t arch) {
    regalloc_context_t* ctx = (regalloc_context_t*)l_mem_alloc(sizeof(regalloc_context_t));
    ctx->function = function;
    ctx->arch = arch;
    ctx->ranges = NULL;
    ctx->range_count = 0;
    ctx->range_capacity = 0;

    // Set up allocatable registers based on architecture
    const int* regs;
    int count;
    if (arch == REGALLOC_ARCH_ARM64) {
        regs = arm64_allocatable_regs;
        count = arm64_allocatable_count;
    } else {
        regs = x64_allocatable_regs;
        count = x64_allocatable_count;
    }

    ctx->available_regs = (int*)l_mem_alloc(sizeof(int) * count);
    memcpy(ctx->available_regs, regs, sizeof(int) * count);
    ctx->available_count = count;

    ctx->vreg_count = function->next_register;
    ctx->vreg_to_preg = (int*)l_mem_alloc(sizeof(int) * ctx->vreg_count);
    ctx->vreg_to_spill = (int*)l_mem_alloc(sizeof(int) * ctx->vreg_count);

    for (int i = 0; i < ctx->vreg_count; i++) {
        ctx->vreg_to_preg[i] = -1;  // No register assigned
        ctx->vreg_to_spill[i] = -1;
    }

    ctx->frame_size = 0;
    ctx->spill_count = 0;

    return ctx;
}

void regalloc_free(regalloc_context_t* ctx) {
    if (!ctx) return;

    if (ctx->ranges) {
        l_mem_free(ctx->ranges, sizeof(live_range_t) * ctx->range_capacity);
    }
    if (ctx->available_regs) {
        l_mem_free(ctx->available_regs, sizeof(int) * ctx->available_count);
    }
    if (ctx->vreg_to_preg) {
        l_mem_free(ctx->vreg_to_preg, sizeof(int) * ctx->vreg_count);
    }
    if (ctx->vreg_to_spill) {
        l_mem_free(ctx->vreg_to_spill, sizeof(int) * ctx->vreg_count);
    }

    l_mem_free(ctx, sizeof(regalloc_context_t));
}

static void add_live_range(regalloc_context_t* ctx, int vreg, int pos) {
    // Find existing range or create new one
    live_range_t* range = NULL;
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
            ctx->ranges = (live_range_t*)l_mem_realloc(
                ctx->ranges,
                sizeof(live_range_t) * old_capacity,
                sizeof(live_range_t) * ctx->range_capacity
            );
        }

        range = &ctx->ranges[ctx->range_count++];
        range->vreg = vreg;
        range->start = pos;
        range->end = pos;
        range->preg = -1;  // No register assigned yet
        range->spill_slot = -1;
        range->is_float = false;
    } else {
        // Extend existing range
        if (pos < range->start) range->start = pos;
        if (pos > range->end) range->end = pos;
    }
}

static void compute_live_ranges(regalloc_context_t* ctx) {
    int pos = 0;

    for (int i = 0; i < ctx->function->block_count; i++) {
        ir_block_t* block = ctx->function->blocks[i];
        ir_instruction_t* instr = block->first;

        while (instr) {
            // Record uses of source operands
            if (instr->operand1.type == IR_VAL_REGISTER) {
                add_live_range(ctx, instr->operand1.as.reg, pos);
            }
            if (instr->operand2.type == IR_VAL_REGISTER) {
                add_live_range(ctx, instr->operand2.as.reg, pos);
            }
            if (instr->operand3.type == IR_VAL_REGISTER) {
                add_live_range(ctx, instr->operand3.as.reg, pos);
            }

            // Record definition of destination
            if (instr->dest.type == IR_VAL_REGISTER) {
                add_live_range(ctx, instr->dest.as.reg, pos);
            }

            pos++;
            instr = instr->next;
        }
    }
}

// Comparison function for sorting live ranges by start position
static int compare_ranges(const void* a, const void* b) {
    const live_range_t* ra = (const live_range_t*)a;
    const live_range_t* rb = (const live_range_t*)b;
    return ra->start - rb->start;
}

// Linear scan register allocation
static bool linear_scan_allocate(regalloc_context_t* ctx) {
    // Sort ranges by start position
    qsort(ctx->ranges, ctx->range_count, sizeof(live_range_t), compare_ranges);

    // Active ranges (currently live)
    live_range_t** active = (live_range_t**)l_mem_alloc(sizeof(live_range_t*) * ctx->range_count);
    int active_count = 0;

    // Free registers pool
    bool* free_regs = (bool*)l_mem_alloc(sizeof(bool) * ctx->available_count);
    for (int i = 0; i < ctx->available_count; i++) {
        free_regs[i] = true;
    }

    for (int i = 0; i < ctx->range_count; i++) {
        live_range_t* range = &ctx->ranges[i];

        // Expire old ranges
        for (int j = 0; j < active_count; ) {
            if (active[j]->end < range->start) {
                // This range is done, free its register
                if (active[j]->preg >= 0) {
                    for (int k = 0; k < ctx->available_count; k++) {
                        if (ctx->available_regs[k] == active[j]->preg) {
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

        // Try to allocate a register
        bool allocated = false;
        for (int j = 0; j < ctx->available_count; j++) {
            if (free_regs[j]) {
                range->preg = ctx->available_regs[j];
                ctx->vreg_to_preg[range->vreg] = range->preg;
                free_regs[j] = false;
                allocated = true;
                active[active_count++] = range;
                break;
            }
        }

        if (!allocated) {
            // Need to spill
            range->spill_slot = ctx->spill_count++;
            ctx->vreg_to_spill[range->vreg] = range->spill_slot;
        }
    }

    l_mem_free(active, sizeof(live_range_t*) * ctx->range_count);
    l_mem_free(free_regs, sizeof(bool) * ctx->available_count);

    // Calculate frame size (spilled registers + local variables)
    ctx->frame_size = (ctx->spill_count + ctx->function->local_count) * 8;
    // Align to 16 bytes (required by System V ABI)
    ctx->frame_size = (ctx->frame_size + 15) & ~15;

    return true;
}

bool regalloc_allocate(regalloc_context_t* ctx) {
    compute_live_ranges(ctx);
    return linear_scan_allocate(ctx);
}

int regalloc_get_register(regalloc_context_t* ctx, int vreg) {
    if (vreg >= 0 && vreg < ctx->vreg_count) {
        return ctx->vreg_to_preg[vreg];
    }
    return -1;  // No register assigned
}

int regalloc_get_spill_slot(regalloc_context_t* ctx, int vreg) {
    if (vreg >= 0 && vreg < ctx->vreg_count) {
        return ctx->vreg_to_spill[vreg];
    }
    return -1;
}

bool regalloc_is_spilled(regalloc_context_t* ctx, int vreg) {
    return regalloc_get_spill_slot(ctx, vreg) >= 0;
}

// Convert generic register number to x64 register
x64_register_t regalloc_to_x64_register(int reg) {
    if (reg < 0 || reg >= 16) return X64_NO_REG;
    return (x64_register_t)reg;
}

// Convert generic register number to ARM64 register
arm64_register_t regalloc_to_arm64_register(int reg) {
    if (reg < 0 || reg >= 32) return ARM64_NO_REG;
    return (arm64_register_t)reg;
}

int regalloc_get_frame_size(regalloc_context_t* ctx) {
    return ctx->frame_size;
}

void regalloc_print(regalloc_context_t* ctx) {
    printf("Register Allocation for %s:\n", ctx->function->name);
    printf("  Virtual registers: %d\n", ctx->vreg_count);
    printf("  Live ranges: %d\n", ctx->range_count);
    printf("  Spilled registers: %d\n", ctx->spill_count);
    printf("  Frame size: %d bytes\n\n", ctx->frame_size);

    printf("  Allocations:\n");
    for (int i = 0; i < ctx->range_count; i++) {
        live_range_t* range = &ctx->ranges[i];
        printf("    v%d [%d-%d]: ", range->vreg, range->start, range->end);

        if (range->preg >= 0) {
            if (ctx->arch == REGALLOC_ARCH_ARM64) {
                printf("%s\n", arm64_register_name(regalloc_to_arm64_register(range->preg)));
            } else {
                printf("%s\n", x64_register_name(regalloc_to_x64_register(range->preg)));
            }
        } else {
            printf("spill[%d]\n", range->spill_slot);
        }
    }
    printf("\n");
}

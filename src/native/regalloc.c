#include "regalloc.h"
#include "../lib/memory.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Available registers for allocation (excluding RSP, RBP which are reserved)
static const x64_register_t allocatable_regs[] = {
    X64_RAX, X64_RCX, X64_RDX, X64_RBX,
    X64_RSI, X64_RDI,
    X64_R8, X64_R9, X64_R10, X64_R11,
    X64_R12, X64_R13, X64_R14, X64_R15
};
static const int allocatable_count = sizeof(allocatable_regs) / sizeof(allocatable_regs[0]);

regalloc_context_t* regalloc_new(ir_function_t* function) {
    regalloc_context_t* ctx = (regalloc_context_t*)l_mem_alloc(sizeof(regalloc_context_t));
    ctx->function = function;
    ctx->ranges = NULL;
    ctx->range_count = 0;
    ctx->range_capacity = 0;

    ctx->available_regs = (x64_register_t*)l_mem_alloc(sizeof(x64_register_t) * allocatable_count);
    memcpy(ctx->available_regs, allocatable_regs, sizeof(x64_register_t) * allocatable_count);
    ctx->available_count = allocatable_count;

    ctx->vreg_count = function->next_register;
    ctx->vreg_to_preg = (x64_register_t*)l_mem_alloc(sizeof(x64_register_t) * ctx->vreg_count);
    ctx->vreg_to_spill = (int*)l_mem_alloc(sizeof(int) * ctx->vreg_count);

    for (int i = 0; i < ctx->vreg_count; i++) {
        ctx->vreg_to_preg[i] = X64_NO_REG;
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
        l_mem_free(ctx->available_regs, sizeof(x64_register_t) * allocatable_count);
    }
    if (ctx->vreg_to_preg) {
        l_mem_free(ctx->vreg_to_preg, sizeof(x64_register_t) * ctx->vreg_count);
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
        range->preg = X64_NO_REG;
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
            if (instr->call_args && instr->call_arg_count > 0) {
                for (int i = 0; i < instr->call_arg_count; i++) {
                    if (instr->call_args[i].type == IR_VAL_REGISTER) {
                        add_live_range(ctx, instr->call_args[i].as.reg, pos);
                    }
                }
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
    bool* free_regs = (bool*)l_mem_alloc(sizeof(bool) * allocatable_count);
    for (int i = 0; i < allocatable_count; i++) {
        free_regs[i] = true;
    }

    for (int i = 0; i < ctx->range_count; i++) {
        live_range_t* range = &ctx->ranges[i];

        // Expire old ranges
        for (int j = 0; j < active_count; ) {
            if (active[j]->end < range->start) {
                // This range is done, free its register
                if (active[j]->preg != X64_NO_REG) {
                    for (int k = 0; k < allocatable_count; k++) {
                        if (allocatable_regs[k] == active[j]->preg) {
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
        for (int j = 0; j < allocatable_count; j++) {
            if (free_regs[j]) {
                range->preg = allocatable_regs[j];
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
    l_mem_free(free_regs, sizeof(bool) * allocatable_count);

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

x64_register_t regalloc_get_register(regalloc_context_t* ctx, int vreg) {
    if (vreg >= 0 && vreg < ctx->vreg_count) {
        return ctx->vreg_to_preg[vreg];
    }
    return X64_NO_REG;
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

        if (range->preg != X64_NO_REG) {
            printf("%s\n", x64_register_name(range->preg));
        } else {
            printf("spill[%d]\n", range->spill_slot);
        }
    }
    printf("\n");
}

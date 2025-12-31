#include "codegen_arm64.h"
#include "../lib/memory.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

codegen_arm64_context_t* codegen_arm64_new(ir_module_t* module) {
    codegen_arm64_context_t* ctx = (codegen_arm64_context_t*)l_mem_alloc(sizeof(codegen_arm64_context_t));
    ctx->module = module;
    ctx->asm_ = arm64_assembler_new();
    ctx->regalloc = NULL;
    ctx->current_function = NULL;
    ctx->label_offsets = NULL;
    ctx->label_count = 0;
    ctx->label_capacity = 0;
    ctx->jump_patches = NULL;
    ctx->patch_count = 0;
    ctx->patch_capacity = 0;
    ctx->call_patches = NULL;
    ctx->call_patch_count = 0;
    ctx->call_patch_capacity = 0;
    ctx->global_vars = NULL;
    ctx->global_count = 0;
    ctx->global_capacity = 0;
    ctx->string_literals = NULL;
    ctx->string_literal_count = 0;
    ctx->string_literal_capacity = 0;
    return ctx;
}

void codegen_arm64_free(codegen_arm64_context_t* ctx) {
    if (!ctx) return;

    if (ctx->asm_) {
        arm64_assembler_free(ctx->asm_);
    }
    if (ctx->regalloc) {
        regalloc_arm64_free(ctx->regalloc);
    }
    if (ctx->label_offsets) {
        l_mem_free(ctx->label_offsets, sizeof(int) * ctx->label_capacity);
    }
    if (ctx->jump_patches) {
        l_mem_free(ctx->jump_patches, sizeof(jump_patch_arm64_t) * ctx->patch_capacity);
    }
    if (ctx->call_patches) {
        l_mem_free(ctx->call_patches, sizeof(call_patch_arm64_t) * ctx->call_patch_capacity);
    }
    if (ctx->global_vars) {
        l_mem_free(ctx->global_vars, sizeof(global_var_entry_t) * ctx->global_capacity);
    }
    if (ctx->string_literals) {
        // Free symbol names for each string literal
        for (int i = 0; i < ctx->string_literal_count; i++) {
            if (ctx->string_literals[i].symbol) {
                l_mem_free(ctx->string_literals[i].symbol, strlen(ctx->string_literals[i].symbol) + 1);
            }
        }
        l_mem_free(ctx->string_literals, sizeof(string_literal_t) * ctx->string_literal_capacity);
    }

    l_mem_free(ctx, sizeof(codegen_arm64_context_t));
}

static void mark_label(codegen_arm64_context_t* ctx, int label) {
    if (ctx->label_capacity < label + 1) {
        int old_capacity = ctx->label_capacity;
        ctx->label_capacity = (label + 1) * 2;
        ctx->label_offsets = (int*)l_mem_realloc(
            ctx->label_offsets,
            sizeof(int) * old_capacity,
            sizeof(int) * ctx->label_capacity
        );
        for (int i = old_capacity; i < ctx->label_capacity; i++) {
            ctx->label_offsets[i] = -1;
        }
    }
    ctx->label_offsets[label] = (int)arm64_get_offset(ctx->asm_);
    if (label >= ctx->label_count) {
        ctx->label_count = label + 1;
    }
}

static void add_jump_patch(codegen_arm64_context_t* ctx, size_t offset, int target_label) {
    if (ctx->patch_capacity < ctx->patch_count + 1) {
        int old_capacity = ctx->patch_capacity;
        ctx->patch_capacity = (old_capacity < 8) ? 8 : old_capacity * 2;
        void* old_ptr = ctx->jump_patches;
        size_t old_size = sizeof(jump_patch_arm64_t) * old_capacity;
        size_t new_size = sizeof(jump_patch_arm64_t) * ctx->patch_capacity;
        ctx->jump_patches = (jump_patch_arm64_t*)l_mem_realloc(old_ptr, old_size, new_size);
    }

    ctx->jump_patches[ctx->patch_count].offset = offset;
    ctx->jump_patches[ctx->patch_count].target_label = target_label;
    ctx->patch_count++;
}

static void add_call_patch(codegen_arm64_context_t* ctx, size_t offset, ir_function_t* target) {
    if (ctx->call_patch_capacity < ctx->call_patch_count + 1) {
        int old_capacity = ctx->call_patch_capacity;
        ctx->call_patch_capacity = (old_capacity < 8) ? 8 : old_capacity * 2;
        void* old_ptr = ctx->call_patches;
        size_t old_size = sizeof(call_patch_arm64_t) * old_capacity;
        size_t new_size = sizeof(call_patch_arm64_t) * ctx->call_patch_capacity;
        ctx->call_patches = (call_patch_arm64_t*)l_mem_realloc(old_ptr, old_size, new_size);
    }

    ctx->call_patches[ctx->call_patch_count].offset = offset;
    ctx->call_patches[ctx->call_patch_count].target = target;
    ctx->call_patch_count++;
}

// Structure for register pairs
typedef struct {
    arm64_register_t low;
    arm64_register_t high;  // ARM64_NO_REG if not a pair
    bool is_pair;
    bool is_spilled;
    int spill_offset;  // Byte offset from FP if spilled
} arm64_reg_pair_t;

// Get register pair for a value (handles both single registers and pairs)
static arm64_reg_pair_t get_register_pair_arm64(codegen_arm64_context_t* ctx, ir_value_t value) {
    arm64_reg_pair_t result = {ARM64_NO_REG, ARM64_NO_REG, false, false, -1};

    if (value.type == IR_VAL_REGISTER) {
        arm64_register_t low_reg = regalloc_arm64_get_register(ctx->regalloc, value.as.reg);

        if (low_reg != ARM64_NO_REG) {
            // Value is in registers
            result.low = low_reg;
            result.is_spilled = false;

            // Check if this is a 16-byte pair by checking the allocator
            if (value.size == IR_SIZE_16BYTE) {
                // Query the allocator for the actual high register
                arm64_register_t high_reg = regalloc_arm64_get_high_register(ctx->regalloc, value.as.reg);
                if (high_reg != ARM64_NO_REG) {
                    result.high = high_reg;
                    result.is_pair = true;
                }
            }
            return result;
        } else {
            // Value is spilled
            int spill_slot = regalloc_arm64_get_spill_slot(ctx->regalloc, value.as.reg);
            if (spill_slot >= 0) {
                result.is_spilled = true;
                result.spill_offset = spill_slot;

                // Determine if it's a pair based on value size
                if (value.size == IR_SIZE_16BYTE) {
                    result.is_pair = true;
                }
                return result;
            }
        }
    }

    return result;
}

// Load a value from stack using LDP (pair) or LDR (single)
static void load_value_from_spill(codegen_arm64_context_t* ctx, int vreg, int spill_offset, ir_value_size_t size, arm64_register_t dest_low, arm64_register_t dest_high) {
    if (size == IR_SIZE_16BYTE) {
        // Load 16-byte pair using LDP
        // Stack offset: FP - ((spill_offset + 2) * 8) for first register
        int offset = -((spill_offset + 2) * 8);
        arm64_ldp(ctx->asm_, dest_low, dest_high, ARM64_FP, offset);
    } else {
        // Load 8-byte single using LDR
        int offset = -((spill_offset + 1) * 8);
        arm64_ldr_reg_reg_offset(ctx->asm_, dest_low, ARM64_FP, offset);
    }
}

// Store a value to stack using STP (pair) or STR (single)
static void store_value_to_spill(codegen_arm64_context_t* ctx, int vreg, int spill_offset, ir_value_size_t size, arm64_register_t src_low, arm64_register_t src_high) {
    if (size == IR_SIZE_16BYTE) {
        // Store 16-byte pair using STP
        int offset = -((spill_offset + 2) * 8);
        arm64_stp(ctx->asm_, src_low, src_high, ARM64_FP, offset);
    } else {
        // Store 8-byte single using STR
        int offset = -((spill_offset + 1) * 8);
        arm64_str_reg_reg_offset(ctx->asm_, src_low, ARM64_FP, offset);
    }
}

// Map virtual register to ARM64 physical register
static arm64_register_t get_physical_register_arm64(codegen_arm64_context_t* ctx, ir_value_t value) {
    if (value.type == IR_VAL_REGISTER) {
        arm64_register_t arm64_reg = regalloc_arm64_get_register(ctx->regalloc, value.as.reg);

        // Check if register was allocated
        if (arm64_reg == ARM64_NO_REG) {
            // Handle spilled registers
            int spill_slot = regalloc_arm64_get_spill_slot(ctx->regalloc, value.as.reg);
            if (spill_slot >= 0) {
                // Load from stack into temporary register X9
                arm64_ldr_reg_reg_offset(ctx->asm_, ARM64_X9, ARM64_FP, -((spill_slot + 1) * 8));
                return ARM64_X9;
            }
            return ARM64_NO_REG;
        }

        return arm64_reg;
    }
    return ARM64_NO_REG;
}

// Helper function to compare two value_t constants (for global variable lookup)
static bool values_equal(value_t a, value_t b) {
    if (a.type != b.type) return false;
    if (a.type == VAL_NUMBER) {
        return a.as.number == b.as.number;
    } else if (a.type == VAL_OBJ) {
        // For object types (like strings), compare pointers
        return a.as.obj == b.as.obj;
    } else if (a.type == VAL_BOOL) {
        return a.as.boolean == b.as.boolean;
    } else if (a.type == VAL_NIL) {
        return true;  // All nil values are equal
    }
    return false;
}

// Get or allocate a global variable index
// Returns the stack index for the given global variable name
static int get_or_allocate_global_index(codegen_arm64_context_t* ctx, value_t var_name) {
    // First, check if we already have this global
    for (int i = 0; i < ctx->global_count; i++) {
        if (values_equal(ctx->global_vars[i].name, var_name)) {
            return ctx->global_vars[i].index;
        }
    }

    // Not found, allocate a new index
    if (ctx->global_count >= ctx->global_capacity) {
        int old_capacity = ctx->global_capacity;
        ctx->global_capacity = old_capacity == 0 ? 8 : old_capacity * 2;
        ctx->global_vars = (global_var_entry_t*)l_mem_realloc(
            ctx->global_vars,
            sizeof(global_var_entry_t) * old_capacity,
            sizeof(global_var_entry_t) * ctx->global_capacity
        );
    }

    int new_index = ctx->global_count;
    ctx->global_vars[new_index].name = var_name;
    ctx->global_vars[new_index].index = new_index;
    ctx->global_count++;

    return new_index;
}

// Load a 16-byte composite value into X0:X1 for function argument passing
// ARM64 ABI: 16-byte composite types (like value_t) go in X0:X1
static void load_16byte_argument_x0x1(codegen_arm64_context_t* ctx, ir_value_t value) {
    if (value.type == IR_VAL_REGISTER) {
        // Get the physical register where the value is stored
        arm64_register_t src_reg = regalloc_arm64_get_register(ctx->regalloc, value.as.reg);

        if (src_reg == ARM64_NO_REG) {
            // Value is spilled on stack - load from stack
            int spill_slot = regalloc_arm64_get_spill_slot(ctx->regalloc, value.as.reg);
            if (spill_slot >= 0) {
                // Load first 8 bytes to X0
                int stack_offset = -((spill_slot + 1) * 8);
                arm64_ldr_reg_reg_offset(ctx->asm_, ARM64_X0, ARM64_FP, stack_offset);
                // Load second 8 bytes to X1
                int stack_offset_x1 = stack_offset - 8;
                arm64_ldr_reg_reg_offset(ctx->asm_, ARM64_X1, ARM64_FP, stack_offset_x1);
                return;
            }
        }

        // Value is in a register pair - move both halves to X0:X1
        // For 16-byte values, we expect both low and high registers
        arm64_register_t high_reg = regalloc_arm64_get_high_register(ctx->regalloc, value.as.reg);

        // Move low register to X0
        if (src_reg != ARM64_X0) {
            arm64_mov_reg_reg(ctx->asm_, ARM64_X0, src_reg);
        }

        // Move high register to X1
        if (high_reg != ARM64_NO_REG) {
            // Use the actual high register from allocator
            if (high_reg != ARM64_X1) {
                arm64_mov_reg_reg(ctx->asm_, ARM64_X1, high_reg);
            }
        }
    }
}

static void move_value_to_reg(codegen_arm64_context_t* ctx, ir_value_t value,
                              arm64_register_t dest) {
    if (value.type == IR_VAL_REGISTER) {
        arm64_register_t src = get_physical_register_arm64(ctx, value);
        if (src != ARM64_NO_REG) {
            if (src != dest) {
                arm64_mov_reg_reg(ctx->asm_, dest, src);
            }
            return;
        }

        int spill_slot = regalloc_arm64_get_spill_slot(ctx->regalloc, value.as.reg);
        if (spill_slot >= 0) {
            load_value_from_spill(ctx, value.as.reg, spill_slot, IR_SIZE_8BYTE, dest, ARM64_NO_REG);
            return;
        }
    } else if (value.type == IR_VAL_CONSTANT) {
        value_t constant = value.as.constant;
        if (IS_NUMBER(constant)) {
            arm64_mov_reg_imm(ctx->asm_, dest, (uint64_t)AS_NUMBER(constant));
            return;
        }
        if (IS_BOOL(constant)) {
            arm64_mov_reg_imm(ctx->asm_, dest, AS_BOOL(constant) ? 1 : 0);
            return;
        }
        if (IS_NIL(constant)) {
            arm64_mov_reg_imm(ctx->asm_, dest, 0);
            return;
        }
    }
}

static void move_value_to_pair(codegen_arm64_context_t* ctx, ir_value_t value,
                               arm64_register_t dest_low, arm64_register_t dest_high) {
    if (value.type != IR_VAL_REGISTER) {
        return;
    }

    arm64_reg_pair_t src_pair = get_register_pair_arm64(ctx, value);
    if (src_pair.is_spilled) {
        load_value_from_spill(ctx, value.as.reg, src_pair.spill_offset, IR_SIZE_16BYTE,
                              dest_low, dest_high);
        return;
    }

    if (src_pair.low != ARM64_NO_REG && src_pair.low != dest_low) {
        arm64_mov_reg_reg(ctx->asm_, dest_low, src_pair.low);
    }
    if (src_pair.is_pair && src_pair.high != ARM64_NO_REG && src_pair.high != dest_high) {
        arm64_mov_reg_reg(ctx->asm_, dest_high, src_pair.high);
    }
}

static void store_argument_on_stack_arm64(codegen_arm64_context_t* ctx, ir_value_t value, int offset) {
    if (value.type == IR_VAL_REGISTER) {
        arm64_reg_pair_t pair = get_register_pair_arm64(ctx, value);
        if (pair.is_spilled) {
            load_value_from_spill(ctx, value.as.reg, pair.spill_offset, IR_SIZE_16BYTE,
                                  ARM64_X16, ARM64_X17);
            arm64_stp(ctx->asm_, ARM64_X16, ARM64_X17, ARM64_SP, offset);
        } else if (pair.low != ARM64_NO_REG && pair.is_pair) {
            arm64_stp(ctx->asm_, pair.low, pair.high, ARM64_SP, offset);
        }
        return;
    }

    if (value.type == IR_VAL_CONSTANT) {
        value_t constant = value.as.constant;
        uint64_t low_val = *(uint64_t*)(&constant);
        uint64_t high_val = *((uint64_t*)(&constant) + 1);
        arm64_mov_reg_imm(ctx->asm_, ARM64_X16, low_val);
        arm64_mov_reg_imm(ctx->asm_, ARM64_X17, high_val);
        arm64_stp(ctx->asm_, ARM64_X16, ARM64_X17, ARM64_SP, offset);
    }
}

static int marshal_arguments_arm64(codegen_arm64_context_t* ctx, ir_value_t* args, int arg_count) {
    const arm64_register_t arg_regs[] = {
        ARM64_X0, ARM64_X1, ARM64_X2, ARM64_X3,
        ARM64_X4, ARM64_X5, ARM64_X6, ARM64_X7
    };

    int reg_arg_count = (arg_count > 4) ? 4 : arg_count;
    int stack_arg_count = (arg_count > 4) ? (arg_count - 4) : 0;
    int stack_arg_bytes = stack_arg_count * 16;
    int temp_bytes = reg_arg_count * 16;
    int stack_bytes = stack_arg_bytes + temp_bytes;

    if (stack_bytes % 16 != 0) {
        stack_bytes += 16 - (stack_bytes % 16);
    }

    if (stack_bytes > 0) {
        if (stack_bytes < 4096) {
            arm64_sub_reg_reg_imm(ctx->asm_, ARM64_SP, ARM64_SP, (uint16_t)stack_bytes);
        } else {
            arm64_mov_reg_imm(ctx->asm_, ARM64_X9, (uint64_t)stack_bytes);
            arm64_sub_reg_reg_reg(ctx->asm_, ARM64_SP, ARM64_SP, ARM64_X9);
        }
    }

    for (int i = 0; i < reg_arg_count; i++) {
        int offset = stack_arg_bytes + (i * 16);
        store_argument_on_stack_arm64(ctx, args[i], offset);
    }

    for (int i = 0; i < stack_arg_count; i++) {
        int arg_index = i + 4;
        int offset = i * 16;
        store_argument_on_stack_arm64(ctx, args[arg_index], offset);
    }

    for (int i = 0; i < reg_arg_count; i++) {
        int offset = stack_arg_bytes + (i * 16);
        arm64_ldp(ctx->asm_, arg_regs[i * 2], arg_regs[i * 2 + 1], ARM64_SP, offset);
    }

    return stack_bytes;
}

static void emit_function_prologue_arm64(codegen_arm64_context_t* ctx) {
    // ARM64 function prologue (AArch64 calling convention)
    // stp x29, x30, [sp, #-16]!  ; Save FP and LR
    // mov x29, sp                 ; Set up frame pointer
    // sub sp, sp, #frame_size     ; Allocate stack frame

    // Save FP and LR
    arm64_stp(ctx->asm_, ARM64_FP, ARM64_LR, ARM64_SP, -16);
    arm64_sub_reg_reg_imm(ctx->asm_, ARM64_SP, ARM64_SP, 16);

    // Set up frame pointer
    arm64_mov_reg_reg(ctx->asm_, ARM64_FP, ARM64_SP);

    // Allocate stack frame
    int frame_size = regalloc_arm64_get_frame_size(ctx->regalloc);
    if (frame_size > 0) {
        if (frame_size < 4096) {
            arm64_sub_reg_reg_imm(ctx->asm_, ARM64_SP, ARM64_SP, (uint16_t)frame_size);
        } else {
            // For large frames, use a register
            arm64_mov_reg_imm(ctx->asm_, ARM64_X9, frame_size);
            arm64_sub_reg_reg_reg(ctx->asm_, ARM64_SP, ARM64_SP, ARM64_X9);
        }
    }

    // Save callee-saved registers (X19-X28)
    // Simplified: save a few commonly used ones
    if (frame_size > 64) {
        arm64_stp(ctx->asm_, ARM64_X19, ARM64_X20, ARM64_SP, 0);
        arm64_stp(ctx->asm_, ARM64_X21, ARM64_X22, ARM64_SP, 16);
    }
}

static int get_local_slot_offset_arm64(codegen_arm64_context_t* ctx, int local_slot) {
    int spill_offset = regalloc_arm64_get_spill_byte_offset(ctx->regalloc);
    return -(spill_offset + (local_slot + 1) * 16);
}

static void spill_incoming_args_arm64(codegen_arm64_context_t* ctx) {
    if (!ctx->current_function || ctx->current_function->arity <= 0) {
        return;
    }

    const arm64_register_t arg_regs[] = {
        ARM64_X0, ARM64_X1, ARM64_X2, ARM64_X3,
        ARM64_X4, ARM64_X5, ARM64_X6, ARM64_X7
    };

    int arity = ctx->current_function->arity;
    for (int i = 0; i < arity; i++) {
        int local_slot = i + 1;
        int local_offset = get_local_slot_offset_arm64(ctx, local_slot);

        if (i < 4) {
            arm64_register_t low = arg_regs[i * 2];
            arm64_register_t high = arg_regs[i * 2 + 1];
            arm64_stp(ctx->asm_, low, high, ARM64_FP, local_offset);
        } else {
            int stack_arg_index = i - 4;
            int stack_offset = 16 + (stack_arg_index * 16);
            arm64_ldp(ctx->asm_, ARM64_X9, ARM64_X10, ARM64_FP, stack_offset);
            arm64_stp(ctx->asm_, ARM64_X9, ARM64_X10, ARM64_FP, local_offset);
        }
    }
}

static void emit_function_epilogue_arm64(codegen_arm64_context_t* ctx) {
    // ARM64 function epilogue
    int frame_size = regalloc_arm64_get_frame_size(ctx->regalloc);

    // Restore callee-saved registers
    if (frame_size > 64) {
        arm64_ldp(ctx->asm_, ARM64_X21, ARM64_X22, ARM64_SP, 16);
        arm64_ldp(ctx->asm_, ARM64_X19, ARM64_X20, ARM64_SP, 0);
    }

    // For entrypoint script, return 0 so the process exits cleanly.
    if (ctx->module && ctx->module->function_count > 0 &&
        ctx->current_function == ctx->module->functions[0]) {
        arm64_mov_reg_imm(ctx->asm_, ARM64_X0, 0);
    }

    // Deallocate stack frame
    arm64_mov_reg_reg(ctx->asm_, ARM64_SP, ARM64_FP);

    // Restore FP and LR
    arm64_ldp(ctx->asm_, ARM64_FP, ARM64_LR, ARM64_SP, 0);
    arm64_add_reg_reg_imm(ctx->asm_, ARM64_SP, ARM64_SP, 16);

    // Return
    arm64_ret(ctx->asm_, ARM64_LR);
}

static void emit_instruction_arm64(codegen_arm64_context_t* ctx, ir_instruction_t* instr) {
    switch (instr->op) {
        case IR_CONST_INT:
        case IR_CONST_FLOAT: {
            if (instr->dest.type == IR_VAL_REGISTER) {
                if (instr->dest.size == IR_SIZE_16BYTE) {
                    // Load 16-byte value_t composite type into register pair
                    // ARM64 ABI: 16-byte values go in register pairs (low:high)
                    // value_t layout: bytes [0-3]=type, [4-7]=padding, [8-15]=union as (double)
                    // Register pair layout: low register = bytes [0-7], high register = bytes [8-15]
                    arm64_reg_pair_t pair = get_register_pair_arm64(ctx, instr->dest);
                    if (pair.low != ARM64_NO_REG && pair.is_pair) {
                        value_t v = instr->operand1.as.constant;

                        // Load low register (first 8 bytes: type + padding)
                        uint64_t low_val = *(uint64_t*)(&v);
                        arm64_mov_reg_imm(ctx->asm_, pair.low, low_val);

                        // Load high register (remaining 8 bytes: the double value)
                        uint64_t high_val = *((uint64_t*)(&v) + 1);
                        arm64_mov_reg_imm(ctx->asm_, pair.high, high_val);
                    } else if (pair.is_spilled) {
                        value_t v = instr->operand1.as.constant;
                        uint64_t low_val = *(uint64_t*)(&v);
                        uint64_t high_val = *((uint64_t*)(&v) + 1);
                        arm64_mov_reg_imm(ctx->asm_, ARM64_X16, low_val);
                        arm64_mov_reg_imm(ctx->asm_, ARM64_X17, high_val);
                        store_value_to_spill(ctx, instr->dest.as.reg, pair.spill_offset,
                                             IR_SIZE_16BYTE, ARM64_X16, ARM64_X17);
                    }
                } else {
                    // Load 8-byte scalar value
                    arm64_register_t dest = get_physical_register_arm64(ctx, instr->dest);
                    if (dest != ARM64_NO_REG) {
                        if (IS_NUMBER(instr->operand1.as.constant)) {
                            int64_t val = (int64_t)AS_NUMBER(instr->operand1.as.constant);
                            arm64_mov_reg_imm(ctx->asm_, dest, (uint64_t)val);
                        }
                    }
                }
            }
            break;
        }

        case IR_CONST_BOOL: {
            if (instr->dest.type == IR_VAL_REGISTER) {
                if (instr->dest.size == IR_SIZE_16BYTE) {
                    // Load 16-byte value_t for boolean into register pair
                    arm64_reg_pair_t pair = get_register_pair_arm64(ctx, instr->dest);
                    if (pair.low != ARM64_NO_REG && pair.is_pair) {
                        value_t v = instr->operand1.as.constant;

                        // Load low register (first 8 bytes: type + padding)
                        uint64_t low_val = *(uint64_t*)(&v);
                        arm64_mov_reg_imm(ctx->asm_, pair.low, low_val);

                        // Load high register (remaining 8 bytes: the boolean value)
                        uint64_t high_val = *((uint64_t*)(&v) + 1);
                        arm64_mov_reg_imm(ctx->asm_, pair.high, high_val);
                    } else if (pair.is_spilled) {
                        value_t v = instr->operand1.as.constant;
                        uint64_t low_val = *(uint64_t*)(&v);
                        uint64_t high_val = *((uint64_t*)(&v) + 1);
                        arm64_mov_reg_imm(ctx->asm_, ARM64_X16, low_val);
                        arm64_mov_reg_imm(ctx->asm_, ARM64_X17, high_val);
                        store_value_to_spill(ctx, instr->dest.as.reg, pair.spill_offset,
                                             IR_SIZE_16BYTE, ARM64_X16, ARM64_X17);
                    }
                } else {
                    arm64_register_t dest = get_physical_register_arm64(ctx, instr->dest);
                    if (dest != ARM64_NO_REG) {
                        uint64_t val = AS_BOOL(instr->operand1.as.constant) ? 1 : 0;
                        arm64_mov_reg_imm(ctx->asm_, dest, val);
                    }
                }
            }
            break;
        }

        case IR_CONST_NIL: {
            if (instr->dest.type == IR_VAL_REGISTER) {
                if (instr->dest.size == IR_SIZE_16BYTE) {
                    // Load 16-byte value_t for nil into register pair
                    arm64_reg_pair_t pair = get_register_pair_arm64(ctx, instr->dest);
                    if (pair.low != ARM64_NO_REG && pair.is_pair) {
                        // NIL_VAL: type=VAL_NIL (1), value doesn't matter
                        value_t v = NIL_VAL;

                        // Load low register (first 8 bytes: type + padding)
                        uint64_t low_val = *(uint64_t*)(&v);
                        arm64_mov_reg_imm(ctx->asm_, pair.low, low_val);

                        // Load high register (remaining 8 bytes: unused for nil)
                        uint64_t high_val = *((uint64_t*)(&v) + 1);
                        arm64_mov_reg_imm(ctx->asm_, pair.high, high_val);
                    } else if (pair.is_spilled) {
                        value_t v = NIL_VAL;
                        uint64_t low_val = *(uint64_t*)(&v);
                        uint64_t high_val = *((uint64_t*)(&v) + 1);
                        arm64_mov_reg_imm(ctx->asm_, ARM64_X16, low_val);
                        arm64_mov_reg_imm(ctx->asm_, ARM64_X17, high_val);
                        store_value_to_spill(ctx, instr->dest.as.reg, pair.spill_offset,
                                             IR_SIZE_16BYTE, ARM64_X16, ARM64_X17);
                    }
                } else {
                    arm64_register_t dest = get_physical_register_arm64(ctx, instr->dest);
                    if (dest != ARM64_NO_REG) {
                        arm64_eor_reg_reg_reg(ctx->asm_, dest, dest, dest); // XOR = 0
                    }
                }
            }
            break;
        }

        case IR_CONST_STRING: {
            // String constant: allocate string at runtime
            // Steps:
            // 1. Add string literal to tracking (for later embedding in __cstring section)
            // 2. Load string address using ADRP + ADD with page-relative relocations
            // 3. Call sox_native_alloc_string(const char* chars, size_t length)
            // 4. Result comes back in X0:X1 as value_t
            if (instr->dest.type == IR_VAL_REGISTER && instr->dest.size == IR_SIZE_16BYTE) {
                arm64_reg_pair_t dest_pair = get_register_pair_arm64(ctx, instr->dest);

                // Register the string literal and get its symbol
                int str_index = codegen_arm64_add_string_literal(ctx, instr->string_data, instr->string_length);
                if (str_index < 0) {
                    fprintf(stderr, "Error: Failed to add string literal\n");
                    break;
                }
                const char* str_symbol = ctx->string_literals[str_index].symbol;

                // Load address of string literal into X15 using ADRP + ADD
                // ADRP X15, <page of string>
                size_t adrp_offset = arm64_get_offset(ctx->asm_);
                arm64_adrp(ctx->asm_, ARM64_X15, 0);
                arm64_add_relocation(ctx->asm_, adrp_offset, ARM64_RELOC_ADR_PREL_PG_HI21, str_symbol, 0);

                // ADD X15, X15, <offset within page>
                size_t add_offset = arm64_get_offset(ctx->asm_);
                arm64_add_reg_reg_imm(ctx->asm_, ARM64_X15, ARM64_X15, 0);
                arm64_add_relocation(ctx->asm_, add_offset, ARM64_RELOC_ADD_ABS_LO12_NC, str_symbol, 0);

                // Set up arguments for sox_native_alloc_string(const char* chars, size_t length)
                // X0 = chars (address in X15)
                // X1 = length
                arm64_mov_reg_reg(ctx->asm_, ARM64_X0, ARM64_X15);
                arm64_mov_reg_imm(ctx->asm_, ARM64_X1, instr->string_length);

                // Call sox_native_alloc_string
                size_t call_offset = arm64_get_offset(ctx->asm_);
                arm64_bl(ctx->asm_, 0);
                arm64_add_relocation(ctx->asm_, call_offset, ARM64_RELOC_CALL26, "sox_native_alloc_string", 0);

                // Result is in X0:X1 (value_t) - move to destination
                if (dest_pair.is_spilled) {
                    // Store to stack
                    arm64_stp(ctx->asm_, ARM64_X0, ARM64_X1, ARM64_FP, dest_pair.spill_offset);
                } else if (dest_pair.low != ARM64_NO_REG && dest_pair.is_pair) {
                    // Move to register pair
                    arm64_mov_reg_reg(ctx->asm_, dest_pair.low, ARM64_X0);
                    arm64_mov_reg_reg(ctx->asm_, dest_pair.high, ARM64_X1);
                }
            }
            break;
        }

        case IR_LOAD_LOCAL: {
            // Load a local variable from stack
            // operand1 contains the local variable index (as constant)
            // dest is where the loaded value should go
            if (instr->dest.type == IR_VAL_REGISTER && instr->operand1.type == IR_VAL_CONSTANT) {
                int local_slot = (int)AS_NUMBER(instr->operand1.as.constant);

                // Calculate offset from FP: -(spill_offset + (slot + 1) * 8)
                int spill_offset = regalloc_arm64_get_spill_byte_offset(ctx->regalloc);
                int stack_offset = -(spill_offset + (local_slot + 1) * 16);

                if (instr->dest.size == IR_SIZE_16BYTE) {
                    // Load 16-byte value into register pair
                    arm64_reg_pair_t dest_pair = get_register_pair_arm64(ctx, instr->dest);

                    if (dest_pair.low != ARM64_NO_REG && dest_pair.is_pair) {
                        // Load 16-byte value using LDP
                        arm64_ldp(ctx->asm_, dest_pair.low, dest_pair.high, ARM64_FP, stack_offset);
                    } else if (dest_pair.is_spilled) {
                        // Load to temporary and then spill
                        arm64_ldp(ctx->asm_, ARM64_X9, ARM64_X10, ARM64_FP, stack_offset);
                        store_value_to_spill(ctx, instr->dest.as.reg, dest_pair.spill_offset,
                                            IR_SIZE_16BYTE, ARM64_X9, ARM64_X10);
                    }
                } else {
                    // Load 8-byte scalar value
                    arm64_register_t dest = get_physical_register_arm64(ctx, instr->dest);
                    if (dest != ARM64_NO_REG) {
                        arm64_ldr_reg_reg_offset(ctx->asm_, dest, ARM64_FP, stack_offset);
                    }
                }
            }
            break;
        }

        case IR_STORE_LOCAL: {
            // Store a local variable to stack
            // dest contains the local variable index (as constant)
            // operand1 is the value to store
            if (instr->dest.type == IR_VAL_CONSTANT && instr->operand1.type == IR_VAL_REGISTER) {
                int local_slot = (int)AS_NUMBER(instr->dest.as.constant);

                // Calculate offset from FP: -(spill_offset + (slot + 1) * 8)
                int spill_offset = regalloc_arm64_get_spill_byte_offset(ctx->regalloc);
                int stack_offset = -(spill_offset + (local_slot + 1) * 16);

                if (instr->operand1.size == IR_SIZE_16BYTE) {
                    // Store 16-byte value from register pair
                    arm64_reg_pair_t src_pair = get_register_pair_arm64(ctx, instr->operand1);

                    if (src_pair.low != ARM64_NO_REG && src_pair.is_pair) {
                        // Store 16-byte value using STP
                        arm64_stp(ctx->asm_, src_pair.low, src_pair.high, ARM64_FP, stack_offset);
                    } else if (src_pair.is_spilled) {
                        // Load from spill and store to local
                        load_value_from_spill(ctx, instr->operand1.as.reg, src_pair.spill_offset,
                                             IR_SIZE_16BYTE, ARM64_X9, ARM64_X10);
                        arm64_stp(ctx->asm_, ARM64_X9, ARM64_X10, ARM64_FP, stack_offset);
                    }
                } else {
                    // Store 8-byte scalar value
                    arm64_register_t src = get_physical_register_arm64(ctx, instr->operand1);
                    if (src != ARM64_NO_REG) {
                        arm64_str_reg_reg_offset(ctx->asm_, src, ARM64_FP, stack_offset);
                    }
                }
            }
            break;
        }

        case IR_LOAD_GLOBAL: {
            // Load a global variable from stack (stored like locals for native codegen)
            // operand1 contains the variable name (as constant)
            // For single-function native compilation, allocate globals on stack
            if (instr->dest.type == IR_VAL_REGISTER && instr->operand1.type == IR_VAL_CONSTANT) {
                value_t var_name_val = instr->operand1.as.constant;

                // Get the global variable index (lookup or allocate)
                int global_index = get_or_allocate_global_index(ctx, var_name_val);
                int spill_offset = regalloc_arm64_get_spill_byte_offset(ctx->regalloc);
                int stack_offset = -(spill_offset +
                                   (ctx->current_function->local_count + 1) * 16 + global_index * 16);

                if (instr->dest.size == IR_SIZE_16BYTE) {
                    arm64_reg_pair_t dest_pair = get_register_pair_arm64(ctx, instr->dest);
                    if (dest_pair.low != ARM64_NO_REG && dest_pair.is_pair) {
                        arm64_ldp(ctx->asm_, dest_pair.low, dest_pair.high, ARM64_FP, stack_offset);
                    }
                }
            }
            break;
        }

        case IR_STORE_GLOBAL: {
            // Store a global variable to stack (stored like locals for native codegen)
            // dest contains the variable name (as constant)
            // operand1 contains the value to store
            if (instr->dest.type == IR_VAL_CONSTANT && instr->operand1.type == IR_VAL_REGISTER) {
                value_t var_name_val = instr->dest.as.constant;

                // Get the global variable index (lookup or allocate)
                int global_index = get_or_allocate_global_index(ctx, var_name_val);
                int spill_offset = regalloc_arm64_get_spill_byte_offset(ctx->regalloc);
                int stack_offset = -(spill_offset +
                                   (ctx->current_function->local_count + 1) * 16 + global_index * 16);

                if (instr->operand1.size == IR_SIZE_16BYTE) {
                    arm64_reg_pair_t src_pair = get_register_pair_arm64(ctx, instr->operand1);
                    if (src_pair.low != ARM64_NO_REG && src_pair.is_pair) {
                        arm64_stp(ctx->asm_, src_pair.low, src_pair.high, ARM64_FP, stack_offset);
                    }
                }
            }
            break;
        }

        case IR_ADD: {
            if (instr->dest.size == IR_SIZE_16BYTE) {
                // 16-byte composite type: marshal to X0:X1 and X2:X3, call sox_add
                arm64_reg_pair_t op1_pair = get_register_pair_arm64(ctx, instr->operand1);
                arm64_reg_pair_t op2_pair = get_register_pair_arm64(ctx, instr->operand2);
                arm64_reg_pair_t dest_pair = get_register_pair_arm64(ctx, instr->dest);

                // Load operand1 into X0:X1
                if (op1_pair.is_spilled) {
                    load_value_from_spill(ctx, instr->operand1.as.reg, op1_pair.spill_offset,
                                         IR_SIZE_16BYTE, ARM64_X0, ARM64_X1);
                } else if (op1_pair.low != ARM64_NO_REG && op1_pair.is_pair) {
                    if (op1_pair.low != ARM64_X0) {
                        arm64_mov_reg_reg(ctx->asm_, ARM64_X0, op1_pair.low);
                    }
                    if (op1_pair.high != ARM64_X1) {
                        arm64_mov_reg_reg(ctx->asm_, ARM64_X1, op1_pair.high);
                    }
                }

                // Load operand2 into X2:X3
                if (op2_pair.is_spilled) {
                    load_value_from_spill(ctx, instr->operand2.as.reg, op2_pair.spill_offset,
                                         IR_SIZE_16BYTE, ARM64_X2, ARM64_X3);
                } else if (op2_pair.low != ARM64_NO_REG && op2_pair.is_pair) {
                    if (op2_pair.low != ARM64_X2) {
                        arm64_mov_reg_reg(ctx->asm_, ARM64_X2, op2_pair.low);
                    }
                    if (op2_pair.high != ARM64_X3) {
                        arm64_mov_reg_reg(ctx->asm_, ARM64_X3, op2_pair.high);
                    }
                }

                // Call sox_add - result comes back in X0:X1
                size_t call_offset = arm64_get_offset(ctx->asm_);
                arm64_bl(ctx->asm_, 0);
                arm64_add_relocation(ctx->asm_, call_offset, ARM64_RELOC_CALL26, "sox_add", 0);

                // Copy result from X0:X1 to dest pair
                if (dest_pair.is_spilled) {
                    store_value_to_spill(ctx, instr->dest.as.reg, dest_pair.spill_offset,
                                        IR_SIZE_16BYTE, ARM64_X0, ARM64_X1);
                } else if (dest_pair.low != ARM64_NO_REG && dest_pair.is_pair) {
                    if (dest_pair.low != ARM64_X0) {
                        arm64_mov_reg_reg(ctx->asm_, dest_pair.low, ARM64_X0);
                    }
                    if (dest_pair.high != ARM64_X1) {
                        arm64_mov_reg_reg(ctx->asm_, dest_pair.high, ARM64_X1);
                    }
                }
            } else {
                // 8-byte scalar: existing single-register logic
                arm64_register_t left = get_physical_register_arm64(ctx, instr->operand1);
                arm64_register_t right = get_physical_register_arm64(ctx, instr->operand2);
                arm64_register_t dest = get_physical_register_arm64(ctx, instr->dest);

                if (dest != ARM64_NO_REG && left != ARM64_NO_REG && right != ARM64_NO_REG) {
                    arm64_add_reg_reg_reg(ctx->asm_, dest, left, right);
                }
            }
            break;
        }

        case IR_SUB: {
            if (instr->dest.size == IR_SIZE_16BYTE) {
                // 16-byte composite type: marshal to X0:X1 and X2:X3, call sox_sub
                arm64_reg_pair_t op1_pair = get_register_pair_arm64(ctx, instr->operand1);
                arm64_reg_pair_t op2_pair = get_register_pair_arm64(ctx, instr->operand2);
                arm64_reg_pair_t dest_pair = get_register_pair_arm64(ctx, instr->dest);

                // Load operand1 into X0:X1
                if (op1_pair.is_spilled) {
                    load_value_from_spill(ctx, instr->operand1.as.reg, op1_pair.spill_offset,
                                         IR_SIZE_16BYTE, ARM64_X0, ARM64_X1);
                } else if (op1_pair.low != ARM64_NO_REG && op1_pair.is_pair) {
                    if (op1_pair.low != ARM64_X0) {
                        arm64_mov_reg_reg(ctx->asm_, ARM64_X0, op1_pair.low);
                    }
                    if (op1_pair.high != ARM64_X1) {
                        arm64_mov_reg_reg(ctx->asm_, ARM64_X1, op1_pair.high);
                    }
                }

                // Load operand2 into X2:X3
                if (op2_pair.is_spilled) {
                    load_value_from_spill(ctx, instr->operand2.as.reg, op2_pair.spill_offset,
                                         IR_SIZE_16BYTE, ARM64_X2, ARM64_X3);
                } else if (op2_pair.low != ARM64_NO_REG && op2_pair.is_pair) {
                    if (op2_pair.low != ARM64_X2) {
                        arm64_mov_reg_reg(ctx->asm_, ARM64_X2, op2_pair.low);
                    }
                    if (op2_pair.high != ARM64_X3) {
                        arm64_mov_reg_reg(ctx->asm_, ARM64_X3, op2_pair.high);
                    }
                }

                // Call sox_sub - result comes back in X0:X1
                size_t call_offset = arm64_get_offset(ctx->asm_);
                arm64_bl(ctx->asm_, 0);
                arm64_add_relocation(ctx->asm_, call_offset, ARM64_RELOC_CALL26, "sox_sub", 0);

                // Copy result from X0:X1 to dest pair
                if (dest_pair.is_spilled) {
                    store_value_to_spill(ctx, instr->dest.as.reg, dest_pair.spill_offset,
                                        IR_SIZE_16BYTE, ARM64_X0, ARM64_X1);
                } else if (dest_pair.low != ARM64_NO_REG && dest_pair.is_pair) {
                    if (dest_pair.low != ARM64_X0) {
                        arm64_mov_reg_reg(ctx->asm_, dest_pair.low, ARM64_X0);
                    }
                    if (dest_pair.high != ARM64_X1) {
                        arm64_mov_reg_reg(ctx->asm_, dest_pair.high, ARM64_X1);
                    }
                }
            } else {
                // 8-byte scalar: existing single-register logic
                arm64_register_t left = get_physical_register_arm64(ctx, instr->operand1);
                arm64_register_t right = get_physical_register_arm64(ctx, instr->operand2);
                arm64_register_t dest = get_physical_register_arm64(ctx, instr->dest);

                if (dest != ARM64_NO_REG && left != ARM64_NO_REG && right != ARM64_NO_REG) {
                    arm64_sub_reg_reg_reg(ctx->asm_, dest, left, right);
                }
            }
            break;
        }

        case IR_MUL: {
            if (instr->dest.size == IR_SIZE_16BYTE) {
                // 16-byte composite type: marshal to X0:X1 and X2:X3, call sox_mul
                arm64_reg_pair_t op1_pair = get_register_pair_arm64(ctx, instr->operand1);
                arm64_reg_pair_t op2_pair = get_register_pair_arm64(ctx, instr->operand2);
                arm64_reg_pair_t dest_pair = get_register_pair_arm64(ctx, instr->dest);

                // Load operand1 into X0:X1
                if (op1_pair.is_spilled) {
                    load_value_from_spill(ctx, instr->operand1.as.reg, op1_pair.spill_offset,
                                         IR_SIZE_16BYTE, ARM64_X0, ARM64_X1);
                } else if (op1_pair.low != ARM64_NO_REG && op1_pair.is_pair) {
                    if (op1_pair.low != ARM64_X0) {
                        arm64_mov_reg_reg(ctx->asm_, ARM64_X0, op1_pair.low);
                    }
                    if (op1_pair.high != ARM64_X1) {
                        arm64_mov_reg_reg(ctx->asm_, ARM64_X1, op1_pair.high);
                    }
                }

                // Load operand2 into X2:X3
                if (op2_pair.is_spilled) {
                    load_value_from_spill(ctx, instr->operand2.as.reg, op2_pair.spill_offset,
                                         IR_SIZE_16BYTE, ARM64_X2, ARM64_X3);
                } else if (op2_pair.low != ARM64_NO_REG && op2_pair.is_pair) {
                    if (op2_pair.low != ARM64_X2) {
                        arm64_mov_reg_reg(ctx->asm_, ARM64_X2, op2_pair.low);
                    }
                    if (op2_pair.high != ARM64_X3) {
                        arm64_mov_reg_reg(ctx->asm_, ARM64_X3, op2_pair.high);
                    }
                }

                // Call sox_mul - result comes back in X0:X1
                size_t call_offset = arm64_get_offset(ctx->asm_);
                arm64_bl(ctx->asm_, 0);
                arm64_add_relocation(ctx->asm_, call_offset, ARM64_RELOC_CALL26, "sox_mul", 0);

                // Copy result from X0:X1 to dest pair
                if (dest_pair.is_spilled) {
                    store_value_to_spill(ctx, instr->dest.as.reg, dest_pair.spill_offset,
                                        IR_SIZE_16BYTE, ARM64_X0, ARM64_X1);
                } else if (dest_pair.low != ARM64_NO_REG && dest_pair.is_pair) {
                    if (dest_pair.low != ARM64_X0) {
                        arm64_mov_reg_reg(ctx->asm_, dest_pair.low, ARM64_X0);
                    }
                    if (dest_pair.high != ARM64_X1) {
                        arm64_mov_reg_reg(ctx->asm_, dest_pair.high, ARM64_X1);
                    }
                }
            } else {
                // 8-byte scalar: existing single-register logic
                arm64_register_t left = get_physical_register_arm64(ctx, instr->operand1);
                arm64_register_t right = get_physical_register_arm64(ctx, instr->operand2);
                arm64_register_t dest = get_physical_register_arm64(ctx, instr->dest);

                if (dest != ARM64_NO_REG && left != ARM64_NO_REG && right != ARM64_NO_REG) {
                    arm64_mul_reg_reg_reg(ctx->asm_, dest, left, right);
                }
            }
            break;
        }

        case IR_DIV: {
            if (instr->dest.size == IR_SIZE_16BYTE) {
                // 16-byte composite type: marshal to X0:X1 and X2:X3, call sox_div
                arm64_reg_pair_t op1_pair = get_register_pair_arm64(ctx, instr->operand1);
                arm64_reg_pair_t op2_pair = get_register_pair_arm64(ctx, instr->operand2);
                arm64_reg_pair_t dest_pair = get_register_pair_arm64(ctx, instr->dest);

                // Load operand1 into X0:X1
                if (op1_pair.is_spilled) {
                    load_value_from_spill(ctx, instr->operand1.as.reg, op1_pair.spill_offset,
                                         IR_SIZE_16BYTE, ARM64_X0, ARM64_X1);
                } else if (op1_pair.low != ARM64_NO_REG && op1_pair.is_pair) {
                    if (op1_pair.low != ARM64_X0) {
                        arm64_mov_reg_reg(ctx->asm_, ARM64_X0, op1_pair.low);
                    }
                    if (op1_pair.high != ARM64_X1) {
                        arm64_mov_reg_reg(ctx->asm_, ARM64_X1, op1_pair.high);
                    }
                }

                // Load operand2 into X2:X3
                if (op2_pair.is_spilled) {
                    load_value_from_spill(ctx, instr->operand2.as.reg, op2_pair.spill_offset,
                                         IR_SIZE_16BYTE, ARM64_X2, ARM64_X3);
                } else if (op2_pair.low != ARM64_NO_REG && op2_pair.is_pair) {
                    if (op2_pair.low != ARM64_X2) {
                        arm64_mov_reg_reg(ctx->asm_, ARM64_X2, op2_pair.low);
                    }
                    if (op2_pair.high != ARM64_X3) {
                        arm64_mov_reg_reg(ctx->asm_, ARM64_X3, op2_pair.high);
                    }
                }

                // Call sox_div - result comes back in X0:X1
                size_t call_offset = arm64_get_offset(ctx->asm_);
                arm64_bl(ctx->asm_, 0);
                arm64_add_relocation(ctx->asm_, call_offset, ARM64_RELOC_CALL26, "sox_div", 0);

                // Copy result from X0:X1 to dest pair
                if (dest_pair.is_spilled) {
                    store_value_to_spill(ctx, instr->dest.as.reg, dest_pair.spill_offset,
                                        IR_SIZE_16BYTE, ARM64_X0, ARM64_X1);
                } else if (dest_pair.low != ARM64_NO_REG && dest_pair.is_pair) {
                    if (dest_pair.low != ARM64_X0) {
                        arm64_mov_reg_reg(ctx->asm_, dest_pair.low, ARM64_X0);
                    }
                    if (dest_pair.high != ARM64_X1) {
                        arm64_mov_reg_reg(ctx->asm_, dest_pair.high, ARM64_X1);
                    }
                }
            } else {
                // 8-byte scalar: existing single-register logic
                arm64_register_t left = get_physical_register_arm64(ctx, instr->operand1);
                arm64_register_t right = get_physical_register_arm64(ctx, instr->operand2);
                arm64_register_t dest = get_physical_register_arm64(ctx, instr->dest);

                if (dest != ARM64_NO_REG && left != ARM64_NO_REG && right != ARM64_NO_REG) {
                    arm64_sdiv_reg_reg_reg(ctx->asm_, dest, left, right);
                }
            }
            break;
        }

        case IR_NEG: {
            if (instr->dest.size == IR_SIZE_16BYTE) {
                // 16-byte composite type: marshal to X0:X1, call sox_neg
                arm64_reg_pair_t op1_pair = get_register_pair_arm64(ctx, instr->operand1);
                arm64_reg_pair_t dest_pair = get_register_pair_arm64(ctx, instr->dest);

                // Load operand into X0:X1
                if (op1_pair.is_spilled) {
                    load_value_from_spill(ctx, instr->operand1.as.reg, op1_pair.spill_offset,
                                         IR_SIZE_16BYTE, ARM64_X0, ARM64_X1);
                } else if (op1_pair.low != ARM64_NO_REG && op1_pair.is_pair) {
                    if (op1_pair.low != ARM64_X0) {
                        arm64_mov_reg_reg(ctx->asm_, ARM64_X0, op1_pair.low);
                    }
                    if (op1_pair.high != ARM64_X1) {
                        arm64_mov_reg_reg(ctx->asm_, ARM64_X1, op1_pair.high);
                    }
                }

                // Call sox_neg - result comes back in X0:X1
                size_t call_offset = arm64_get_offset(ctx->asm_);
                arm64_bl(ctx->asm_, 0);
                arm64_add_relocation(ctx->asm_, call_offset, ARM64_RELOC_CALL26, "sox_neg", 0);

                // Copy result from X0:X1 to dest pair
                if (dest_pair.is_spilled) {
                    store_value_to_spill(ctx, instr->dest.as.reg, dest_pair.spill_offset,
                                        IR_SIZE_16BYTE, ARM64_X0, ARM64_X1);
                } else if (dest_pair.low != ARM64_NO_REG && dest_pair.is_pair) {
                    if (dest_pair.low != ARM64_X0) {
                        arm64_mov_reg_reg(ctx->asm_, dest_pair.low, ARM64_X0);
                    }
                    if (dest_pair.high != ARM64_X1) {
                        arm64_mov_reg_reg(ctx->asm_, dest_pair.high, ARM64_X1);
                    }
                }
            } else {
                // 8-byte scalar: existing single-register logic
                arm64_register_t src = get_physical_register_arm64(ctx, instr->operand1);
                arm64_register_t dest = get_physical_register_arm64(ctx, instr->dest);

                if (dest != ARM64_NO_REG && src != ARM64_NO_REG) {
                    arm64_neg_reg_reg(ctx->asm_, dest, src);
                }
            }
            break;
        }

        case IR_EQ:
        case IR_NE:
        case IR_LT:
        case IR_LE:
        case IR_GT:
        case IR_GE: {
            // For comparisons, check operand size to determine if we need runtime call
            if (instr->operand1.size == IR_SIZE_16BYTE) {
                // 16-byte composite type: marshal to X0:X1 and X2:X3, call runtime
                arm64_reg_pair_t op1_pair = get_register_pair_arm64(ctx, instr->operand1);
                arm64_reg_pair_t op2_pair = get_register_pair_arm64(ctx, instr->operand2);
                arm64_reg_pair_t dest_pair = get_register_pair_arm64(ctx, instr->dest);

                // Load operand1 into X0:X1
                if (op1_pair.is_spilled) {
                    load_value_from_spill(ctx, instr->operand1.as.reg, op1_pair.spill_offset,
                                         IR_SIZE_16BYTE, ARM64_X0, ARM64_X1);
                } else if (op1_pair.low != ARM64_NO_REG && op1_pair.is_pair) {
                    if (op1_pair.low != ARM64_X0) {
                        arm64_mov_reg_reg(ctx->asm_, ARM64_X0, op1_pair.low);
                    }
                    if (op1_pair.high != ARM64_X1) {
                        arm64_mov_reg_reg(ctx->asm_, ARM64_X1, op1_pair.high);
                    }
                }

                // Load operand2 into X2:X3
                if (op2_pair.is_spilled) {
                    load_value_from_spill(ctx, instr->operand2.as.reg, op2_pair.spill_offset,
                                         IR_SIZE_16BYTE, ARM64_X2, ARM64_X3);
                } else if (op2_pair.low != ARM64_NO_REG && op2_pair.is_pair) {
                    if (op2_pair.low != ARM64_X2) {
                        arm64_mov_reg_reg(ctx->asm_, ARM64_X2, op2_pair.low);
                    }
                    if (op2_pair.high != ARM64_X3) {
                        arm64_mov_reg_reg(ctx->asm_, ARM64_X3, op2_pair.high);
                    }
                }

                // Call the appropriate runtime function
                const char* func_name;
                switch (instr->op) {
                    case IR_EQ: func_name = "sox_eq"; break;
                    case IR_NE: func_name = "sox_ne"; break;
                    case IR_LT: func_name = "sox_lt"; break;
                    case IR_LE: func_name = "sox_le"; break;
                    case IR_GT: func_name = "sox_gt"; break;
                    case IR_GE: func_name = "sox_ge"; break;
                    default: func_name = "sox_eq";
                }

                size_t call_offset = arm64_get_offset(ctx->asm_);
                arm64_bl(ctx->asm_, 0);
                arm64_add_relocation(ctx->asm_, call_offset, ARM64_RELOC_CALL26, func_name, 0);

                // Copy result from X0:X1 to dest pair
                if (dest_pair.is_spilled) {
                    store_value_to_spill(ctx, instr->dest.as.reg, dest_pair.spill_offset,
                                        IR_SIZE_16BYTE, ARM64_X0, ARM64_X1);
                } else if (dest_pair.low != ARM64_NO_REG && dest_pair.is_pair) {
                    if (dest_pair.low != ARM64_X0) {
                        arm64_mov_reg_reg(ctx->asm_, dest_pair.low, ARM64_X0);
                    }
                    if (dest_pair.high != ARM64_X1) {
                        arm64_mov_reg_reg(ctx->asm_, dest_pair.high, ARM64_X1);
                    }
                }
            } else {
                // 8-byte scalar: existing single-register logic
                arm64_register_t left = get_physical_register_arm64(ctx, instr->operand1);
                arm64_register_t right = get_physical_register_arm64(ctx, instr->operand2);
                arm64_register_t dest = get_physical_register_arm64(ctx, instr->dest);

                if (dest != ARM64_NO_REG && left != ARM64_NO_REG && right != ARM64_NO_REG) {
                    arm64_cmp_reg_reg(ctx->asm_, left, right);

                    arm64_condition_t cond;
                    switch (instr->op) {
                        case IR_EQ: cond = ARM64_CC_EQ; break;
                        case IR_NE: cond = ARM64_CC_NE; break;
                        case IR_LT: cond = ARM64_CC_LT; break;
                        case IR_LE: cond = ARM64_CC_LE; break;
                        case IR_GT: cond = ARM64_CC_GT; break;
                        case IR_GE: cond = ARM64_CC_GE; break;
                        default: cond = ARM64_CC_EQ;
                    }

                    arm64_cset(ctx->asm_, dest, cond);
                }
            }
            break;
        }

        case IR_NOT: {
            if (instr->operand1.size == IR_SIZE_16BYTE) {
                // 16-byte composite type: marshal to X0:X1, call sox_not
                arm64_reg_pair_t op1_pair = get_register_pair_arm64(ctx, instr->operand1);

                // Load operand into X0:X1
                if (op1_pair.is_spilled) {
                    load_value_from_spill(ctx, instr->operand1.as.reg, op1_pair.spill_offset,
                                         IR_SIZE_16BYTE, ARM64_X0, ARM64_X1);
                } else if (op1_pair.low != ARM64_NO_REG && op1_pair.is_pair) {
                    if (op1_pair.low != ARM64_X0) {
                        arm64_mov_reg_reg(ctx->asm_, ARM64_X0, op1_pair.low);
                    }
                    if (op1_pair.high != ARM64_X1) {
                        arm64_mov_reg_reg(ctx->asm_, ARM64_X1, op1_pair.high);
                    }
                }

                // Call sox_not - result comes back in X0:X1
                size_t call_offset = arm64_get_offset(ctx->asm_);
                arm64_bl(ctx->asm_, 0);
                arm64_add_relocation(ctx->asm_, call_offset, ARM64_RELOC_CALL26, "sox_not", 0);

                // Result comes back in X0:X1 as a value_t (boolean)
                // Destination should be 16-byte register pair
                if (instr->dest.size == IR_SIZE_16BYTE) {
                    arm64_reg_pair_t dest_pair = get_register_pair_arm64(ctx, instr->dest);
                    if (dest_pair.low != ARM64_NO_REG && dest_pair.is_pair) {
                        // Copy X0:X1 to destination pair
                        if (dest_pair.low != ARM64_X0) {
                            arm64_mov_reg_reg(ctx->asm_, dest_pair.low, ARM64_X0);
                        }
                        if (dest_pair.high != ARM64_X1) {
                            arm64_mov_reg_reg(ctx->asm_, dest_pair.high, ARM64_X1);
                        }
                    }
                } else {
                    // Fallback for 8-byte destination (legacy)
                    arm64_register_t dest = get_physical_register_arm64(ctx, instr->dest);
                    if (dest != ARM64_NO_REG) {
                        if (dest != ARM64_X0) {
                            arm64_mov_reg_reg(ctx->asm_, dest, ARM64_X0);
                        }
                    }
                }
            } else {
                // 8-byte scalar: existing single-register logic
                arm64_register_t src = get_physical_register_arm64(ctx, instr->operand1);
                arm64_register_t dest = get_physical_register_arm64(ctx, instr->dest);

                if (dest != ARM64_NO_REG && src != ARM64_NO_REG) {
                    arm64_cmp_reg_imm(ctx->asm_, src, 0);
                    arm64_cset(ctx->asm_, dest, ARM64_CC_EQ); // Set if zero
                }
            }
            break;
        }

        case IR_MOVE: {
            if (instr->dest.size == IR_SIZE_16BYTE) {
                // Move 16-byte pair
                arm64_reg_pair_t src_pair = get_register_pair_arm64(ctx, instr->operand1);
                arm64_reg_pair_t dest_pair = get_register_pair_arm64(ctx, instr->dest);

                if (src_pair.is_spilled && dest_pair.low != ARM64_NO_REG) {
                    // Load from spill and copy to dest pair
                    load_value_from_spill(ctx, instr->operand1.as.reg, src_pair.spill_offset,
                                        IR_SIZE_16BYTE, dest_pair.low, dest_pair.high);
                } else if (src_pair.low != ARM64_NO_REG && dest_pair.low != ARM64_NO_REG && src_pair.is_pair && dest_pair.is_pair) {
                    // Move both registers if different
                    if (dest_pair.low != src_pair.low) {
                        arm64_mov_reg_reg(ctx->asm_, dest_pair.low, src_pair.low);
                    }
                    if (dest_pair.high != src_pair.high) {
                        arm64_mov_reg_reg(ctx->asm_, dest_pair.high, src_pair.high);
                    }
                }
            } else {
                // Move 8-byte scalar
                arm64_register_t src = get_physical_register_arm64(ctx, instr->operand1);
                arm64_register_t dest = get_physical_register_arm64(ctx, instr->dest);

                if (dest != ARM64_NO_REG && src != ARM64_NO_REG && dest != src) {
                    arm64_mov_reg_reg(ctx->asm_, dest, src);
                }
            }
            break;
        }

        case IR_JUMP: {
            if (instr->operand1.type == IR_VAL_LABEL) {
                size_t patch_offset = arm64_get_offset(ctx->asm_);
                arm64_b(ctx->asm_, 0); // Will be patched later
                add_jump_patch(ctx, patch_offset, instr->operand1.as.label);
            }
            break;
        }

        case IR_BRANCH: {
            if (instr->operand2.type == IR_VAL_LABEL) {
                arm64_register_t cond = get_physical_register_arm64(ctx, instr->operand1);
                if (cond != ARM64_NO_REG) {
                    arm64_cmp_reg_imm(ctx->asm_, cond, 0);
                    size_t patch_offset = arm64_get_offset(ctx->asm_);
                    arm64_b_cond(ctx->asm_, ARM64_CC_NE, 0); // Jump if not zero
                    add_jump_patch(ctx, patch_offset, instr->operand2.as.label);
                }
            }
            break;
        }

        case IR_CALL: {
            int stack_bytes = 0;
            if (instr->call_args && instr->call_arg_count > 0) {
                stack_bytes = marshal_arguments_arm64(ctx, instr->call_args, instr->call_arg_count);
            }

            size_t call_offset = arm64_get_offset(ctx->asm_);
            arm64_bl(ctx->asm_, 0);

            if (instr->call_function) {
                add_call_patch(ctx, call_offset, instr->call_function);
            } else if (instr->call_target) {
                arm64_add_relocation(ctx->asm_, call_offset, ARM64_RELOC_CALL26, instr->call_target, 0);
            }

            if (stack_bytes > 0) {
                if (stack_bytes < 4096) {
                    arm64_add_reg_reg_imm(ctx->asm_, ARM64_SP, ARM64_SP, (uint16_t)stack_bytes);
                } else {
                    arm64_mov_reg_imm(ctx->asm_, ARM64_X9, (uint64_t)stack_bytes);
                    arm64_add_reg_reg_reg(ctx->asm_, ARM64_SP, ARM64_SP, ARM64_X9);
                }
            }

            if (instr->dest.type == IR_VAL_REGISTER) {
                if (instr->dest.size == IR_SIZE_16BYTE) {
                    arm64_reg_pair_t dest_pair = get_register_pair_arm64(ctx, instr->dest);
                    if (dest_pair.is_spilled) {
                        store_value_to_spill(ctx, instr->dest.as.reg, dest_pair.spill_offset,
                                            IR_SIZE_16BYTE, ARM64_X0, ARM64_X1);
                    } else if (dest_pair.low != ARM64_NO_REG && dest_pair.is_pair) {
                        if (dest_pair.low != ARM64_X0) {
                            arm64_mov_reg_reg(ctx->asm_, dest_pair.low, ARM64_X0);
                        }
                        if (dest_pair.high != ARM64_X1) {
                            arm64_mov_reg_reg(ctx->asm_, dest_pair.high, ARM64_X1);
                        }
                    }
                } else {
                    arm64_register_t dest = get_physical_register_arm64(ctx, instr->dest);
                    if (dest != ARM64_NO_REG && dest != ARM64_X0) {
                        arm64_mov_reg_reg(ctx->asm_, dest, ARM64_X0);
                    }
                }
            }
            break;
        }

        case IR_RETURN: {
            if (instr->operand1.size == IR_SIZE_16BYTE) {
                // 16-byte composite type: move to X0:X1 pair
                arm64_reg_pair_t ret_pair = get_register_pair_arm64(ctx, instr->operand1);

                if (ret_pair.is_spilled) {
                    // Load from spill directly to X0:X1
                    load_value_from_spill(ctx, instr->operand1.as.reg, ret_pair.spill_offset,
                                         IR_SIZE_16BYTE, ARM64_X0, ARM64_X1);
                } else if (ret_pair.low != ARM64_NO_REG && ret_pair.is_pair) {
                    // Move pair to X0:X1
                    if (ret_pair.low != ARM64_X0) {
                        arm64_mov_reg_reg(ctx->asm_, ARM64_X0, ret_pair.low);
                    }
                    if (ret_pair.high != ARM64_X1) {
                        arm64_mov_reg_reg(ctx->asm_, ARM64_X1, ret_pair.high);
                    }
                }
            } else {
                // 8-byte scalar: existing single-register logic
                arm64_register_t ret = get_physical_register_arm64(ctx, instr->operand1);
                if (ret != ARM64_NO_REG && ret != ARM64_X0) {
                    arm64_mov_reg_reg(ctx->asm_, ARM64_X0, ret);
                }
            }
            emit_function_epilogue_arm64(ctx);
            break;
        }

        case IR_PRINT: {
            // Print a value: call sox_native_print(value)
            // ARM64 ABI: value_t is 16 bytes and goes in X0:X1
            // value_t is: {ValueType type(4) + padding(4) + union as(8)} = 16 bytes

            if (instr->operand1.size == IR_SIZE_16BYTE) {
                // 16-byte composite type: load pair into X0:X1
                arm64_reg_pair_t op1_pair = get_register_pair_arm64(ctx, instr->operand1);

                if (op1_pair.is_spilled) {
                    // Load from spill directly to X0:X1
                    load_value_from_spill(ctx, instr->operand1.as.reg, op1_pair.spill_offset,
                                         IR_SIZE_16BYTE, ARM64_X0, ARM64_X1);
                } else if (op1_pair.low != ARM64_NO_REG && op1_pair.is_pair) {
                    // Move pair to X0:X1
                    if (op1_pair.low != ARM64_X0) {
                        arm64_mov_reg_reg(ctx->asm_, ARM64_X0, op1_pair.low);
                    }
                    if (op1_pair.high != ARM64_X1) {
                        arm64_mov_reg_reg(ctx->asm_, ARM64_X1, op1_pair.high);
                    }
                }
            } else {
                // 8-byte scalar: use existing helper (falls back for scalars)
                load_16byte_argument_x0x1(ctx, instr->operand1);
            }

            // Call sox_native_print with relocation
            size_t call_offset = arm64_get_offset(ctx->asm_);
            arm64_bl(ctx->asm_, 0); // Placeholder - will be relocated by linker

            // Record relocation for the linker
            // Mach-O ARM64_RELOC_CALL26 for branch link
            arm64_add_relocation(ctx->asm_, call_offset, ARM64_RELOC_CALL26, "sox_native_print", 0);

            break;
        }

        default:
            // Unsupported instruction - emit NOP
            arm64_emit(ctx->asm_, 0xD503201F); // NOP
            break;
    }
}

bool codegen_arm64_generate_function(codegen_arm64_context_t* ctx, ir_function_t* func) {
    ctx->current_function = func;
    func->code_offset = arm64_get_offset(ctx->asm_) * 4;
    ctx->patch_count = 0;
    ctx->label_count = 0;
    if (ctx->label_offsets) {
        for (int i = 0; i < ctx->label_capacity; i++) {
            ctx->label_offsets[i] = -1;
        }
    }

    // Perform register allocation
    ctx->regalloc = regalloc_arm64_new(func);
    if (!regalloc_arm64_allocate(ctx->regalloc)) {
        return false;
    }

    // Debug: print allocation
    regalloc_arm64_print(ctx->regalloc);

    // Emit function prologue
    emit_function_prologue_arm64(ctx);
    spill_incoming_args_arm64(ctx);

    // Generate code for each basic block
    for (int i = 0; i < func->block_count; i++) {
        ir_block_t* block = func->blocks[i];

        // Mark block label
        mark_label(ctx, block->label);

        // Generate code for each instruction
        ir_instruction_t* instr = block->first;
        while (instr) {
            emit_instruction_arm64(ctx, instr);
            instr = instr->next;
        }
    }

    // Patch jump targets
    for (int i = 0; i < ctx->patch_count; i++) {
        size_t offset = ctx->jump_patches[i].offset;
        int target_label = ctx->jump_patches[i].target_label;

        if (target_label < ctx->label_count && ctx->label_offsets[target_label] >= 0) {
            // Validate offset fits in int32_t range (instructions are 4 bytes)
            if (offset > (size_t)(INT32_MAX / 4)) {
                fprintf(stderr, "ERROR: Jump patch offset too large: %zu\n", offset);
                return false;
            }

            int32_t target_offset = ctx->label_offsets[target_label] * 4; // Instructions are 4 bytes
            int32_t current_offset = (int32_t)(offset * 4);
            int32_t rel = target_offset - current_offset;

            // Patch the jump offset
            uint32_t* code = ctx->asm_->code.code;
            uint32_t instr = code[offset];

            // Check if it's a B instruction (unconditional) or B.cond
            if ((instr & 0xFC000000) == 0x14000000) {
                // B instruction - 26-bit offset
                uint32_t imm26 = (rel >> 2) & 0x3FFFFFF;
                code[offset] = (instr & 0xFC000000) | imm26;
            } else if ((instr & 0xFF000010) == 0x54000000) {
                // B.cond instruction - 19-bit offset
                uint32_t imm19 = (rel >> 2) & 0x7FFFF;
                code[offset] = (instr & 0xFF00001F) | (imm19 << 5);
            }
        }
    }

    return true;
}

bool codegen_arm64_generate(codegen_arm64_context_t* ctx) {
    for (int i = 0; i < ctx->module->function_count; i++) {
        if (!codegen_arm64_generate_function(ctx, ctx->module->functions[i])) {
            return false;
        }
    }

    for (int i = 0; i < ctx->call_patch_count; i++) {
        call_patch_arm64_t* patch = &ctx->call_patches[i];
        if (!patch->target) {
            continue;
        }
        uint32_t* code = ctx->asm_->code.code;
        uint32_t instr = code[patch->offset];
        int32_t target_offset = (int32_t)patch->target->code_offset;
        int32_t current_offset = (int32_t)(patch->offset * 4);
        int32_t rel = target_offset - current_offset;
        uint32_t imm26 = (rel >> 2) & 0x3FFFFFF;
        code[patch->offset] = (instr & 0xFC000000) | imm26;
    }

    return true;
}

uint8_t* codegen_arm64_get_code(codegen_arm64_context_t* ctx, size_t* size) {
    return arm64_get_code(ctx->asm_, size);
}

arm64_relocation_t* codegen_arm64_get_relocations(codegen_arm64_context_t* ctx, int* count) {
    if (!ctx || !ctx->asm_) {
        fprintf(stderr, "[CODEGEN] ERROR: NULL context or asm in get_relocations\n");
        *count = 0;
        return NULL;
    }
    *count = (int)ctx->asm_->reloc_count;
    fprintf(stderr, "[CODEGEN] Extracting relocations: count=%d, ptr=%p\n", *count, ctx->asm_->relocations);
    if (*count > 0 && ctx->asm_->relocations) {
        for (int i = 0; i < *count; i++) {
            fprintf(stderr, "[CODEGEN]   [%d] offset=%zu, type=%d, symbol=%s\n",
                   i, ctx->asm_->relocations[i].offset, ctx->asm_->relocations[i].type,
                   ctx->asm_->relocations[i].symbol ? ctx->asm_->relocations[i].symbol : "<NULL>");
        }
    }
    return ctx->asm_->relocations;
}

int codegen_arm64_add_string_literal(codegen_arm64_context_t* ctx, const char* data, size_t length) {
    if (!ctx || !data) return -1;

    // Grow capacity if needed
    if (ctx->string_literal_capacity < ctx->string_literal_count + 1) {
        int old_capacity = ctx->string_literal_capacity;
        ctx->string_literal_capacity = (old_capacity < 4) ? 4 : old_capacity * 2;
        ctx->string_literals = (string_literal_t*)l_mem_realloc(
            ctx->string_literals,
            sizeof(string_literal_t) * old_capacity,
            sizeof(string_literal_t) * ctx->string_literal_capacity
        );
    }

    // Create symbol name: .L.str.N
    char symbol[32];
    snprintf(symbol, sizeof(symbol), ".L.str.%d", ctx->string_literal_count);

    // Allocate and copy symbol
    size_t symbol_len = strlen(symbol) + 1;
    char* symbol_copy = (char*)l_mem_alloc(symbol_len);
    memcpy(symbol_copy, symbol, symbol_len);

    // Store string literal
    string_literal_t* lit = &ctx->string_literals[ctx->string_literal_count];
    lit->data = data;  // Note: We assume the data pointer remains valid (from IR)
    lit->length = length;
    lit->symbol = symbol_copy;
    lit->section_index = -1;  // Will be set during object file generation
    lit->section_offset = 0;  // Will be set during object file generation

    return ctx->string_literal_count++;
}

string_literal_t* codegen_arm64_get_string_literals(codegen_arm64_context_t* ctx, int* count) {
    if (!ctx) {
        *count = 0;
        return NULL;
    }
    *count = ctx->string_literal_count;
    return ctx->string_literals;
}

void codegen_arm64_print(codegen_arm64_context_t* ctx) {
    size_t size;
    uint8_t* code = arm64_get_code(ctx->asm_, &size);

    printf("Generated ARM64 machine code (%zu bytes):\n", size);
    for (size_t i = 0; i < size; i++) {
        printf("%02x ", code[i]);
        if ((i + 1) % 16 == 0) {
            printf("\n");
        }
    }
    printf("\n");
}

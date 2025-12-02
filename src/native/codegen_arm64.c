#include "codegen_arm64.h"
#include "elf_writer.h"
#include "../lib/memory.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
    ctx->relocations = NULL;
    ctx->relocation_count = 0;
    ctx->relocation_capacity = 0;
    return ctx;
}

void codegen_arm64_free(codegen_arm64_context_t* ctx) {
    if (!ctx) return;

    if (ctx->asm_) {
        arm64_assembler_free(ctx->asm_);
    }
    if (ctx->regalloc) {
        regalloc_free(ctx->regalloc);
    }
    if (ctx->label_offsets) {
        l_mem_free(ctx->label_offsets, sizeof(int) * ctx->label_capacity);
    }
    if (ctx->jump_patches) {
        l_mem_free(ctx->jump_patches, sizeof(jump_patch_arm64_t) * ctx->patch_capacity);
    }
    if (ctx->relocations) {
        l_mem_free(ctx->relocations, sizeof(codegen_arm64_relocation_t) * ctx->relocation_capacity);
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

// Add relocation entry
static void add_relocation_arm64(codegen_arm64_context_t* ctx, size_t offset,
                                 const char* symbol, uint32_t type, int64_t addend) {
    if (ctx->relocation_capacity < ctx->relocation_count + 1) {
        int old_capacity = ctx->relocation_capacity;
        ctx->relocation_capacity = (old_capacity < 8) ? 8 : old_capacity * 2;
        void* old_ptr = ctx->relocations;
        size_t old_size = sizeof(codegen_arm64_relocation_t) * old_capacity;
        size_t new_size = sizeof(codegen_arm64_relocation_t) * ctx->relocation_capacity;
        ctx->relocations = (codegen_arm64_relocation_t*)l_mem_realloc(old_ptr, old_size, new_size);
    }

    ctx->relocations[ctx->relocation_count].offset = offset;
    ctx->relocations[ctx->relocation_count].symbol = symbol;
    ctx->relocations[ctx->relocation_count].type = type;
    ctx->relocations[ctx->relocation_count].addend = addend;
    ctx->relocation_count++;
}

// Map virtual register to ARM64 physical register
static arm64_register_t get_physical_register_arm64(codegen_arm64_context_t* ctx, ir_value_t value) {
    if (value.type == IR_VAL_REGISTER) {
        int preg = regalloc_get_register(ctx->regalloc, value.as.reg);

        // Check if register was spilled
        if (preg < 0) {
            int spill_slot = regalloc_get_spill_slot(ctx->regalloc, value.as.reg);
            if (spill_slot >= 0) {
                // Load from stack into temporary register X9
                arm64_ldr_reg_reg_offset(ctx->asm_, ARM64_X9, ARM64_FP, -((spill_slot + 1) * 8));
                return ARM64_X9;
            }
            return ARM64_NO_REG;
        }

        // Convert generic register number to ARM64 register
        return regalloc_to_arm64_register(preg);
    }
    return ARM64_NO_REG;
}

static void emit_function_prologue_arm64(codegen_arm64_context_t* ctx) {
    // ARM64 function prologue (AArch64 calling convention)
    // Calculate total frame size: saved regs + locals + alignment
    int locals_size = regalloc_get_frame_size(ctx->regalloc);

    // Need to save callee-saved registers if used
    // For now, always save X19-X22 (32 bytes) if we have locals
    int callee_saved_size = (locals_size > 0) ? 32 : 0;
    int total_frame_size = locals_size + callee_saved_size + 16; // +16 for FP/LR

    // Ensure 16-byte alignment
    total_frame_size = (total_frame_size + 15) & ~15;

    // Save FP and LR with pre-index: stp x29, x30, [sp, #-frame_size]!
    arm64_stp_pre(ctx->asm_, ARM64_FP, ARM64_LR, ARM64_SP, -total_frame_size);

    // Set up frame pointer: mov x29, sp
    arm64_mov_reg_reg(ctx->asm_, ARM64_FP, ARM64_SP);

    // Save callee-saved registers if needed
    if (callee_saved_size > 0) {
        arm64_stp(ctx->asm_, ARM64_X19, ARM64_X20, ARM64_SP, 16);
        arm64_stp(ctx->asm_, ARM64_X21, ARM64_X22, ARM64_SP, 32);
    }
}

static void emit_function_epilogue_arm64(codegen_arm64_context_t* ctx) {
    // ARM64 function epilogue
    int locals_size = regalloc_get_frame_size(ctx->regalloc);
    int callee_saved_size = (locals_size > 0) ? 32 : 0;
    int total_frame_size = locals_size + callee_saved_size + 16;
    total_frame_size = (total_frame_size + 15) & ~15;

    // Restore callee-saved registers if they were saved
    if (callee_saved_size > 0) {
        arm64_ldp(ctx->asm_, ARM64_X21, ARM64_X22, ARM64_SP, 32);
        arm64_ldp(ctx->asm_, ARM64_X19, ARM64_X20, ARM64_SP, 16);
    }

    // Restore FP and LR with post-index: ldp x29, x30, [sp], #frame_size
    arm64_ldp_post(ctx->asm_, ARM64_FP, ARM64_LR, ARM64_SP, total_frame_size);

    // Return
    arm64_ret(ctx->asm_, ARM64_LR);
}

// ARM64 EABI calling convention argument marshalling
static void marshal_arguments_arm64(codegen_arm64_context_t* ctx, ir_value_t* args, int arg_count) {
    // ARM64 EABI integer argument registers: X0-X7
    const arm64_register_t arg_regs[8] = {
        ARM64_X0, ARM64_X1, ARM64_X2, ARM64_X3,
        ARM64_X4, ARM64_X5, ARM64_X6, ARM64_X7
    };

    // First pass: marshal arguments into registers (first 8 arguments)
    int reg_arg_count = (arg_count < 8) ? arg_count : 8;
    for (int i = 0; i < reg_arg_count; i++) {
        ir_value_t arg = args[i];
        arm64_register_t dest_reg = arg_regs[i];

        if (arg.type == IR_VAL_REGISTER) {
            // Get physical register for virtual register
            arm64_register_t src_reg = get_physical_register_arm64(ctx, arg);
            if (src_reg != ARM64_NO_REG && src_reg != dest_reg) {
                // Move from source register to argument register
                arm64_mov_reg_reg(ctx->asm_, dest_reg, src_reg);
            }
        } else if (arg.type == IR_VAL_CONSTANT) {
            // Load constant into argument register
            value_t val = arg.as.constant;
            if (IS_NUMBER(val)) {
                int64_t num = (int64_t)AS_NUMBER(val);
                arm64_mov_reg_imm(ctx->asm_, dest_reg, (uint64_t)num);
            } else if (IS_BOOL(val)) {
                uint64_t bool_val = AS_BOOL(val) ? 1 : 0;
                arm64_mov_reg_imm(ctx->asm_, dest_reg, bool_val);
            } else if (IS_NIL(val)) {
                arm64_eor_reg_reg_reg(ctx->asm_, dest_reg, dest_reg, dest_reg); // XOR = 0
            }
        }
    }

    // Second pass: push stack arguments (arguments 9+) in reverse order
    // ARM64 stack arguments must be 8-byte aligned
    for (int i = arg_count - 1; i >= 8; i--) {
        ir_value_t arg = args[i];

        if (arg.type == IR_VAL_REGISTER) {
            arm64_register_t src_reg = get_physical_register_arm64(ctx, arg);
            if (src_reg != ARM64_NO_REG) {
                // Store register to stack: str src, [sp, #-16]!
                arm64_str_reg_reg_offset(ctx->asm_, src_reg, ARM64_SP, -16);
                arm64_sub_reg_reg_imm(ctx->asm_, ARM64_SP, ARM64_SP, 16);
            }
        } else if (arg.type == IR_VAL_CONSTANT) {
            // Load constant into X9 (temporary) and push
            value_t val = arg.as.constant;
            if (IS_NUMBER(val)) {
                int64_t num = (int64_t)AS_NUMBER(val);
                arm64_mov_reg_imm(ctx->asm_, ARM64_X9, (uint64_t)num);
            } else if (IS_BOOL(val)) {
                uint64_t bool_val = AS_BOOL(val) ? 1 : 0;
                arm64_mov_reg_imm(ctx->asm_, ARM64_X9, bool_val);
            } else if (IS_NIL(val)) {
                arm64_eor_reg_reg_reg(ctx->asm_, ARM64_X9, ARM64_X9, ARM64_X9);
            }
            arm64_str_reg_reg_offset(ctx->asm_, ARM64_X9, ARM64_SP, -16);
            arm64_sub_reg_reg_imm(ctx->asm_, ARM64_SP, ARM64_SP, 16);
        }
    }
}

static void emit_instruction_arm64(codegen_arm64_context_t* ctx, ir_instruction_t* instr) {
    switch (instr->op) {
        case IR_CONST_INT:
        case IR_CONST_FLOAT: {
            if (instr->dest.type == IR_VAL_REGISTER) {
                arm64_register_t dest = get_physical_register_arm64(ctx, instr->dest);
                if (dest != ARM64_NO_REG) {
                    if (IS_NUMBER(instr->operand1.as.constant)) {
                        int64_t val = (int64_t)AS_NUMBER(instr->operand1.as.constant);
                        arm64_mov_reg_imm(ctx->asm_, dest, (uint64_t)val);
                    }
                }
            }
            break;
        }

        case IR_CONST_BOOL: {
            if (instr->dest.type == IR_VAL_REGISTER) {
                arm64_register_t dest = get_physical_register_arm64(ctx, instr->dest);
                if (dest != ARM64_NO_REG) {
                    uint64_t val = AS_BOOL(instr->operand1.as.constant) ? 1 : 0;
                    arm64_mov_reg_imm(ctx->asm_, dest, val);
                }
            }
            break;
        }

        case IR_CONST_NIL: {
            if (instr->dest.type == IR_VAL_REGISTER) {
                arm64_register_t dest = get_physical_register_arm64(ctx, instr->dest);
                if (dest != ARM64_NO_REG) {
                    arm64_eor_reg_reg_reg(ctx->asm_, dest, dest, dest); // XOR = 0
                }
            }
            break;
        }

        case IR_ADD: {
            arm64_register_t left = get_physical_register_arm64(ctx, instr->operand1);
            arm64_register_t right = get_physical_register_arm64(ctx, instr->operand2);
            arm64_register_t dest = get_physical_register_arm64(ctx, instr->dest);

            if (dest != ARM64_NO_REG && left != ARM64_NO_REG && right != ARM64_NO_REG) {
                arm64_add_reg_reg_reg(ctx->asm_, dest, left, right);
            }
            break;
        }

        case IR_SUB: {
            arm64_register_t left = get_physical_register_arm64(ctx, instr->operand1);
            arm64_register_t right = get_physical_register_arm64(ctx, instr->operand2);
            arm64_register_t dest = get_physical_register_arm64(ctx, instr->dest);

            if (dest != ARM64_NO_REG && left != ARM64_NO_REG && right != ARM64_NO_REG) {
                arm64_sub_reg_reg_reg(ctx->asm_, dest, left, right);
            }
            break;
        }

        case IR_MUL: {
            arm64_register_t left = get_physical_register_arm64(ctx, instr->operand1);
            arm64_register_t right = get_physical_register_arm64(ctx, instr->operand2);
            arm64_register_t dest = get_physical_register_arm64(ctx, instr->dest);

            if (dest != ARM64_NO_REG && left != ARM64_NO_REG && right != ARM64_NO_REG) {
                arm64_mul_reg_reg_reg(ctx->asm_, dest, left, right);
            }
            break;
        }

        case IR_DIV: {
            arm64_register_t left = get_physical_register_arm64(ctx, instr->operand1);
            arm64_register_t right = get_physical_register_arm64(ctx, instr->operand2);
            arm64_register_t dest = get_physical_register_arm64(ctx, instr->dest);

            if (dest != ARM64_NO_REG && left != ARM64_NO_REG && right != ARM64_NO_REG) {
                arm64_sdiv_reg_reg_reg(ctx->asm_, dest, left, right);
            }
            break;
        }

        case IR_NEG: {
            arm64_register_t src = get_physical_register_arm64(ctx, instr->operand1);
            arm64_register_t dest = get_physical_register_arm64(ctx, instr->dest);

            if (dest != ARM64_NO_REG && src != ARM64_NO_REG) {
                arm64_neg_reg_reg(ctx->asm_, dest, src);
            }
            break;
        }

        case IR_EQ:
        case IR_NE:
        case IR_LT:
        case IR_LE:
        case IR_GT:
        case IR_GE: {
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
            break;
        }

        case IR_NOT: {
            arm64_register_t src = get_physical_register_arm64(ctx, instr->operand1);
            arm64_register_t dest = get_physical_register_arm64(ctx, instr->dest);

            if (dest != ARM64_NO_REG && src != ARM64_NO_REG) {
                arm64_cmp_reg_imm(ctx->asm_, src, 0);
                arm64_cset(ctx->asm_, dest, ARM64_CC_EQ); // Set if zero
            }
            break;
        }

        case IR_MOVE: {
            arm64_register_t src = get_physical_register_arm64(ctx, instr->operand1);
            arm64_register_t dest = get_physical_register_arm64(ctx, instr->dest);

            if (dest != ARM64_NO_REG && src != ARM64_NO_REG && dest != src) {
                arm64_mov_reg_reg(ctx->asm_, dest, src);
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
            // 1. Marshal arguments to correct registers/stack (ARM64 EABI)
            if (instr->call_args && instr->call_arg_count > 0) {
                marshal_arguments_arm64(ctx, instr->call_args, instr->call_arg_count);
            }

            // 2. Emit call instruction with placeholder offset
            size_t call_offset = arm64_get_offset(ctx->asm_);
            arm64_bl(ctx->asm_, 0); // Placeholder - will be relocated by linker

            // 3. Record relocation if we have a target symbol
            if (instr->call_target) {
                // ARM64 uses R_AARCH64_CALL26 relocation type
                add_relocation_arm64(ctx, call_offset, instr->call_target, R_AARCH64_CALL26, 0);
            }

            // 4. Clean up stack arguments if any (9+ arguments)
            int stack_arg_count = (instr->call_arg_count > 8) ? (instr->call_arg_count - 8) : 0;
            if (stack_arg_count > 0) {
                int cleanup_bytes = stack_arg_count * 16; // ARM64 uses 16-byte stack slots
                arm64_add_reg_reg_imm(ctx->asm_, ARM64_SP, ARM64_SP, cleanup_bytes);
            }

            // 5. Handle return value in X0
            if (instr->dest.type == IR_VAL_REGISTER) {
                arm64_register_t dest = get_physical_register_arm64(ctx, instr->dest);
                if (dest != ARM64_NO_REG && dest != ARM64_X0) {
                    arm64_mov_reg_reg(ctx->asm_, dest, ARM64_X0);
                }
            }
            break;
        }

        case IR_RETURN: {
            arm64_register_t ret = get_physical_register_arm64(ctx, instr->operand1);
            if (ret != ARM64_NO_REG && ret != ARM64_X0) {
                arm64_mov_reg_reg(ctx->asm_, ARM64_X0, ret);
            }
            emit_function_epilogue_arm64(ctx);
            break;
        }

        case IR_PRINT: {
            // Would need to call runtime print function
            arm64_bl(ctx->asm_, 0); // Placeholder
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

    // Perform register allocation
    ctx->regalloc = regalloc_new(func, REGALLOC_ARCH_ARM64);
    if (!regalloc_allocate(ctx->regalloc)) {
        return false;
    }

    // Debug: print allocation
    printf("ARM64 ");
    regalloc_print(ctx->regalloc);

    // Emit function prologue
    emit_function_prologue_arm64(ctx);

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
            int32_t target_offset = ctx->label_offsets[target_label] * 4; // Instructions are 4 bytes
            int32_t current_offset = offset * 4;
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
    return true;
}

uint8_t* codegen_arm64_get_code(codegen_arm64_context_t* ctx, size_t* size) {
    return arm64_get_code(ctx->asm_, size);
}

codegen_arm64_relocation_t* codegen_arm64_get_relocations(codegen_arm64_context_t* ctx, int* count) {
    if (count) {
        *count = ctx->relocation_count;
    }
    return ctx->relocations;
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

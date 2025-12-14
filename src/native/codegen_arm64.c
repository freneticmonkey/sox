#include "codegen_arm64.h"
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

static void emit_function_epilogue_arm64(codegen_arm64_context_t* ctx) {
    // ARM64 function epilogue
    int frame_size = regalloc_arm64_get_frame_size(ctx->regalloc);

    // Restore callee-saved registers
    if (frame_size > 64) {
        arm64_ldp(ctx->asm_, ARM64_X21, ARM64_X22, ARM64_SP, 16);
        arm64_ldp(ctx->asm_, ARM64_X19, ARM64_X20, ARM64_SP, 0);
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
            // Simplified call - would need proper ABI handling
            arm64_bl(ctx->asm_, 0); // Would need to be relocated
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
            // Print a value: call sox_native_print(value)
            // ARM64 ABI: First argument goes in X0

            // 1. Move value to X0 (argument register)
            arm64_register_t src_reg = get_physical_register_arm64(ctx, instr->operand1);
            if (src_reg != ARM64_X0) {
                arm64_mov_reg_reg(ctx->asm_, ARM64_X0, src_reg);
            }

            // 2. Call sox_native_print with relocation
            size_t call_offset = arm64_get_offset(ctx->asm_);
            arm64_bl(ctx->asm_, 0); // Placeholder - will be relocated by linker

            // 3. Record relocation for the linker
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

    // Perform register allocation
    ctx->regalloc = regalloc_arm64_new(func);
    if (!regalloc_arm64_allocate(ctx->regalloc)) {
        return false;
    }

    // Debug: print allocation
    regalloc_arm64_print(ctx->regalloc);

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

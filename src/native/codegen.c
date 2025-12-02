#include "codegen.h"
#include "../lib/memory.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

codegen_context_t* codegen_new(ir_module_t* module) {
    codegen_context_t* ctx = (codegen_context_t*)l_mem_alloc(sizeof(codegen_context_t));
    ctx->module = module;
    ctx->asm_ = x64_assembler_new();
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

void codegen_free(codegen_context_t* ctx) {
    if (!ctx) return;

    if (ctx->asm_) {
        x64_assembler_free(ctx->asm_);
    }
    if (ctx->regalloc) {
        regalloc_free(ctx->regalloc);
    }
    if (ctx->label_offsets) {
        l_mem_free(ctx->label_offsets, sizeof(int) * ctx->label_capacity);
    }
    if (ctx->jump_patches) {
        l_mem_free(ctx->jump_patches, sizeof(jump_patch_t) * ctx->patch_capacity);
    }

    l_mem_free(ctx, sizeof(codegen_context_t));
}

static void mark_label(codegen_context_t* ctx, int label) {
    if (ctx->label_capacity < label + 1) {
        int old_capacity = ctx->label_capacity;
        ctx->label_capacity = (label + 1) * 2;
        ctx->label_offsets = (int*)l_mem_realloc(
            ctx->label_offsets,
            sizeof(int) * old_capacity,
            sizeof(int) * ctx->label_capacity
        );
        // Initialize new entries to -1
        for (int i = old_capacity; i < ctx->label_capacity; i++) {
            ctx->label_offsets[i] = -1;
        }
    }
    ctx->label_offsets[label] = (int)x64_get_offset(ctx->asm_);
    if (label >= ctx->label_count) {
        ctx->label_count = label + 1;
    }
}

static void add_jump_patch(codegen_context_t* ctx, size_t offset, int target_label) {
    if (ctx->patch_capacity < ctx->patch_count + 1) {
        int old_capacity = ctx->patch_capacity;
        ctx->patch_capacity = (old_capacity < 8) ? 8 : old_capacity * 2;
        void* old_ptr = ctx->jump_patches;
        size_t old_size = sizeof(jump_patch_t) * old_capacity;
        size_t new_size = sizeof(jump_patch_t) * ctx->patch_capacity;
        ctx->jump_patches = (jump_patch_t*)l_mem_realloc(old_ptr, old_size, new_size);
    }

    ctx->jump_patches[ctx->patch_count].offset = offset;
    ctx->jump_patches[ctx->patch_count].target_label = target_label;
    ctx->patch_count++;
}

static x64_register_t get_physical_register(codegen_context_t* ctx, ir_value_t value) {
    if (value.type == IR_VAL_REGISTER) {
        int preg = regalloc_get_register(ctx->regalloc, value.as.reg);
        if (preg >= 0) {
            return regalloc_to_x64_register(preg);
        }
        // Handle spilled registers by loading into a temporary
        // For now, use RAX as scratch
        int spill_slot = regalloc_get_spill_slot(ctx->regalloc, value.as.reg);
        if (spill_slot >= 0) {
            x64_mov_reg_mem(ctx->asm_, X64_RAX, X64_RBP, -((spill_slot + 1) * 8));
            return X64_RAX;
        }
    }
    return X64_NO_REG;
}

static void emit_function_prologue(codegen_context_t* ctx) {
    // push rbp
    x64_push_reg(ctx->asm_, X64_RBP);

    // mov rbp, rsp
    x64_mov_reg_reg(ctx->asm_, X64_RBP, X64_RSP);

    // sub rsp, frame_size
    int frame_size = regalloc_get_frame_size(ctx->regalloc);
    if (frame_size > 0) {
        x64_sub_reg_imm(ctx->asm_, X64_RSP, frame_size);
    }

    // Save callee-saved registers that we use
    // (This is simplified - should track which registers are actually used)
    x64_push_reg(ctx->asm_, X64_RBX);
    x64_push_reg(ctx->asm_, X64_R12);
    x64_push_reg(ctx->asm_, X64_R13);
    x64_push_reg(ctx->asm_, X64_R14);
    x64_push_reg(ctx->asm_, X64_R15);
}

static void emit_function_epilogue(codegen_context_t* ctx) {
    // Restore callee-saved registers
    x64_pop_reg(ctx->asm_, X64_R15);
    x64_pop_reg(ctx->asm_, X64_R14);
    x64_pop_reg(ctx->asm_, X64_R13);
    x64_pop_reg(ctx->asm_, X64_R12);
    x64_pop_reg(ctx->asm_, X64_RBX);

    // mov rsp, rbp
    x64_mov_reg_reg(ctx->asm_, X64_RSP, X64_RBP);

    // pop rbp
    x64_pop_reg(ctx->asm_, X64_RBP);

    // ret
    x64_ret(ctx->asm_);
}

static void emit_instruction(codegen_context_t* ctx, ir_instruction_t* instr) {
    switch (instr->op) {
        case IR_CONST_INT:
        case IR_CONST_FLOAT: {
            if (instr->dest.type == IR_VAL_REGISTER) {
                x64_register_t dest = get_physical_register(ctx, instr->dest);
                if (dest != X64_NO_REG) {
                    if (IS_NUMBER(instr->operand1.as.constant)) {
                        // For now, treat as integer (simplified)
                        int64_t val = (int64_t)AS_NUMBER(instr->operand1.as.constant);
                        x64_mov_reg_imm64(ctx->asm_, dest, val);
                    }
                }
            }
            break;
        }

        case IR_CONST_BOOL: {
            if (instr->dest.type == IR_VAL_REGISTER) {
                x64_register_t dest = get_physical_register(ctx, instr->dest);
                if (dest != X64_NO_REG) {
                    int64_t val = AS_BOOL(instr->operand1.as.constant) ? 1 : 0;
                    x64_mov_reg_imm32(ctx->asm_, dest, (int32_t)val);
                }
            }
            break;
        }

        case IR_CONST_NIL: {
            if (instr->dest.type == IR_VAL_REGISTER) {
                x64_register_t dest = get_physical_register(ctx, instr->dest);
                if (dest != X64_NO_REG) {
                    x64_xor_reg_reg(ctx->asm_, dest, dest); // xor reg, reg = 0
                }
            }
            break;
        }

        case IR_ADD: {
            x64_register_t left = get_physical_register(ctx, instr->operand1);
            x64_register_t right = get_physical_register(ctx, instr->operand2);
            x64_register_t dest = get_physical_register(ctx, instr->dest);

            if (dest != X64_NO_REG && left != X64_NO_REG && right != X64_NO_REG) {
                if (dest != left) {
                    x64_mov_reg_reg(ctx->asm_, dest, left);
                }
                x64_add_reg_reg(ctx->asm_, dest, right);
            }
            break;
        }

        case IR_SUB: {
            x64_register_t left = get_physical_register(ctx, instr->operand1);
            x64_register_t right = get_physical_register(ctx, instr->operand2);
            x64_register_t dest = get_physical_register(ctx, instr->dest);

            if (dest != X64_NO_REG && left != X64_NO_REG && right != X64_NO_REG) {
                if (dest != left) {
                    x64_mov_reg_reg(ctx->asm_, dest, left);
                }
                x64_sub_reg_reg(ctx->asm_, dest, right);
            }
            break;
        }

        case IR_MUL: {
            x64_register_t left = get_physical_register(ctx, instr->operand1);
            x64_register_t right = get_physical_register(ctx, instr->operand2);
            x64_register_t dest = get_physical_register(ctx, instr->dest);

            if (dest != X64_NO_REG && left != X64_NO_REG && right != X64_NO_REG) {
                if (dest != left) {
                    x64_mov_reg_reg(ctx->asm_, dest, left);
                }
                x64_imul_reg_reg(ctx->asm_, dest, right);
            }
            break;
        }

        case IR_DIV: {
            x64_register_t left = get_physical_register(ctx, instr->operand1);
            x64_register_t right = get_physical_register(ctx, instr->operand2);
            x64_register_t dest = get_physical_register(ctx, instr->dest);

            if (dest != X64_NO_REG && left != X64_NO_REG && right != X64_NO_REG) {
                // Division requires RAX and RDX
                x64_mov_reg_reg(ctx->asm_, X64_RAX, left);
                // Sign-extend RAX into RDX:RAX
                x64_emit_byte(ctx->asm_, X64_REX_W);
                x64_emit_byte(ctx->asm_, 0x99); // CQO
                x64_idiv_reg(ctx->asm_, right);
                if (dest != X64_RAX) {
                    x64_mov_reg_reg(ctx->asm_, dest, X64_RAX);
                }
            }
            break;
        }

        case IR_NEG: {
            x64_register_t src = get_physical_register(ctx, instr->operand1);
            x64_register_t dest = get_physical_register(ctx, instr->dest);

            if (dest != X64_NO_REG && src != X64_NO_REG) {
                if (dest != src) {
                    x64_mov_reg_reg(ctx->asm_, dest, src);
                }
                x64_neg_reg(ctx->asm_, dest);
            }
            break;
        }

        case IR_EQ:
        case IR_NE:
        case IR_LT:
        case IR_LE:
        case IR_GT:
        case IR_GE: {
            x64_register_t left = get_physical_register(ctx, instr->operand1);
            x64_register_t right = get_physical_register(ctx, instr->operand2);
            x64_register_t dest = get_physical_register(ctx, instr->dest);

            if (dest != X64_NO_REG && left != X64_NO_REG && right != X64_NO_REG) {
                x64_cmp_reg_reg(ctx->asm_, left, right);

                x64_condition_t cond;
                switch (instr->op) {
                    case IR_EQ: cond = X64_CC_E; break;
                    case IR_NE: cond = X64_CC_NE; break;
                    case IR_LT: cond = X64_CC_L; break;
                    case IR_LE: cond = X64_CC_LE; break;
                    case IR_GT: cond = X64_CC_G; break;
                    case IR_GE: cond = X64_CC_GE; break;
                    default: cond = X64_CC_E;
                }

                x64_setcc(ctx->asm_, cond, dest);
                // Zero-extend to 64-bit
                x64_and_reg_reg(ctx->asm_, dest, dest);
            }
            break;
        }

        case IR_NOT: {
            x64_register_t src = get_physical_register(ctx, instr->operand1);
            x64_register_t dest = get_physical_register(ctx, instr->dest);

            if (dest != X64_NO_REG && src != X64_NO_REG) {
                if (dest != src) {
                    x64_mov_reg_reg(ctx->asm_, dest, src);
                }
                x64_test_reg_reg(ctx->asm_, dest, dest);
                x64_setcc(ctx->asm_, X64_CC_E, dest); // Set if zero
                x64_and_reg_reg(ctx->asm_, dest, dest);
            }
            break;
        }

        case IR_MOVE: {
            x64_register_t src = get_physical_register(ctx, instr->operand1);
            x64_register_t dest = get_physical_register(ctx, instr->dest);

            if (dest != X64_NO_REG && src != X64_NO_REG && dest != src) {
                x64_mov_reg_reg(ctx->asm_, dest, src);
            }
            break;
        }

        case IR_JUMP: {
            if (instr->operand1.type == IR_VAL_LABEL) {
                size_t patch_offset = x64_get_offset(ctx->asm_);
                x64_jmp_rel32(ctx->asm_, 0); // Will be patched later
                add_jump_patch(ctx, patch_offset + 1, instr->operand1.as.label);
            }
            break;
        }

        case IR_BRANCH: {
            if (instr->operand2.type == IR_VAL_LABEL) {
                x64_register_t cond = get_physical_register(ctx, instr->operand1);
                if (cond != X64_NO_REG) {
                    x64_test_reg_reg(ctx->asm_, cond, cond);
                    size_t patch_offset = x64_get_offset(ctx->asm_);
                    x64_jcc_rel32(ctx->asm_, X64_CC_NE, 0); // Jump if not zero
                    add_jump_patch(ctx, patch_offset + 2, instr->operand2.as.label);
                }
            }
            break;
        }

        case IR_CALL: {
            // Simplified call - would need proper ABI handling
            // For now, just emit a call to a runtime function
            x64_call_rel32(ctx->asm_, 0); // Would need to be relocated
            break;
        }

        case IR_RETURN: {
            x64_register_t ret = get_physical_register(ctx, instr->operand1);
            if (ret != X64_NO_REG && ret != X64_RAX) {
                x64_mov_reg_reg(ctx->asm_, X64_RAX, ret);
            }
            emit_function_epilogue(ctx);
            break;
        }

        case IR_PRINT: {
            // Would need to call runtime print function
            x64_call_rel32(ctx->asm_, 0); // Placeholder
            break;
        }

        default:
            // Unsupported instruction - emit nop or call runtime
            x64_emit_byte(ctx->asm_, 0x90); // NOP
            break;
    }
}

bool codegen_generate_function(codegen_context_t* ctx, ir_function_t* func) {
    ctx->current_function = func;

    // Perform register allocation
    ctx->regalloc = regalloc_new(func, REGALLOC_ARCH_X64);
    if (!regalloc_allocate(ctx->regalloc)) {
        return false;
    }

    // Debug: print allocation
    regalloc_print(ctx->regalloc);

    // Emit function prologue
    emit_function_prologue(ctx);

    // Generate code for each basic block
    for (int i = 0; i < func->block_count; i++) {
        ir_block_t* block = func->blocks[i];

        // Mark block label
        mark_label(ctx, block->label);

        // Generate code for each instruction
        ir_instruction_t* instr = block->first;
        while (instr) {
            emit_instruction(ctx, instr);
            instr = instr->next;
        }
    }

    // Patch jump targets
    for (int i = 0; i < ctx->patch_count; i++) {
        size_t offset = ctx->jump_patches[i].offset;
        int target_label = ctx->jump_patches[i].target_label;

        if (target_label < ctx->label_count && ctx->label_offsets[target_label] >= 0) {
            int32_t rel = ctx->label_offsets[target_label] - (offset + 4);
            // Patch the jump offset
            ctx->asm_->code.code[offset] = rel & 0xFF;
            ctx->asm_->code.code[offset + 1] = (rel >> 8) & 0xFF;
            ctx->asm_->code.code[offset + 2] = (rel >> 16) & 0xFF;
            ctx->asm_->code.code[offset + 3] = (rel >> 24) & 0xFF;
        }
    }

    return true;
}

bool codegen_generate(codegen_context_t* ctx) {
    for (int i = 0; i < ctx->module->function_count; i++) {
        if (!codegen_generate_function(ctx, ctx->module->functions[i])) {
            return false;
        }
    }
    return true;
}

uint8_t* codegen_get_code(codegen_context_t* ctx, size_t* size) {
    *size = ctx->asm_->code.size;
    return ctx->asm_->code.code;
}

void codegen_print(codegen_context_t* ctx) {
    printf("Generated machine code (%zu bytes):\n", ctx->asm_->code.size);
    for (size_t i = 0; i < ctx->asm_->code.size; i++) {
        printf("%02x ", ctx->asm_->code.code[i]);
        if ((i + 1) % 16 == 0) {
            printf("\n");
        }
    }
    printf("\n");
}

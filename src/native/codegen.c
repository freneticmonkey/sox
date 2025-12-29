#include "codegen.h"
#include "elf_writer.h"
#include "../lib/memory.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

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
    ctx->call_patches = NULL;
    ctx->call_patch_count = 0;
    ctx->call_patch_capacity = 0;
    ctx->relocations = NULL;
    ctx->relocation_count = 0;
    ctx->relocation_capacity = 0;
    ctx->current_stack_offset = 0;
    ctx->current_frame_alignment = 0;
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
    if (ctx->call_patches) {
        l_mem_free(ctx->call_patches, sizeof(call_patch_t) * ctx->call_patch_capacity);
    }
    if (ctx->relocations) {
        l_mem_free(ctx->relocations, sizeof(codegen_relocation_t) * ctx->relocation_capacity);
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

static void add_call_patch(codegen_context_t* ctx, size_t offset, ir_function_t* target) {
    if (ctx->call_patch_capacity < ctx->call_patch_count + 1) {
        int old_capacity = ctx->call_patch_capacity;
        ctx->call_patch_capacity = (old_capacity < 8) ? 8 : old_capacity * 2;
        void* old_ptr = ctx->call_patches;
        size_t old_size = sizeof(call_patch_t) * old_capacity;
        size_t new_size = sizeof(call_patch_t) * ctx->call_patch_capacity;
        ctx->call_patches = (call_patch_t*)l_mem_realloc(old_ptr, old_size, new_size);
    }

    ctx->call_patches[ctx->call_patch_count].offset = offset;
    ctx->call_patches[ctx->call_patch_count].target = target;
    ctx->call_patch_count++;
}

static void add_relocation(codegen_context_t* ctx, size_t offset, const char* symbol,
                          uint32_t type, int64_t addend) {
    if (ctx->relocation_capacity < ctx->relocation_count + 1) {
        int old_capacity = ctx->relocation_capacity;
        ctx->relocation_capacity = (old_capacity < 8) ? 8 : old_capacity * 2;
        void* old_ptr = ctx->relocations;
        size_t old_size = sizeof(codegen_relocation_t) * old_capacity;
        size_t new_size = sizeof(codegen_relocation_t) * ctx->relocation_capacity;
        ctx->relocations = (codegen_relocation_t*)l_mem_realloc(old_ptr, old_size, new_size);
    }

    ctx->relocations[ctx->relocation_count].offset = offset;
    ctx->relocations[ctx->relocation_count].symbol = symbol;
    ctx->relocations[ctx->relocation_count].type = type;
    ctx->relocations[ctx->relocation_count].addend = addend;
    ctx->relocation_count++;
}

// Calculate aligned frame size to maintain 16-byte stack alignment
// This accounts for all pushes in the prologue and ensures proper alignment
static int calculate_aligned_frame_size(int locals_size, int saved_regs_count) {
    // Frame layout after prologue:
    // [Entry RSP]          <- RSP on function entry (must be 16n+8 due to call instruction)
    // [Return Address]     <- 8 bytes (from call instruction)
    // [RBP]                <- 8 bytes (first push in prologue)
    // [Saved regs]         <- saved_regs_count * 8 bytes
    // [Locals + padding]   <- locals_size + padding bytes
    //
    // After "push rbp", RSP is at 16n (aligned)
    // After saving callee-saved registers, RSP is at 16n - (saved_regs_count * 8)
    // We need to adjust locals_size so final RSP is 16-byte aligned
    //
    // Total bytes after push rbp: (saved_regs_count * 8) + locals_size
    // This must be a multiple of 16

    int after_saved_regs = saved_regs_count * 8;
    int total_after_rbp = after_saved_regs + locals_size;

    // Round up to nearest multiple of 16
    int aligned_total = ((total_after_rbp + 15) / 16) * 16;

    // Calculate padding needed for locals
    int aligned_locals = aligned_total - after_saved_regs;

    return aligned_locals;
}

static x64_register_t get_physical_register(codegen_context_t* ctx, ir_value_t value) {
    if (value.type == IR_VAL_REGISTER) {
        x64_register_t reg = regalloc_get_register(ctx->regalloc, value.as.reg);
        if (reg != X64_NO_REG) {
            return reg;
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
    // Count how many callee-saved registers we need to save
    // For now: save all 5 (RBX, R12-R15) - total 5 registers
    // Later phases can optimize to save only used registers
    int saved_regs = 5;  // RBX, R12, R13, R14, R15 (not counting RBP)

    // Get frame size from register allocator (for spilled variables)
    int locals_size = regalloc_get_frame_size(ctx->regalloc);

    // Calculate aligned frame size including padding
    int aligned_frame = calculate_aligned_frame_size(locals_size, saved_regs);

    // 1. Push RBP to save it and establish previous frame pointer
    // This creates 8-byte offset that we account for in alignment
    x64_push_reg(ctx->asm_, X64_RBP);

    // 2. Set up new frame pointer
    x64_mov_reg_reg(ctx->asm_, X64_RBP, X64_RSP);

    // 3. Save callee-saved registers that we'll use
    // These pushes are accounted for in our alignment calculation
    x64_push_reg(ctx->asm_, X64_RBX);
    x64_push_reg(ctx->asm_, X64_R12);
    x64_push_reg(ctx->asm_, X64_R13);
    x64_push_reg(ctx->asm_, X64_R14);
    x64_push_reg(ctx->asm_, X64_R15);

    // 4. Allocate space for local variables and padding
    // This ensures RSP is 16-byte aligned before any call instruction
    if (aligned_frame > 0) {
        x64_sub_reg_imm(ctx->asm_, X64_RSP, aligned_frame);
    }

    // Store the frame size for use in emit_epilogue and call site alignment
    ctx->current_frame_alignment = aligned_frame;

    // Track stack offset (for future call site alignment verification)
    // After prologue: RSP = entry_RSP - 8 (ret addr) - 8 (rbp) - 40 (saved regs) - aligned_frame
    ctx->current_stack_offset = 8 + 8 + (saved_regs * 8) + aligned_frame;
}

static void emit_function_epilogue(codegen_context_t* ctx) {
    // 1. Deallocate local variables (reverse of prologue step 4)
    if (ctx->current_frame_alignment > 0) {
        x64_add_reg_imm(ctx->asm_, X64_RSP, ctx->current_frame_alignment);
    }

    // 2. Restore callee-saved registers (reverse order of prologue step 3)
    x64_pop_reg(ctx->asm_, X64_R15);
    x64_pop_reg(ctx->asm_, X64_R14);
    x64_pop_reg(ctx->asm_, X64_R13);
    x64_pop_reg(ctx->asm_, X64_R12);
    x64_pop_reg(ctx->asm_, X64_RBX);

    // 3. Restore frame pointer and return
    x64_pop_reg(ctx->asm_, X64_RBP);
    x64_ret(ctx->asm_);
}

// Marshal function arguments according to System V AMD64 ABI
// Integer/pointer arguments: RDI, RSI, RDX, RCX, R8, R9
// Additional arguments: pushed onto stack in reverse order
static void marshal_arguments_x64(codegen_context_t* ctx, ir_value_t* args, int arg_count) {
    // System V AMD64 ABI integer argument registers
    const x64_register_t arg_regs[6] = {
        X64_RDI, X64_RSI, X64_RDX, X64_RCX, X64_R8, X64_R9
    };

    // First pass: marshal arguments into registers (first 6 arguments)
    int reg_arg_count = (arg_count < 6) ? arg_count : 6;
    for (int i = 0; i < reg_arg_count; i++) {
        ir_value_t arg = args[i];
        x64_register_t dest_reg = arg_regs[i];

        if (arg.type == IR_VAL_REGISTER) {
            // Get physical register for virtual register
            x64_register_t src_reg = get_physical_register(ctx, arg);
            if (src_reg != X64_NO_REG && src_reg != dest_reg) {
                // Move from source register to argument register
                x64_mov_reg_reg(ctx->asm_, dest_reg, src_reg);
            }
        } else if (arg.type == IR_VAL_CONSTANT) {
            // Load constant into argument register
            value_t val = arg.as.constant;
            if (IS_NUMBER(val)) {
                int64_t num = (int64_t)AS_NUMBER(val);
                x64_mov_reg_imm64(ctx->asm_, dest_reg, num);
            } else if (IS_BOOL(val)) {
                int64_t bool_val = AS_BOOL(val) ? 1 : 0;
                x64_mov_reg_imm64(ctx->asm_, dest_reg, bool_val);
            } else if (IS_NIL(val)) {
                x64_xor_reg_reg(ctx->asm_, dest_reg, dest_reg); // Zero register
            }
        }
    }

    // Second pass: push stack arguments (arguments 7+) in reverse order
    // This ensures they appear in correct order on stack
    for (int i = arg_count - 1; i >= 6; i--) {
        ir_value_t arg = args[i];

        if (arg.type == IR_VAL_REGISTER) {
            x64_register_t src_reg = get_physical_register(ctx, arg);
            if (src_reg != X64_NO_REG) {
                x64_push_reg(ctx->asm_, src_reg);
            }
        } else if (arg.type == IR_VAL_CONSTANT) {
            // Load constant into RAX and push
            value_t val = arg.as.constant;
            if (IS_NUMBER(val)) {
                int64_t num = (int64_t)AS_NUMBER(val);
                x64_mov_reg_imm64(ctx->asm_, X64_RAX, num);
            } else if (IS_BOOL(val)) {
                int64_t bool_val = AS_BOOL(val) ? 1 : 0;
                x64_mov_reg_imm64(ctx->asm_, X64_RAX, bool_val);
            } else if (IS_NIL(val)) {
                x64_xor_reg_reg(ctx->asm_, X64_RAX, X64_RAX);
            }
            x64_push_reg(ctx->asm_, X64_RAX);
        }
    }
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
            // Phase 3: Proper calling convention implementation
            // 1. Marshal arguments to correct registers/stack
            if (instr->call_args && instr->call_arg_count > 0) {
                marshal_arguments_x64(ctx, instr->call_args, instr->call_arg_count);
            }

            // 2. Stack is already 16-byte aligned from prologue (Phase 2)
            // Additional stack arguments (7+) maintain alignment since we push in pairs

            // 3. Emit call instruction with placeholder offset
            size_t call_offset = x64_get_offset(ctx->asm_);
            x64_call_rel32(ctx->asm_, 0); // Placeholder - will be relocated by linker

            // 4. Record relocation or direct call patch
            if (instr->call_function) {
                add_call_patch(ctx, call_offset, instr->call_function);
            } else if (instr->call_target) {
                // For PLT32 relocations, addend is -4 (size of offset field)
                add_relocation(ctx, call_offset + 1, instr->call_target, R_X86_64_PLT32, -4);
            }

            // 5. Clean up stack arguments if any (7+ arguments)
            int stack_arg_count = (instr->call_arg_count > 6) ? (instr->call_arg_count - 6) : 0;
            if (stack_arg_count > 0) {
                int cleanup_bytes = stack_arg_count * 8;
                x64_add_reg_imm(ctx->asm_, X64_RSP, cleanup_bytes);
            }

            // 6. Handle return value in RAX
            if (instr->dest.type == IR_VAL_REGISTER) {
                x64_register_t dest = get_physical_register(ctx, instr->dest);
                if (dest != X64_NO_REG && dest != X64_RAX) {
                    // Move return value from RAX to destination register
                    x64_mov_reg_reg(ctx->asm_, dest, X64_RAX);
                }
            }
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
            // Print a value: call sox_native_print(value)
            // System V AMD64 ABI: First argument goes in RDI

            // 1. Move value to RDI (argument register)
            x64_register_t src_reg = get_physical_register(ctx, instr->operand1);
            if (src_reg != X64_NO_REG && src_reg != X64_RDI) {
                x64_mov_reg_reg(ctx->asm_, X64_RDI, src_reg);
            }

            // 2. Call sox_native_print with relocation
            size_t call_offset = x64_get_offset(ctx->asm_);
            x64_call_rel32(ctx->asm_, 0); // Placeholder - will be relocated by linker

            // 3. Record relocation for the linker
            // PLT32 relocation for branch call
            add_relocation(ctx, call_offset + 1, "sox_native_print", R_X86_64_PLT32, -4);

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
    func->code_offset = x64_get_offset(ctx->asm_);
    ctx->patch_count = 0;
    ctx->label_count = 0;
    if (ctx->label_offsets) {
        for (int i = 0; i < ctx->label_capacity; i++) {
            ctx->label_offsets[i] = -1;
        }
    }

    // Perform register allocation
    ctx->regalloc = regalloc_new(func);
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
            // Validate offset fits in int32_t range
            if (offset > (size_t)(INT32_MAX - 4)) {
                fprintf(stderr, "ERROR: Jump patch offset too large: %zu\n", offset);
                return false;
            }

            int32_t rel = ctx->label_offsets[target_label] - ((int32_t)offset + 4);
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

    for (int i = 0; i < ctx->call_patch_count; i++) {
        call_patch_t* patch = &ctx->call_patches[i];
        if (!patch->target) {
            continue;
        }
        int64_t call_site = (int64_t)patch->offset;
        int64_t target = (int64_t)patch->target->code_offset;
        int64_t rel = target - (call_site + 5);
        ctx->asm_->code.code[call_site + 1] = rel & 0xFF;
        ctx->asm_->code.code[call_site + 2] = (rel >> 8) & 0xFF;
        ctx->asm_->code.code[call_site + 3] = (rel >> 16) & 0xFF;
        ctx->asm_->code.code[call_site + 4] = (rel >> 24) & 0xFF;
    }

    return true;
}

uint8_t* codegen_get_code(codegen_context_t* ctx, size_t* size) {
    *size = ctx->asm_->code.size;
    return ctx->asm_->code.code;
}

codegen_relocation_t* codegen_get_relocations(codegen_context_t* ctx, int* count) {
    *count = ctx->relocation_count;
    return ctx->relocations;
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

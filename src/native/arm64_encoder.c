#include "arm64_encoder.h"
#include "../lib/memory.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

arm64_assembler_t* arm64_assembler_new(void) {
    arm64_assembler_t* asm_ = (arm64_assembler_t*)l_mem_alloc(sizeof(arm64_assembler_t));
    asm_->code.code = NULL;
    asm_->code.size = 0;
    asm_->code.capacity = 0;
    asm_->relocations = NULL;
    asm_->reloc_count = 0;
    asm_->reloc_capacity = 0;
    return asm_;
}

void arm64_assembler_free(arm64_assembler_t* asm_) {
    if (!asm_) return;
    if (asm_->code.code) {
        l_mem_free(asm_->code.code, asm_->code.capacity * sizeof(uint32_t));
    }
    if (asm_->relocations) {
        // Free symbol strings that were copied in arm64_add_relocation
        for (size_t i = 0; i < asm_->reloc_count; i++) {
            if (asm_->relocations[i].symbol) {
                size_t symbol_len = strlen(asm_->relocations[i].symbol) + 1;
                l_mem_free(asm_->relocations[i].symbol, symbol_len);
            }
        }
        l_mem_free(asm_->relocations, sizeof(arm64_relocation_t) * asm_->reloc_capacity);
    }
    l_mem_free(asm_, sizeof(arm64_assembler_t));
}

size_t arm64_get_offset(arm64_assembler_t* asm_) {
    return asm_->code.size;
}

void arm64_add_relocation(arm64_assembler_t* asm_, size_t offset,
                          arm64_reloc_type_t type, const char* symbol, int64_t addend) {
    if (asm_->reloc_capacity < asm_->reloc_count + 1) {
        size_t old_capacity = asm_->reloc_capacity;
        asm_->reloc_capacity = (old_capacity < 8) ? 8 : old_capacity * 2;
        asm_->relocations = (arm64_relocation_t*)l_mem_realloc(
            asm_->relocations,
            sizeof(arm64_relocation_t) * old_capacity,
            sizeof(arm64_relocation_t) * asm_->reloc_capacity
        );
    }

    asm_->relocations[asm_->reloc_count].offset = offset;
    asm_->relocations[asm_->reloc_count].type = type;

    // Copy symbol string to ensure it remains valid throughout object file generation
    // The original pointer may reference temporary memory that gets freed later
    size_t symbol_len = strlen(symbol) + 1;
    char* symbol_copy = (char*)l_mem_alloc(symbol_len);
    if (symbol_copy) {
        memcpy(symbol_copy, symbol, symbol_len);
        asm_->relocations[asm_->reloc_count].symbol = symbol_copy;
    } else {
        // Fallback to original pointer if allocation fails (degrades safely)
        asm_->relocations[asm_->reloc_count].symbol = (char*)symbol;
    }

    asm_->relocations[asm_->reloc_count].addend = addend;
    asm_->reloc_count++;
}

void arm64_emit(arm64_assembler_t* asm_, uint32_t instruction) {
    if (asm_->code.capacity < asm_->code.size + 1) {
        size_t old_capacity = asm_->code.capacity;
        asm_->code.capacity = (old_capacity < 64) ? 64 : old_capacity * 2;
        asm_->code.code = (uint32_t*)l_mem_realloc(
            asm_->code.code,
            old_capacity * sizeof(uint32_t),
            asm_->code.capacity * sizeof(uint32_t)
        );
    }
    asm_->code.code[asm_->code.size++] = instruction;
}

// Data movement
void arm64_mov_reg_reg(arm64_assembler_t* asm_, arm64_register_t dst, arm64_register_t src) {
    if (dst == src) return; // Optimize out self-moves

    // When SP (register 31) is involved, we must use ADD Xd, Xn, #0
    // instead of ORR, because in ORR context, register 31 means XZR, not SP
    if (src == ARM64_SP || dst == ARM64_SP) {
        // MOV dst, src is encoded as ADD dst, src, #0
        // Format: sf=1 (bit 31) | op=00 (bits 30:29) | S=0 (bit 28) | op2=010 (bits 27:24) |
        //         sh=00 (bits 23:22) | imm12=0 (bits 21:10) | Rn (bits 9:5) | Rd (bits 4:0)
        uint32_t instr = 0x91000000 | (src << 5) | dst;
        arm64_emit(asm_, instr);
    } else {
        // MOV (register) is an alias for ORR Xd, XZR, Xm
        uint32_t instr = 0xAA0003E0 | (src << 16) | dst;
        arm64_emit(asm_, instr);
    }
}

void arm64_movz(arm64_assembler_t* asm_, arm64_register_t dst, uint16_t imm, uint8_t shift) {
    // MOVZ Xd, #imm, LSL #shift
    // sf=1 (64-bit), opc=10 (MOVZ), hw=shift/16
    uint32_t hw = (shift / 16) & 0x3;
    uint32_t instr = 0xD2800000 | (hw << 21) | (imm << 5) | dst;
    arm64_emit(asm_, instr);
}

void arm64_movk(arm64_assembler_t* asm_, arm64_register_t dst, uint16_t imm, uint8_t shift) {
    // MOVK Xd, #imm, LSL #shift
    uint32_t hw = (shift / 16) & 0x3;
    uint32_t instr = 0xF2800000 | (hw << 21) | (imm << 5) | dst;
    arm64_emit(asm_, instr);
}

void arm64_mov_reg_imm(arm64_assembler_t* asm_, arm64_register_t dst, uint64_t imm) {
    // Build 64-bit immediate using MOVZ and MOVK
    arm64_movz(asm_, dst, imm & 0xFFFF, 0);
    if (imm > 0xFFFF) {
        arm64_movk(asm_, dst, (imm >> 16) & 0xFFFF, 16);
    }
    if (imm > 0xFFFFFFFF) {
        arm64_movk(asm_, dst, (imm >> 32) & 0xFFFF, 32);
    }
    if (imm > 0xFFFFFFFFFFFF) {
        arm64_movk(asm_, dst, (imm >> 48) & 0xFFFF, 48);
    }
}

void arm64_ldr_reg_reg_offset(arm64_assembler_t* asm_, arm64_register_t dst,
                               arm64_register_t base, int32_t offset) {
    // LDR Xt, [Xn, #offset]
    // Offset must be aligned to 8 bytes and divided by 8
    uint32_t imm12 = (offset / 8) & 0xFFF;
    uint32_t instr = 0xF9400000 | (imm12 << 10) | (base << 5) | dst;
    arm64_emit(asm_, instr);
}

void arm64_str_reg_reg_offset(arm64_assembler_t* asm_, arm64_register_t src,
                               arm64_register_t base, int32_t offset) {
    // STR Xt, [Xn, #offset]
    uint32_t imm12 = (offset / 8) & 0xFFF;
    uint32_t instr = 0xF9000000 | (imm12 << 10) | (base << 5) | src;
    arm64_emit(asm_, instr);
}

// Arithmetic instructions
void arm64_add_reg_reg_reg(arm64_assembler_t* asm_, arm64_register_t dst,
                            arm64_register_t src1, arm64_register_t src2) {
    // ADD Xd, Xn, Xm
    uint32_t instr = 0x8B000000 | (src2 << 16) | (src1 << 5) | dst;
    arm64_emit(asm_, instr);
}

void arm64_add_reg_reg_imm(arm64_assembler_t* asm_, arm64_register_t dst,
                            arm64_register_t src, uint16_t imm) {
    // ADD Xd, Xn, #imm
    uint32_t instr = 0x91000000 | ((imm & 0xFFF) << 10) | (src << 5) | dst;
    arm64_emit(asm_, instr);
}

void arm64_sub_reg_reg_reg(arm64_assembler_t* asm_, arm64_register_t dst,
                            arm64_register_t src1, arm64_register_t src2) {
    // SUB Xd, Xn, Xm
    uint32_t instr = 0xCB000000 | (src2 << 16) | (src1 << 5) | dst;
    arm64_emit(asm_, instr);
}

void arm64_sub_reg_reg_imm(arm64_assembler_t* asm_, arm64_register_t dst,
                            arm64_register_t src, uint16_t imm) {
    // SUB Xd, Xn, #imm
    uint32_t instr = 0xD1000000 | ((imm & 0xFFF) << 10) | (src << 5) | dst;
    arm64_emit(asm_, instr);
}

void arm64_mul_reg_reg_reg(arm64_assembler_t* asm_, arm64_register_t dst,
                            arm64_register_t src1, arm64_register_t src2) {
    // MADD Xd, Xn, Xm, XZR (multiply and add, with XZR = 0)
    uint32_t instr = 0x9B007C00 | (src2 << 16) | (31 << 10) | (src1 << 5) | dst;
    arm64_emit(asm_, instr);
}

void arm64_sdiv_reg_reg_reg(arm64_assembler_t* asm_, arm64_register_t dst,
                             arm64_register_t src1, arm64_register_t src2) {
    // SDIV Xd, Xn, Xm
    uint32_t instr = 0x9AC00C00 | (src2 << 16) | (src1 << 5) | dst;
    arm64_emit(asm_, instr);
}

void arm64_neg_reg_reg(arm64_assembler_t* asm_, arm64_register_t dst, arm64_register_t src) {
    // NEG Xd, Xm (alias for SUB Xd, XZR, Xm)
    uint32_t instr = 0xCB0003E0 | (src << 16) | dst;
    arm64_emit(asm_, instr);
}

// Logical instructions
void arm64_and_reg_reg_reg(arm64_assembler_t* asm_, arm64_register_t dst,
                            arm64_register_t src1, arm64_register_t src2) {
    // AND Xd, Xn, Xm
    uint32_t instr = 0x8A000000 | (src2 << 16) | (src1 << 5) | dst;
    arm64_emit(asm_, instr);
}

void arm64_orr_reg_reg_reg(arm64_assembler_t* asm_, arm64_register_t dst,
                            arm64_register_t src1, arm64_register_t src2) {
    // ORR Xd, Xn, Xm
    uint32_t instr = 0xAA000000 | (src2 << 16) | (src1 << 5) | dst;
    arm64_emit(asm_, instr);
}

void arm64_eor_reg_reg_reg(arm64_assembler_t* asm_, arm64_register_t dst,
                            arm64_register_t src1, arm64_register_t src2) {
    // EOR Xd, Xn, Xm
    uint32_t instr = 0xCA000000 | (src2 << 16) | (src1 << 5) | dst;
    arm64_emit(asm_, instr);
}

void arm64_mvn_reg_reg(arm64_assembler_t* asm_, arm64_register_t dst, arm64_register_t src) {
    // MVN Xd, Xm (alias for ORN Xd, XZR, Xm)
    uint32_t instr = 0xAA2003E0 | (src << 16) | dst;
    arm64_emit(asm_, instr);
}

void arm64_lsl_reg_reg_imm(arm64_assembler_t* asm_, arm64_register_t dst,
                            arm64_register_t src, uint8_t shift) {
    // LSL Xd, Xn, #shift (alias for UBFM)
    uint32_t instr = 0xD3400000 | ((63 - shift) << 16) | ((63 - shift) << 10) | (src << 5) | dst;
    arm64_emit(asm_, instr);
}

void arm64_lsr_reg_reg_imm(arm64_assembler_t* asm_, arm64_register_t dst,
                            arm64_register_t src, uint8_t shift) {
    // LSR Xd, Xn, #shift (alias for UBFM)
    uint32_t instr = 0xD340FC00 | (shift << 16) | (src << 5) | dst;
    arm64_emit(asm_, instr);
}

// Comparison
void arm64_cmp_reg_reg(arm64_assembler_t* asm_, arm64_register_t src1, arm64_register_t src2) {
    // CMP Xn, Xm (alias for SUBS XZR, Xn, Xm)
    uint32_t instr = 0xEB00001F | (src2 << 16) | (src1 << 5);
    arm64_emit(asm_, instr);
}

void arm64_cmp_reg_imm(arm64_assembler_t* asm_, arm64_register_t src, uint16_t imm) {
    // CMP Xn, #imm (alias for SUBS XZR, Xn, #imm)
    uint32_t instr = 0xF100001F | ((imm & 0xFFF) << 10) | (src << 5);
    arm64_emit(asm_, instr);
}

void arm64_tst_reg_reg(arm64_assembler_t* asm_, arm64_register_t src1, arm64_register_t src2) {
    // TST Xn, Xm (alias for ANDS XZR, Xn, Xm)
    uint32_t instr = 0xEA00001F | (src2 << 16) | (src1 << 5);
    arm64_emit(asm_, instr);
}

// Conditional select
void arm64_csel(arm64_assembler_t* asm_, arm64_register_t dst, arm64_register_t src1,
                arm64_register_t src2, arm64_condition_t cond) {
    // CSEL Xd, Xn, Xm, cond
    uint32_t instr = 0x9A800000 | (src2 << 16) | (cond << 12) | (src1 << 5) | dst;
    arm64_emit(asm_, instr);
}

void arm64_cset(arm64_assembler_t* asm_, arm64_register_t dst, arm64_condition_t cond) {
    // CSET Xd, cond (alias for CSINC Xd, XZR, XZR, invert(cond))
    uint32_t inv_cond = cond ^ 1; // Invert condition
    uint32_t instr = 0x9A9F07E0 | (inv_cond << 12) | dst;
    arm64_emit(asm_, instr);
}

// Stack operations
void arm64_stp(arm64_assembler_t* asm_, arm64_register_t reg1, arm64_register_t reg2,
               arm64_register_t base, int32_t offset) {
    // STP Xt1, Xt2, [Xn, #offset]
    // Offset must be 8-byte aligned and divided by 8
    int32_t imm7 = (offset / 8) & 0x7F;
    uint32_t instr = 0xA9000000 | (imm7 << 15) | (reg2 << 10) | (base << 5) | reg1;
    arm64_emit(asm_, instr);
}

void arm64_ldp(arm64_assembler_t* asm_, arm64_register_t reg1, arm64_register_t reg2,
               arm64_register_t base, int32_t offset) {
    // LDP Xt1, Xt2, [Xn, #offset]
    int32_t imm7 = (offset / 8) & 0x7F;
    uint32_t instr = 0xA9400000 | (imm7 << 15) | (reg2 << 10) | (base << 5) | reg1;
    arm64_emit(asm_, instr);
}

// Control flow
void arm64_b(arm64_assembler_t* asm_, int32_t offset) {
    // B #offset (26-bit signed offset in instructions)
    uint32_t imm26 = (offset >> 2) & 0x3FFFFFF;
    uint32_t instr = 0x14000000 | imm26;
    arm64_emit(asm_, instr);
}

void arm64_b_cond(arm64_assembler_t* asm_, arm64_condition_t cond, int32_t offset) {
    // B.cond #offset (19-bit signed offset in instructions)
    uint32_t imm19 = (offset >> 2) & 0x7FFFF;
    uint32_t instr = 0x54000000 | (imm19 << 5) | cond;
    arm64_emit(asm_, instr);
}

void arm64_bl(arm64_assembler_t* asm_, int32_t offset) {
    // BL #offset
    uint32_t imm26 = (offset >> 2) & 0x3FFFFFF;
    uint32_t instr = 0x94000000 | imm26;
    arm64_emit(asm_, instr);
}

void arm64_br(arm64_assembler_t* asm_, arm64_register_t reg) {
    // BR Xn
    uint32_t instr = 0xD61F0000 | (reg << 5);
    arm64_emit(asm_, instr);
}

void arm64_blr(arm64_assembler_t* asm_, arm64_register_t reg) {
    // BLR Xn
    uint32_t instr = 0xD63F0000 | (reg << 5);
    arm64_emit(asm_, instr);
}

void arm64_ret(arm64_assembler_t* asm_, arm64_register_t reg) {
    // RET Xn (default is X30/LR)
    uint32_t instr = 0xD65F0000 | (reg << 5);
    arm64_emit(asm_, instr);
}

// Floating point (scalar double precision)
void arm64_fmov_vreg_vreg(arm64_assembler_t* asm_, arm64_vreg_t dst, arm64_vreg_t src) {
    // FMOV Dd, Dn
    uint32_t instr = 0x1E604000 | (src << 5) | dst;
    arm64_emit(asm_, instr);
}

void arm64_fadd_vreg_vreg_vreg(arm64_assembler_t* asm_, arm64_vreg_t dst,
                                arm64_vreg_t src1, arm64_vreg_t src2) {
    // FADD Dd, Dn, Dm
    uint32_t instr = 0x1E602800 | (src2 << 16) | (src1 << 5) | dst;
    arm64_emit(asm_, instr);
}

void arm64_fsub_vreg_vreg_vreg(arm64_assembler_t* asm_, arm64_vreg_t dst,
                                arm64_vreg_t src1, arm64_vreg_t src2) {
    // FSUB Dd, Dn, Dm
    uint32_t instr = 0x1E603800 | (src2 << 16) | (src1 << 5) | dst;
    arm64_emit(asm_, instr);
}

void arm64_fmul_vreg_vreg_vreg(arm64_assembler_t* asm_, arm64_vreg_t dst,
                                arm64_vreg_t src1, arm64_vreg_t src2) {
    // FMUL Dd, Dn, Dm
    uint32_t instr = 0x1E600800 | (src2 << 16) | (src1 << 5) | dst;
    arm64_emit(asm_, instr);
}

void arm64_fdiv_vreg_vreg_vreg(arm64_assembler_t* asm_, arm64_vreg_t dst,
                                arm64_vreg_t src1, arm64_vreg_t src2) {
    // FDIV Dd, Dn, Dm
    uint32_t instr = 0x1E601800 | (src2 << 16) | (src1 << 5) | dst;
    arm64_emit(asm_, instr);
}

// Conversion
void arm64_scvtf(arm64_assembler_t* asm_, arm64_vreg_t dst, arm64_register_t src) {
    // SCVTF Dd, Xn (signed integer to double)
    uint32_t instr = 0x9E620000 | (src << 5) | dst;
    arm64_emit(asm_, instr);
}

void arm64_fcvtzs(arm64_assembler_t* asm_, arm64_register_t dst, arm64_vreg_t src) {
    // FCVTZS Xd, Dn (double to signed integer, round toward zero)
    uint32_t instr = 0x9E780000 | (src << 5) | dst;
    arm64_emit(asm_, instr);
}

// PC-relative address loading
void arm64_adrp(arm64_assembler_t* asm_, arm64_register_t dst, int32_t page_offset) {
    // ADRP Xd, label
    // Loads the page address of a label (4KB-aligned)
    // Encoding: op=1 | immlo (bits 30:29) | 10000 (bits 28:24) | immhi (bits 23:5) | Rd (bits 4:0)
    // Note: page_offset is typically 0 when used with relocations, as the linker fills in the actual value
    uint32_t imm = (uint32_t)page_offset & 0x1FFFFF; // 21-bit immediate
    uint32_t immlo = (imm >> 0) & 0x3;
    uint32_t immhi = (imm >> 2) & 0x7FFFF;
    uint32_t instr = 0x90000000 | (immlo << 29) | (immhi << 5) | dst;
    arm64_emit(asm_, instr);
}

// Utility functions
const char* arm64_register_name(arm64_register_t reg) {
    static const char* names[] = {
        "x0", "x1", "x2", "x3", "x4", "x5", "x6", "x7",
        "x8", "x9", "x10", "x11", "x12", "x13", "x14", "x15",
        "x16", "x17", "x18", "x19", "x20", "x21", "x22", "x23",
        "x24", "x25", "x26", "x27", "x28", "x29(fp)", "x30(lr)", "sp"
    };
    if (reg >= 0 && reg < ARM64_REG_COUNT) {
        return names[reg];
    }
    return "unknown";
}

const char* arm64_vreg_name(arm64_vreg_t reg) {
    static const char* names[] = {
        "v0", "v1", "v2", "v3", "v4", "v5", "v6", "v7",
        "v8", "v9", "v10", "v11", "v12", "v13", "v14", "v15",
        "v16", "v17", "v18", "v19", "v20", "v21", "v22", "v23",
        "v24", "v25", "v26", "v27", "v28", "v29", "v30", "v31"
    };
    if (reg >= 0 && reg < ARM64_VREG_COUNT) {
        return names[reg];
    }
    return "unknown";
}

uint8_t* arm64_get_code(arm64_assembler_t* asm_, size_t* size) {
    *size = asm_->code.size * sizeof(uint32_t);
    return (uint8_t*)asm_->code.code;
}

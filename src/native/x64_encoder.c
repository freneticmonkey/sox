#include "x64_encoder.h"
#include "../lib/memory.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// REX prefix bits
#define REX_W 0x48  // 64-bit operand size
#define REX_R 0x44  // Extension of ModR/M reg field
#define REX_X 0x42  // Extension of SIB index field
#define REX_B 0x41  // Extension of ModR/M r/m field, SIB base, or opcode reg

// ModR/M encoding helpers
#define MODRM(mod, reg, rm) ((uint8_t)(((mod) << 6) | ((reg) << 3) | (rm)))
#define SIB(scale, index, base) ((uint8_t)(((scale) << 6) | ((index) << 3) | (base)))

x64_assembler_t* x64_assembler_new(void) {
    x64_assembler_t* asm_ = (x64_assembler_t*)l_mem_alloc(sizeof(x64_assembler_t));
    asm_->code.code = NULL;
    asm_->code.size = 0;
    asm_->code.capacity = 0;
    asm_->relocations = NULL;
    asm_->reloc_count = 0;
    asm_->reloc_capacity = 0;
    return asm_;
}

void x64_assembler_free(x64_assembler_t* asm_) {
    if (!asm_) return;
    if (asm_->code.code) {
        l_mem_free(asm_->code.code, asm_->code.capacity);
    }
    if (asm_->relocations) {
        l_mem_free(asm_->relocations, sizeof(x64_relocation_t) * asm_->reloc_capacity);
    }
    l_mem_free(asm_, sizeof(x64_assembler_t));
}

size_t x64_get_offset(x64_assembler_t* asm_) {
    return asm_->code.size;
}

void x64_add_relocation(x64_assembler_t* asm_, size_t offset,
                        x64_reloc_type_t type, const char* symbol, int64_t addend) {
    if (asm_->reloc_capacity < asm_->reloc_count + 1) {
        size_t old_capacity = asm_->reloc_capacity;
        asm_->reloc_capacity = (old_capacity < 8) ? 8 : old_capacity * 2;
        asm_->relocations = (x64_relocation_t*)l_mem_realloc(
            asm_->relocations,
            sizeof(x64_relocation_t) * old_capacity,
            sizeof(x64_relocation_t) * asm_->reloc_capacity
        );
    }

    asm_->relocations[asm_->reloc_count].offset = offset;
    asm_->relocations[asm_->reloc_count].type = type;
    asm_->relocations[asm_->reloc_count].symbol = symbol;
    asm_->relocations[asm_->reloc_count].addend = addend;
    asm_->reloc_count++;
}

void x64_emit_byte(x64_assembler_t* asm_, uint8_t byte) {
    if (asm_->code.capacity < asm_->code.size + 1) {
        size_t old_capacity = asm_->code.capacity;
        asm_->code.capacity = (old_capacity < 256) ? 256 : old_capacity * 2;
        asm_->code.code = (uint8_t*)l_mem_realloc(
            asm_->code.code,
            old_capacity,
            asm_->code.capacity
        );
    }
    asm_->code.code[asm_->code.size++] = byte;
}

void x64_emit_word(x64_assembler_t* asm_, uint16_t word) {
    x64_emit_byte(asm_, word & 0xFF);
    x64_emit_byte(asm_, (word >> 8) & 0xFF);
}

void x64_emit_dword(x64_assembler_t* asm_, uint32_t dword) {
    x64_emit_byte(asm_, dword & 0xFF);
    x64_emit_byte(asm_, (dword >> 8) & 0xFF);
    x64_emit_byte(asm_, (dword >> 16) & 0xFF);
    x64_emit_byte(asm_, (dword >> 24) & 0xFF);
}

void x64_emit_qword(x64_assembler_t* asm_, uint64_t qword) {
    x64_emit_dword(asm_, qword & 0xFFFFFFFF);
    x64_emit_dword(asm_, (qword >> 32) & 0xFFFFFFFF);
}

bool x64_needs_rex(x64_register_t reg) {
    return reg >= X64_R8;
}

static uint8_t x64_rex_for_regs(x64_register_t dst, x64_register_t src) {
    uint8_t rex = REX_W;
    if (dst >= X64_R8) rex |= REX_B;
    if (src >= X64_R8) rex |= REX_R;
    return rex;
}

// MOV instructions
void x64_mov_reg_reg(x64_assembler_t* asm_, x64_register_t dst, x64_register_t src) {
    if (dst == src) return; // Optimize out self-moves

    uint8_t rex = x64_rex_for_regs(dst, src);
    x64_emit_byte(asm_, rex);
    x64_emit_byte(asm_, 0x89); // MOV r/m64, r64
    x64_emit_byte(asm_, MODRM(3, src & 7, dst & 7));
}

void x64_mov_reg_imm64(x64_assembler_t* asm_, x64_register_t dst, int64_t imm) {
    uint8_t rex = REX_W;
    if (dst >= X64_R8) rex |= REX_B;
    x64_emit_byte(asm_, rex);
    x64_emit_byte(asm_, 0xB8 + (dst & 7)); // MOV r64, imm64
    x64_emit_qword(asm_, (uint64_t)imm);
}

void x64_mov_reg_imm32(x64_assembler_t* asm_, x64_register_t dst, int32_t imm) {
    uint8_t rex = REX_W;
    if (dst >= X64_R8) rex |= REX_B;
    x64_emit_byte(asm_, rex);
    x64_emit_byte(asm_, 0xC7); // MOV r/m64, imm32 (sign-extended)
    x64_emit_byte(asm_, MODRM(3, 0, dst & 7));
    x64_emit_dword(asm_, (uint32_t)imm);
}

void x64_mov_reg_mem(x64_assembler_t* asm_, x64_register_t dst, x64_register_t base, int32_t disp) {
    uint8_t rex = x64_rex_for_regs(dst, base);
    x64_emit_byte(asm_, rex);
    x64_emit_byte(asm_, 0x8B); // MOV r64, r/m64

    if (disp == 0 && (base & 7) != X64_RBP) {
        x64_emit_byte(asm_, MODRM(0, dst & 7, base & 7));
    } else if (disp >= -128 && disp <= 127) {
        x64_emit_byte(asm_, MODRM(1, dst & 7, base & 7));
        x64_emit_byte(asm_, (uint8_t)disp);
    } else {
        x64_emit_byte(asm_, MODRM(2, dst & 7, base & 7));
        x64_emit_dword(asm_, (uint32_t)disp);
    }
}

void x64_mov_mem_reg(x64_assembler_t* asm_, x64_register_t base, int32_t disp, x64_register_t src) {
    uint8_t rex = x64_rex_for_regs(src, base);
    x64_emit_byte(asm_, rex);
    x64_emit_byte(asm_, 0x89); // MOV r/m64, r64

    if (disp == 0 && (base & 7) != X64_RBP) {
        x64_emit_byte(asm_, MODRM(0, src & 7, base & 7));
    } else if (disp >= -128 && disp <= 127) {
        x64_emit_byte(asm_, MODRM(1, src & 7, base & 7));
        x64_emit_byte(asm_, (uint8_t)disp);
    } else {
        x64_emit_byte(asm_, MODRM(2, src & 7, base & 7));
        x64_emit_dword(asm_, (uint32_t)disp);
    }
}

void x64_lea(x64_assembler_t* asm_, x64_register_t dst, x64_register_t base, int32_t disp) {
    uint8_t rex = x64_rex_for_regs(dst, base);
    x64_emit_byte(asm_, rex);
    x64_emit_byte(asm_, 0x8D); // LEA r64, m

    if (disp == 0 && (base & 7) != X64_RBP) {
        x64_emit_byte(asm_, MODRM(0, dst & 7, base & 7));
    } else if (disp >= -128 && disp <= 127) {
        x64_emit_byte(asm_, MODRM(1, dst & 7, base & 7));
        x64_emit_byte(asm_, (uint8_t)disp);
    } else {
        x64_emit_byte(asm_, MODRM(2, dst & 7, base & 7));
        x64_emit_dword(asm_, (uint32_t)disp);
    }
}

// Arithmetic instructions
void x64_add_reg_reg(x64_assembler_t* asm_, x64_register_t dst, x64_register_t src) {
    uint8_t rex = x64_rex_for_regs(dst, src);
    x64_emit_byte(asm_, rex);
    x64_emit_byte(asm_, 0x01); // ADD r/m64, r64
    x64_emit_byte(asm_, MODRM(3, src & 7, dst & 7));
}

void x64_add_reg_imm(x64_assembler_t* asm_, x64_register_t dst, int32_t imm) {
    uint8_t rex = REX_W;
    if (dst >= X64_R8) rex |= REX_B;
    x64_emit_byte(asm_, rex);

    if (imm >= -128 && imm <= 127) {
        x64_emit_byte(asm_, 0x83); // ADD r/m64, imm8
        x64_emit_byte(asm_, MODRM(3, 0, dst & 7));
        x64_emit_byte(asm_, (uint8_t)imm);
    } else {
        x64_emit_byte(asm_, 0x81); // ADD r/m64, imm32
        x64_emit_byte(asm_, MODRM(3, 0, dst & 7));
        x64_emit_dword(asm_, (uint32_t)imm);
    }
}

void x64_sub_reg_reg(x64_assembler_t* asm_, x64_register_t dst, x64_register_t src) {
    uint8_t rex = x64_rex_for_regs(dst, src);
    x64_emit_byte(asm_, rex);
    x64_emit_byte(asm_, 0x29); // SUB r/m64, r64
    x64_emit_byte(asm_, MODRM(3, src & 7, dst & 7));
}

void x64_sub_reg_imm(x64_assembler_t* asm_, x64_register_t dst, int32_t imm) {
    uint8_t rex = REX_W;
    if (dst >= X64_R8) rex |= REX_B;
    x64_emit_byte(asm_, rex);

    if (imm >= -128 && imm <= 127) {
        x64_emit_byte(asm_, 0x83); // SUB r/m64, imm8
        x64_emit_byte(asm_, MODRM(3, 5, dst & 7));
        x64_emit_byte(asm_, (uint8_t)imm);
    } else {
        x64_emit_byte(asm_, 0x81); // SUB r/m64, imm32
        x64_emit_byte(asm_, MODRM(3, 5, dst & 7));
        x64_emit_dword(asm_, (uint32_t)imm);
    }
}

void x64_imul_reg_reg(x64_assembler_t* asm_, x64_register_t dst, x64_register_t src) {
    uint8_t rex = x64_rex_for_regs(dst, src);
    x64_emit_byte(asm_, rex);
    x64_emit_byte(asm_, 0x0F); // Two-byte opcode
    x64_emit_byte(asm_, 0xAF); // IMUL r64, r/m64
    x64_emit_byte(asm_, MODRM(3, dst & 7, src & 7));
}

void x64_idiv_reg(x64_assembler_t* asm_, x64_register_t divisor) {
    uint8_t rex = REX_W;
    if (divisor >= X64_R8) rex |= REX_B;
    x64_emit_byte(asm_, rex);
    x64_emit_byte(asm_, 0xF7); // IDIV r/m64
    x64_emit_byte(asm_, MODRM(3, 7, divisor & 7));
}

void x64_neg_reg(x64_assembler_t* asm_, x64_register_t reg) {
    uint8_t rex = REX_W;
    if (reg >= X64_R8) rex |= REX_B;
    x64_emit_byte(asm_, rex);
    x64_emit_byte(asm_, 0xF7); // NEG r/m64
    x64_emit_byte(asm_, MODRM(3, 3, reg & 7));
}

// Logical instructions
void x64_and_reg_reg(x64_assembler_t* asm_, x64_register_t dst, x64_register_t src) {
    uint8_t rex = x64_rex_for_regs(dst, src);
    x64_emit_byte(asm_, rex);
    x64_emit_byte(asm_, 0x21); // AND r/m64, r64
    x64_emit_byte(asm_, MODRM(3, src & 7, dst & 7));
}

void x64_or_reg_reg(x64_assembler_t* asm_, x64_register_t dst, x64_register_t src) {
    uint8_t rex = x64_rex_for_regs(dst, src);
    x64_emit_byte(asm_, rex);
    x64_emit_byte(asm_, 0x09); // OR r/m64, r64
    x64_emit_byte(asm_, MODRM(3, src & 7, dst & 7));
}

void x64_xor_reg_reg(x64_assembler_t* asm_, x64_register_t dst, x64_register_t src) {
    uint8_t rex = x64_rex_for_regs(dst, src);
    x64_emit_byte(asm_, rex);
    x64_emit_byte(asm_, 0x31); // XOR r/m64, r64
    x64_emit_byte(asm_, MODRM(3, src & 7, dst & 7));
}

void x64_not_reg(x64_assembler_t* asm_, x64_register_t reg) {
    uint8_t rex = REX_W;
    if (reg >= X64_R8) rex |= REX_B;
    x64_emit_byte(asm_, rex);
    x64_emit_byte(asm_, 0xF7); // NOT r/m64
    x64_emit_byte(asm_, MODRM(3, 2, reg & 7));
}

void x64_shl_reg_imm(x64_assembler_t* asm_, x64_register_t reg, uint8_t imm) {
    uint8_t rex = REX_W;
    if (reg >= X64_R8) rex |= REX_B;
    x64_emit_byte(asm_, rex);
    x64_emit_byte(asm_, 0xC1); // SHL r/m64, imm8
    x64_emit_byte(asm_, MODRM(3, 4, reg & 7));
    x64_emit_byte(asm_, imm);
}

void x64_shr_reg_imm(x64_assembler_t* asm_, x64_register_t reg, uint8_t imm) {
    uint8_t rex = REX_W;
    if (reg >= X64_R8) rex |= REX_B;
    x64_emit_byte(asm_, rex);
    x64_emit_byte(asm_, 0xC1); // SHR r/m64, imm8
    x64_emit_byte(asm_, MODRM(3, 5, reg & 7));
    x64_emit_byte(asm_, imm);
}

// Comparison instructions
void x64_cmp_reg_reg(x64_assembler_t* asm_, x64_register_t left, x64_register_t right) {
    uint8_t rex = x64_rex_for_regs(left, right);
    x64_emit_byte(asm_, rex);
    x64_emit_byte(asm_, 0x39); // CMP r/m64, r64
    x64_emit_byte(asm_, MODRM(3, right & 7, left & 7));
}

void x64_cmp_reg_imm(x64_assembler_t* asm_, x64_register_t reg, int32_t imm) {
    uint8_t rex = REX_W;
    if (reg >= X64_R8) rex |= REX_B;
    x64_emit_byte(asm_, rex);

    if (imm >= -128 && imm <= 127) {
        x64_emit_byte(asm_, 0x83); // CMP r/m64, imm8
        x64_emit_byte(asm_, MODRM(3, 7, reg & 7));
        x64_emit_byte(asm_, (uint8_t)imm);
    } else {
        x64_emit_byte(asm_, 0x81); // CMP r/m64, imm32
        x64_emit_byte(asm_, MODRM(3, 7, reg & 7));
        x64_emit_dword(asm_, (uint32_t)imm);
    }
}

void x64_test_reg_reg(x64_assembler_t* asm_, x64_register_t left, x64_register_t right) {
    uint8_t rex = x64_rex_for_regs(left, right);
    x64_emit_byte(asm_, rex);
    x64_emit_byte(asm_, 0x85); // TEST r/m64, r64
    x64_emit_byte(asm_, MODRM(3, right & 7, left & 7));
}

// Conditional set
void x64_setcc(x64_assembler_t* asm_, x64_condition_t cond, x64_register_t dst) {
    uint8_t rex = 0x40;
    if (dst >= X64_R8) rex |= REX_B;
    if (rex != 0x40) x64_emit_byte(asm_, rex);

    x64_emit_byte(asm_, 0x0F);
    x64_emit_byte(asm_, 0x90 + cond); // SETcc r/m8
    x64_emit_byte(asm_, MODRM(3, 0, dst & 7));
}

// Stack operations
void x64_push_reg(x64_assembler_t* asm_, x64_register_t reg) {
    if (reg >= X64_R8) {
        x64_emit_byte(asm_, REX_B);
    }
    x64_emit_byte(asm_, 0x50 + (reg & 7)); // PUSH r64
}

void x64_pop_reg(x64_assembler_t* asm_, x64_register_t reg) {
    if (reg >= X64_R8) {
        x64_emit_byte(asm_, REX_B);
    }
    x64_emit_byte(asm_, 0x58 + (reg & 7)); // POP r64
}

// Control flow
void x64_jmp_rel32(x64_assembler_t* asm_, int32_t offset) {
    x64_emit_byte(asm_, 0xE9); // JMP rel32
    x64_emit_dword(asm_, (uint32_t)offset);
}

void x64_jcc_rel32(x64_assembler_t* asm_, x64_condition_t cond, int32_t offset) {
    x64_emit_byte(asm_, 0x0F);
    x64_emit_byte(asm_, 0x80 + cond); // Jcc rel32
    x64_emit_dword(asm_, (uint32_t)offset);
}

void x64_call_rel32(x64_assembler_t* asm_, int32_t offset) {
    x64_emit_byte(asm_, 0xE8); // CALL rel32
    x64_emit_dword(asm_, (uint32_t)offset);
}

void x64_call_reg(x64_assembler_t* asm_, x64_register_t reg) {
    uint8_t rex = 0;
    if (reg >= X64_R8) rex = REX_B;
    if (rex) x64_emit_byte(asm_, rex);

    x64_emit_byte(asm_, 0xFF); // CALL r/m64
    x64_emit_byte(asm_, MODRM(3, 2, reg & 7));
}

void x64_ret(x64_assembler_t* asm_) {
    x64_emit_byte(asm_, 0xC3); // RET
}

// Floating point (SSE2)
void x64_movsd_xmm_xmm(x64_assembler_t* asm_, x64_xmm_register_t dst, x64_xmm_register_t src) {
    x64_emit_byte(asm_, 0xF2); // MOVSD prefix
    if (dst >= 8 || src >= 8) {
        uint8_t rex = 0x40;
        if (dst >= 8) rex |= REX_R;
        if (src >= 8) rex |= REX_B;
        x64_emit_byte(asm_, rex);
    }
    x64_emit_byte(asm_, 0x0F);
    x64_emit_byte(asm_, 0x10); // MOVSD xmm, xmm
    x64_emit_byte(asm_, MODRM(3, dst & 7, src & 7));
}

void x64_addsd_xmm_xmm(x64_assembler_t* asm_, x64_xmm_register_t dst, x64_xmm_register_t src) {
    x64_emit_byte(asm_, 0xF2);
    if (dst >= 8 || src >= 8) {
        uint8_t rex = 0x40;
        if (dst >= 8) rex |= REX_R;
        if (src >= 8) rex |= REX_B;
        x64_emit_byte(asm_, rex);
    }
    x64_emit_byte(asm_, 0x0F);
    x64_emit_byte(asm_, 0x58); // ADDSD
    x64_emit_byte(asm_, MODRM(3, dst & 7, src & 7));
}

void x64_subsd_xmm_xmm(x64_assembler_t* asm_, x64_xmm_register_t dst, x64_xmm_register_t src) {
    x64_emit_byte(asm_, 0xF2);
    if (dst >= 8 || src >= 8) {
        uint8_t rex = 0x40;
        if (dst >= 8) rex |= REX_R;
        if (src >= 8) rex |= REX_B;
        x64_emit_byte(asm_, rex);
    }
    x64_emit_byte(asm_, 0x0F);
    x64_emit_byte(asm_, 0x5C); // SUBSD
    x64_emit_byte(asm_, MODRM(3, dst & 7, src & 7));
}

void x64_mulsd_xmm_xmm(x64_assembler_t* asm_, x64_xmm_register_t dst, x64_xmm_register_t src) {
    x64_emit_byte(asm_, 0xF2);
    if (dst >= 8 || src >= 8) {
        uint8_t rex = 0x40;
        if (dst >= 8) rex |= REX_R;
        if (src >= 8) rex |= REX_B;
        x64_emit_byte(asm_, rex);
    }
    x64_emit_byte(asm_, 0x0F);
    x64_emit_byte(asm_, 0x59); // MULSD
    x64_emit_byte(asm_, MODRM(3, dst & 7, src & 7));
}

void x64_divsd_xmm_xmm(x64_assembler_t* asm_, x64_xmm_register_t dst, x64_xmm_register_t src) {
    x64_emit_byte(asm_, 0xF2);
    if (dst >= 8 || src >= 8) {
        uint8_t rex = 0x40;
        if (dst >= 8) rex |= REX_R;
        if (src >= 8) rex |= REX_B;
        x64_emit_byte(asm_, rex);
    }
    x64_emit_byte(asm_, 0x0F);
    x64_emit_byte(asm_, 0x5E); // DIVSD
    x64_emit_byte(asm_, MODRM(3, dst & 7, src & 7));
}

// Utility functions
const char* x64_register_name(x64_register_t reg) {
    static const char* names[] = {
        "rax", "rcx", "rdx", "rbx", "rsp", "rbp", "rsi", "rdi",
        "r8", "r9", "r10", "r11", "r12", "r13", "r14", "r15"
    };
    if (reg >= 0 && reg < X64_REG_COUNT) {
        return names[reg];
    }
    return "unknown";
}

const char* x64_xmm_register_name(x64_xmm_register_t reg) {
    static const char* names[] = {
        "xmm0", "xmm1", "xmm2", "xmm3", "xmm4", "xmm5", "xmm6", "xmm7",
        "xmm8", "xmm9", "xmm10", "xmm11", "xmm12", "xmm13", "xmm14", "xmm15"
    };
    if (reg >= 0 && reg < X64_XMM_COUNT) {
        return names[reg];
    }
    return "unknown";
}

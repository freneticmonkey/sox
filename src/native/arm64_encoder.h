#ifndef SOX_ARM64_ENCODER_H
#define SOX_ARM64_ENCODER_H

#include "../common.h"
#include <stdint.h>

// ARM64 (AArch64) registers
typedef enum {
    // General purpose registers (64-bit)
    ARM64_X0 = 0,   // 1st argument, return value
    ARM64_X1 = 1,   // 2nd argument
    ARM64_X2 = 2,   // 3rd argument
    ARM64_X3 = 3,   // 4th argument
    ARM64_X4 = 4,   // 5th argument
    ARM64_X5 = 5,   // 6th argument
    ARM64_X6 = 6,   // 7th argument
    ARM64_X7 = 7,   // 8th argument
    ARM64_X8 = 8,   // Indirect result location
    ARM64_X9 = 9,   // Temporary
    ARM64_X10 = 10, // Temporary
    ARM64_X11 = 11, // Temporary
    ARM64_X12 = 12, // Temporary
    ARM64_X13 = 13, // Temporary
    ARM64_X14 = 14, // Temporary
    ARM64_X15 = 15, // Temporary
    ARM64_X16 = 16, // IP0 (intra-procedure call)
    ARM64_X17 = 17, // IP1 (intra-procedure call)
    ARM64_X18 = 18, // Platform register (reserved)
    ARM64_X19 = 19, // Callee-saved
    ARM64_X20 = 20, // Callee-saved
    ARM64_X21 = 21, // Callee-saved
    ARM64_X22 = 22, // Callee-saved
    ARM64_X23 = 23, // Callee-saved
    ARM64_X24 = 24, // Callee-saved
    ARM64_X25 = 25, // Callee-saved
    ARM64_X26 = 26, // Callee-saved
    ARM64_X27 = 27, // Callee-saved
    ARM64_X28 = 28, // Callee-saved
    ARM64_X29 = 29, // Frame pointer (FP)
    ARM64_X30 = 30, // Link register (LR)
    ARM64_SP = 31,  // Stack pointer

    // Aliases
    ARM64_FP = 29,
    ARM64_LR = 30,

    ARM64_REG_COUNT = 32,
    ARM64_NO_REG = -1,
} arm64_register_t;

// SIMD/Floating point registers
typedef enum {
    ARM64_V0 = 0,   // 1st FP argument, return value
    ARM64_V1 = 1,   // 2nd FP argument
    ARM64_V2 = 2,   // 3rd FP argument
    ARM64_V3 = 3,   // 4th FP argument
    ARM64_V4 = 4,   // 5th FP argument
    ARM64_V5 = 5,   // 6th FP argument
    ARM64_V6 = 6,   // 7th FP argument
    ARM64_V7 = 7,   // 8th FP argument
    ARM64_V8 = 8,   // Callee-saved (lower 64 bits)
    ARM64_V9 = 9,   // Callee-saved (lower 64 bits)
    ARM64_V10 = 10, // Callee-saved (lower 64 bits)
    ARM64_V11 = 11, // Callee-saved (lower 64 bits)
    ARM64_V12 = 12, // Callee-saved (lower 64 bits)
    ARM64_V13 = 13, // Callee-saved (lower 64 bits)
    ARM64_V14 = 14, // Callee-saved (lower 64 bits)
    ARM64_V15 = 15, // Callee-saved (lower 64 bits)
    ARM64_V16 = 16, // Temporary
    ARM64_V17 = 17, // Temporary
    ARM64_V18 = 18, // Temporary
    ARM64_V19 = 19, // Temporary
    ARM64_V20 = 20, // Temporary
    ARM64_V21 = 21, // Temporary
    ARM64_V22 = 22, // Temporary
    ARM64_V23 = 23, // Temporary
    ARM64_V24 = 24, // Temporary
    ARM64_V25 = 25, // Temporary
    ARM64_V26 = 26, // Temporary
    ARM64_V27 = 27, // Temporary
    ARM64_V28 = 28, // Temporary
    ARM64_V29 = 29, // Temporary
    ARM64_V30 = 30, // Temporary
    ARM64_V31 = 31, // Temporary

    ARM64_VREG_COUNT = 32,
} arm64_vreg_t;

// Condition codes
typedef enum {
    ARM64_CC_EQ = 0,  // Equal (Z == 1)
    ARM64_CC_NE = 1,  // Not equal (Z == 0)
    ARM64_CC_HS = 2,  // Unsigned higher or same (C == 1)
    ARM64_CC_LO = 3,  // Unsigned lower (C == 0)
    ARM64_CC_MI = 4,  // Minus, negative (N == 1)
    ARM64_CC_PL = 5,  // Plus, positive or zero (N == 0)
    ARM64_CC_VS = 6,  // Overflow (V == 1)
    ARM64_CC_VC = 7,  // No overflow (V == 0)
    ARM64_CC_HI = 8,  // Unsigned higher (C == 1 && Z == 0)
    ARM64_CC_LS = 9,  // Unsigned lower or same (C == 0 || Z == 1)
    ARM64_CC_GE = 10, // Signed greater or equal (N == V)
    ARM64_CC_LT = 11, // Signed less than (N != V)
    ARM64_CC_GT = 12, // Signed greater than (Z == 0 && N == V)
    ARM64_CC_LE = 13, // Signed less or equal (Z == 1 || N != V)
    ARM64_CC_AL = 14, // Always (unconditional)
    ARM64_CC_NV = 15, // Never (reserved)
} arm64_condition_t;

// Machine code buffer
typedef struct {
    uint32_t* code;  // ARM64 uses 32-bit instructions
    size_t size;     // Size in instructions
    size_t capacity; // Capacity in instructions
} arm64_code_buffer_t;

// Relocation types
typedef enum {
    ARM64_RELOC_NONE = 0,
    ARM64_RELOC_CALL26,      // 26-bit PC-relative (BL)
    ARM64_RELOC_JUMP26,      // 26-bit PC-relative (B)
    ARM64_RELOC_ADR_PREL_PG_HI21,  // Page-relative
    ARM64_RELOC_ADD_ABS_LO12_NC,   // Low 12 bits
} arm64_reloc_type_t;

typedef struct {
    size_t offset;           // Offset in instruction stream
    arm64_reloc_type_t type;
    char* symbol;            // Owned copy of symbol string
    int64_t addend;
} arm64_relocation_t;

typedef struct {
    arm64_code_buffer_t code;
    arm64_relocation_t* relocations;
    size_t reloc_count;
    size_t reloc_capacity;
} arm64_assembler_t;

// Initialize/free assembler
arm64_assembler_t* arm64_assembler_new(void);
void arm64_assembler_free(arm64_assembler_t* asm_);

// Get current code offset (for labels)
size_t arm64_get_offset(arm64_assembler_t* asm_);

// Add relocation
void arm64_add_relocation(arm64_assembler_t* asm_, size_t offset,
                          arm64_reloc_type_t type, const char* symbol, int64_t addend);

// Emit instruction
void arm64_emit(arm64_assembler_t* asm_, uint32_t instruction);

// Data movement instructions
void arm64_mov_reg_reg(arm64_assembler_t* asm_, arm64_register_t dst, arm64_register_t src);
void arm64_mov_reg_imm(arm64_assembler_t* asm_, arm64_register_t dst, uint64_t imm);
void arm64_movz(arm64_assembler_t* asm_, arm64_register_t dst, uint16_t imm, uint8_t shift);
void arm64_movk(arm64_assembler_t* asm_, arm64_register_t dst, uint16_t imm, uint8_t shift);
void arm64_ldr_reg_reg_offset(arm64_assembler_t* asm_, arm64_register_t dst,
                               arm64_register_t base, int32_t offset);
void arm64_str_reg_reg_offset(arm64_assembler_t* asm_, arm64_register_t src,
                               arm64_register_t base, int32_t offset);

// Arithmetic instructions (64-bit)
void arm64_add_reg_reg_reg(arm64_assembler_t* asm_, arm64_register_t dst,
                            arm64_register_t src1, arm64_register_t src2);
void arm64_add_reg_reg_imm(arm64_assembler_t* asm_, arm64_register_t dst,
                            arm64_register_t src, uint16_t imm);
void arm64_sub_reg_reg_reg(arm64_assembler_t* asm_, arm64_register_t dst,
                            arm64_register_t src1, arm64_register_t src2);
void arm64_sub_reg_reg_imm(arm64_assembler_t* asm_, arm64_register_t dst,
                            arm64_register_t src, uint16_t imm);
void arm64_mul_reg_reg_reg(arm64_assembler_t* asm_, arm64_register_t dst,
                            arm64_register_t src1, arm64_register_t src2);
void arm64_sdiv_reg_reg_reg(arm64_assembler_t* asm_, arm64_register_t dst,
                             arm64_register_t src1, arm64_register_t src2);
void arm64_neg_reg_reg(arm64_assembler_t* asm_, arm64_register_t dst, arm64_register_t src);

// Logical instructions
void arm64_and_reg_reg_reg(arm64_assembler_t* asm_, arm64_register_t dst,
                            arm64_register_t src1, arm64_register_t src2);
void arm64_orr_reg_reg_reg(arm64_assembler_t* asm_, arm64_register_t dst,
                            arm64_register_t src1, arm64_register_t src2);
void arm64_eor_reg_reg_reg(arm64_assembler_t* asm_, arm64_register_t dst,
                            arm64_register_t src1, arm64_register_t src2);
void arm64_mvn_reg_reg(arm64_assembler_t* asm_, arm64_register_t dst, arm64_register_t src);
void arm64_lsl_reg_reg_imm(arm64_assembler_t* asm_, arm64_register_t dst,
                            arm64_register_t src, uint8_t shift);
void arm64_lsr_reg_reg_imm(arm64_assembler_t* asm_, arm64_register_t dst,
                            arm64_register_t src, uint8_t shift);

// Comparison instructions
void arm64_cmp_reg_reg(arm64_assembler_t* asm_, arm64_register_t src1, arm64_register_t src2);
void arm64_cmp_reg_imm(arm64_assembler_t* asm_, arm64_register_t src, uint16_t imm);
void arm64_tst_reg_reg(arm64_assembler_t* asm_, arm64_register_t src1, arm64_register_t src2);

// Conditional select
void arm64_csel(arm64_assembler_t* asm_, arm64_register_t dst, arm64_register_t src1,
                arm64_register_t src2, arm64_condition_t cond);
void arm64_cset(arm64_assembler_t* asm_, arm64_register_t dst, arm64_condition_t cond);

// Stack operations
void arm64_stp(arm64_assembler_t* asm_, arm64_register_t reg1, arm64_register_t reg2,
               arm64_register_t base, int32_t offset);
void arm64_ldp(arm64_assembler_t* asm_, arm64_register_t reg1, arm64_register_t reg2,
               arm64_register_t base, int32_t offset);

// Control flow
void arm64_b(arm64_assembler_t* asm_, int32_t offset);
void arm64_b_cond(arm64_assembler_t* asm_, arm64_condition_t cond, int32_t offset);
void arm64_bl(arm64_assembler_t* asm_, int32_t offset);
void arm64_br(arm64_assembler_t* asm_, arm64_register_t reg);
void arm64_blr(arm64_assembler_t* asm_, arm64_register_t reg);
void arm64_ret(arm64_assembler_t* asm_, arm64_register_t reg);

// Floating point instructions
void arm64_fmov_vreg_vreg(arm64_assembler_t* asm_, arm64_vreg_t dst, arm64_vreg_t src);
void arm64_fadd_vreg_vreg_vreg(arm64_assembler_t* asm_, arm64_vreg_t dst,
                                arm64_vreg_t src1, arm64_vreg_t src2);
void arm64_fsub_vreg_vreg_vreg(arm64_assembler_t* asm_, arm64_vreg_t dst,
                                arm64_vreg_t src1, arm64_vreg_t src2);
void arm64_fmul_vreg_vreg_vreg(arm64_assembler_t* asm_, arm64_vreg_t dst,
                                arm64_vreg_t src1, arm64_vreg_t src2);
void arm64_fdiv_vreg_vreg_vreg(arm64_assembler_t* asm_, arm64_vreg_t dst,
                                arm64_vreg_t src1, arm64_vreg_t src2);

// Conversion instructions
void arm64_scvtf(arm64_assembler_t* asm_, arm64_vreg_t dst, arm64_register_t src);
void arm64_fcvtzs(arm64_assembler_t* asm_, arm64_register_t dst, arm64_vreg_t src);

// PC-relative address loading
void arm64_adrp(arm64_assembler_t* asm_, arm64_register_t dst, int32_t page_offset);

// Utility functions
const char* arm64_register_name(arm64_register_t reg);
const char* arm64_vreg_name(arm64_vreg_t reg);

// Get code buffer
uint8_t* arm64_get_code(arm64_assembler_t* asm_, size_t* size);

#endif

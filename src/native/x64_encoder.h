#ifndef SOX_X64_ENCODER_H
#define SOX_X64_ENCODER_H

#include "../common.h"
#include <stdint.h>

// x86-64 registers (using System V ABI conventions)
typedef enum {
    // 64-bit general purpose registers
    X64_RAX = 0,  // Return value, caller-saved
    X64_RCX = 1,  // 4th argument, caller-saved
    X64_RDX = 2,  // 3rd argument, caller-saved
    X64_RBX = 3,  // Callee-saved
    X64_RSP = 4,  // Stack pointer
    X64_RBP = 5,  // Frame pointer, callee-saved
    X64_RSI = 6,  // 2nd argument, caller-saved
    X64_RDI = 7,  // 1st argument, caller-saved
    X64_R8 = 8,   // 5th argument, caller-saved
    X64_R9 = 9,   // 6th argument, caller-saved
    X64_R10 = 10, // Caller-saved
    X64_R11 = 11, // Caller-saved
    X64_R12 = 12, // Callee-saved
    X64_R13 = 13, // Callee-saved
    X64_R14 = 14, // Callee-saved
    X64_R15 = 15, // Callee-saved

    X64_REG_COUNT = 16,

    // Special register aliases
    X64_NO_REG = -1,
} x64_register_t;

// XMM registers (SSE/floating point)
typedef enum {
    X64_XMM0 = 0,   // 1st FP argument, return value
    X64_XMM1 = 1,   // 2nd FP argument
    X64_XMM2 = 2,   // 3rd FP argument
    X64_XMM3 = 3,   // 4th FP argument
    X64_XMM4 = 4,   // 5th FP argument
    X64_XMM5 = 5,   // 6th FP argument
    X64_XMM6 = 6,   // 7th FP argument
    X64_XMM7 = 7,   // 8th FP argument
    X64_XMM8 = 8,
    X64_XMM9 = 9,
    X64_XMM10 = 10,
    X64_XMM11 = 11,
    X64_XMM12 = 12,
    X64_XMM13 = 13,
    X64_XMM14 = 14,
    X64_XMM15 = 15,

    X64_XMM_COUNT = 16,
} x64_xmm_register_t;

// Condition codes (for conditional jumps and moves)
typedef enum {
    X64_CC_O = 0,    // Overflow
    X64_CC_NO = 1,   // Not overflow
    X64_CC_B = 2,    // Below (unsigned <)
    X64_CC_AE = 3,   // Above or equal (unsigned >=)
    X64_CC_E = 4,    // Equal (==)
    X64_CC_NE = 5,   // Not equal (!=)
    X64_CC_BE = 6,   // Below or equal (unsigned <=)
    X64_CC_A = 7,    // Above (unsigned >)
    X64_CC_S = 8,    // Sign
    X64_CC_NS = 9,   // Not sign
    X64_CC_P = 10,   // Parity
    X64_CC_NP = 11,  // Not parity
    X64_CC_L = 12,   // Less (signed <)
    X64_CC_GE = 13,  // Greater or equal (signed >=)
    X64_CC_LE = 14,  // Less or equal (signed <=)
    X64_CC_G = 15,   // Greater (signed >)
} x64_condition_t;

// Machine code buffer
typedef struct {
    uint8_t* code;
    size_t size;
    size_t capacity;
} x64_code_buffer_t;

// Relocations (for linking)
typedef enum {
    X64_RELOC_NONE = 0,
    X64_RELOC_PC32,      // 32-bit PC-relative
    X64_RELOC_ABS64,     // 64-bit absolute
    X64_RELOC_PLT32,     // PLT entry (for function calls)
} x64_reloc_type_t;

typedef struct {
    size_t offset;           // Offset in code buffer
    x64_reloc_type_t type;   // Type of relocation
    const char* symbol;      // Symbol name
    int64_t addend;          // Addend value
} x64_relocation_t;

typedef struct {
    x64_code_buffer_t code;
    x64_relocation_t* relocations;
    size_t reloc_count;
    size_t reloc_capacity;
} x64_assembler_t;

// Initialize/free assembler
x64_assembler_t* x64_assembler_new(void);
void x64_assembler_free(x64_assembler_t* asm_);

// Get current code offset (for labels)
size_t x64_get_offset(x64_assembler_t* asm_);

// Add relocation
void x64_add_relocation(x64_assembler_t* asm_, size_t offset,
                        x64_reloc_type_t type, const char* symbol, int64_t addend);

// Emit raw bytes
void x64_emit_byte(x64_assembler_t* asm_, uint8_t byte);
void x64_emit_word(x64_assembler_t* asm_, uint16_t word);
void x64_emit_dword(x64_assembler_t* asm_, uint32_t dword);
void x64_emit_qword(x64_assembler_t* asm_, uint64_t qword);

// Data movement instructions
void x64_mov_reg_reg(x64_assembler_t* asm_, x64_register_t dst, x64_register_t src);
void x64_mov_reg_imm64(x64_assembler_t* asm_, x64_register_t dst, int64_t imm);
void x64_mov_reg_imm32(x64_assembler_t* asm_, x64_register_t dst, int32_t imm);
void x64_mov_reg_mem(x64_assembler_t* asm_, x64_register_t dst, x64_register_t base, int32_t disp);
void x64_mov_mem_reg(x64_assembler_t* asm_, x64_register_t base, int32_t disp, x64_register_t src);
void x64_lea(x64_assembler_t* asm_, x64_register_t dst, x64_register_t base, int32_t disp);

// Arithmetic instructions
void x64_add_reg_reg(x64_assembler_t* asm_, x64_register_t dst, x64_register_t src);
void x64_add_reg_imm(x64_assembler_t* asm_, x64_register_t dst, int32_t imm);
void x64_sub_reg_reg(x64_assembler_t* asm_, x64_register_t dst, x64_register_t src);
void x64_sub_reg_imm(x64_assembler_t* asm_, x64_register_t dst, int32_t imm);
void x64_imul_reg_reg(x64_assembler_t* asm_, x64_register_t dst, x64_register_t src);
void x64_idiv_reg(x64_assembler_t* asm_, x64_register_t divisor);
void x64_neg_reg(x64_assembler_t* asm_, x64_register_t reg);

// Logical instructions
void x64_and_reg_reg(x64_assembler_t* asm_, x64_register_t dst, x64_register_t src);
void x64_or_reg_reg(x64_assembler_t* asm_, x64_register_t dst, x64_register_t src);
void x64_xor_reg_reg(x64_assembler_t* asm_, x64_register_t dst, x64_register_t src);
void x64_not_reg(x64_assembler_t* asm_, x64_register_t reg);
void x64_shl_reg_imm(x64_assembler_t* asm_, x64_register_t reg, uint8_t imm);
void x64_shr_reg_imm(x64_assembler_t* asm_, x64_register_t reg, uint8_t imm);

// Comparison instructions
void x64_cmp_reg_reg(x64_assembler_t* asm_, x64_register_t left, x64_register_t right);
void x64_cmp_reg_imm(x64_assembler_t* asm_, x64_register_t reg, int32_t imm);
void x64_test_reg_reg(x64_assembler_t* asm_, x64_register_t left, x64_register_t right);

// Conditional set instructions
void x64_setcc(x64_assembler_t* asm_, x64_condition_t cond, x64_register_t dst);

// Stack operations
void x64_push_reg(x64_assembler_t* asm_, x64_register_t reg);
void x64_pop_reg(x64_assembler_t* asm_, x64_register_t reg);

// Control flow
void x64_jmp_rel32(x64_assembler_t* asm_, int32_t offset);
void x64_jcc_rel32(x64_assembler_t* asm_, x64_condition_t cond, int32_t offset);
void x64_call_rel32(x64_assembler_t* asm_, int32_t offset);
void x64_call_reg(x64_assembler_t* asm_, x64_register_t reg);
void x64_ret(x64_assembler_t* asm_);

// Floating point instructions (SSE2)
void x64_movsd_xmm_xmm(x64_assembler_t* asm_, x64_xmm_register_t dst, x64_xmm_register_t src);
void x64_movsd_xmm_mem(x64_assembler_t* asm_, x64_xmm_register_t dst, x64_register_t base, int32_t disp);
void x64_movsd_mem_xmm(x64_assembler_t* asm_, x64_register_t base, int32_t disp, x64_xmm_register_t src);
void x64_addsd_xmm_xmm(x64_assembler_t* asm_, x64_xmm_register_t dst, x64_xmm_register_t src);
void x64_subsd_xmm_xmm(x64_assembler_t* asm_, x64_xmm_register_t dst, x64_xmm_register_t src);
void x64_mulsd_xmm_xmm(x64_assembler_t* asm_, x64_xmm_register_t dst, x64_xmm_register_t src);
void x64_divsd_xmm_xmm(x64_assembler_t* asm_, x64_xmm_register_t dst, x64_xmm_register_t src);

// Conversion instructions
void x64_cvtsi2sd(x64_assembler_t* asm_, x64_xmm_register_t dst, x64_register_t src); // int to double
void x64_cvttsd2si(x64_assembler_t* asm_, x64_register_t dst, x64_xmm_register_t src); // double to int (truncate)

// Utility functions
const char* x64_register_name(x64_register_t reg);
const char* x64_xmm_register_name(x64_xmm_register_t reg);
bool x64_needs_rex(x64_register_t reg);

#endif

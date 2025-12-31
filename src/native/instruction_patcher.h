#ifndef SOX_INSTRUCTION_PATCHER_H
#define SOX_INSTRUCTION_PATCHER_H

#include "linker_core.h"
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/*
 * Instruction Patching
 *
 * This module implements platform-specific instruction patching for relocation
 * processing. It handles the low-level details of modifying x86-64 and ARM64
 * instructions with calculated relocation values.
 *
 * Key Responsibilities:
 * - Patch x86-64 immediate values (32-bit and 64-bit)
 * - Patch x86-64 PC-relative offsets
 * - Patch ARM64 branch instructions (26-bit offset)
 * - Patch ARM64 ADRP instructions (21-bit page offset)
 * - Patch ARM64 ADD instructions (12-bit immediate)
 * - Validate patching doesn't violate instruction encoding constraints
 *
 * Phase 4.2: Instruction Patching
 */

/*
 * x86-64 Instruction Patching
 *
 * SECURITY: All patching functions include bounds checking to prevent buffer overflows.
 * The code_size parameter must be the size of the code buffer in bytes.
 */

/* Patch 32-bit immediate value at offset */
bool patch_x64_imm32(uint8_t* code, size_t code_size, size_t offset, int32_t value);

/* Patch 64-bit immediate value at offset */
bool patch_x64_imm64(uint8_t* code, size_t code_size, size_t offset, int64_t value);

/* Patch 32-bit PC-relative offset (for calls/jumps) */
bool patch_x64_rel32(uint8_t* code, size_t code_size, size_t offset, int32_t value);

/*
 * ARM64 Instruction Patching
 *
 * SECURITY: All patching functions include bounds checking to prevent buffer overflows.
 */

/* Patch 26-bit branch offset for BL/B instructions
 *
 * ARM64 BL/B instruction format:
 *   [31:26] opcode (6 bits)
 *   [25:0]  imm26 (26 bits, signed, shifted right by 2)
 *
 * The value parameter is the byte offset (not shifted).
 * This function will shift it right by 2 and validate range.
 *
 * Valid range: -128MB to +128MB (-0x8000000 to +0x7FFFFFC bytes)
 */
bool patch_arm64_branch26(uint8_t* code, size_t code_size, size_t offset, int32_t value);

/* Patch ADRP instruction (Page-relative address)
 *
 * ARM64 ADRP instruction format:
 *   [31]    op (1 bit)
 *   [30:29] immlo (2 bits) - low 2 bits of page offset
 *   [28:24] opcode (5 bits)
 *   [23:5]  immhi (19 bits) - high 19 bits of page offset
 *   [4:0]   Rd (5 bits) - destination register
 *
 * The page offset is (target >> 12) - (pc >> 12)
 * Total: 21-bit signed page offset
 *
 * Valid range: -1MB to +1MB pages (-0x100000 to +0xFFFFF)
 */
bool patch_arm64_adrp(uint8_t* code, size_t code_size, size_t offset, uint64_t target, uint64_t pc);

/* Patch ADD instruction with 12-bit immediate
 *
 * ARM64 ADD instruction format:
 *   [31:23] opcode (9 bits)
 *   [22]    shift (1 bit) - 0 for LSL #0, 1 for LSL #12
 *   [21:10] imm12 (12 bits) - immediate value
 *   [9:5]   Rn (5 bits) - source register
 *   [4:0]   Rd (5 bits) - destination register
 *
 * This is used for the low 12 bits of an address after ADRP.
 *
 * Valid range: 0 to 4095 (0x000 to 0xFFF)
 */
bool patch_arm64_add_imm12(uint8_t* code, size_t code_size, size_t offset, uint16_t imm12);
bool patch_arm64_add_imm12_rewrite(uint8_t* code, size_t code_size, size_t offset, uint16_t imm12);
bool patch_arm64_load_imm12(uint8_t* code, size_t code_size, size_t offset, uint16_t imm12);
bool patch_arm64_ldr_imm12_scaled(uint8_t* code, size_t code_size, size_t offset, uint64_t value);

/* Patch 64-bit absolute address for ARM64 */
bool patch_arm64_abs64(uint8_t* code, size_t code_size, size_t offset, uint64_t value);

/*
 * Generic Patching
 */

/* Patch instruction based on relocation type
 *
 * This is the main entry point that delegates to the appropriate
 * platform-specific patching function based on the relocation type.
 *
 * Returns true on success, false on error (e.g., range overflow).
 */
bool patch_instruction(uint8_t* code,
                        size_t code_size,
                        size_t offset,
                        int64_t value,
                        relocation_type_t type,
                        uint64_t pc);

/*
 * Range Validation
 */

/* Check if value fits in signed N-bit field */
bool fits_in_signed_bits(int64_t value, int bits);

/* Check if value fits in unsigned N-bit field */
bool fits_in_unsigned_bits(uint64_t value, int bits);

/* Validate relocation value for specific type */
bool validate_relocation_range(int64_t value, relocation_type_t type);

/*
 * Utility Functions
 */

/* Sign-extend N-bit value to 64 bits */
int64_t sign_extend(uint64_t value, int bits);

/* Extract bit field from instruction */
uint32_t extract_bits(uint32_t insn, int low, int high);

/* Insert bit field into instruction */
uint32_t insert_bits(uint32_t insn, uint32_t value, int low, int high);

#endif

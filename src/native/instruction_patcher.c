#include "instruction_patcher.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

/*
 * Bounds Checking Macro
 *
 * Validates that we can safely write 'patch_size' bytes at 'offset'
 * within a code buffer of size 'code_size'. This prevents heap corruption
 * from out-of-bounds writes when processing malicious or malformed object files.
 *
 * CRITICAL SECURITY: All instruction patching must validate bounds before
 * writing to prevent buffer overflows.
 */
#define CHECK_PATCH_BOUNDS(code, code_size, offset, patch_size) \
    do { \
        if (!(code) || (offset) + (patch_size) > (code_size)) { \
            fprintf(stderr, "Instruction patcher error: Patch offset %zu + size %zu " \
                    "exceeds code size %zu\n", (size_t)(offset), (size_t)(patch_size), \
                    (size_t)(code_size)); \
            return false; \
        } \
    } while (0)

/*
 * x86-64 Instruction Patching
 */

bool patch_x64_imm32(uint8_t* code, size_t code_size, size_t offset, int32_t value) {
    CHECK_PATCH_BOUNDS(code, code_size, offset, sizeof(int32_t));

    *(int32_t*)(code + offset) = value;
    return true;
}

bool patch_x64_imm64(uint8_t* code, size_t code_size, size_t offset, int64_t value) {
    CHECK_PATCH_BOUNDS(code, code_size, offset, sizeof(int64_t));

    *(int64_t*)(code + offset) = value;
    return true;
}

bool patch_x64_rel32(uint8_t* code, size_t code_size, size_t offset, int32_t value) {
    CHECK_PATCH_BOUNDS(code, code_size, offset, sizeof(int32_t));

    *(int32_t*)(code + offset) = value;
    return true;
}

/*
 * ARM64 Instruction Patching
 */

bool patch_arm64_branch26(uint8_t* code, size_t code_size, size_t offset, int32_t value) {
    CHECK_PATCH_BOUNDS(code, code_size, offset, sizeof(uint32_t));

    /* Validate range: -128MB to +128MB (must be 4-byte aligned)
     * After shifting right by 2: -0x2000000 to +0x1FFFFFF */
    if (value < -0x8000000 || value > 0x7FFFFFC) {
        fprintf(stderr, "Instruction patcher error: ARM64 branch offset %d (0x%x) out of range "
                "[-128MB, +128MB]\n", value, value);
        return false;
    }

    /* Check 4-byte alignment */
    if ((value & 0x3) != 0) {
        fprintf(stderr, "Instruction patcher error: ARM64 branch offset %d not 4-byte aligned\n",
                value);
        return false;
    }

    /* Shift right by 2 to get 26-bit immediate */
    int32_t imm26 = (value >> 2) & 0x3FFFFFF;

    /* Read current instruction */
    uint32_t* insn = (uint32_t*)(code + offset);

    /* Preserve opcode (bits 31-26), replace immediate (bits 25-0) */
    *insn = (*insn & 0xFC000000) | (imm26 & 0x3FFFFFF);

    return true;
}

bool patch_arm64_adrp(uint8_t* code, size_t code_size, size_t offset, uint64_t target, uint64_t pc) {
    /* SECURITY FIX: Critical Issue #4 - Bounds Checking */
    CHECK_PATCH_BOUNDS(code, code_size, offset, sizeof(uint32_t));

    /* Calculate page offset: (target_page - pc_page)
     * Pages are 4KB (12-bit page offset) */
    int64_t page_offset = (int64_t)(target >> 12) - (int64_t)(pc >> 12);

    /* Validate range: 21-bit signed (-1MB to +1MB pages) */
    if (page_offset < -0x100000 || page_offset > 0xFFFFF) {
        fprintf(stderr, "Instruction patcher error: ARM64 ADRP page offset %lld (0x%llx) out of range "
                "[-1MB, +1MB]\n", (long long)page_offset, (unsigned long long)page_offset);
        fprintf(stderr, "  Target: 0x%llx, PC: 0x%llx\n",
                (unsigned long long)target, (unsigned long long)pc);
        return false;
    }

    /* Extract immlo (bits 1:0) and immhi (bits 20:2)
     * immlo goes to instruction bits 30:29
     * immhi goes to instruction bits 23:5 */
    uint32_t immlo = ((uint32_t)page_offset & 0x3) << 29;
    uint32_t immhi = (((uint32_t)page_offset >> 2) & 0x7FFFF) << 5;

    /* Read current instruction */
    uint32_t* insn = (uint32_t*)(code + offset);

    /* Preserve bits: [31] op, [28:24] opcode, [4:0] Rd
     * Replace bits: [30:29] immlo, [23:5] immhi */
    *insn = (*insn & 0x9F00001F) | immlo | immhi;

    return true;
}

bool patch_arm64_add_imm12(uint8_t* code, size_t code_size, size_t offset, uint16_t imm12) {
    /* SECURITY FIX: Critical Issue #4 - Bounds Checking */
    CHECK_PATCH_BOUNDS(code, code_size, offset, sizeof(uint32_t));

    /* Validate range: 12-bit unsigned (0 to 4095) */
    if (imm12 > 0xFFF) {
        fprintf(stderr, "Instruction patcher error: ARM64 ADD immediate %u (0x%x) out of range [0, 4095]\n",
                imm12, imm12);
        return false;
    }

    /* Read current instruction */
    uint32_t* insn = (uint32_t*)(code + offset);

    /* Preserve bits: [31:22] opcode/shift, [9:5] Rn, [4:0] Rd
     * Replace bits: [21:10] imm12 */
    *insn = (*insn & 0xFFC003FF) | ((imm12 & 0xFFF) << 10);

    return true;
}

bool patch_arm64_abs64(uint8_t* code, size_t code_size, size_t offset, uint64_t value) {
    /* SECURITY FIX: Critical Issue #4 - Bounds Checking */
    CHECK_PATCH_BOUNDS(code, code_size, offset, sizeof(uint64_t));

    *(uint64_t*)(code + offset) = value;
    return true;
}

/*
 * Generic Patching
 */

bool patch_instruction(uint8_t* code,
                        size_t code_size,
                        size_t offset,
                        int64_t value,
                        relocation_type_t type,
                        uint64_t pc) {
    if (!code) return false;

    /* Validate range first */
    if (!validate_relocation_range(value, type)) {
        return false;
    }

    switch (type) {
        /* x86-64 relocations */
        case RELOC_X64_64:
            return patch_x64_imm64(code, code_size, offset, value);

        case RELOC_X64_PC32:
        case RELOC_X64_PLT32:
            /* Validate 32-bit range */
            if (value < INT32_MIN || value > INT32_MAX) {
                fprintf(stderr, "Error: x86-64 PC-relative relocation overflow: %lld\n",
                        (long long)value);
                return false;
            }
            return patch_x64_rel32(code, code_size, offset, (int32_t)value);

        case RELOC_X64_GOTPCREL:
            /* GOT-relative, treat like PC32 */
            if (value < INT32_MIN || value > INT32_MAX) {
                fprintf(stderr, "Error: x86-64 GOT-relative relocation overflow: %lld\n",
                        (long long)value);
                return false;
            }
            return patch_x64_rel32(code, code_size, offset, (int32_t)value);

        /* ARM64 relocations */
        case RELOC_ARM64_ABS64:
            return patch_arm64_abs64(code, code_size, offset, (uint64_t)value);

        case RELOC_ARM64_CALL26:
        case RELOC_ARM64_JUMP26:
            return patch_arm64_branch26(code, code_size, offset, (int32_t)value);

        case RELOC_ARM64_ADR_PREL_PG_HI21:
            /* For ADRP, value contains the target address
             * pc is the location of the ADRP instruction */
            return patch_arm64_adrp(code, code_size, offset, (uint64_t)value, pc);

        case RELOC_ARM64_ADD_ABS_LO12_NC:
            /* Extract low 12 bits of absolute address */
            return patch_arm64_add_imm12(code, code_size, offset, (uint16_t)(value & 0xFFF));

        case RELOC_NONE:
            /* No patching needed */
            return true;

        default:
            fprintf(stderr, "Error: Unknown relocation type: %d\n", type);
            return false;
    }
}

/*
 * Range Validation
 */

bool fits_in_signed_bits(int64_t value, int bits) {
    if (bits <= 0 || bits >= 64) return false;

    int64_t min = -(1LL << (bits - 1));
    int64_t max = (1LL << (bits - 1)) - 1;

    return value >= min && value <= max;
}

bool fits_in_unsigned_bits(uint64_t value, int bits) {
    if (bits <= 0 || bits >= 64) return false;

    uint64_t max = (1ULL << bits) - 1;

    return value <= max;
}

bool validate_relocation_range(int64_t value, relocation_type_t type) {
    switch (type) {
        case RELOC_X64_64:
        case RELOC_ARM64_ABS64:
            /* 64-bit absolute, no range check needed */
            return true;

        case RELOC_X64_PC32:
        case RELOC_X64_PLT32:
        case RELOC_X64_GOTPCREL:
            /* 32-bit signed */
            if (value < INT32_MIN || value > INT32_MAX) {
                fprintf(stderr, "Error: 32-bit relocation overflow: %lld\n",
                        (long long)value);
                fprintf(stderr, "Valid range: %d to %d\n", INT32_MIN, INT32_MAX);
                return false;
            }
            return true;

        case RELOC_ARM64_CALL26:
        case RELOC_ARM64_JUMP26:
            /* 26-bit signed, shifted right by 2
             * Valid byte range: -128MB to +128MB */
            if (value < -0x8000000 || value > 0x7FFFFFF) {
                fprintf(stderr, "Error: ARM64 26-bit branch out of range: %lld\n",
                        (long long)value);
                fprintf(stderr, "Valid range: -128MB to +128MB\n");
                return false;
            }
            /* Must be 4-byte aligned */
            if ((value & 0x3) != 0) {
                fprintf(stderr, "Error: ARM64 branch not 4-byte aligned: %lld\n",
                        (long long)value);
                return false;
            }
            return true;

        case RELOC_ARM64_ADR_PREL_PG_HI21:
            /* 21-bit signed page offset
             * Validated in patch_arm64_adrp */
            return true;

        case RELOC_ARM64_ADD_ABS_LO12_NC:
            /* 12-bit unsigned
             * NC = No Check, so we only validate against maximum */
            if ((value & 0xFFF) > 0xFFF) {
                fprintf(stderr, "Error: ARM64 12-bit immediate out of range: %lld\n",
                        (long long)(value & 0xFFF));
                return false;
            }
            return true;

        case RELOC_NONE:
            return true;

        default:
            fprintf(stderr, "Error: Unknown relocation type for validation: %d\n", type);
            return false;
    }
}

/*
 * Utility Functions
 */

int64_t sign_extend(uint64_t value, int bits) {
    if (bits <= 0 || bits >= 64) return (int64_t)value;

    /* Check if sign bit is set */
    uint64_t sign_bit = 1ULL << (bits - 1);

    if (value & sign_bit) {
        /* Sign extend by setting all upper bits */
        uint64_t mask = ~((1ULL << bits) - 1);
        return (int64_t)(value | mask);
    } else {
        /* Positive, just mask to bits */
        uint64_t mask = (1ULL << bits) - 1;
        return (int64_t)(value & mask);
    }
}

uint32_t extract_bits(uint32_t insn, int low, int high) {
    if (low < 0 || high > 31 || low > high) return 0;

    int width = high - low + 1;
    uint32_t mask = (1U << width) - 1;

    return (insn >> low) & mask;
}

uint32_t insert_bits(uint32_t insn, uint32_t value, int low, int high) {
    if (low < 0 || high > 31 || low > high) return insn;

    int width = high - low + 1;
    uint32_t mask = (1U << width) - 1;

    /* Clear the bits in the field */
    insn &= ~(mask << low);

    /* Insert the new value */
    insn |= (value & mask) << low;

    return insn;
}

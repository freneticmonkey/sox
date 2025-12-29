/*
 * Unit tests for relocation_processor.c and instruction_patcher.c
 *
 * Tests the relocation processing engine including:
 * - Relocation calculation formulas
 * - Instruction patching (x86-64 and ARM64)
 * - Range validation
 * - Error handling
 * - Cross-section relocations
 */

#include "../native/relocation_processor.h"
#include "../native/instruction_patcher.h"
#include "../native/linker_core.h"
#include "../native/section_layout.h"
#include "../native/symbol_resolver.h"
#include "../../ext/munit/munit.h"
#include <string.h>
#include <stdio.h>

/* Helper: Create a test object with a section and symbol */
static linker_object_t* create_test_object(const char* name,
                                            section_type_t sec_type,
                                            size_t sec_size,
                                            const char* sym_name,
                                            uint64_t sym_value) {
    linker_object_t* obj = linker_object_new(name, PLATFORM_FORMAT_ELF);
    if (!obj) return NULL;

    /* Add section */
    linker_section_t* section = linker_object_add_section(obj);
    section->name = strdup(sec_type == SECTION_TYPE_TEXT ? ".text" : ".data");
    section->type = sec_type;
    section->size = sec_size;
    section->alignment = 16;
    section->data = calloc(1, sec_size);
    section->flags = 0x7;

    /* Add symbol */
    linker_symbol_t* symbol = linker_object_add_symbol(obj);
    symbol->name = strdup(sym_name);
    symbol->type = SYMBOL_TYPE_FUNC;
    symbol->binding = SYMBOL_BINDING_GLOBAL;
    symbol->section_index = 0;
    symbol->value = sym_value;
    symbol->size = 16;
    symbol->is_defined = true;
    symbol->defining_object = 0;

    return obj;
}

/* Helper: Add relocation to object */
static void add_relocation(linker_object_t* obj,
                           uint64_t offset,
                           relocation_type_t type,
                           int symbol_index,
                           int64_t addend) {
    linker_relocation_t* reloc = linker_object_add_relocation(obj);
    reloc->offset = offset;
    reloc->type = type;
    reloc->symbol_index = symbol_index;
    reloc->addend = addend;
    reloc->section_index = 0;
    reloc->object_index = 0;
}

/*
 * Instruction Patcher Tests
 */

/* Test: x86-64 32-bit immediate patching */
static MunitResult test_x64_imm32_patch(const MunitParameter params[], void* data) {
    (void)params;
    (void)data;

    uint8_t code[8] = {0};
    patch_x64_imm32(code, sizeof(code), 0, 0x12345678);

    munit_assert_uint32(*(uint32_t*)code, ==, 0x12345678);

    return MUNIT_OK;
}

/* Test: x86-64 64-bit immediate patching */
static MunitResult test_x64_imm64_patch(const MunitParameter params[], void* data) {
    (void)params;
    (void)data;

    uint8_t code[16] = {0};
    patch_x64_imm64(code, sizeof(code), 0, 0x123456789ABCDEF0LL);

    munit_assert_uint64(*(uint64_t*)code, ==, 0x123456789ABCDEF0ULL);

    return MUNIT_OK;
}

/* Test: x86-64 PC-relative patching */
static MunitResult test_x64_rel32_patch(const MunitParameter params[], void* data) {
    (void)params;
    (void)data;

    uint8_t code[8] = {0};
    patch_x64_rel32(code, sizeof(code), 0, -1234);

    munit_assert_int32(*(int32_t*)code, ==, -1234);

    return MUNIT_OK;
}

/* Test: ARM64 branch26 patching */
static MunitResult test_arm64_branch26_patch(const MunitParameter params[], void* data) {
    (void)params;
    (void)data;

    /* BL instruction: 0x94000000 | imm26
     * Initial: BL #0 (0x94000000) */
    uint8_t code[4] = {0x00, 0x00, 0x00, 0x94};

    /* Patch with offset +100 bytes (25 instructions) */
    bool success = patch_arm64_branch26(code, sizeof(code), 0, 100);
    munit_assert_true(success);

    uint32_t insn = *(uint32_t*)code;
    /* Extract imm26 */
    uint32_t imm26 = insn & 0x03FFFFFF;
    /* Should be 100 >> 2 = 25 */
    munit_assert_uint32(imm26, ==, 25);

    /* Verify opcode preserved (bits 31-26 should be 0x25 for BL) */
    munit_assert_uint32(insn >> 26, ==, 0x25);

    return MUNIT_OK;
}

/* Test: ARM64 branch26 range validation */
static MunitResult test_arm64_branch26_range(const MunitParameter params[], void* data) {
    (void)params;
    (void)data;

    uint8_t code[4] = {0x00, 0x00, 0x00, 0x94};

    /* Valid range: -128MB to +128MB */
    munit_assert_true(patch_arm64_branch26(code, sizeof(code), 0, 0x7FFFFFC));   /* Max positive */
    munit_assert_true(patch_arm64_branch26(code, sizeof(code), 0, -0x8000000));  /* Max negative */

    /* Out of range should fail */
    munit_assert_false(patch_arm64_branch26(code, sizeof(code), 0, 0x8000000));  /* Too large */
    munit_assert_false(patch_arm64_branch26(code, sizeof(code), 0, -0x8000004)); /* Too small */

    /* Misalignment should fail */
    munit_assert_false(patch_arm64_branch26(code, sizeof(code), 0, 101)); /* Not 4-byte aligned */

    return MUNIT_OK;
}

/* Test: ARM64 ADRP patching */
static MunitResult test_arm64_adrp_patch(const MunitParameter params[], void* data) {
    (void)params;
    (void)data;

    /* ADRP instruction: 0x90000000 | immlo | immhi | Rd
     * Initial: ADRP X0, #0 (0x90000000) */
    uint8_t code[4] = {0x00, 0x00, 0x00, 0x90};

    /* Target at page 0x401000, PC at page 0x400000
     * Page offset = (0x401000 >> 12) - (0x400000 >> 12) = 0x401 - 0x400 = 1 */
    uint64_t target = 0x401000;
    uint64_t pc = 0x400000;

    bool success = patch_arm64_adrp(code, sizeof(code), 0, target, pc);
    munit_assert_true(success);

    uint32_t insn = *(uint32_t*)code;

    /* Extract immlo (bits 30:29) and immhi (bits 23:5) */
    uint32_t immlo = (insn >> 29) & 0x3;
    uint32_t immhi = (insn >> 5) & 0x7FFFF;
    uint32_t page_offset = (immhi << 2) | immlo;

    munit_assert_uint32(page_offset, ==, 1);

    /* Verify opcode preserved and Rd = 0 */
    munit_assert_uint32(insn & 0x9F00001F, ==, 0x90000000);

    return MUNIT_OK;
}

/* Test: ARM64 ADD imm12 patching */
static MunitResult test_arm64_add_imm12_patch(const MunitParameter params[], void* data) {
    (void)params;
    (void)data;

    /* ADD instruction: 0x91000000 | imm12 | Rn | Rd
     * Initial: ADD X0, X1, #0 (0x91000020 - X1 in bits 9:5) */
    uint8_t code[4] = {0x20, 0x00, 0x00, 0x91};

    /* Patch with imm12 = 0x123 */
    bool success = patch_arm64_add_imm12(code, sizeof(code), 0, 0x123);
    munit_assert_true(success);

    uint32_t insn = *(uint32_t*)code;

    /* Extract imm12 (bits 21:10) */
    uint32_t imm12 = (insn >> 10) & 0xFFF;
    munit_assert_uint32(imm12, ==, 0x123);

    /* Verify opcode and registers preserved */
    munit_assert_uint32(insn & 0xFFC003FF, ==, 0x91000020);

    return MUNIT_OK;
}

/* Test: ARM64 ADD imm12 range validation */
static MunitResult test_arm64_add_imm12_range(const MunitParameter params[], void* data) {
    (void)params;
    (void)data;

    uint8_t code[4] = {0x20, 0x00, 0x00, 0x91};

    /* Valid range: 0 to 4095 */
    munit_assert_true(patch_arm64_add_imm12(code, sizeof(code), 0, 0));
    munit_assert_true(patch_arm64_add_imm12(code, sizeof(code), 0, 0xFFF));

    /* Out of range should fail */
    munit_assert_false(patch_arm64_add_imm12(code, sizeof(code), 0, 0x1000));

    return MUNIT_OK;
}

/*
 * Relocation Calculation Tests
 */

/* Test: Relocation calculation formulas */
static MunitResult test_relocation_formulas(const MunitParameter params[], void* data) {
    (void)params;
    (void)data;

    uint64_t S = 0x401000;  /* Symbol address */
    int64_t A = 0x10;       /* Addend */
    uint64_t P = 0x400100;  /* Place */

    /* R_X86_64_64: S + A */
    int64_t result = relocation_calculate_value(RELOC_X64_64, S, A, P);
    munit_assert_int64(result, ==, 0x401010);

    /* R_X86_64_PC32: S + A - P */
    result = relocation_calculate_value(RELOC_X64_PC32, S, A, P);
    munit_assert_int64(result, ==, 0x401010 - 0x400100);
    munit_assert_int64(result, ==, 0xF10);

    /* R_ARM64_ABS64: S + A */
    result = relocation_calculate_value(RELOC_ARM64_ABS64, S, A, P);
    munit_assert_int64(result, ==, 0x401010);

    /* R_ARM64_CALL26: S + A - P */
    result = relocation_calculate_value(RELOC_ARM64_CALL26, S, A, P);
    munit_assert_int64(result, ==, 0xF10);

    return MUNIT_OK;
}

/* Test: Range validation */
static MunitResult test_range_validation(const MunitParameter params[], void* data) {
    (void)params;
    (void)data;

    /* 32-bit signed range */
    munit_assert_true(relocation_validate_range(RELOC_X64_PC32, 0));
    munit_assert_true(relocation_validate_range(RELOC_X64_PC32, 0x7FFFFFFF));
    munit_assert_true(relocation_validate_range(RELOC_X64_PC32, -0x80000000LL));
    munit_assert_false(relocation_validate_range(RELOC_X64_PC32, 0x80000000LL));
    munit_assert_false(relocation_validate_range(RELOC_X64_PC32, -0x80000001LL));

    /* 26-bit branch (byte offset, 4-byte aligned) */
    munit_assert_true(relocation_validate_range(RELOC_ARM64_CALL26, 0));
    munit_assert_true(relocation_validate_range(RELOC_ARM64_CALL26, 0x7FFFFFC));
    munit_assert_true(relocation_validate_range(RELOC_ARM64_CALL26, -0x8000000));
    munit_assert_false(relocation_validate_range(RELOC_ARM64_CALL26, 0x8000000));
    munit_assert_false(relocation_validate_range(RELOC_ARM64_CALL26, -0x8000004));
    munit_assert_false(relocation_validate_range(RELOC_ARM64_CALL26, 101)); /* Misaligned */

    /* 64-bit absolute (no range check) */
    munit_assert_true(relocation_validate_range(RELOC_X64_64, 0x7FFFFFFFFFFFFFFFLL));
    munit_assert_true(relocation_validate_range(RELOC_ARM64_ABS64, 0x7FFFFFFFFFFFFFFFLL));

    return MUNIT_OK;
}

/*
 * Relocation Processor Tests
 */

/* Test: Relocation processor lifecycle */
static MunitResult test_reloc_processor_lifecycle(const MunitParameter params[], void* data) {
    (void)params;
    (void)data;

    linker_context_t* ctx = linker_context_new();
    section_layout_t* layout = section_layout_new(0x400000, PLATFORM_FORMAT_ELF);
    symbol_resolver_t* resolver = symbol_resolver_new();

    relocation_processor_t* proc = relocation_processor_new(ctx, layout, resolver);
    munit_assert_not_null(proc);
    munit_assert_ptr_equal(proc->context, ctx);
    munit_assert_ptr_equal(proc->layout, layout);
    munit_assert_ptr_equal(proc->symbols, resolver);
    munit_assert_int(proc->error_count, ==, 0);
    munit_assert_int(proc->relocations_processed, ==, 0);

    relocation_processor_free(proc);
    symbol_resolver_free(resolver);
    section_layout_free(layout);
    linker_context_free(ctx);

    return MUNIT_OK;
}

/* Test: Simple x86-64 PC-relative relocation */
static MunitResult test_x64_pc_relative_reloc(const MunitParameter params[], void* data) {
    (void)params;
    (void)data;

    /* Create linker context */
    linker_context_t* ctx = linker_context_new();

    /* Create object with .text section and function */
    linker_object_t* obj = create_test_object("test.o", SECTION_TYPE_TEXT, 256,
                                                "foo", 0x100);

    /* Add relocation at offset 0x10 for symbol "foo" */
    add_relocation(obj, 0x10, RELOC_X64_PC32, 0, -4);

    linker_context_add_object(ctx, obj);

    /* Create section layout and compute addresses */
    section_layout_t* layout = section_layout_new(0x400000, PLATFORM_FORMAT_ELF);
    section_layout_add_object(layout, obj, 0);
    section_layout_compute(layout);

    /* Update symbol addresses - compute based on section + symbol offset */
    merged_section_t* text_section = section_layout_find_section_by_type(layout, SECTION_TYPE_TEXT);
    obj->symbols[0].final_address = text_section->vaddr + obj->symbols[0].value;

    /* Create symbol resolver */
    symbol_resolver_t* resolver = symbol_resolver_new();
    symbol_resolver_add_object(resolver, obj, 0);
    symbol_resolver_resolve(resolver);

    /* Create relocation processor and process */
    relocation_processor_t* proc = relocation_processor_new(ctx, layout, resolver);
    bool success = relocation_processor_process_all(proc);

    munit_assert_true(success);
    munit_assert_int(proc->error_count, ==, 0);
    munit_assert_int(proc->relocations_processed, ==, 1);

    /* Verify relocation was applied
     * S = base + section_offset + symbol_value = 0x401000 + 0x100 = 0x401100
     * A = -4
     * P = base + section_offset + reloc_offset = 0x401000 + 0x10 = 0x401010
     * Value = S + A - P = 0x401100 - 4 - 0x401010 = 0xEC */

    merged_section_t* text = section_layout_find_section_by_type(layout, SECTION_TYPE_TEXT);
    int32_t* patched = (int32_t*)(text->data + 0x10);
    munit_assert_int32(*patched, ==, 0xEC);

    relocation_processor_free(proc);
    symbol_resolver_free(resolver);
    section_layout_free(layout);
    linker_context_free(ctx);

    return MUNIT_OK;
}

/* Test: ARM64 branch relocation */
static MunitResult test_arm64_branch_reloc(const MunitParameter params[], void* data) {
    (void)params;
    (void)data;

    /* Create linker context */
    linker_context_t* ctx = linker_context_new();

    /* Create object with .text section and function */
    linker_object_t* obj = create_test_object("test.o", SECTION_TYPE_TEXT, 256,
                                                "target", 0x100);

    /* Add ARM64 BL instruction placeholder at offset 0x10 */
    uint32_t* code = (uint32_t*)(obj->sections[0].data + 0x10);
    *code = 0x94000000;  /* BL #0 */

    /* Add relocation for BL to "target" */
    add_relocation(obj, 0x10, RELOC_ARM64_CALL26, 0, 0);

    linker_context_add_object(ctx, obj);

    /* Create section layout */
    section_layout_t* layout = section_layout_new(0x100000000, PLATFORM_FORMAT_MACH_O);
    section_layout_add_object(layout, obj, 0);
    section_layout_compute(layout);

    /* Update symbol addresses - compute based on section + symbol offset */
    merged_section_t* text_section = section_layout_find_section_by_type(layout, SECTION_TYPE_TEXT);
    obj->symbols[0].final_address = text_section->vaddr + obj->symbols[0].value;

    /* Create symbol resolver */
    symbol_resolver_t* resolver = symbol_resolver_new();
    symbol_resolver_add_object(resolver, obj, 0);
    symbol_resolver_resolve(resolver);

    /* Process relocations */
    relocation_processor_t* proc = relocation_processor_new(ctx, layout, resolver);
    bool success = relocation_processor_process_all(proc);

    munit_assert_true(success);
    munit_assert_int(proc->error_count, ==, 0);
    munit_assert_int(proc->relocations_processed, ==, 1);

    /* Verify relocation
     * S = base + offset + symbol_value = 0x100001000 + 0x100 = 0x100001100
     * A = 0
     * P = base + offset + reloc_offset = 0x100001000 + 0x10 = 0x100001010
     * Value = S + A - P = 0x100001100 - 0x100001010 = 0xF0 (240 bytes = 60 instructions) */

    merged_section_t* text = section_layout_find_section_by_type(layout, SECTION_TYPE_TEXT);
    uint32_t* insn = (uint32_t*)(text->data + 0x10);
    uint32_t imm26 = *insn & 0x03FFFFFF;

    /* 0xF0 >> 2 = 60 (0x3C) */
    munit_assert_uint32(imm26, ==, 0x3C);

    /* Opcode should be preserved (BL = 0x94) */
    munit_assert_uint32(*insn >> 26, ==, 0x25);

    relocation_processor_free(proc);
    symbol_resolver_free(resolver);
    section_layout_free(layout);
    linker_context_free(ctx);

    return MUNIT_OK;
}

/* Test: Undefined symbol error */
static MunitResult test_undefined_symbol_error(const MunitParameter params[], void* data) {
    (void)params;
    (void)data;

    /* Create linker context */
    linker_context_t* ctx = linker_context_new();

    /* Create object with undefined symbol relocation */
    linker_object_t* obj = linker_object_new("test.o", PLATFORM_FORMAT_ELF);

    /* Add section */
    linker_section_t* section = linker_object_add_section(obj);
    section->name = strdup(".text");
    section->type = SECTION_TYPE_TEXT;
    section->size = 256;
    section->data = calloc(1, 256);

    /* Add undefined symbol */
    linker_symbol_t* symbol = linker_object_add_symbol(obj);
    symbol->name = strdup("undefined_func");
    symbol->is_defined = false;
    symbol->section_index = -1;
    symbol->defining_object = 0;  /* Symbol is from this object (index 0) */

    /* Add relocation referencing undefined symbol */
    add_relocation(obj, 0x10, RELOC_X64_PC32, 0, 0);

    linker_context_add_object(ctx, obj);

    /* Create layout and resolver */
    section_layout_t* layout = section_layout_new(0x400000, PLATFORM_FORMAT_ELF);
    section_layout_add_object(layout, obj, 0);
    section_layout_compute(layout);

    symbol_resolver_t* resolver = symbol_resolver_new();
    symbol_resolver_add_object(resolver, obj, 0);
    /* Note: Don't resolve - symbol remains undefined */

    /* Process relocations - should fail */
    relocation_processor_t* proc = relocation_processor_new(ctx, layout, resolver);
    bool success = relocation_processor_process_all(proc);

    munit_assert_false(success);
    munit_assert_int(proc->error_count, >, 0);

    /* Check error type */
    int error_count;
    relocation_error_t* errors = relocation_processor_get_errors(proc, &error_count);
    munit_assert_int(error_count, >, 0);
    munit_assert_int(errors[0].type, ==, RELOC_ERROR_UNDEFINED_SYMBOL);

    relocation_processor_free(proc);
    symbol_resolver_free(resolver);
    section_layout_free(layout);
    linker_context_free(ctx);

    return MUNIT_OK;
}

/* Test: Range overflow error */
static MunitResult test_range_overflow_error(const MunitParameter params[], void* data) {
    (void)params;
    (void)data;

    /* Create linker context */
    linker_context_t* ctx = linker_context_new();

    /* Create object */
    linker_object_t* obj = create_test_object("test.o", SECTION_TYPE_TEXT, 256,
                                                "far_func", 0x100);

    /* Add relocation that will overflow 32-bit PC-relative range
     * We'll manually set a very high final address */
    add_relocation(obj, 0x10, RELOC_X64_PC32, 0, 0);

    linker_context_add_object(ctx, obj);

    /* Create layout */
    section_layout_t* layout = section_layout_new(0x400000, PLATFORM_FORMAT_ELF);
    section_layout_add_object(layout, obj, 0);
    section_layout_compute(layout);

    /* Set symbol to far address that will cause overflow */
    obj->symbols[0].final_address = 0x100000000ULL;  /* 4GB away */

    /* Create resolver */
    symbol_resolver_t* resolver = symbol_resolver_new();
    symbol_resolver_add_object(resolver, obj, 0);
    symbol_resolver_resolve(resolver);

    /* Process relocations - should fail with range error */
    relocation_processor_t* proc = relocation_processor_new(ctx, layout, resolver);
    bool success = relocation_processor_process_all(proc);

    munit_assert_false(success);
    munit_assert_int(proc->error_count, >, 0);

    /* Check error type */
    int error_count;
    relocation_error_t* errors = relocation_processor_get_errors(proc, &error_count);
    munit_assert_int(error_count, >, 0);
    munit_assert_int(errors[0].type, ==, RELOC_ERROR_RANGE_OVERFLOW);

    relocation_processor_free(proc);
    symbol_resolver_free(resolver);
    section_layout_free(layout);
    linker_context_free(ctx);

    return MUNIT_OK;
}

/*
 * Test Suite Definition
 */

static MunitTest relocation_processor_tests[] = {
    /* Instruction patcher tests */
    {"/patcher/x64_imm32", test_x64_imm32_patch, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL},
    {"/patcher/x64_imm64", test_x64_imm64_patch, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL},
    {"/patcher/x64_rel32", test_x64_rel32_patch, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL},
    {"/patcher/arm64_branch26", test_arm64_branch26_patch, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL},
    {"/patcher/arm64_branch26_range", test_arm64_branch26_range, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL},
    {"/patcher/arm64_adrp", test_arm64_adrp_patch, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL},
    {"/patcher/arm64_add_imm12", test_arm64_add_imm12_patch, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL},
    {"/patcher/arm64_add_imm12_range", test_arm64_add_imm12_range, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL},

    /* Relocation calculation tests */
    {"/calculation/formulas", test_relocation_formulas, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL},
    {"/calculation/range_validation", test_range_validation, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL},

    /* Relocation processor tests */
    {"/processor/lifecycle", test_reloc_processor_lifecycle, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL},
    {"/processor/x64_pc_relative", test_x64_pc_relative_reloc, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL},
    {"/processor/arm64_branch", test_arm64_branch_reloc, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL},
    {"/processor/undefined_symbol", test_undefined_symbol_error, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL},
    {"/processor/range_overflow", test_range_overflow_error, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL},

    {NULL, NULL, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL}
};

static const MunitSuite relocation_processor_suite = {
    "relocation_processor",
    relocation_processor_tests,
    NULL,
    1,
    MUNIT_SUITE_OPTION_NONE
};

/* Get test suite */
const MunitSuite* get_relocation_processor_test_suite(void) {
    return &relocation_processor_suite;
}

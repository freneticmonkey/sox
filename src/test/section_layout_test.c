/*
 * Unit tests for section_layout.c
 *
 * Tests the section layout and address assignment engine including:
 * - Section merging from multiple object files
 * - Alignment handling
 * - Virtual address assignment
 * - Platform-specific layouts (ELF vs Mach-O)
 * - Symbol address calculation
 */

#include "../native/section_layout.h"
#include "../native/linker_core.h"
#include "../../ext/munit/munit.h"
#include <string.h>
#include <stdio.h>
#include <inttypes.h>

/* Test: Create and destroy section layout */
static MunitResult test_section_layout_lifecycle(const MunitParameter params[], void* data) {
    (void)params;
    (void)data;

    section_layout_t* layout = section_layout_new(0x400000, PLATFORM_FORMAT_ELF);
    munit_assert_not_null(layout);
    munit_assert_uint64(layout->base_address, ==, 0x400000);
    munit_assert_size(layout->page_size, ==, 4096);
    munit_assert_int(layout->section_count, ==, 0);

    section_layout_free(layout);

    return MUNIT_OK;
}

/* Test: Default base addresses for different platforms */
static MunitResult test_default_base_addresses(const MunitParameter params[], void* data) {
    (void)params;
    (void)data;

    /* ELF default */
    section_layout_t* elf_layout = section_layout_new(0, PLATFORM_FORMAT_ELF);
    munit_assert_not_null(elf_layout);
    munit_assert_uint64(elf_layout->base_address, ==, 0x400000);
    munit_assert_size(elf_layout->page_size, ==, 4096);
    section_layout_free(elf_layout);

    /* Mach-O default */
    section_layout_t* macho_layout = section_layout_new(0, PLATFORM_FORMAT_MACH_O);
    munit_assert_not_null(macho_layout);
    munit_assert_uint64(macho_layout->base_address, ==, 0x100000000);
    munit_assert_size(macho_layout->page_size, ==, 16384);
    section_layout_free(macho_layout);

    return MUNIT_OK;
}

/* Test: Alignment utility function */
static MunitResult test_align_to(const MunitParameter params[], void* data) {
    (void)params;
    (void)data;

    /* Already aligned */
    munit_assert_uint64(align_to(0x1000, 4096), ==, 0x1000);
    munit_assert_uint64(align_to(0x2000, 4096), ==, 0x2000);

    /* Needs alignment */
    munit_assert_uint64(align_to(0x1001, 4096), ==, 0x2000);
    munit_assert_uint64(align_to(0x1fff, 4096), ==, 0x2000);
    munit_assert_uint64(align_to(0x2001, 4096), ==, 0x3000);

    /* Small alignments */
    munit_assert_uint64(align_to(0x1001, 8), ==, 0x1008);
    munit_assert_uint64(align_to(0x1005, 16), ==, 0x1010);

    /* Zero and one alignment (no-ops) */
    munit_assert_uint64(align_to(0x1234, 0), ==, 0x1234);
    munit_assert_uint64(align_to(0x1234, 1), ==, 0x1234);

    return MUNIT_OK;
}

/* Test: Single section from one object file */
static MunitResult test_single_section_layout(const MunitParameter params[], void* data) {
    (void)params;
    (void)data;

    /* Create object with a .text section */
    linker_object_t* obj = linker_object_new("test.o", PLATFORM_FORMAT_ELF);
    munit_assert_not_null(obj);

    linker_section_t* text_section = linker_object_add_section(obj);
    text_section->name = strdup(".text");
    text_section->type = SECTION_TYPE_TEXT;
    text_section->size = 256;
    text_section->alignment = 16;
    text_section->flags = 0x5;  /* R-X */
    text_section->data = malloc(256);
    memset(text_section->data, 0x90, 256);  /* NOP instructions */

    /* Create layout and add object */
    section_layout_t* layout = section_layout_new(0x400000, PLATFORM_FORMAT_ELF);
    munit_assert_not_null(layout);

    section_layout_add_object(layout, obj, 0);
    munit_assert_int(layout->section_count, ==, 1);

    /* Compute layout */
    section_layout_compute(layout);

    /* Verify section was assigned an address */
    merged_section_t* text = section_layout_find_section_by_type(layout, SECTION_TYPE_TEXT);
    munit_assert_not_null(text);
    munit_assert_uint64(text->vaddr, >, 0x400000);
    munit_assert_uint64(text->vaddr, ==, 0x401000);  /* Base + page size */
    munit_assert_size(text->size, ==, 256);

    section_layout_free(layout);
    linker_object_free(obj);

    return MUNIT_OK;
}

/* Test: Multiple sections from one object file */
static MunitResult test_multiple_sections_layout(const MunitParameter params[], void* data) {
    (void)params;
    (void)data;

    /* Create object with multiple sections */
    linker_object_t* obj = linker_object_new("test.o", PLATFORM_FORMAT_ELF);
    munit_assert_not_null(obj);

    /* .text section */
    linker_section_t* text = linker_object_add_section(obj);
    text->name = strdup(".text");
    text->type = SECTION_TYPE_TEXT;
    text->size = 512;
    text->alignment = 16;
    text->data = malloc(512);
    memset(text->data, 0x90, 512);

    /* .data section */
    linker_section_t* data_sec = linker_object_add_section(obj);
    data_sec->name = strdup(".data");
    data_sec->type = SECTION_TYPE_DATA;
    data_sec->size = 128;
    data_sec->alignment = 8;
    data_sec->data = malloc(128);
    memset(data_sec->data, 0x00, 128);

    /* .bss section */
    linker_section_t* bss = linker_object_add_section(obj);
    bss->name = strdup(".bss");
    bss->type = SECTION_TYPE_BSS;
    bss->size = 64;
    bss->alignment = 8;
    bss->data = NULL;  /* BSS has no data */

    /* Create layout and process */
    section_layout_t* layout = section_layout_new(0x400000, PLATFORM_FORMAT_ELF);
    section_layout_add_object(layout, obj, 0);
    section_layout_compute(layout);

    /* Verify sections are in correct order: .text, .rodata, .data, .bss */
    munit_assert_int(layout->section_count, ==, 3);

    /* .text should be first and page-aligned */
    merged_section_t* merged_text = &layout->sections[0];
    munit_assert_int(merged_text->type, ==, SECTION_TYPE_TEXT);
    munit_assert_uint64(merged_text->vaddr, ==, 0x401000);

    /* .data should be after .text, page-aligned */
    merged_section_t* merged_data = &layout->sections[1];
    munit_assert_int(merged_data->type, ==, SECTION_TYPE_DATA);
    munit_assert_uint64(merged_data->vaddr, >, merged_text->vaddr);
    munit_assert_uint64(merged_data->vaddr % 4096, ==, 0);  /* Page aligned */

    /* .bss should be after .data */
    merged_section_t* merged_bss = &layout->sections[2];
    munit_assert_int(merged_bss->type, ==, SECTION_TYPE_BSS);
    munit_assert_uint64(merged_bss->vaddr, >, merged_data->vaddr);

    section_layout_free(layout);
    linker_object_free(obj);

    return MUNIT_OK;
}

/* Test: Merging sections from multiple object files */
static MunitResult test_section_merging(const MunitParameter params[], void* data) {
    (void)params;
    (void)data;

    /* Create first object */
    linker_object_t* obj1 = linker_object_new("obj1.o", PLATFORM_FORMAT_ELF);
    linker_section_t* text1 = linker_object_add_section(obj1);
    text1->name = strdup(".text");
    text1->type = SECTION_TYPE_TEXT;
    text1->size = 100;
    text1->alignment = 16;
    text1->data = malloc(100);
    memset(text1->data, 0xAA, 100);

    /* Create second object */
    linker_object_t* obj2 = linker_object_new("obj2.o", PLATFORM_FORMAT_ELF);
    linker_section_t* text2 = linker_object_add_section(obj2);
    text2->name = strdup(".text");
    text2->type = SECTION_TYPE_TEXT;
    text2->size = 200;
    text2->alignment = 8;
    text2->data = malloc(200);
    memset(text2->data, 0xBB, 200);

    /* Create layout and add both objects */
    section_layout_t* layout = section_layout_new(0x400000, PLATFORM_FORMAT_ELF);
    section_layout_add_object(layout, obj1, 0);
    section_layout_add_object(layout, obj2, 1);

    /* Should have only one merged .text section */
    munit_assert_int(layout->section_count, ==, 1);

    merged_section_t* merged_text = &layout->sections[0];
    munit_assert_int(merged_text->type, ==, SECTION_TYPE_TEXT);

    /* Size should be both sections plus alignment padding */
    /* obj1: 100 bytes, aligned to 16 = 112 bytes */
    /* obj2: 200 bytes, aligned to 8 (but follows 16-byte aligned, so no change) */
    /* Total: 112 + 200 = 312 bytes (or similar with alignment) */
    munit_assert_size(merged_text->size, >=, 300);
    munit_assert_size(merged_text->size, <=, 320);

    /* Alignment should be the maximum of both */
    munit_assert_size(merged_text->alignment, ==, 16);

    section_layout_free(layout);
    linker_object_free(obj1);
    linker_object_free(obj2);

    return MUNIT_OK;
}

/* Test: Section alignment handling */
static MunitResult test_section_alignment(const MunitParameter params[], void* data) {
    (void)params;
    (void)data;

    /* Create object with sections requiring different alignments */
    linker_object_t* obj = linker_object_new("test.o", PLATFORM_FORMAT_ELF);

    /* First section: 4-byte alignment */
    linker_section_t* sec1 = linker_object_add_section(obj);
    sec1->name = strdup(".data");
    sec1->type = SECTION_TYPE_DATA;
    sec1->size = 10;
    sec1->alignment = 4;
    sec1->data = malloc(10);

    /* Second section: 16-byte alignment */
    linker_section_t* sec2 = linker_object_add_section(obj);
    sec2->name = strdup(".data");
    sec2->type = SECTION_TYPE_DATA;
    sec2->size = 20;
    sec2->alignment = 16;
    sec2->data = malloc(20);

    /* Create layout */
    section_layout_t* layout = section_layout_new(0x400000, PLATFORM_FORMAT_ELF);
    section_layout_add_object(layout, obj, 0);

    merged_section_t* data_merged = section_layout_find_section_by_type(layout, SECTION_TYPE_DATA);
    munit_assert_not_null(data_merged);

    /* Merged section should have maximum alignment */
    munit_assert_size(data_merged->alignment, ==, 16);

    /* Size should account for alignment padding */
    /* sec1: 10 bytes, padded to 16 = 16 bytes */
    /* sec2: 20 bytes */
    /* Total: 36 bytes */
    munit_assert_size(data_merged->size, ==, 36);

    section_layout_free(layout);
    linker_object_free(obj);

    return MUNIT_OK;
}

/* Test: Page alignment for all sections */
static MunitResult test_page_alignment(const MunitParameter params[], void* data) {
    (void)params;
    (void)data;

    /* Create object with multiple sections */
    linker_object_t* obj = linker_object_new("test.o", PLATFORM_FORMAT_ELF);

    linker_section_t* text = linker_object_add_section(obj);
    text->name = strdup(".text");
    text->type = SECTION_TYPE_TEXT;
    text->size = 100;
    text->alignment = 1;
    text->data = malloc(100);

    linker_section_t* data_sec = linker_object_add_section(obj);
    data_sec->name = strdup(".data");
    data_sec->type = SECTION_TYPE_DATA;
    data_sec->size = 100;
    data_sec->alignment = 1;
    data_sec->data = malloc(100);

    linker_section_t* bss = linker_object_add_section(obj);
    bss->name = strdup(".bss");
    bss->type = SECTION_TYPE_BSS;
    bss->size = 100;
    bss->alignment = 1;
    bss->data = NULL;

    /* Create layout and compute addresses */
    section_layout_t* layout = section_layout_new(0x400000, PLATFORM_FORMAT_ELF);
    section_layout_add_object(layout, obj, 0);
    section_layout_compute(layout);

    /* All sections should be page-aligned (4KB = 4096 bytes) */
    for (int i = 0; i < layout->section_count; i++) {
        munit_assert_uint64(layout->sections[i].vaddr % 4096, ==, 0);
    }

    section_layout_free(layout);
    linker_object_free(obj);

    return MUNIT_OK;
}

/* Test: Mach-O layout with 16KB pages */
static MunitResult test_macho_layout(const MunitParameter params[], void* data) {
    (void)params;
    (void)data;

    /* Create object */
    linker_object_t* obj = linker_object_new("test.o", PLATFORM_FORMAT_MACH_O);

    linker_section_t* text = linker_object_add_section(obj);
    text->name = strdup("__TEXT,__text");
    text->type = SECTION_TYPE_TEXT;
    text->size = 1024;
    text->alignment = 16;
    text->data = malloc(1024);

    linker_section_t* data_sec = linker_object_add_section(obj);
    data_sec->name = strdup("__DATA,__data");
    data_sec->type = SECTION_TYPE_DATA;
    data_sec->size = 512;
    data_sec->alignment = 8;
    data_sec->data = malloc(512);

    /* Create Mach-O layout */
    section_layout_t* layout = section_layout_new(0, PLATFORM_FORMAT_MACH_O);
    munit_assert_uint64(layout->base_address, ==, 0x100000000);
    munit_assert_size(layout->page_size, ==, 16384);

    section_layout_add_object(layout, obj, 0);
    section_layout_compute(layout);

    /* All sections should be 16KB aligned */
    for (int i = 0; i < layout->section_count; i++) {
        munit_assert_uint64(layout->sections[i].vaddr % 16384, ==, 0);
    }

    /* First section should be at base + 16KB */
    munit_assert_uint64(layout->sections[0].vaddr, ==, 0x100000000 + 16384);

    section_layout_free(layout);
    linker_object_free(obj);

    return MUNIT_OK;
}

/* Test: Symbol address calculation */
static MunitResult test_symbol_address_calculation(const MunitParameter params[], void* data) {
    (void)params;
    (void)data;

    /* Create object with sections and symbols */
    linker_object_t* obj = linker_object_new("test.o", PLATFORM_FORMAT_ELF);

    linker_section_t* text = linker_object_add_section(obj);
    text->name = strdup(".text");
    text->type = SECTION_TYPE_TEXT;
    text->size = 256;
    text->alignment = 16;
    text->data = malloc(256);

    /* Create layout */
    section_layout_t* layout = section_layout_new(0x400000, PLATFORM_FORMAT_ELF);
    section_layout_add_object(layout, obj, 0);
    section_layout_compute(layout);

    /* Get merged text section address */
    merged_section_t* merged_text = section_layout_find_section_by_type(layout,
                                                                         SECTION_TYPE_TEXT);
    munit_assert_not_null(merged_text);
    uint64_t text_base = merged_text->vaddr;

    /* Calculate address for offset within section */
    uint64_t addr = section_layout_get_address(layout, 0, 0, 0x50);
    munit_assert_uint64(addr, ==, text_base + 0x50);

    section_layout_free(layout);
    linker_object_free(obj);

    return MUNIT_OK;
}

/* Test: Section contribution tracking */
static MunitResult test_section_contributions(const MunitParameter params[], void* data) {
    (void)params;
    (void)data;

    /* Create two objects */
    linker_object_t* obj1 = linker_object_new("obj1.o", PLATFORM_FORMAT_ELF);
    linker_section_t* text1 = linker_object_add_section(obj1);
    text1->name = strdup(".text");
    text1->type = SECTION_TYPE_TEXT;
    text1->size = 100;
    text1->alignment = 16;
    text1->data = malloc(100);

    linker_object_t* obj2 = linker_object_new("obj2.o", PLATFORM_FORMAT_ELF);
    linker_section_t* text2 = linker_object_add_section(obj2);
    text2->name = strdup(".text");
    text2->type = SECTION_TYPE_TEXT;
    text2->size = 200;
    text2->alignment = 16;
    text2->data = malloc(200);

    /* Create layout */
    section_layout_t* layout = section_layout_new(0x400000, PLATFORM_FORMAT_ELF);
    section_layout_add_object(layout, obj1, 0);
    section_layout_add_object(layout, obj2, 1);

    merged_section_t* text = section_layout_find_section_by_type(layout, SECTION_TYPE_TEXT);
    munit_assert_not_null(text);

    /* Verify contributions */
    int contrib_count = 0;
    section_contribution_t* contrib = text->contributions;
    while (contrib != NULL) {
        contrib_count++;
        munit_assert_int(contrib->object_index, >=, 0);
        munit_assert_int(contrib->object_index, <, 2);
        contrib = contrib->next;
    }

    munit_assert_int(contrib_count, ==, 2);

    section_layout_free(layout);
    linker_object_free(obj1);
    linker_object_free(obj2);

    return MUNIT_OK;
}

/* Test: Total size calculation */
static MunitResult test_total_size(const MunitParameter params[], void* data) {
    (void)params;
    (void)data;

    /* Create object */
    linker_object_t* obj = linker_object_new("test.o", PLATFORM_FORMAT_ELF);

    linker_section_t* text = linker_object_add_section(obj);
    text->name = strdup(".text");
    text->type = SECTION_TYPE_TEXT;
    text->size = 1000;
    text->alignment = 16;
    text->data = malloc(1000);

    linker_section_t* data_sec = linker_object_add_section(obj);
    data_sec->name = strdup(".data");
    data_sec->type = SECTION_TYPE_DATA;
    data_sec->size = 500;
    data_sec->alignment = 8;
    data_sec->data = malloc(500);

    /* Create layout */
    section_layout_t* layout = section_layout_new(0x400000, PLATFORM_FORMAT_ELF);
    section_layout_add_object(layout, obj, 0);
    section_layout_compute(layout);

    /* Total size should be greater than section sizes due to alignment */
    munit_assert_uint64(layout->total_size, >, 1500);
    munit_assert_uint64(layout->total_size, >, 0);

    section_layout_free(layout);
    linker_object_free(obj);

    return MUNIT_OK;
}

/* Test suite definition */
static MunitTest section_layout_tests[] = {
    { "/lifecycle", test_section_layout_lifecycle, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL },
    { "/default_base_addresses", test_default_base_addresses, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL },
    { "/align_to", test_align_to, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL },
    { "/single_section", test_single_section_layout, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL },
    { "/multiple_sections", test_multiple_sections_layout, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL },
    { "/section_merging", test_section_merging, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL },
    { "/section_alignment", test_section_alignment, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL },
    { "/page_alignment", test_page_alignment, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL },
    { "/macho_layout", test_macho_layout, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL },
    { "/symbol_address", test_symbol_address_calculation, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL },
    { "/contributions", test_section_contributions, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL },
    { "/total_size", test_total_size, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL },
    { NULL, NULL, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL }
};

static const MunitSuite section_layout_suite = {
    "/section_layout",
    section_layout_tests,
    NULL,
    1,
    MUNIT_SUITE_OPTION_NONE
};

/* Export the suite */
const MunitSuite* get_section_layout_test_suite(void) {
    return &section_layout_suite;
}

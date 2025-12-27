#include <munit/munit.h>
#include "native/elf_executable.h"
#include "native/linker_core.h"
#include "lib/memory.h"
#include <string.h>
#include <unistd.h>

/* Helper: Create a minimal linker context with a .text section */
static linker_context_t* create_test_context(void) {
    linker_context_t* context = linker_context_new();
    context->target_format = PLATFORM_FORMAT_ELF;
    context->base_address = 0x400000;

    /* Allocate merged sections array */
    context->merged_section_count = 1;
    context->merged_sections = (linker_section_t*)l_mem_alloc(
        sizeof(linker_section_t) * context->merged_section_count);

    /* Create .text section with simple code */
    linker_section_t* text = &context->merged_sections[0];
    linker_section_init(text);
    text->name = strdup(".text");
    text->type = SECTION_TYPE_TEXT;
    text->alignment = 16;

    /* Simple x86-64 code: mov rax, 42; ret */
    uint8_t code[] = {
        0x48, 0xC7, 0xC0, 0x2A, 0x00, 0x00, 0x00,  /* mov rax, 42 */
        0xC3                                         /* ret */
    };
    text->size = sizeof(code);
    text->data = (uint8_t*)l_mem_alloc(text->size);
    memcpy(text->data, code, text->size);

    /* Create main symbol */
    context->global_symbol_count = 1;
    context->global_symbols = (linker_symbol_t*)l_mem_alloc(
        sizeof(linker_symbol_t) * context->global_symbol_count);

    linker_symbol_t* main_sym = &context->global_symbols[0];
    linker_symbol_init(main_sym);
    main_sym->name = strdup("main");
    main_sym->type = SYMBOL_TYPE_FUNC;
    main_sym->binding = SYMBOL_BINDING_GLOBAL;
    main_sym->is_defined = true;
    main_sym->section_index = 0;
    main_sym->value = 0;

    return context;
}

/* Test: Get default entry point options */
static MunitResult test_get_default_entry_options(const MunitParameter params[],
                                                    void* user_data) {
    (void)params;
    (void)user_data;

    entry_point_options_t x64_opts = elf_get_default_entry_options(EM_X86_64);
    munit_assert_true(x64_opts.generate_start);
    munit_assert_true(x64_opts.call_main);
    munit_assert_uint16(x64_opts.machine_type, ==, EM_X86_64);

    entry_point_options_t arm64_opts = elf_get_default_entry_options(EM_AARCH64);
    munit_assert_true(arm64_opts.generate_start);
    munit_assert_true(arm64_opts.call_main);
    munit_assert_uint16(arm64_opts.machine_type, ==, EM_AARCH64);

    return MUNIT_OK;
}

/* Test: Find section by type */
static MunitResult test_find_section_by_type(const MunitParameter params[],
                                               void* user_data) {
    (void)params;
    (void)user_data;

    linker_context_t* context = create_test_context();

    linker_section_t* text = elf_find_section_by_type(context, SECTION_TYPE_TEXT);
    munit_assert_not_null(text);
    munit_assert_string_equal(text->name, ".text");

    linker_section_t* data = elf_find_section_by_type(context, SECTION_TYPE_DATA);
    munit_assert_null(data);

    linker_context_free(context);
    return MUNIT_OK;
}

/* Test: Calculate layout */
static MunitResult test_calculate_layout(const MunitParameter params[],
                                           void* user_data) {
    (void)params;
    (void)user_data;

    linker_context_t* context = create_test_context();

    bool result = elf_calculate_layout(context);
    munit_assert_true(result);

    /* Check that .text section got a virtual address */
    linker_section_t* text = &context->merged_sections[0];
    munit_assert_uint64(text->vaddr, >, 0);
    munit_assert_uint64(text->vaddr, >=, context->base_address);

    /* Check total size is calculated */
    munit_assert_uint64(context->total_size, >, 0);

    linker_context_free(context);
    return MUNIT_OK;
}

/* Test: Calculate layout with multiple sections */
static MunitResult test_calculate_layout_multi_section(const MunitParameter params[],
                                                         void* user_data) {
    (void)params;
    (void)user_data;

    linker_context_t* context = create_test_context();

    /* Add .data and .bss sections */
    context->merged_section_count = 3;
    context->merged_sections = (linker_section_t*)l_mem_realloc(
        context->merged_sections,
        sizeof(linker_section_t) * 1,
        sizeof(linker_section_t) * 3);

    /* .data section */
    linker_section_t* data = &context->merged_sections[1];
    linker_section_init(data);
    data->name = strdup(".data");
    data->type = SECTION_TYPE_DATA;
    data->alignment = 8;
    data->size = 64;
    data->data = (uint8_t*)l_mem_alloc(64);
    memset(data->data, 0, 64);

    /* .bss section */
    linker_section_t* bss = &context->merged_sections[2];
    linker_section_init(bss);
    bss->name = strdup(".bss");
    bss->type = SECTION_TYPE_BSS;
    bss->alignment = 8;
    bss->size = 128;
    bss->data = NULL;  /* BSS has no data */

    bool result = elf_calculate_layout(context);
    munit_assert_true(result);

    /* Check all sections have addresses */
    munit_assert_uint64(context->merged_sections[0].vaddr, >, 0);
    munit_assert_uint64(context->merged_sections[1].vaddr, >, 0);
    munit_assert_uint64(context->merged_sections[2].vaddr, >, 0);

    /* Check sections are in order */
    munit_assert_uint64(context->merged_sections[1].vaddr, >,
                        context->merged_sections[0].vaddr);
    munit_assert_uint64(context->merged_sections[2].vaddr, >,
                        context->merged_sections[1].vaddr);

    linker_context_free(context);
    return MUNIT_OK;
}

/* Test: Generate x86-64 entry point */
static MunitResult test_generate_entry_point_x64(const MunitParameter params[],
                                                   void* user_data) {
    (void)params;
    (void)user_data;

    linker_context_t* context = create_test_context();

    /* Calculate layout first */
    elf_calculate_layout(context);

    /* Update main symbol with final address */
    context->global_symbols[0].final_address = context->merged_sections[0].vaddr;

    /* Generate entry point */
    entry_point_options_t options = elf_get_default_entry_options(EM_X86_64);
    bool result = elf_generate_entry_point(context, &options);
    munit_assert_true(result);

    /* Check entry point is set */
    munit_assert_uint64(context->entry_point, ==, context->merged_sections[0].vaddr);

    /* Check .text section size increased */
    linker_section_t* text = &context->merged_sections[0];
    munit_assert_size(text->size, >, 8);  /* Original code was 8 bytes */

    /* Check entry code starts with xor rbp, rbp (0x48 0x31 0xED) */
    munit_assert_uint8(text->data[0], ==, 0x48);
    munit_assert_uint8(text->data[1], ==, 0x31);
    munit_assert_uint8(text->data[2], ==, 0xED);

    /* Check for call instruction (0xE8) */
    munit_assert_uint8(text->data[3], ==, 0xE8);

    /* x64 entry is 18 bytes total, syscall is at bytes 16-17 */
    /* Check for syscall instruction (0x0F 0x05) at bytes 16-17 */
    if (text->size < 18) {
        munit_errorf("Text section too small: %zu bytes", text->size);
    }
    munit_assert_uint8(text->data[16], ==, 0x0F);
    munit_assert_uint8(text->data[17], ==, 0x05);

    linker_context_free(context);
    return MUNIT_OK;
}

/* Test: Generate ARM64 entry point */
static MunitResult test_generate_entry_point_arm64(const MunitParameter params[],
                                                     void* user_data) {
    (void)params;
    (void)user_data;

    linker_context_t* context = create_test_context();

    /* Calculate layout first */
    elf_calculate_layout(context);

    /* Update main symbol with final address */
    context->global_symbols[0].final_address = context->merged_sections[0].vaddr + 16;

    /* Generate entry point */
    entry_point_options_t options = elf_get_default_entry_options(EM_AARCH64);
    bool result = elf_generate_entry_point(context, &options);
    munit_assert_true(result);

    /* Check entry point is set */
    munit_assert_uint64(context->entry_point, ==, context->merged_sections[0].vaddr);

    /* Check .text section size increased */
    linker_section_t* text = &context->merged_sections[0];
    munit_assert_size(text->size, >, 8);  /* Original code was 8 bytes */

    /* Check entry code starts with mov x29, #0 (0xD2 0x80 0x00 0x1D in little-endian) */
    munit_assert_uint8(text->data[0], ==, 0x1D);
    munit_assert_uint8(text->data[1], ==, 0x00);
    munit_assert_uint8(text->data[2], ==, 0x80);
    munit_assert_uint8(text->data[3], ==, 0xD2);

    /* Check for bl instruction at offset 4 (opcode 0x94xxxxxx) */
    munit_assert_uint8(text->data[7], ==, 0x94);

    /* Check for svc #0 at bytes 12-15 (0x01 0x00 0x00 0xD4 in little-endian) */
    munit_assert_uint8(text->data[12], ==, 0x01);
    munit_assert_uint8(text->data[13], ==, 0x00);
    munit_assert_uint8(text->data[14], ==, 0x00);
    munit_assert_uint8(text->data[15], ==, 0xD4);

    linker_context_free(context);
    return MUNIT_OK;
}

/* Test: Write minimal executable */
static MunitResult test_write_executable(const MunitParameter params[],
                                           void* user_data) {
    (void)params;
    (void)user_data;

    linker_context_t* context = create_test_context();

    /* Calculate layout */
    elf_calculate_layout(context);

    /* Update main symbol */
    context->global_symbols[0].final_address = context->merged_sections[0].vaddr;

    /* Generate entry point */
    #ifdef __aarch64__
    entry_point_options_t options = elf_get_default_entry_options(EM_AARCH64);
    #else
    entry_point_options_t options = elf_get_default_entry_options(EM_X86_64);
    #endif

    elf_generate_entry_point(context, &options);

    /* Write executable */
    const char* output_path = "/tmp/test_executable";
    bool result = elf_write_executable(output_path, context);
    munit_assert_true(result);

    /* Check file exists and is executable */
    munit_assert_int(access(output_path, F_OK), ==, 0);
    munit_assert_int(access(output_path, X_OK), ==, 0);

    /* Read and verify ELF header */
    FILE* f = fopen(output_path, "rb");
    munit_assert_not_null(f);

    Elf64_Ehdr ehdr;
    size_t read = fread(&ehdr, sizeof(ehdr), 1, f);
    munit_assert_size(read, ==, 1);

    /* Check ELF magic */
    munit_assert_uint8(ehdr.e_ident[EI_MAG0], ==, ELFMAG0);
    munit_assert_uint8(ehdr.e_ident[EI_MAG1], ==, ELFMAG1);
    munit_assert_uint8(ehdr.e_ident[EI_MAG2], ==, ELFMAG2);
    munit_assert_uint8(ehdr.e_ident[EI_MAG3], ==, ELFMAG3);

    /* Check ELF class and data encoding */
    munit_assert_uint8(ehdr.e_ident[EI_CLASS], ==, ELFCLASS64);
    munit_assert_uint8(ehdr.e_ident[EI_DATA], ==, ELFDATA2LSB);

    /* Check file type */
    munit_assert_uint16(ehdr.e_type, ==, ET_EXEC);

    /* Check machine type */
    #ifdef __aarch64__
    munit_assert_uint16(ehdr.e_machine, ==, EM_AARCH64);
    #else
    munit_assert_uint16(ehdr.e_machine, ==, EM_X86_64);
    #endif

    /* Check entry point */
    munit_assert_uint64(ehdr.e_entry, ==, context->entry_point);

    /* Check program header count */
    munit_assert_uint16(ehdr.e_phnum, ==, 2);

    /* Read and verify program headers */
    Elf64_Phdr phdr_text, phdr_data;
    fseek(f, ehdr.e_phoff, SEEK_SET);
    fread(&phdr_text, sizeof(phdr_text), 1, f);
    fread(&phdr_data, sizeof(phdr_data), 1, f);

    /* Check text segment */
    munit_assert_uint32(phdr_text.p_type, ==, PT_LOAD);
    munit_assert_uint32(phdr_text.p_flags, ==, (PF_R | PF_X));
    munit_assert_uint64(phdr_text.p_vaddr, >, 0);

    fclose(f);

    /* Clean up */
    unlink(output_path);
    linker_context_free(context);

    return MUNIT_OK;
}

/* Test: Write executable with data section */
static MunitResult test_write_executable_with_data(const MunitParameter params[],
                                                     void* user_data) {
    (void)params;
    (void)user_data;

    linker_context_t* context = create_test_context();

    /* Add .data section */
    context->merged_section_count = 2;
    context->merged_sections = (linker_section_t*)l_mem_realloc(
        context->merged_sections,
        sizeof(linker_section_t) * 1,
        sizeof(linker_section_t) * 2);

    linker_section_t* data = &context->merged_sections[1];
    linker_section_init(data);
    data->name = strdup(".data");
    data->type = SECTION_TYPE_DATA;
    data->alignment = 8;
    data->size = 32;
    data->data = (uint8_t*)l_mem_alloc(32);
    memset(data->data, 0x42, 32);

    /* Calculate layout */
    elf_calculate_layout(context);

    /* Update main symbol */
    context->global_symbols[0].final_address = context->merged_sections[0].vaddr;

    /* Generate entry point */
    #ifdef __aarch64__
    entry_point_options_t options = elf_get_default_entry_options(EM_AARCH64);
    #else
    entry_point_options_t options = elf_get_default_entry_options(EM_X86_64);
    #endif

    elf_generate_entry_point(context, &options);

    /* Write executable */
    const char* output_path = "/tmp/test_executable_data";
    bool result = elf_write_executable(output_path, context);
    munit_assert_true(result);

    /* Check file exists */
    munit_assert_int(access(output_path, F_OK), ==, 0);

    /* Read and verify program headers */
    FILE* f = fopen(output_path, "rb");
    munit_assert_not_null(f);

    Elf64_Ehdr ehdr;
    fread(&ehdr, sizeof(ehdr), 1, f);

    Elf64_Phdr phdr_text, phdr_data;
    fseek(f, ehdr.e_phoff, SEEK_SET);
    fread(&phdr_text, sizeof(phdr_text), 1, f);
    fread(&phdr_data, sizeof(phdr_data), 1, f);

    /* Check data segment exists and has write permission */
    munit_assert_uint32(phdr_data.p_type, ==, PT_LOAD);
    munit_assert_uint32(phdr_data.p_flags, ==, (PF_R | PF_W));
    munit_assert_uint64(phdr_data.p_filesz, ==, 32);
    munit_assert_uint64(phdr_data.p_memsz, ==, 32);

    fclose(f);

    /* Clean up */
    unlink(output_path);
    linker_context_free(context);

    return MUNIT_OK;
}

/* Test: Error handling - invalid parameters */
static MunitResult test_error_invalid_params(const MunitParameter params[],
                                               void* user_data) {
    (void)params;
    (void)user_data;

    linker_context_t* context = create_test_context();
    entry_point_options_t options = elf_get_default_entry_options(EM_X86_64);

    /* NULL context */
    munit_assert_false(elf_write_executable("/tmp/test", NULL));
    munit_assert_false(elf_generate_entry_point(NULL, &options));
    munit_assert_null(elf_find_section_by_type(NULL, SECTION_TYPE_TEXT));

    /* NULL path */
    munit_assert_false(elf_write_executable(NULL, context));

    /* NULL options */
    munit_assert_false(elf_generate_entry_point(context, NULL));

    linker_context_free(context);
    return MUNIT_OK;
}

/* Test: Error handling - missing main symbol */
static MunitResult test_error_no_main(const MunitParameter params[],
                                        void* user_data) {
    (void)params;
    (void)user_data;

    linker_context_t* context = create_test_context();
    elf_calculate_layout(context);

    /* Remove main symbol */
    l_mem_free(context->global_symbols[0].name, strlen(context->global_symbols[0].name) + 1);
    l_mem_free(context->global_symbols, sizeof(linker_symbol_t));
    context->global_symbols = NULL;
    context->global_symbol_count = 0;

    entry_point_options_t options = elf_get_default_entry_options(EM_X86_64);
    bool result = elf_generate_entry_point(context, &options);
    munit_assert_false(result);

    linker_context_free(context);
    return MUNIT_OK;
}

/* Test: Error handling - no text section */
static MunitResult test_error_no_text(const MunitParameter params[],
                                        void* user_data) {
    (void)params;
    (void)user_data;

    linker_context_t* context = linker_context_new();
    context->target_format = PLATFORM_FORMAT_ELF;

    /* No sections at all */
    bool result = elf_write_executable("/tmp/test", context);
    munit_assert_false(result);

    linker_context_free(context);
    return MUNIT_OK;
}

/* Define test suite */
static MunitTest elf_executable_tests[] = {
    {"/get_default_entry_options", test_get_default_entry_options, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL},
    {"/find_section_by_type", test_find_section_by_type, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL},
    {"/calculate_layout", test_calculate_layout, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL},
    {"/calculate_layout_multi_section", test_calculate_layout_multi_section, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL},
    {"/generate_entry_point_x64", test_generate_entry_point_x64, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL},
    {"/generate_entry_point_arm64", test_generate_entry_point_arm64, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL},
    {"/write_executable", test_write_executable, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL},
    {"/write_executable_with_data", test_write_executable_with_data, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL},
    {"/error_invalid_params", test_error_invalid_params, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL},
    {"/error_no_main", test_error_no_main, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL},
    {"/error_no_text", test_error_no_text, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL},
    {NULL, NULL, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL}
};

MunitSuite elf_executable_suite = {
    "/elf_executable",
    elf_executable_tests,
    NULL,
    1,
    MUNIT_SUITE_OPTION_NONE
};

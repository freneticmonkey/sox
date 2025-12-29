#include "elf_executable.h"
#include "../lib/memory.h"
#include "../lib/file.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

/* Default memory layout */
#define DEFAULT_BASE_ADDRESS 0x400000
#define PAGE_SIZE 4096
#define TEXT_BASE 0x400000
#define DATA_BASE 0x600000

/* Align value to alignment (must be power of 2) */
static uint64_t align_up(uint64_t value, uint64_t alignment) {
    return (value + alignment - 1) & ~(alignment - 1);
}

/* Round up to next page boundary */
static uint64_t page_align(uint64_t value) {
    return align_up(value, PAGE_SIZE);
}

entry_point_options_t elf_get_default_entry_options(uint16_t machine_type) {
    entry_point_options_t options = {0};
    options.generate_start = true;
    options.call_main = true;
    options.machine_type = machine_type;
    return options;
}

linker_section_t* elf_find_section_by_type(linker_context_t* context,
                                             section_type_t type) {
    if (!context || !context->merged_sections) {
        return NULL;
    }

    for (int i = 0; i < context->merged_section_count; i++) {
        if (context->merged_sections[i].type == type) {
            return &context->merged_sections[i];
        }
    }

    return NULL;
}

bool elf_calculate_layout(linker_context_t* context) {
    if (!context || !context->merged_sections) {
        fprintf(stderr, "elf_calculate_layout: invalid context\n");
        return false;
    }

    /* Use base address from context or default */
    uint64_t base_addr = context->base_address;
    if (base_addr == 0) {
        base_addr = DEFAULT_BASE_ADDRESS;
        context->base_address = base_addr;
    }

    /* Calculate layout for each section type in order:
     * .text at base_address
     * .rodata after .text (page-aligned)
     * .data after .rodata (page-aligned)
     * .bss after .data (no file space, just virtual)
     */
    uint64_t current_vaddr = base_addr;
    uint64_t current_offset = 0;

    /* Reserve space for ELF header and program headers */
    size_t header_size = sizeof(Elf64_Ehdr) + 2 * sizeof(Elf64_Phdr);
    current_offset = page_align(header_size);
    current_vaddr = page_align(base_addr + current_offset);

    for (int i = 0; i < context->merged_section_count; i++) {
        linker_section_t* section = &context->merged_sections[i];

        /* Align based on section requirements */
        if (section->alignment > 0) {
            current_vaddr = align_up(current_vaddr, section->alignment);
        }

        /* Assign virtual address */
        section->vaddr = current_vaddr;

        /* Move to next section */
        if (section->type != SECTION_TYPE_BSS) {
            /* BSS doesn't occupy file space */
            current_vaddr += section->size;
            /* Page align between different section types */
            if (i + 1 < context->merged_section_count) {
                section_type_t next_type = context->merged_sections[i + 1].type;
                if (next_type != section->type) {
                    current_vaddr = page_align(current_vaddr);
                }
            }
        } else {
            /* BSS is in memory only */
            current_vaddr += section->size;
        }
    }

    /* Calculate total size */
    context->total_size = current_vaddr - base_addr;

    return true;
}

bool elf_generate_entry_point(linker_context_t* context,
                                const entry_point_options_t* options) {
    if (!context || !options) {
        fprintf(stderr, "elf_generate_entry_point: invalid parameters\n");
        return false;
    }

    if (!options->generate_start) {
        return true;  /* Nothing to do */
    }

    /* Find .text section to add entry point code */
    linker_section_t* text_section = elf_find_section_by_type(context,
                                                                SECTION_TYPE_TEXT);
    if (!text_section) {
        fprintf(stderr, "elf_generate_entry_point: no .text section found\n");
        return false;
    }

    /* Find main symbol to calculate call offset */
    linker_symbol_t* main_sym = NULL;
    for (int i = 0; i < context->global_symbol_count; i++) {
        if (strcmp(context->global_symbols[i].name, "main") == 0) {
            main_sym = &context->global_symbols[i];
            break;
        }
    }

    if (!main_sym || !main_sym->is_defined) {
        fprintf(stderr, "elf_generate_entry_point: main symbol not found\n");
        return false;
    }

    /* Generate architecture-specific entry point code */
    uint8_t* entry_code = NULL;
    size_t entry_size = 0;

    if (options->machine_type == EM_X86_64) {
        /*
         * x86-64 entry point:
         *   xor rbp, rbp           ; Clear frame pointer
         *   call main              ; Call main function
         *   mov rdi, rax           ; Exit code from main
         *   mov rax, 60            ; sys_exit syscall number
         *   syscall                ; Exit
         */
        static uint8_t x64_start_template[] = {
            0x48, 0x31, 0xED,              /* xor rbp, rbp */
            0xE8, 0x00, 0x00, 0x00, 0x00,  /* call main (offset to be patched) */
            0x48, 0x89, 0xC7,              /* mov rdi, rax */
            0xB8, 0x3C, 0x00, 0x00, 0x00,  /* mov eax, 60 */
            0x0F, 0x05                      /* syscall */
        };

        entry_size = sizeof(x64_start_template);
        entry_code = (uint8_t*)l_mem_alloc(entry_size);
        memcpy(entry_code, x64_start_template, entry_size);

        /* Calculate call offset (relative to next instruction after call) */
        /* _start is at text_section->vaddr, call instruction is at offset 3 */
        uint64_t call_addr = text_section->vaddr + 3;
        uint64_t next_instr_addr = call_addr + 5;  /* call is 5 bytes */
        int32_t call_offset = (int32_t)(main_sym->final_address - next_instr_addr);

        /* Patch the call offset at bytes 4-7 */
        memcpy(&entry_code[4], &call_offset, sizeof(int32_t));

        /* Set entry point to start of .text section */
        context->entry_point = text_section->vaddr;

    } else if (options->machine_type == EM_AARCH64) {
        /*
         * ARM64 entry point:
         *   mov x29, #0            ; Clear frame pointer
         *   bl main                ; Call main function
         *   mov x8, #93            ; sys_exit syscall number
         *   svc #0                 ; Syscall (exit code already in x0)
         */
        static uint8_t arm64_start_template[] = {
            0x1D, 0x00, 0x80, 0xD2,  /* mov x29, #0 */
            0x00, 0x00, 0x00, 0x94,  /* bl main (offset to be patched) */
            0xA8, 0x0B, 0x80, 0xD2,  /* mov x8, #93 */
            0x01, 0x00, 0x00, 0xD4   /* svc #0 */
        };

        entry_size = sizeof(arm64_start_template);
        entry_code = (uint8_t*)l_mem_alloc(entry_size);
        memcpy(entry_code, arm64_start_template, entry_size);

        /* Calculate branch offset (in instructions, relative to bl instruction) */
        /* _start is at text_section->vaddr, bl instruction is at offset 4 */
        uint64_t bl_addr = text_section->vaddr + 4;
        int64_t byte_offset = main_sym->final_address - bl_addr;
        int32_t instr_offset = (int32_t)(byte_offset / 4);  /* ARM64 instructions are 4 bytes */

        /* ARM64 BL encoding: 0x94000000 | (offset & 0x03FFFFFF) */
        uint32_t bl_instr = 0x94000000 | (instr_offset & 0x03FFFFFF);

        /* Patch the bl instruction at bytes 4-7 */
        memcpy(&entry_code[4], &bl_instr, sizeof(uint32_t));

        /* Set entry point to start of .text section */
        context->entry_point = text_section->vaddr;

    } else {
        fprintf(stderr, "elf_generate_entry_point: unsupported machine type %d\n",
                options->machine_type);
        return false;
    }

    /* Prepend entry code to .text section */
    size_t old_size = text_section->size;
    size_t new_size = old_size + entry_size;
    uint8_t* new_data = (uint8_t*)l_mem_alloc(new_size);

    /* Copy entry code first, then existing code */
    memcpy(new_data, entry_code, entry_size);
    if (text_section->data && old_size > 0) {
        memcpy(new_data + entry_size, text_section->data, old_size);
        l_mem_free(text_section->data, old_size);
    }

    text_section->data = new_data;
    text_section->size = new_size;

    /* Update main symbol address to account for prepended entry code */
    main_sym->final_address += entry_size;

    /* Free temporary entry code buffer */
    l_mem_free(entry_code, entry_size);

    return true;
}

bool elf_write_executable(const char* output_path,
                           linker_context_t* context) {
    if (!output_path || !context) {
        fprintf(stderr, "elf_write_executable: invalid parameters\n");
        return false;
    }

    /* Calculate layout if not already done */
    if (context->merged_section_count > 0 &&
        context->merged_sections[0].vaddr == 0) {
        if (!elf_calculate_layout(context)) {
            return false;
        }
    }

    /* Determine machine type from target format */
    uint16_t machine_type;
    if (context->target_format == PLATFORM_FORMAT_ELF) {
        /* Default to x86-64 for now - should be configurable */
        #ifdef __aarch64__
        machine_type = EM_AARCH64;
        #else
        machine_type = EM_X86_64;
        #endif
    } else {
        fprintf(stderr, "elf_write_executable: unsupported target format\n");
        return false;
    }

    /* Find required sections */
    linker_section_t* text_section = elf_find_section_by_type(context,
                                                                SECTION_TYPE_TEXT);
    linker_section_t* data_section = elf_find_section_by_type(context,
                                                                SECTION_TYPE_DATA);
    linker_section_t* rodata_section = elf_find_section_by_type(context,
                                                                  SECTION_TYPE_RODATA);
    linker_section_t* bss_section = elf_find_section_by_type(context,
                                                               SECTION_TYPE_BSS);

    if (!text_section) {
        fprintf(stderr, "elf_write_executable: no .text section found\n");
        return false;
    }

    /* Create ELF header */
    Elf64_Ehdr ehdr = {0};

    /* ELF magic number */
    ehdr.e_ident[EI_MAG0] = ELFMAG0;
    ehdr.e_ident[EI_MAG1] = ELFMAG1;
    ehdr.e_ident[EI_MAG2] = ELFMAG2;
    ehdr.e_ident[EI_MAG3] = ELFMAG3;
    ehdr.e_ident[EI_CLASS] = ELFCLASS64;
    ehdr.e_ident[EI_DATA] = ELFDATA2LSB;
    ehdr.e_ident[EI_VERSION] = EV_CURRENT;
    ehdr.e_ident[EI_OSABI] = ELFOSABI_SYSV;
    ehdr.e_ident[EI_ABIVERSION] = 0;

    /* Clear padding bytes */
    for (int i = EI_PAD; i < EI_NIDENT; i++) {
        ehdr.e_ident[i] = 0;
    }

    /* ELF header fields */
    ehdr.e_type = ET_EXEC;  /* Executable file */
    ehdr.e_machine = machine_type;
    ehdr.e_version = EV_CURRENT;
    ehdr.e_entry = context->entry_point;
    ehdr.e_phoff = sizeof(Elf64_Ehdr);  /* Program headers immediately after ELF header */
    ehdr.e_shoff = 0;  /* No section headers for minimal executable */
    ehdr.e_flags = 0;
    ehdr.e_ehsize = sizeof(Elf64_Ehdr);
    ehdr.e_phentsize = sizeof(Elf64_Phdr);
    ehdr.e_phnum = 2;  /* Two segments: code (R-X) and data (RW-) */
    ehdr.e_shentsize = 0;
    ehdr.e_shnum = 0;
    ehdr.e_shstrndx = 0;

    /* Create program headers */
    Elf64_Phdr phdr_text = {0};
    Elf64_Phdr phdr_data = {0};

    /* Calculate file offsets and sizes */
    size_t header_size = sizeof(Elf64_Ehdr) + 2 * sizeof(Elf64_Phdr);
    uint64_t text_offset = page_align(header_size);
    uint64_t text_size = text_section->size;
    if (rodata_section) {
        text_size += rodata_section->size;
    }

    /* Text segment (code + rodata) */
    phdr_text.p_type = PT_LOAD;
    phdr_text.p_flags = PF_R | PF_X;  /* Read + Execute */
    phdr_text.p_offset = text_offset;
    phdr_text.p_vaddr = text_section->vaddr;
    phdr_text.p_paddr = text_section->vaddr;
    phdr_text.p_filesz = text_size;
    phdr_text.p_memsz = text_size;
    phdr_text.p_align = PAGE_SIZE;

    /* Data segment (data + bss) */
    if (data_section || bss_section) {
        uint64_t data_offset = text_offset + page_align(text_size);
        uint64_t data_vaddr = data_section ? data_section->vaddr :
                              (bss_section ? bss_section->vaddr : 0);
        uint64_t data_filesz = data_section ? data_section->size : 0;
        uint64_t data_memsz = data_filesz + (bss_section ? bss_section->size : 0);

        phdr_data.p_type = PT_LOAD;
        phdr_data.p_flags = PF_R | PF_W;  /* Read + Write */
        phdr_data.p_offset = data_offset;
        phdr_data.p_vaddr = data_vaddr;
        phdr_data.p_paddr = data_vaddr;
        phdr_data.p_filesz = data_filesz;
        phdr_data.p_memsz = data_memsz;
        phdr_data.p_align = PAGE_SIZE;
    } else {
        /* No data/bss segment - create empty one */
        phdr_data.p_type = PT_NULL;
    }

    /* Open output file */
    FILE* f = fopen(output_path, "wb");
    if (!f) {
        fprintf(stderr, "elf_write_executable: failed to open %s\n", output_path);
        return false;
    }

    /* Write ELF header */
    if (fwrite(&ehdr, sizeof(ehdr), 1, f) != 1) {
        fprintf(stderr, "elf_write_executable: failed to write ELF header\n");
        fclose(f);
        return false;
    }

    /* Write program headers */
    if (fwrite(&phdr_text, sizeof(phdr_text), 1, f) != 1) {
        fprintf(stderr, "elf_write_executable: failed to write text program header\n");
        fclose(f);
        return false;
    }

    if (fwrite(&phdr_data, sizeof(phdr_data), 1, f) != 1) {
        fprintf(stderr, "elf_write_executable: failed to write data program header\n");
        fclose(f);
        return false;
    }

    /* Pad to text section offset */
    size_t current_pos = sizeof(ehdr) + 2 * sizeof(Elf64_Phdr);
    size_t padding = text_offset - current_pos;
    if (padding > 0) {
        uint8_t* pad_buf = (uint8_t*)calloc(padding, 1);
        fwrite(pad_buf, 1, padding, f);
        free(pad_buf);
    }

    /* Write .text section */
    if (fwrite(text_section->data, 1, text_section->size, f) != text_section->size) {
        fprintf(stderr, "elf_write_executable: failed to write .text section\n");
        fclose(f);
        return false;
    }

    /* Write .rodata section (if present) */
    if (rodata_section && rodata_section->size > 0) {
        if (fwrite(rodata_section->data, 1, rodata_section->size, f) != rodata_section->size) {
            fprintf(stderr, "elf_write_executable: failed to write .rodata section\n");
            fclose(f);
            return false;
        }
    }

    /* Pad to data section offset (if needed) */
    if (data_section || bss_section) {
        current_pos = ftell(f);
        size_t target_offset = phdr_data.p_offset;
        if (current_pos < target_offset) {
            padding = target_offset - current_pos;
            uint8_t* pad_buf = (uint8_t*)calloc(padding, 1);
            fwrite(pad_buf, 1, padding, f);
            free(pad_buf);
        }

        /* Write .data section (if present) */
        if (data_section && data_section->size > 0) {
            if (fwrite(data_section->data, 1, data_section->size, f) != data_section->size) {
                fprintf(stderr, "elf_write_executable: failed to write .data section\n");
                fclose(f);
                return false;
            }
        }
    }

    /* Note: .bss is not written to file (it's zero-initialized in memory) */

    fclose(f);

    /* Set executable permissions (0755) */
    #ifndef _WIN32
    if (chmod(output_path, S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH) != 0) {
        fprintf(stderr, "elf_write_executable: warning: failed to set executable permissions\n");
        /* Not a fatal error */
    }
    #endif

    return true;
}

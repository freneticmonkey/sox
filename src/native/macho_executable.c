#include "macho_executable.h"
#include "macho_writer.h"
#include "section_layout.h"
#include "symbol_resolver.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <sys/stat.h>

/*
 * Mach-O Executable Writer Implementation
 *
 * This module creates executable Mach-O binaries for macOS/ARM64.
 * It takes the linked sections and symbols from the linker context
 * and generates a complete runnable executable.
 *
 * Phase 5.2: Mach-O Executable Writer
 */

/* Helper: Round up to page boundary (16KB for Mach-O) */
static uint64_t round_up_to_page(uint64_t value, size_t page_size) {
    return align_to(value, page_size);
}

/* Helper: Write structure to file with error checking */
static bool write_struct(FILE* f, const void* data, size_t size) {
    if (fwrite(data, size, 1, f) != 1) {
        fprintf(stderr, "Mach-O error: Failed to write structure to file\n");
        return false;
    }
    return true;
}

/* Helper: Write padding bytes to file */
static bool write_padding(FILE* f, size_t count) {
    if (count == 0) {
        return true;
    }
    uint8_t* padding = calloc(count, 1);
    if (padding == NULL) {
        fprintf(stderr, "Mach-O error: Failed to allocate padding\n");
        return false;
    }
    bool success = (fwrite(padding, count, 1, f) == 1);
    free(padding);
    if (!success) {
        fprintf(stderr, "Mach-O error: Failed to write padding\n");
    }
    return success;
}

/**
 * Get section count for a segment
 */
uint32_t macho_get_segment_section_count(linker_context_t* context,
                                          const char* segment_name) {
    if (context == NULL || segment_name == NULL) {
        return 0;
    }

    uint32_t count = 0;

    /* Check for __TEXT segment sections */
    if (strcmp(segment_name, SEG_TEXT) == 0) {
        /* Check for __text section */
        for (int i = 0; i < context->merged_section_count; i++) {
            if (context->merged_sections[i].type == SECTION_TYPE_TEXT) {
                count++;
                break;
            }
        }
        /* Check for __const section (rodata) */
        for (int i = 0; i < context->merged_section_count; i++) {
            if (context->merged_sections[i].type == SECTION_TYPE_RODATA) {
                count++;
                break;
            }
        }
    }
    /* Check for __DATA segment sections */
    else if (strcmp(segment_name, SEG_DATA) == 0) {
        /* Check for __data section */
        for (int i = 0; i < context->merged_section_count; i++) {
            if (context->merged_sections[i].type == SECTION_TYPE_DATA) {
                count++;
                break;
            }
        }
        /* Check for __bss section */
        for (int i = 0; i < context->merged_section_count; i++) {
            if (context->merged_sections[i].type == SECTION_TYPE_BSS) {
                count++;
                break;
            }
        }
    }

    return count;
}

/**
 * Calculate segment size
 */
uint64_t macho_calculate_segment_size(linker_context_t* context,
                                       const char* segment_name) {
    if (context == NULL || segment_name == NULL) {
        return 0;
    }

    uint64_t size = 0;

    /* Calculate size for __TEXT segment */
    if (strcmp(segment_name, SEG_TEXT) == 0) {
        /* Add __text section size */
        for (int i = 0; i < context->merged_section_count; i++) {
            if (context->merged_sections[i].type == SECTION_TYPE_TEXT) {
                size += context->merged_sections[i].size;
                break;
            }
        }
        /* Add __const section size (rodata) */
        for (int i = 0; i < context->merged_section_count; i++) {
            if (context->merged_sections[i].type == SECTION_TYPE_RODATA) {
                /* Align to 8-byte boundary */
                size = align_to(size, 8);
                size += context->merged_sections[i].size;
                break;
            }
        }
    }
    /* Calculate size for __DATA segment */
    else if (strcmp(segment_name, SEG_DATA) == 0) {
        /* Add __data section size */
        for (int i = 0; i < context->merged_section_count; i++) {
            if (context->merged_sections[i].type == SECTION_TYPE_DATA) {
                size += context->merged_sections[i].size;
                break;
            }
        }
        /* Add __bss section size */
        for (int i = 0; i < context->merged_section_count; i++) {
            if (context->merged_sections[i].type == SECTION_TYPE_BSS) {
                /* Align to 8-byte boundary */
                size = align_to(size, 8);
                size += context->merged_sections[i].size;
                break;
            }
        }
    }

    return size;
}

/**
 * Calculate total size of load commands
 */
uint32_t macho_calculate_load_commands_size(linker_context_t* context) {
    if (context == NULL) {
        return 0;
    }

    uint32_t size = 0;

    /* LC_SEGMENT_64 for __TEXT */
    uint32_t text_section_count = macho_get_segment_section_count(context, SEG_TEXT);
    size += sizeof(segment_command_64_t);
    size += text_section_count * sizeof(section_64_t);

    /* LC_SEGMENT_64 for __DATA */
    uint32_t data_section_count = macho_get_segment_section_count(context, SEG_DATA);
    size += sizeof(segment_command_64_t);
    size += data_section_count * sizeof(section_64_t);

    /* LC_MAIN */
    size += sizeof(entry_point_command_t);

    /* LC_LOAD_DYLINKER */
    size += sizeof(dylinker_command_t);
    /* Add space for dylinker path string (aligned to 8 bytes) */
    size_t dyld_path_len = strlen(DYLD_PATH) + 1;
    size += align_to(dyld_path_len, 8);

    /* LC_SYMTAB */
    size += sizeof(symtab_command_t);

    /* LC_DYSYMTAB */
    size += sizeof(dysymtab_command_t);

    /* LC_UUID */
    size += sizeof(uuid_command_t);

    /* LC_BUILD_VERSION */
    size += sizeof(build_version_command_t);

    /* LC_SEGMENT_64 for __LINKEDIT (no section headers) */
    size += sizeof(segment_command_64_t);

    /* LC_SEGMENT_64 for __PAGEZERO (no section headers) */
    size += sizeof(segment_command_64_t);

    return size;
}

/**
 * Set entry point to _main symbol address
 */
bool macho_set_entry_point(linker_context_t* context) {
    if (context == NULL) {
        fprintf(stderr, "Mach-O error: NULL context passed to macho_set_entry_point\n");
        return false;
    }

    /* Look for _main symbol in global symbols */
    linker_symbol_t* main_sym = NULL;
    for (int i = 0; i < context->global_symbol_count; i++) {
        if (context->global_symbols[i].name != NULL &&
            strcmp(context->global_symbols[i].name, "_main") == 0) {
            main_sym = &context->global_symbols[i];
            break;
        }
    }

    if (main_sym == NULL) {
        fprintf(stderr, "Mach-O error: _main symbol not found\n");
        return false;
    }

    if (!main_sym->is_defined) {
        fprintf(stderr, "Mach-O error: _main symbol is not defined\n");
        return false;
    }

    /* Set entry point to _main's final address */
    context->entry_point = main_sym->final_address;

    return true;
}

/**
 * Write a Mach-O executable file from linker context
 */
bool macho_write_executable(const char* output_path,
                             linker_context_t* context) {
    fprintf(stderr, "[MACHO-EXEC] Starting macho_write_executable for %s\n", output_path);

    if (output_path == NULL || context == NULL) {
        fprintf(stderr, "Mach-O error: NULL parameter passed to macho_write_executable\n");
        return false;
    }

    if (context->merged_section_count == 0) {
        fprintf(stderr, "Mach-O error: No sections to write\n");
        return false;
    }

    fprintf(stderr, "[MACHO-EXEC] Merged sections: %d\n", context->merged_section_count);

    /* Calculate sizes */
    size_t page_size = get_page_size(PLATFORM_FORMAT_MACH_O);
    uint32_t load_cmds_size = macho_calculate_load_commands_size(context);
    uint32_t text_section_count = macho_get_segment_section_count(context, SEG_TEXT);
    uint32_t data_section_count = macho_get_segment_section_count(context, SEG_DATA);
    uint64_t text_size = macho_calculate_segment_size(context, SEG_TEXT);
    uint64_t data_vmsize = macho_calculate_segment_size(context, SEG_DATA);

    /* Calculate DATA segment file size (exclude BSS which has no file content) */
    uint64_t data_filesize = 0;
    for (int i = 0; i < context->merged_section_count; i++) {
        if (context->merged_sections[i].type == SECTION_TYPE_DATA) {
            data_filesize += context->merged_sections[i].size;
            break;
        }
    }

    /* Calculate file offsets */
    uint64_t header_size = sizeof(mach_header_64_t);
    uint64_t text_file_offset = round_up_to_page(header_size + load_cmds_size, page_size);
    uint64_t data_file_offset = text_file_offset + round_up_to_page(text_size, page_size);
    uint64_t linkedit_file_offset = data_file_offset + round_up_to_page(data_filesize, page_size);

    /* Calculate virtual addresses */
    uint64_t base_addr = context->base_address;
    if (base_addr == 0) {
        base_addr = get_default_base_address(PLATFORM_FORMAT_MACH_O);
    }
    uint64_t text_vm_addr = base_addr;
    uint64_t data_vm_addr = text_vm_addr + round_up_to_page(text_size, page_size);
    uint64_t linkedit_vm_addr = data_vm_addr + round_up_to_page(data_vmsize, page_size);

    /* Create Mach-O header */
    mach_header_64_t header = {0};
    header.magic = MH_MAGIC_64;
    header.cputype = CPU_TYPE_ARM64;
    header.cpusubtype = CPU_SUBTYPE_ARM64_ALL;
    header.filetype = MH_EXECUTE;
    header.ncmds = 10;  /* __PAGEZERO, __TEXT, __DATA, __LINKEDIT, LC_MAIN, LC_LOAD_DYLINKER, LC_SYMTAB, LC_DYSYMTAB, LC_UUID, LC_BUILD_VERSION */
    header.sizeofcmds = load_cmds_size;
    header.flags = MH_NOUNDEFS | MH_PIE;
    header.reserved = 0;

    /* Open output file */
    FILE* f = fopen(output_path, "wb");
    if (f == NULL) {
        fprintf(stderr, "Mach-O error: Failed to open output file: %s\n", output_path);
        return false;
    }

    /* Write Mach-O header */
    if (!write_struct(f, &header, sizeof(header))) {
        fclose(f);
        return false;
    }

    /* Create and write LC_SEGMENT_64 for __PAGEZERO */
    segment_command_64_t pagezero_segment = {0};
    pagezero_segment.cmd = LC_SEGMENT_64;
    pagezero_segment.cmdsize = sizeof(segment_command_64_t);  /* No section headers */
    strncpy(pagezero_segment.segname, "__PAGEZERO", 16);
    pagezero_segment.vmaddr = 0x0;
    pagezero_segment.vmsize = 0x100000000ULL;  /* 4GB on 64-bit */
    pagezero_segment.fileoff = 0;
    pagezero_segment.filesize = 0;
    pagezero_segment.maxprot = VM_PROT_NONE;
    pagezero_segment.initprot = VM_PROT_NONE;
    pagezero_segment.nsects = 0;  /* No section headers */
    pagezero_segment.flags = 0;

    if (!write_struct(f, &pagezero_segment, sizeof(pagezero_segment))) {
        fclose(f);
        return false;
    }

    /* Create and write LC_SEGMENT_64 for __TEXT */
    segment_command_64_t text_segment = {0};
    text_segment.cmd = LC_SEGMENT_64;
    text_segment.cmdsize = sizeof(segment_command_64_t) +
                           text_section_count * sizeof(section_64_t);
    strncpy(text_segment.segname, SEG_TEXT, 16);
    text_segment.vmaddr = text_vm_addr;
    text_segment.vmsize = round_up_to_page(text_size, page_size);
    text_segment.fileoff = text_file_offset;
    text_segment.filesize = text_size;
    text_segment.maxprot = VM_PROT_READ | VM_PROT_EXECUTE;
    text_segment.initprot = VM_PROT_READ | VM_PROT_EXECUTE;
    text_segment.nsects = text_section_count;
    text_segment.flags = 0;

    if (!write_struct(f, &text_segment, sizeof(text_segment))) {
        fclose(f);
        return false;
    }

    /* Write __text section header */
    uint64_t current_offset = 0;
    for (int i = 0; i < context->merged_section_count; i++) {
        if (context->merged_sections[i].type == SECTION_TYPE_TEXT) {
            section_64_t text_sect = {0};
            strncpy(text_sect.sectname, SECT_TEXT, 16);
            strncpy(text_sect.segname, SEG_TEXT, 16);
            text_sect.addr = text_vm_addr + current_offset;
            text_sect.size = context->merged_sections[i].size;
            text_sect.offset = (uint32_t)(text_file_offset + current_offset);
            text_sect.align = 4;  /* 2^4 = 16 bytes for ARM64 */
            text_sect.reloff = 0;
            text_sect.nreloc = 0;
            text_sect.flags = S_REGULAR | S_ATTR_PURE_INSTRUCTIONS | S_ATTR_SOME_INSTRUCTIONS;
            text_sect.reserved1 = 0;
            text_sect.reserved2 = 0;
            text_sect.reserved3 = 0;

            if (!write_struct(f, &text_sect, sizeof(text_sect))) {
                fclose(f);
                return false;
            }

            current_offset += context->merged_sections[i].size;
            break;
        }
    }

    /* Write __const section header (rodata) */
    for (int i = 0; i < context->merged_section_count; i++) {
        if (context->merged_sections[i].type == SECTION_TYPE_RODATA) {
            current_offset = align_to(current_offset, 8);
            section_64_t const_sect = {0};
            strncpy(const_sect.sectname, SECT_CONST, 16);
            strncpy(const_sect.segname, SEG_TEXT, 16);
            const_sect.addr = text_vm_addr + current_offset;
            const_sect.size = context->merged_sections[i].size;
            const_sect.offset = (uint32_t)(text_file_offset + current_offset);
            const_sect.align = 3;  /* 2^3 = 8 bytes */
            const_sect.reloff = 0;
            const_sect.nreloc = 0;
            const_sect.flags = S_REGULAR;
            const_sect.reserved1 = 0;
            const_sect.reserved2 = 0;
            const_sect.reserved3 = 0;

            if (!write_struct(f, &const_sect, sizeof(const_sect))) {
                fclose(f);
                return false;
            }
            break;
        }
    }

    /* Create and write LC_SEGMENT_64 for __DATA */
    segment_command_64_t data_segment = {0};
    data_segment.cmd = LC_SEGMENT_64;
    data_segment.cmdsize = sizeof(segment_command_64_t) +
                           data_section_count * sizeof(section_64_t);
    strncpy(data_segment.segname, SEG_DATA, 16);
    data_segment.vmaddr = data_vm_addr;
    data_segment.vmsize = round_up_to_page(data_vmsize, page_size);
    data_segment.fileoff = data_file_offset;
    data_segment.filesize = data_filesize;  /* Exclude BSS from file size */
    data_segment.maxprot = VM_PROT_READ | VM_PROT_WRITE;
    data_segment.initprot = VM_PROT_READ | VM_PROT_WRITE;
    data_segment.nsects = data_section_count;
    data_segment.flags = 0;

    if (!write_struct(f, &data_segment, sizeof(data_segment))) {
        fclose(f);
        return false;
    }

    /* Write __data section header */
    current_offset = 0;
    for (int i = 0; i < context->merged_section_count; i++) {
        if (context->merged_sections[i].type == SECTION_TYPE_DATA) {
            section_64_t data_sect = {0};
            strncpy(data_sect.sectname, SECT_DATA, 16);
            strncpy(data_sect.segname, SEG_DATA, 16);
            data_sect.addr = data_vm_addr + current_offset;
            data_sect.size = context->merged_sections[i].size;
            data_sect.offset = (uint32_t)(data_file_offset + current_offset);
            data_sect.align = 3;  /* 2^3 = 8 bytes */
            data_sect.reloff = 0;
            data_sect.nreloc = 0;
            data_sect.flags = S_REGULAR;
            data_sect.reserved1 = 0;
            data_sect.reserved2 = 0;
            data_sect.reserved3 = 0;

            if (!write_struct(f, &data_sect, sizeof(data_sect))) {
                fclose(f);
                return false;
            }

            current_offset += context->merged_sections[i].size;
            break;
        }
    }

    /* Write __bss section header */
    for (int i = 0; i < context->merged_section_count; i++) {
        if (context->merged_sections[i].type == SECTION_TYPE_BSS) {
            current_offset = align_to(current_offset, 8);
            section_64_t bss_sect = {0};
            strncpy(bss_sect.sectname, SECT_BSS, 16);
            strncpy(bss_sect.segname, SEG_DATA, 16);
            bss_sect.addr = data_vm_addr + current_offset;
            bss_sect.size = context->merged_sections[i].size;
            bss_sect.offset = 0;  /* BSS has no file content */
            bss_sect.align = 3;  /* 2^3 = 8 bytes */
            bss_sect.reloff = 0;
            bss_sect.nreloc = 0;
            bss_sect.flags = S_REGULAR;
            bss_sect.reserved1 = 0;
            bss_sect.reserved2 = 0;
            bss_sect.reserved3 = 0;

            if (!write_struct(f, &bss_sect, sizeof(bss_sect))) {
                fclose(f);
                return false;
            }
            break;
        }
    }

    /* Create and write LC_SEGMENT_64 for __LINKEDIT */
    segment_command_64_t linkedit_segment = {0};
    linkedit_segment.cmd = LC_SEGMENT_64;
    linkedit_segment.cmdsize = sizeof(segment_command_64_t);  /* No section headers */
    strncpy(linkedit_segment.segname, "__LINKEDIT", 16);
    linkedit_segment.vmaddr = linkedit_vm_addr;
    linkedit_segment.vmsize = round_up_to_page(0, page_size);  /* Empty for now */
    linkedit_segment.fileoff = linkedit_file_offset;
    linkedit_segment.filesize = 0;  /* Empty for now */
    linkedit_segment.maxprot = VM_PROT_READ;
    linkedit_segment.initprot = VM_PROT_READ;
    linkedit_segment.nsects = 0;  /* No section headers */
    linkedit_segment.flags = 0;

    if (!write_struct(f, &linkedit_segment, sizeof(linkedit_segment))) {
        fclose(f);
        return false;
    }

    /* Create and write LC_MAIN */
    entry_point_command_t main_cmd = {0};
    main_cmd.cmd = LC_MAIN;
    main_cmd.cmdsize = sizeof(entry_point_command_t);
    main_cmd.entryoff = context->entry_point - text_vm_addr;
    main_cmd.stacksize = 0;  /* Use default */

    if (!write_struct(f, &main_cmd, sizeof(main_cmd))) {
        fclose(f);
        return false;
    }

    /* Create and write LC_LOAD_DYLINKER */
    dylinker_command_t dyld_cmd = {0};
    dyld_cmd.cmd = LC_LOAD_DYLINKER;
    size_t dyld_path_len = strlen(DYLD_PATH) + 1;
    dyld_cmd.cmdsize = (uint32_t)(sizeof(dylinker_command_t) + align_to(dyld_path_len, 8));
    dyld_cmd.name_offset = sizeof(dylinker_command_t);

    if (!write_struct(f, &dyld_cmd, sizeof(dyld_cmd))) {
        fclose(f);
        return false;
    }

    /* Write dylinker path */
    if (fwrite(DYLD_PATH, dyld_path_len, 1, f) != 1) {
        fprintf(stderr, "Mach-O error: Failed to write dylinker path\n");
        fclose(f);
        return false;
    }

    /* Write padding to align dylinker command */
    size_t dyld_padding = align_to(dyld_path_len, 8) - dyld_path_len;
    if (!write_padding(f, dyld_padding)) {
        fclose(f);
        return false;
    }

    /* Create and write LC_SYMTAB (empty symbol table for now) */
    symtab_command_t symtab_cmd = {0};
    symtab_cmd.cmd = LC_SYMTAB;
    symtab_cmd.cmdsize = sizeof(symtab_command_t);
    symtab_cmd.symoff = 0;     /* No symbol table data */
    symtab_cmd.nsyms = 0;
    symtab_cmd.stroff = 0;
    symtab_cmd.strsize = 0;

    if (!write_struct(f, &symtab_cmd, sizeof(symtab_cmd))) {
        fclose(f);
        return false;
    }

    /* Create and write LC_DYSYMTAB (empty dynamic symbol table) */
    dysymtab_command_t dysymtab_cmd = {0};
    dysymtab_cmd.cmd = LC_DYSYMTAB;
    dysymtab_cmd.cmdsize = sizeof(dysymtab_command_t);
    dysymtab_cmd.ilocalsym = 0;
    dysymtab_cmd.nlocalsym = 0;
    dysymtab_cmd.iextdefsym = 0;
    dysymtab_cmd.nextdefsym = 0;
    dysymtab_cmd.iundefsym = 0;
    dysymtab_cmd.nundefsym = 0;
    dysymtab_cmd.tocoff = 0;
    dysymtab_cmd.ntoc = 0;
    dysymtab_cmd.modtaboff = 0;
    dysymtab_cmd.nmodtab = 0;
    dysymtab_cmd.extrefsymoff = 0;
    dysymtab_cmd.nextrefsyms = 0;
    dysymtab_cmd.indirectsymoff = 0;
    dysymtab_cmd.nindirectsyms = 0;
    dysymtab_cmd.extreloff = 0;
    dysymtab_cmd.nextrel = 0;
    dysymtab_cmd.locreloff = 0;
    dysymtab_cmd.nlocrel = 0;

    if (!write_struct(f, &dysymtab_cmd, sizeof(dysymtab_cmd))) {
        fclose(f);
        return false;
    }

    /* Create and write LC_UUID */
    uuid_command_t uuid_cmd = {0};
    uuid_cmd.cmd = LC_UUID;
    uuid_cmd.cmdsize = sizeof(uuid_command_t);
    /* Generate a simple UUID from the entry point and text size */
    for (int i = 0; i < 16; i++) {
        if (i < 8) {
            uuid_cmd.uuid[i] = (context->entry_point >> (i * 8)) & 0xFF;
        } else {
            uuid_cmd.uuid[i] = (text_size >> ((i - 8) * 8)) & 0xFF;
        }
    }

    if (!write_struct(f, &uuid_cmd, sizeof(uuid_cmd))) {
        fclose(f);
        return false;
    }

    /* Create and write LC_BUILD_VERSION */
    build_version_command_t build_cmd = {0};
    build_cmd.cmd = LC_BUILD_VERSION;
    build_cmd.cmdsize = sizeof(build_version_command_t);
    build_cmd.platform = 1;     /* 1 = macOS */
    build_cmd.minos = 0x000b0000;   /* macOS 11.0.0 minimum */
    build_cmd.sdk = 0x000e0000;     /* macOS 14.0.0 SDK */
    build_cmd.ntools = 0;       /* No tool entries */

    if (!write_struct(f, &build_cmd, sizeof(build_cmd))) {
        fclose(f);
        return false;
    }

    /* Pad to page boundary before __TEXT segment data */
    uint64_t current_pos = header_size + load_cmds_size;
    size_t padding_before_text = text_file_offset - current_pos;
    if (!write_padding(f, padding_before_text)) {
        fclose(f);
        return false;
    }

    /* Write __TEXT segment data */
    /* Write __text section data */
    for (int i = 0; i < context->merged_section_count; i++) {
        if (context->merged_sections[i].type == SECTION_TYPE_TEXT) {
            if (context->merged_sections[i].data != NULL &&
                context->merged_sections[i].size > 0) {
                if (fwrite(context->merged_sections[i].data,
                          context->merged_sections[i].size, 1, f) != 1) {
                    fprintf(stderr, "Mach-O error: Failed to write __text section data\n");
                    fclose(f);
                    return false;
                }
            }
            break;
        }
    }

    /* Align to 8-byte boundary before __const section */
    current_pos = text_file_offset;
    for (int i = 0; i < context->merged_section_count; i++) {
        if (context->merged_sections[i].type == SECTION_TYPE_TEXT) {
            current_pos += context->merged_sections[i].size;
            break;
        }
    }
    uint64_t aligned_pos = align_to(current_pos, 8);
    if (!write_padding(f, aligned_pos - current_pos)) {
        fclose(f);
        return false;
    }

    /* Write __const section data (rodata) */
    for (int i = 0; i < context->merged_section_count; i++) {
        if (context->merged_sections[i].type == SECTION_TYPE_RODATA) {
            if (context->merged_sections[i].data != NULL &&
                context->merged_sections[i].size > 0) {
                if (fwrite(context->merged_sections[i].data,
                          context->merged_sections[i].size, 1, f) != 1) {
                    fprintf(stderr, "Mach-O error: Failed to write __const section data\n");
                    fclose(f);
                    return false;
                }
            }
            break;
        }
    }

    /* Pad to page boundary before __DATA segment */
    current_pos = text_file_offset + text_size;
    aligned_pos = round_up_to_page(current_pos, page_size);
    if (!write_padding(f, aligned_pos - current_pos)) {
        fclose(f);
        return false;
    }

    /* Write __DATA segment data */
    /* Write __data section data */
    for (int i = 0; i < context->merged_section_count; i++) {
        if (context->merged_sections[i].type == SECTION_TYPE_DATA) {
            if (context->merged_sections[i].data != NULL &&
                context->merged_sections[i].size > 0) {
                if (fwrite(context->merged_sections[i].data,
                          context->merged_sections[i].size, 1, f) != 1) {
                    fprintf(stderr, "Mach-O error: Failed to write __data section data\n");
                    fclose(f);
                    return false;
                }
            }
            break;
        }
    }

    /* Note: __bss section has no file content (zero-initialized at runtime) */

    fclose(f);

    fprintf(stderr, "[MACHO-EXEC] File written, setting permissions...\n");

    /* Set executable permissions (0755) */
    if (chmod(output_path, 0755) != 0) {
        fprintf(stderr, "Mach-O warning: Failed to set executable permissions\n");
        /* Not a fatal error, continue */
    }

    fprintf(stderr, "[MACHO-EXEC] Successfully completed macho_write_executable\n");

    return true;
}

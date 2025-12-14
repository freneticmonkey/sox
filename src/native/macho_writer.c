#include "macho_writer.h"
#include "../lib/memory.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

macho_builder_t* macho_builder_new(uint32_t cputype, uint32_t cpusubtype) {
    macho_builder_t* builder = (macho_builder_t*)l_mem_alloc(sizeof(macho_builder_t));
    builder->cputype = cputype;
    builder->cpusubtype = cpusubtype;
    builder->data = NULL;
    builder->size = 0;
    builder->capacity = 0;
    builder->sections = NULL;
    builder->section_count = 0;
    builder->section_capacity = 0;
    builder->section_data = NULL;
    builder->section_sizes = NULL;
    builder->strtab = NULL;
    builder->strtab_size = 0;
    builder->strtab_capacity = 0;
    builder->symtab = NULL;
    builder->symtab_count = 0;
    builder->symtab_capacity = 0;
    builder->relocs = NULL;
    builder->reloc_count = 0;
    builder->reloc_capacity = 0;

    // Initialize string table with space and null byte (required by Mach-O)
    builder->strtab_capacity = 256;
    builder->strtab = (char*)l_mem_alloc(builder->strtab_capacity);
    builder->strtab[0] = ' ';
    builder->strtab[1] = '\0';
    builder->strtab_size = 2;

    return builder;
}

void macho_builder_free(macho_builder_t* builder) {
    if (!builder) return;

    if (builder->data) {
        l_mem_free(builder->data, builder->capacity);
    }
    if (builder->sections) {
        l_mem_free(builder->sections, sizeof(section_64_t) * builder->section_capacity);
    }
    if (builder->section_data) {
        for (int i = 0; i < builder->section_count; i++) {
            if (builder->section_data[i]) {
                l_mem_free(builder->section_data[i], builder->section_sizes[i]);
            }
        }
        l_mem_free(builder->section_data, sizeof(uint8_t*) * builder->section_capacity);
        l_mem_free(builder->section_sizes, sizeof(size_t) * builder->section_capacity);
    }
    if (builder->strtab) {
        l_mem_free(builder->strtab, builder->strtab_capacity);
    }
    if (builder->symtab) {
        l_mem_free(builder->symtab, sizeof(nlist_64_t) * builder->symtab_capacity);
    }
    if (builder->relocs) {
        l_mem_free(builder->relocs, sizeof(relocation_info_t) * builder->reloc_capacity);
    }

    l_mem_free(builder, sizeof(macho_builder_t));
}

static uint32_t add_string(macho_builder_t* builder, const char* str) {
    size_t len = strlen(str) + 1;

    if (builder->strtab_capacity < builder->strtab_size + len) {
        size_t old_capacity = builder->strtab_capacity;
        builder->strtab_capacity = (builder->strtab_size + len) * 2;
        builder->strtab = (char*)l_mem_realloc(
            builder->strtab,
            old_capacity,
            builder->strtab_capacity
        );
    }

    uint32_t offset = (uint32_t)builder->strtab_size;
    memcpy(builder->strtab + offset, str, len);
    builder->strtab_size += len;

    return offset;
}

int macho_add_section(macho_builder_t* builder, const char* sectname,
                      const char* segname, uint32_t flags,
                      const uint8_t* data, size_t size, uint32_t align) {
    if (builder->section_capacity < builder->section_count + 1) {
        int old_capacity = builder->section_capacity;
        builder->section_capacity = (old_capacity < 4) ? 4 : old_capacity * 2;

        builder->sections = (section_64_t*)l_mem_realloc(
            builder->sections,
            sizeof(section_64_t) * old_capacity,
            sizeof(section_64_t) * builder->section_capacity
        );

        builder->section_data = (uint8_t**)l_mem_realloc(
            builder->section_data,
            sizeof(uint8_t*) * old_capacity,
            sizeof(uint8_t*) * builder->section_capacity
        );

        builder->section_sizes = (size_t*)l_mem_realloc(
            builder->section_sizes,
            sizeof(size_t) * old_capacity,
            sizeof(size_t) * builder->section_capacity
        );
    }

    section_64_t* sect = &builder->sections[builder->section_count];
    memset(sect, 0, sizeof(section_64_t));

    strncpy(sect->sectname, sectname, 16);
    strncpy(sect->segname, segname, 16);
    sect->size = size;
    sect->align = align;
    sect->flags = flags;

    // Copy section data
    if (data && size > 0) {
        builder->section_data[builder->section_count] = (uint8_t*)l_mem_alloc(size);
        memcpy(builder->section_data[builder->section_count], data, size);
        builder->section_sizes[builder->section_count] = size;
    } else {
        builder->section_data[builder->section_count] = NULL;
        builder->section_sizes[builder->section_count] = 0;
    }

    return builder->section_count++;
}

int macho_add_symbol(macho_builder_t* builder, const char* name,
                     uint8_t type, uint8_t sect, uint64_t value) {
    if (builder->symtab_capacity < builder->symtab_count + 1) {
        int old_capacity = builder->symtab_capacity;
        builder->symtab_capacity = (old_capacity < 8) ? 8 : old_capacity * 2;
        builder->symtab = (nlist_64_t*)l_mem_realloc(
            builder->symtab,
            sizeof(nlist_64_t) * old_capacity,
            sizeof(nlist_64_t) * builder->symtab_capacity
        );
    }

    nlist_64_t* sym = &builder->symtab[builder->symtab_count];

    // Add underscore prefix for macOS (C calling convention)
    char prefixed_name[256];
    snprintf(prefixed_name, sizeof(prefixed_name), "_%s", name);
    sym->n_strx = add_string(builder, prefixed_name);
    sym->n_type = type;
    sym->n_sect = sect;
    sym->n_desc = 0;
    sym->n_value = value;

    return builder->symtab_count++;
}

void macho_add_relocation(macho_builder_t* builder, int32_t address,
                          uint32_t symbolnum, bool pcrel, uint32_t length,
                          bool external, uint32_t type) {
    if (builder->reloc_capacity < builder->reloc_count + 1) {
        int old_capacity = builder->reloc_capacity;
        builder->reloc_capacity = (old_capacity < 8) ? 8 : old_capacity * 2;
        builder->relocs = (relocation_info_t*)l_mem_realloc(
            builder->relocs,
            sizeof(relocation_info_t) * old_capacity,
            sizeof(relocation_info_t) * builder->reloc_capacity
        );
    }

    relocation_info_t* reloc = &builder->relocs[builder->reloc_count++];
    reloc->r_address = address;
    reloc->r_symbolnum = symbolnum;
    reloc->r_pcrel = pcrel ? 1 : 0;
    reloc->r_length = length;
    reloc->r_extern = external ? 1 : 0;
    reloc->r_type = type;
}

static void write_data(macho_builder_t* builder, const void* data, size_t size) {
    if (builder->capacity < builder->size + size) {
        size_t old_capacity = builder->capacity;
        builder->capacity = (builder->size + size) * 2;
        builder->data = (uint8_t*)l_mem_realloc(
            builder->data,
            old_capacity,
            builder->capacity
        );
    }

    memcpy(builder->data + builder->size, data, size);
    builder->size += size;
}

static void write_zeros(macho_builder_t* builder, size_t count) {
    if (builder->capacity < builder->size + count) {
        size_t old_capacity = builder->capacity;
        builder->capacity = (builder->size + count) * 2;
        builder->data = (uint8_t*)l_mem_realloc(
            builder->data,
            old_capacity,
            builder->capacity
        );
    }

    memset(builder->data + builder->size, 0, count);
    builder->size += count;
}

// Align to boundary
static void align_to(macho_builder_t* builder, size_t alignment) {
    size_t remainder = builder->size % alignment;
    if (remainder != 0) {
        write_zeros(builder, alignment - remainder);
    }
}

bool macho_write_file(macho_builder_t* builder, const char* filename) {
    // Reset builder data
    builder->size = 0;

    // Calculate sizes
    size_t header_size = sizeof(mach_header_64_t);
    size_t segment_cmd_size = sizeof(segment_command_64_t) +
                               builder->section_count * sizeof(section_64_t);
    size_t symtab_cmd_size = sizeof(symtab_command_t);
    size_t dysymtab_cmd_size = sizeof(dysymtab_command_t);
    size_t build_version_cmd_size = sizeof(build_version_command_t) + sizeof(build_tool_version_t);

    size_t load_commands_size = segment_cmd_size + symtab_cmd_size +
                                 dysymtab_cmd_size + build_version_cmd_size;

    // Debug: Log section data
    if (builder->section_count > 0 && builder->section_data[0]) {
        fprintf(stderr, "DEBUG: Section 0 has %zu bytes of data\n", builder->section_sizes[0]);
        fprintf(stderr, "DEBUG: First 8 bytes: ");
        for (int j = 0; j < 8 && j < builder->section_sizes[0]; j++) {
            fprintf(stderr, "%02x ", builder->section_data[0][j]);
        }
        fprintf(stderr, "\n");
    }

    // Write Mach-O header
    mach_header_64_t header;
    memset(&header, 0, sizeof(header));
    header.magic = MH_MAGIC_64;
    header.cputype = builder->cputype;
    header.cpusubtype = builder->cpusubtype;
    header.filetype = MH_OBJECT;
    header.ncmds = 4; // segment, symtab, dysymtab, build_version
    header.sizeofcmds = (uint32_t)load_commands_size;
    header.flags = MH_SUBSECTIONS_VIA_SYMBOLS;
    header.reserved = 0;
    write_data(builder, &header, sizeof(header));

    // Calculate file offsets
    size_t current_offset = header_size + load_commands_size;

    // Align to 16 bytes for first section
    if (current_offset % 16 != 0) {
        current_offset += 16 - (current_offset % 16);
    }

    // Update section offsets
    for (int i = 0; i < builder->section_count; i++) {
        builder->sections[i].offset = (uint32_t)current_offset;
        builder->sections[i].addr = 0; // Object files have no VM addresses
        current_offset += builder->sections[i].size;

        // Align each section
        if (current_offset % (1 << builder->sections[i].align) != 0) {
            size_t alignment = 1 << builder->sections[i].align;
            current_offset += alignment - (current_offset % alignment);
        }
    }

    // Symbol table offset
    size_t symtab_offset = current_offset;
    size_t symtab_size = builder->symtab_count * sizeof(nlist_64_t);

    // String table offset
    size_t strtab_offset = symtab_offset + symtab_size;

    // Write segment command with sections
    segment_command_64_t seg_cmd;
    memset(&seg_cmd, 0, sizeof(seg_cmd));
    seg_cmd.cmd = LC_SEGMENT_64;
    seg_cmd.cmdsize = (uint32_t)segment_cmd_size;
    memset(seg_cmd.segname, 0, 16); // Empty segment name for object files
    seg_cmd.vmaddr = 0;
    seg_cmd.fileoff = builder->section_count > 0 ? builder->sections[0].offset : 0;
    seg_cmd.filesize = builder->section_count > 0 ?
                       (builder->sections[builder->section_count-1].offset +
                        builder->sections[builder->section_count-1].size - seg_cmd.fileoff) : 0;
    // For object files, vmsize must equal filesize (sections must fit within segment)
    seg_cmd.vmsize = seg_cmd.filesize;
    seg_cmd.maxprot = 7; // rwx
    seg_cmd.initprot = 7;
    seg_cmd.nsects = builder->section_count;
    seg_cmd.flags = 0;
    write_data(builder, &seg_cmd, sizeof(seg_cmd));

    // Write sections
    for (int i = 0; i < builder->section_count; i++) {
        write_data(builder, &builder->sections[i], sizeof(section_64_t));
    }

    // Write symtab command
    symtab_command_t symtab_cmd;
    memset(&symtab_cmd, 0, sizeof(symtab_cmd));
    symtab_cmd.cmd = LC_SYMTAB;
    symtab_cmd.cmdsize = sizeof(symtab_command_t);
    symtab_cmd.symoff = (uint32_t)symtab_offset;
    symtab_cmd.nsyms = builder->symtab_count;
    symtab_cmd.stroff = (uint32_t)strtab_offset;
    symtab_cmd.strsize = (uint32_t)builder->strtab_size;
    write_data(builder, &symtab_cmd, sizeof(symtab_cmd));

    // Write dysymtab command
    dysymtab_command_t dysymtab_cmd;
    memset(&dysymtab_cmd, 0, sizeof(dysymtab_cmd));
    dysymtab_cmd.cmd = LC_DYSYMTAB;
    dysymtab_cmd.cmdsize = sizeof(dysymtab_command_t);
    dysymtab_cmd.ilocalsym = 0;
    dysymtab_cmd.nlocalsym = 0;
    dysymtab_cmd.iextdefsym = 0;
    dysymtab_cmd.nextdefsym = builder->symtab_count;
    dysymtab_cmd.iundefsym = builder->symtab_count;
    dysymtab_cmd.nundefsym = 0;
    write_data(builder, &dysymtab_cmd, sizeof(dysymtab_cmd));

    // Write build version command
    build_version_command_t build_cmd;
    memset(&build_cmd, 0, sizeof(build_cmd));
    build_cmd.cmd = LC_BUILD_VERSION;
    build_cmd.cmdsize = sizeof(build_version_command_t) + sizeof(build_tool_version_t);
    build_cmd.platform = PLATFORM_MACOS;
    build_cmd.minos = 0x000c0000; // macOS 12.0
    build_cmd.sdk = 0x000c0000;
    build_cmd.ntools = 1;
    write_data(builder, &build_cmd, sizeof(build_cmd));

    // Write build tool
    build_tool_version_t tool;
    tool.tool = TOOL_CLANG;
    tool.version = 0x000d0000; // Version 13.0.0
    write_data(builder, &tool, sizeof(tool));

    // Align to section offset
    while (builder->size < (builder->section_count > 0 ? builder->sections[0].offset : 0)) {
        write_zeros(builder, 1);
    }

    // Write section data
    for (int i = 0; i < builder->section_count; i++) {
        if (builder->section_data[i]) {
            write_data(builder, builder->section_data[i], builder->section_sizes[i]);
        }

        // Align to next section
        if (i < builder->section_count - 1) {
            align_to(builder, 1 << builder->sections[i].align);
        }
    }

    // Write symbol table
    align_to(builder, 8);
    write_data(builder, builder->symtab, builder->symtab_count * sizeof(nlist_64_t));

    // Write string table
    write_data(builder, builder->strtab, builder->strtab_size);

    // Write to file
    FILE* fp = fopen(filename, "wb");
    if (!fp) {
        return false;
    }

    fwrite(builder->data, 1, builder->size, fp);
    fclose(fp);

    return true;
}

bool macho_create_object_file(const char* filename, const uint8_t* code,
                               size_t code_size, const char* function_name,
                               uint32_t cputype, uint32_t cpusubtype) {
    macho_builder_t* builder = macho_builder_new(cputype, cpusubtype);

    // Add __text section
    int text_section = macho_add_section(builder, "__text", "__TEXT",
                                          S_REGULAR | S_ATTR_PURE_INSTRUCTIONS | S_ATTR_SOME_INSTRUCTIONS,
                                          code, code_size, 4);

    // Add function symbol (external, defined)
    macho_add_symbol(builder, function_name, N_SECT | N_EXT, text_section + 1, 0);

    bool result = macho_write_file(builder, filename);

    macho_builder_free(builder);
    return result;
}

bool macho_create_executable_object_file(const char* filename, const uint8_t* code,
                                         size_t code_size,
                                         uint32_t cputype, uint32_t cpusubtype) {
    macho_builder_t* builder = macho_builder_new(cputype, cpusubtype);

    // Add __text section
    int text_section = macho_add_section(builder, "__text", "__TEXT",
                                          S_REGULAR | S_ATTR_PURE_INSTRUCTIONS | S_ATTR_SOME_INSTRUCTIONS,
                                          code, code_size, 4);

    // Add main symbol at offset 0 (entry point for executable linking)
    // This is the symbol the linker looks for as the executable entry point
    macho_add_symbol(builder, "main", N_SECT | N_EXT, text_section + 1, 0);

    // Also add sox_main for reference
    macho_add_symbol(builder, "sox_main", N_SECT | N_EXT, text_section + 1, 0);

    bool result = macho_write_file(builder, filename);

    macho_builder_free(builder);
    return result;
}

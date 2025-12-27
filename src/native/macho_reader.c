#include "macho_reader.h"
#include "macho_writer.h"  /* For Mach-O structure definitions */
#include "../lib/memory.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * Mach-O Object File Reader Implementation
 *
 * This module implements full Mach-O 64-bit object file parsing, converting
 * Mach-O format into the unified linker_object_t representation.
 *
 * Phase 1.4: Complete Mach-O object file reader
 */

/* ARM64 relocation type constants from <mach-o/arm64/reloc.h> */
#define ARM64_RELOC_UNSIGNED              0  /* For pointers */
#define ARM64_RELOC_SUBTRACTOR            1  /* Must be followed by UNSIGNED */
#define ARM64_RELOC_BRANCH26              2  /* B/BL instruction (26-bit displacement) */
#define ARM64_RELOC_PAGE21                3  /* PC-relative distance to page */
#define ARM64_RELOC_PAGEOFF12             4  /* Offset within page */
#define ARM64_RELOC_GOT_LOAD_PAGE21       5  /* PC-rel distance to GOT page */
#define ARM64_RELOC_GOT_LOAD_PAGEOFF12    6  /* Offset within GOT page */
#define ARM64_RELOC_POINTER_TO_GOT        7  /* Pointer to GOT slot */
#define ARM64_RELOC_TLVP_LOAD_PAGE21      8  /* Thread-local page distance */
#define ARM64_RELOC_TLVP_LOAD_PAGEOFF12   9  /* Thread-local page offset */
#define ARM64_RELOC_ADDEND                10 /* Must be followed by PAGE21/PAGEOFF12 */
#define ARM64_RELOC_AUTHENTICATED_POINTER 11 /* 64-bit pointer with authentication */

/* Helper macros for relocation_info unpacking */
#define RELOC_SYMBOLNUM(info)  ((info) & 0xFFFFFF)           /* Bits 0-23 */
#define RELOC_PCREL(info)      (((info) >> 24) & 0x1)        /* Bit 24 */
#define RELOC_LENGTH(info)     (((info) >> 25) & 0x3)        /* Bits 25-26 */
#define RELOC_EXTERN(info)     (((info) >> 27) & 0x1)        /* Bit 27 */
#define RELOC_TYPE(info)       (((info) >> 28) & 0xF)        /* Bits 28-31 */

/**
 * Map section type from Mach-O section name to unified section type.
 */
static section_type_t map_section_type(const char* sectname, const char* segname) {
    if (strcmp(sectname, "__text") == 0 && strcmp(segname, "__TEXT") == 0) {
        return SECTION_TYPE_TEXT;
    } else if (strcmp(sectname, "__data") == 0 && strcmp(segname, "__DATA") == 0) {
        return SECTION_TYPE_DATA;
    } else if (strcmp(sectname, "__bss") == 0 && strcmp(segname, "__DATA") == 0) {
        return SECTION_TYPE_BSS;
    } else if (strcmp(sectname, "__rodata") == 0 ||
               strcmp(sectname, "__const") == 0 ||
               strcmp(sectname, "__cstring") == 0 ||
               strcmp(sectname, "__literal4") == 0 ||
               strcmp(sectname, "__literal8") == 0) {
        return SECTION_TYPE_RODATA;
    }
    return SECTION_TYPE_UNKNOWN;
}

/**
 * Map Mach-O symbol type (n_type field) to unified symbol type and binding.
 */
static void map_symbol_type_and_binding(uint8_t n_type, uint8_t n_sect,
                                        symbol_type_t* type, symbol_binding_t* binding) {
    /* Extract type bits (N_TYPE mask = 0x0e) */
    uint8_t type_bits = n_type & 0x0e;

    /* Extract external bit (N_EXT = 0x01) */
    bool is_external = (n_type & 0x01) != 0;

    /* Determine binding */
    if (is_external) {
        *binding = SYMBOL_BINDING_GLOBAL;
    } else {
        *binding = SYMBOL_BINDING_LOCAL;
    }

    /* Determine type based on n_type and section */
    if (type_bits == 0x00) {  /* N_UNDF */
        *type = SYMBOL_TYPE_NOTYPE;
    } else if (type_bits == 0x0e) {  /* N_SECT */
        /* Symbol is defined in a section - infer type from section */
        *type = SYMBOL_TYPE_OBJECT;  /* Default to object */
        /* Note: We could inspect section flags to determine if it's code/data */
    } else if (type_bits == 0x02) {  /* N_ABS */
        *type = SYMBOL_TYPE_NOTYPE;
    } else {
        *type = SYMBOL_TYPE_NOTYPE;
    }
}

/**
 * Map Mach-O ARM64 relocation type to unified relocation type.
 */
relocation_type_t macho_map_relocation_type(uint32_t macho_type) {
    switch (macho_type) {
        case ARM64_RELOC_UNSIGNED:
            return RELOC_ARM64_ABS64;
        case ARM64_RELOC_BRANCH26:
            return RELOC_ARM64_CALL26;  /* Used for both BL and B */
        case ARM64_RELOC_PAGE21:
            return RELOC_ARM64_ADR_PREL_PG_HI21;
        case ARM64_RELOC_PAGEOFF12:
            return RELOC_ARM64_ADD_ABS_LO12_NC;
        case ARM64_RELOC_GOT_LOAD_PAGE21:
        case ARM64_RELOC_POINTER_TO_GOT:
            /* GOT relocations - map to page-relative for now */
            return RELOC_ARM64_ADR_PREL_PG_HI21;
        case ARM64_RELOC_GOT_LOAD_PAGEOFF12:
            return RELOC_ARM64_ADD_ABS_LO12_NC;
        case ARM64_RELOC_ADDEND:
            /* ADDEND is a modifier, not a standalone relocation */
            return RELOC_NONE;
        default:
            fprintf(stderr, "Warning: Unknown Mach-O relocation type: %u\n", macho_type);
            return RELOC_NONE;
    }
}

/**
 * Parse sections from a Mach-O segment_command_64.
 */
bool macho_parse_sections(linker_object_t* obj, const uint8_t* data, size_t size,
                           const void* seg_cmd_ptr) {
    const segment_command_64_t* seg_cmd = (const segment_command_64_t*)seg_cmd_ptr;

    /* Validate segment command size */
    if (seg_cmd->cmdsize < sizeof(segment_command_64_t)) {
        fprintf(stderr, "Mach-O error: Invalid segment command size\n");
        return false;
    }

    /* Calculate number of sections */
    uint32_t nsects = seg_cmd->nsects;
    if (nsects == 0) {
        return true;  /* No sections in this segment */
    }

    /* Validate that sections fit within command size */
    size_t expected_size = sizeof(segment_command_64_t) + nsects * sizeof(section_64_t);
    if (seg_cmd->cmdsize < expected_size) {
        fprintf(stderr, "Mach-O error: Segment command too small for %u sections\n", nsects);
        return false;
    }

    /* Parse each section */
    const section_64_t* sections = (const section_64_t*)((const uint8_t*)seg_cmd + sizeof(segment_command_64_t));

    for (uint32_t i = 0; i < nsects; i++) {
        const section_64_t* sect = &sections[i];

        /* Validate section offset and size */
        if (sect->offset > 0 && sect->size > 0) {
            if (sect->offset + sect->size > size) {
                fprintf(stderr, "Mach-O error: Section data exceeds file bounds\n");
                return false;
            }
        }

        /* Add section to linker object */
        linker_section_t* link_sect = linker_object_add_section(obj);
        if (!link_sect) {
            fprintf(stderr, "Mach-O error: Failed to allocate section\n");
            return false;
        }

        /* Copy section name (ensure null termination) */
        char sectname[17] = {0};
        char segname[17] = {0};
        memcpy(sectname, sect->sectname, 16);
        memcpy(segname, sect->segname, 16);

        link_sect->name = strdup(sectname);
        link_sect->type = map_section_type(sectname, segname);
        link_sect->size = sect->size;
        link_sect->alignment = (sect->align > 0) ? (1UL << sect->align) : 1;
        link_sect->flags = sect->flags;
        link_sect->vaddr = sect->addr;
        link_sect->object_index = -1;  /* Will be set by caller */

        /* Copy section data if present */
        if (sect->offset > 0 && sect->size > 0) {
            link_sect->data = (uint8_t*)l_mem_alloc(sect->size);
            if (!link_sect->data) {
                fprintf(stderr, "Mach-O error: Failed to allocate section data\n");
                return false;
            }
            memcpy(link_sect->data, data + sect->offset, sect->size);
        } else {
            link_sect->data = NULL;
        }
    }

    return true;
}

/**
 * Parse Mach-O symbol table (nlist_64 entries).
 */
bool macho_parse_symbols(linker_object_t* obj, const uint8_t* data, size_t size,
                          uint32_t symoff, uint32_t nsyms,
                          uint32_t stroff, uint32_t strsize) {
    /* Validate symbol table bounds */
    if (symoff + nsyms * sizeof(nlist_64_t) > size) {
        fprintf(stderr, "Mach-O error: Symbol table exceeds file bounds\n");
        return false;
    }

    /* Validate string table bounds */
    if (stroff + strsize > size) {
        fprintf(stderr, "Mach-O error: String table exceeds file bounds\n");
        return false;
    }

    const nlist_64_t* symtab = (const nlist_64_t*)(data + symoff);
    const char* strtab = (const char*)(data + stroff);

    /* Parse each symbol */
    for (uint32_t i = 0; i < nsyms; i++) {
        const nlist_64_t* sym = &symtab[i];

        /* Validate string table index */
        if (sym->n_strx >= strsize) {
            fprintf(stderr, "Mach-O error: Symbol string index out of bounds\n");
            return false;
        }

        /* Get symbol name (skip underscore prefix if present) */
        const char* name = strtab + sym->n_strx;
        if (name[0] == '_') {
            name++;  /* Skip Mach-O underscore prefix */
        }

        /* Skip empty symbol names */
        if (name[0] == '\0') {
            continue;
        }

        /* Add symbol to linker object */
        linker_symbol_t* link_sym = linker_object_add_symbol(obj);
        if (!link_sym) {
            fprintf(stderr, "Mach-O error: Failed to allocate symbol\n");
            return false;
        }

        link_sym->name = strdup(name);
        map_symbol_type_and_binding(sym->n_type, sym->n_sect,
                                     &link_sym->type, &link_sym->binding);

        /* Section index: n_sect is 1-based, convert to 0-based (-1 for undefined) */
        if (sym->n_sect == 0) {
            link_sym->section_index = -1;  /* Undefined symbol */
            link_sym->is_defined = false;
        } else {
            link_sym->section_index = sym->n_sect - 1;
            link_sym->is_defined = true;
        }

        link_sym->value = sym->n_value;
        link_sym->size = 0;  /* Mach-O doesn't store symbol size in nlist_64 */
        link_sym->final_address = 0;
        link_sym->defining_object = -1;  /* Will be set by caller */
    }

    return true;
}

/**
 * Parse Mach-O relocations for all sections.
 */
bool macho_parse_relocations(linker_object_t* obj, const uint8_t* data, size_t size) {
    /* Iterate through all sections and parse their relocations */
    for (int sect_idx = 0; sect_idx < obj->section_count; sect_idx++) {
        linker_section_t* sect = &obj->sections[sect_idx];

        /* Find corresponding Mach-O section to get relocation info */
        /* We need to walk the load commands again to find section headers */
        /* For now, we'll store reloff/nreloc in section flags temporarily */
        /* This is a simplification - in practice, we'd need to pass this info */

        /* Skip sections without relocations (indicated by flags field being 0) */
        /* Note: This is a temporary approach - see note above */
        if (sect->flags == 0) {
            continue;
        }

        /* In a complete implementation, we would parse relocations here */
        /* For Phase 1.4, we defer this to a second pass in macho_parse_load_commands */
    }

    return true;
}

/**
 * Parse relocations for a specific section.
 */
static bool parse_section_relocations(linker_object_t* obj, const uint8_t* data, size_t size,
                                       int section_index, uint32_t reloff, uint32_t nreloc) {
    if (nreloc == 0) {
        return true;  /* No relocations */
    }

    /* Validate relocation table bounds */
    if (reloff + nreloc * sizeof(relocation_info_t) > size) {
        fprintf(stderr, "Mach-O error: Relocation table exceeds file bounds\n");
        return false;
    }

    const relocation_info_t* relocs = (const relocation_info_t*)(data + reloff);

    /* Parse each relocation */
    for (uint32_t i = 0; i < nreloc; i++) {
        const relocation_info_t* reloc = &relocs[i];

        /* Extract relocation fields from packed info word */
        uint32_t symbolnum = RELOC_SYMBOLNUM(reloc->r_info);
        uint32_t external = RELOC_EXTERN(reloc->r_info);
        uint32_t type = RELOC_TYPE(reloc->r_info);

        /* Unused fields that might be needed in the future */
        (void)RELOC_PCREL(reloc->r_info);
        (void)RELOC_LENGTH(reloc->r_info);

        /* Skip ADDEND relocations (they modify the next relocation) */
        if (type == ARM64_RELOC_ADDEND) {
            /* TODO: Handle addend for next relocation */
            continue;
        }

        /* Add relocation to linker object */
        linker_relocation_t* link_reloc = linker_object_add_relocation(obj);
        if (!link_reloc) {
            fprintf(stderr, "Mach-O error: Failed to allocate relocation\n");
            return false;
        }

        link_reloc->offset = (uint64_t)reloc->r_address;
        link_reloc->type = macho_map_relocation_type(type);
        link_reloc->section_index = section_index;
        link_reloc->addend = 0;  /* TODO: Extract from ADDEND relocation if present */
        link_reloc->object_index = -1;  /* Will be set by caller */

        /* Set symbol index */
        if (external) {
            link_reloc->symbol_index = (int)symbolnum;
        } else {
            /* Section-relative relocation - symbolnum is section number (1-based) */
            link_reloc->symbol_index = -1;  /* No symbol, section-relative */
        }
    }

    return true;
}

/**
 * Parse Mach-O load commands.
 */
bool macho_parse_load_commands(linker_object_t* obj, const uint8_t* data, size_t size,
                                uint32_t ncmds, uint32_t sizeofcmds) {
    /* Validate load commands size */
    size_t header_size = sizeof(mach_header_64_t);
    if (header_size + sizeofcmds > size) {
        fprintf(stderr, "Mach-O error: Load commands exceed file bounds\n");
        return false;
    }

    const uint8_t* cmd_ptr = data + header_size;
    const uint8_t* cmd_end = cmd_ptr + sizeofcmds;

    /* Storage for symtab info (needed for later symbol parsing) */
    uint32_t symoff = 0, nsyms = 0, stroff = 0, strsize = 0;
    bool has_symtab = false;

    /* Storage for section relocation info */
    typedef struct {
        int section_index;
        uint32_t reloff;
        uint32_t nreloc;
    } section_reloc_info_t;

    section_reloc_info_t* reloc_infos = NULL;
    int reloc_info_count = 0;
    int reloc_info_capacity = 0;

    /* First pass: parse load commands */
    for (uint32_t i = 0; i < ncmds; i++) {
        if (cmd_ptr + sizeof(load_command_t) > cmd_end) {
            fprintf(stderr, "Mach-O error: Load command exceeds bounds\n");
            free(reloc_infos);
            return false;
        }

        const load_command_t* cmd = (const load_command_t*)cmd_ptr;

        if (cmd_ptr + cmd->cmdsize > cmd_end) {
            fprintf(stderr, "Mach-O error: Load command size exceeds bounds\n");
            free(reloc_infos);
            return false;
        }

        switch (cmd->cmd) {
            case LC_SEGMENT_64: {
                const segment_command_64_t* seg_cmd = (const segment_command_64_t*)cmd_ptr;

                /* Parse sections from this segment */
                int section_start = obj->section_count;
                if (!macho_parse_sections(obj, data, size, seg_cmd)) {
                    free(reloc_infos);
                    return false;
                }

                /* Store relocation info for each section */
                const section_64_t* sections = (const section_64_t*)((const uint8_t*)seg_cmd + sizeof(segment_command_64_t));
                for (uint32_t j = 0; j < seg_cmd->nsects; j++) {
                    if (sections[j].nreloc > 0) {
                        /* Grow reloc_infos array if needed */
                        if (reloc_info_count >= reloc_info_capacity) {
                            reloc_info_capacity = (reloc_info_capacity == 0) ? 8 : reloc_info_capacity * 2;
                            reloc_infos = (section_reloc_info_t*)realloc(reloc_infos,
                                reloc_info_capacity * sizeof(section_reloc_info_t));
                            if (!reloc_infos) {
                                fprintf(stderr, "Mach-O error: Failed to allocate relocation info\n");
                                return false;
                            }
                        }

                        reloc_infos[reloc_info_count].section_index = section_start + j;
                        reloc_infos[reloc_info_count].reloff = sections[j].reloff;
                        reloc_infos[reloc_info_count].nreloc = sections[j].nreloc;
                        reloc_info_count++;
                    }
                }
                break;
            }

            case LC_SYMTAB: {
                const symtab_command_t* symtab_cmd = (const symtab_command_t*)cmd_ptr;
                symoff = symtab_cmd->symoff;
                nsyms = symtab_cmd->nsyms;
                stroff = symtab_cmd->stroff;
                strsize = symtab_cmd->strsize;
                has_symtab = true;
                break;
            }

            case LC_DYSYMTAB:
            case LC_BUILD_VERSION:
                /* Skip these commands for object file parsing */
                break;

            default:
                /* Ignore unknown load commands */
                break;
        }

        cmd_ptr += cmd->cmdsize;
    }

    /* Second pass: parse symbols */
    if (has_symtab && nsyms > 0) {
        if (!macho_parse_symbols(obj, data, size, symoff, nsyms, stroff, strsize)) {
            free(reloc_infos);
            return false;
        }
    }

    /* Third pass: parse relocations */
    for (int i = 0; i < reloc_info_count; i++) {
        if (!parse_section_relocations(obj, data, size,
                                        reloc_infos[i].section_index,
                                        reloc_infos[i].reloff,
                                        reloc_infos[i].nreloc)) {
            free(reloc_infos);
            return false;
        }
    }

    free(reloc_infos);
    return true;
}

/**
 * Main Mach-O object file reader.
 */
linker_object_t* macho_read_object(const char* filename, const uint8_t* data, size_t size) {
    /* Validate minimum file size */
    if (size < sizeof(mach_header_64_t)) {
        fprintf(stderr, "Mach-O error: File too small for header\n");
        return NULL;
    }

    /* Parse Mach-O header */
    const mach_header_64_t* header = (const mach_header_64_t*)data;

    /* Validate magic number */
    if (header->magic != MH_MAGIC_64) {
        fprintf(stderr, "Mach-O error: Invalid magic number (expected 0x%08x, got 0x%08x)\n",
                MH_MAGIC_64, header->magic);
        return NULL;
    }

    /* Validate file type (should be MH_OBJECT for object files) */
    if (header->filetype != MH_OBJECT) {
        fprintf(stderr, "Mach-O warning: File type is not MH_OBJECT (0x%x)\n", header->filetype);
    }

    /* Create linker object */
    linker_object_t* obj = linker_object_new(filename, PLATFORM_FORMAT_MACH_O);
    if (!obj) {
        fprintf(stderr, "Mach-O error: Failed to allocate linker object\n");
        return NULL;
    }

    /* Parse load commands (sections, symbols, relocations) */
    if (!macho_parse_load_commands(obj, data, size, header->ncmds, header->sizeofcmds)) {
        linker_object_free(obj);
        return NULL;
    }

    /* Store raw data for debugging */
    obj->raw_size = size;
    obj->raw_data = (uint8_t*)l_mem_alloc(size);
    if (obj->raw_data) {
        memcpy(obj->raw_data, data, size);
    }

    return obj;
}

#include "elf_reader.h"
#include "elf_writer.h"
#include "../lib/memory.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * ELF Object File Reader Implementation
 *
 * Phase 1.3: Full ELF64 object file parsing
 * Supports both x86-64 and ARM64 architectures
 *
 * SECURITY: Contains critical bounds checking to prevent buffer overflows
 * and integer overflows from maliciously crafted ELF files.
 */

/*
 * Safe bounds checking macro for offset+size calculations.
 * Prevents integer overflow by checking: offset > limit - size
 * This is mathematically equivalent to: offset + size > limit
 * but avoids the overflow risk inherent in addition.
 *
 * Usage: CHECK_OFFSET_SIZE(offset, size, file_size, "description")
 */
#define CHECK_OFFSET_SIZE(offset, size, limit, msg) \
    do { \
        if ((size) > 0 && (offset) > (limit) - (size)) { \
            fprintf(stderr, "ELF reader error: %s (offset=%lu, size=%lu, limit=%zu)\n", \
                    (msg), (uint64_t)(offset), (uint64_t)(size), (size_t)(limit)); \
            return false; \
        } \
    } while (0)

/* ARM64 relocation types (not in elf_writer.h) */
#define R_AARCH64_NONE              0
#define R_AARCH64_ABS64             257
#define R_AARCH64_CALL26            283
#define R_AARCH64_JUMP26            282
#define R_AARCH64_ADR_PREL_PG_HI21  275
#define R_AARCH64_ADD_ABS_LO12_NC   277
#define R_AARCH64_LDST64_ABS_LO12_NC 286

/* Helper structure for ELF parsing context */
typedef struct {
    const uint8_t* data;
    size_t size;
    const Elf64_Ehdr* ehdr;
    const Elf64_Shdr* shdr_table;
    const char* shstrtab;
    size_t shstrtab_size;
    int* section_index_map;  /* Maps ELF section indices to linker section indices */
    size_t num_elf_sections; /* Total number of ELF sections */
} elf_parse_context_t;

/* Forward declarations */
static bool elf_validate_header(const Elf64_Ehdr* ehdr, size_t size);
static bool elf_parse_sections(linker_object_t* obj, elf_parse_context_t* ctx);
static bool elf_parse_symbols(linker_object_t* obj, elf_parse_context_t* ctx);
static bool elf_parse_relocations(linker_object_t* obj, elf_parse_context_t* ctx);
static relocation_type_t elf_map_relocation_type(uint32_t elf_type, uint16_t machine);
static section_type_t elf_map_section_type(const char* name, uint32_t type);
static symbol_type_t elf_map_symbol_type(unsigned char st_type);
static symbol_binding_t elf_map_symbol_binding(unsigned char st_bind);

/**
 * Validate ELF64 header
 */
static bool elf_validate_header(const Elf64_Ehdr* ehdr, size_t size) {
    if (size < sizeof(Elf64_Ehdr)) {
        fprintf(stderr, "ELF reader error: File too small for ELF header\n");
        return false;
    }

    /* Check magic number */
    if (ehdr->e_ident[0] != ELFMAG0 ||
        ehdr->e_ident[1] != ELFMAG1 ||
        ehdr->e_ident[2] != ELFMAG2 ||
        ehdr->e_ident[3] != ELFMAG3) {
        fprintf(stderr, "ELF reader error: Invalid ELF magic number\n");
        return false;
    }

    /* Check ELF class (64-bit) */
    if (ehdr->e_ident[4] != ELFCLASS64) {
        fprintf(stderr, "ELF reader error: Not a 64-bit ELF file\n");
        return false;
    }

    /* Check endianness (little-endian) */
    if (ehdr->e_ident[5] != ELFDATA2LSB) {
        fprintf(stderr, "ELF reader error: Not a little-endian ELF file\n");
        return false;
    }

    /* Check version */
    if (ehdr->e_ident[6] != EV_CURRENT) {
        fprintf(stderr, "ELF reader error: Invalid ELF version\n");
        return false;
    }

    /* Check file type (relocatable object) */
    if (ehdr->e_type != ET_REL) {
        fprintf(stderr, "ELF reader error: Not a relocatable object file (e_type=%d)\n", ehdr->e_type);
        return false;
    }

    /* Check machine type */
    if (ehdr->e_machine != EM_X86_64 && ehdr->e_machine != EM_AARCH64) {
        fprintf(stderr, "ELF reader error: Unsupported machine type (e_machine=%d)\n", ehdr->e_machine);
        return false;
    }

    return true;
}

/**
 * Map ELF section type to unified section type
 */
static section_type_t elf_map_section_type(const char* name, uint32_t type) {
    if (type == SHT_NOBITS) {
        return SECTION_TYPE_BSS;
    }

    if (strcmp(name, ".text") == 0) {
        return SECTION_TYPE_TEXT;
    } else if (strcmp(name, ".data") == 0) {
        return SECTION_TYPE_DATA;
    } else if (strcmp(name, ".rodata") == 0) {
        return SECTION_TYPE_RODATA;
    } else if (strcmp(name, ".bss") == 0) {
        return SECTION_TYPE_BSS;
    }

    return SECTION_TYPE_UNKNOWN;
}

/**
 * Map ELF symbol type to unified symbol type
 */
static symbol_type_t elf_map_symbol_type(unsigned char st_type) {
    switch (st_type) {
        case STT_NOTYPE: return SYMBOL_TYPE_NOTYPE;
        case STT_FUNC:   return SYMBOL_TYPE_FUNC;
        default:         return SYMBOL_TYPE_OBJECT;
    }
}

/**
 * Map ELF symbol binding to unified symbol binding
 */
static symbol_binding_t elf_map_symbol_binding(unsigned char st_bind) {
    switch (st_bind) {
        case STB_LOCAL:  return SYMBOL_BINDING_LOCAL;
        case STB_GLOBAL: return SYMBOL_BINDING_GLOBAL;
        default:         return SYMBOL_BINDING_WEAK;
    }
}

/**
 * Map ELF relocation type to unified relocation type
 */
static relocation_type_t elf_map_relocation_type(uint32_t elf_type, uint16_t machine) {
    if (machine == EM_X86_64) {
        switch (elf_type) {
            case R_X86_64_NONE:   return RELOC_NONE;
            case R_X86_64_64:     return RELOC_X64_64;
            case R_X86_64_PC32:   return RELOC_X64_PC32;
            case R_X86_64_PLT32:  return RELOC_X64_PLT32;
            default:
                fprintf(stderr, "Warning: Unknown x86-64 relocation type %u\n", elf_type);
                return RELOC_NONE;
        }
    } else if (machine == EM_AARCH64) {
        switch (elf_type) {
            case R_AARCH64_NONE:              return RELOC_NONE;
            case R_AARCH64_ABS64:             return RELOC_ARM64_ABS64;
            case R_AARCH64_CALL26:            return RELOC_ARM64_CALL26;
            case R_AARCH64_JUMP26:            return RELOC_ARM64_JUMP26;
            case R_AARCH64_ADR_PREL_PG_HI21:  return RELOC_ARM64_ADR_PREL_PG_HI21;
            case R_AARCH64_ADD_ABS_LO12_NC:   return RELOC_ARM64_ADD_ABS_LO12_NC;
            case R_AARCH64_LDST64_ABS_LO12_NC: return RELOC_ARM64_ADD_ABS_LO12_NC; /* Similar to ADD */
            default:
                fprintf(stderr, "Warning: Unknown ARM64 relocation type %u\n", elf_type);
                return RELOC_NONE;
        }
    }

    return RELOC_NONE;
}

/**
 * Parse ELF sections and extract section data
 */
static bool elf_parse_sections(linker_object_t* obj, elf_parse_context_t* ctx) {
    const Elf64_Ehdr* ehdr = ctx->ehdr;
    const Elf64_Shdr* shdr_table = ctx->shdr_table;

    /* Skip null section (index 0) and section header string table */
    for (int i = 1; i < ehdr->e_shnum; i++) {
        const Elf64_Shdr* shdr = &shdr_table[i];

        /* Skip special sections */
        if (shdr->sh_type == SHT_NULL ||
            shdr->sh_type == SHT_STRTAB ||
            shdr->sh_type == SHT_SYMTAB ||
            shdr->sh_type == SHT_RELA) {
            continue;
        }

        /* Get section name */
        const char* name = ctx->shstrtab + shdr->sh_name;

        /* Only process known section types */
        section_type_t type = elf_map_section_type(name, shdr->sh_type);
        if (type == SECTION_TYPE_UNKNOWN) {
            continue;
        }

        /* Create new section */
        linker_section_t* section = linker_object_add_section(obj);
        if (!section) {
            fprintf(stderr, "ELF reader error: Failed to allocate section\n");
            return false;
        }

        section->name = strdup(name);
        section->type = type;
        section->size = shdr->sh_size;
        section->alignment = shdr->sh_addralign;
        section->flags = 0;

        /* Set flags based on section flags */
        if (shdr->sh_flags & SHF_WRITE) section->flags |= 0x1;
        if (shdr->sh_flags & SHF_ALLOC) section->flags |= 0x2;
        if (shdr->sh_flags & SHF_EXECINSTR) section->flags |= 0x4;

        /* Copy section data (unless BSS) */
        if (shdr->sh_type != SHT_NOBITS && shdr->sh_size > 0) {
            /*
             * SECURITY FIX: Critical Issue #2 - Integer Overflow Protection
             * Check for overflow BEFORE performing addition (offset + size).
             * Using safe check: offset > limit - size to avoid overflow.
             */
            CHECK_OFFSET_SIZE(shdr->sh_offset, shdr->sh_size, ctx->size,
                             "Section data extends beyond file");

            section->data = (uint8_t*)l_mem_alloc(shdr->sh_size);
            if (!section->data) {
                fprintf(stderr, "ELF reader error: Failed to allocate section data\n");
                return false;
            }

            memcpy(section->data, ctx->data + shdr->sh_offset, shdr->sh_size);
        } else {
            section->data = NULL;
        }

        section->vaddr = 0; /* Will be assigned during layout */
        section->object_index = 0;

        /*
         * SECURITY FIX: Critical Issue #3 - Record Section Mapping
         * Map ELF section index `i` to linker section index.
         * This is necessary because we skip some sections (symtab, strtab, rela).
         */
        int linker_section_index = obj->section_count - 1;  /* Just added */
        ctx->section_index_map[i] = linker_section_index;
    }

    return true;
}

/**
 * Parse ELF symbol table
 */
static bool elf_parse_symbols(linker_object_t* obj, elf_parse_context_t* ctx) {
    const Elf64_Ehdr* ehdr = ctx->ehdr;
    const Elf64_Shdr* shdr_table = ctx->shdr_table;

    /* Find .symtab and .strtab sections */
    const Elf64_Shdr* symtab_shdr = NULL;
    const Elf64_Shdr* strtab_shdr = NULL;

    for (int i = 0; i < ehdr->e_shnum; i++) {
        const Elf64_Shdr* shdr = &shdr_table[i];
        const char* name = ctx->shstrtab + shdr->sh_name;

        if (strcmp(name, ".symtab") == 0) {
            symtab_shdr = shdr;
        } else if (strcmp(name, ".strtab") == 0) {
            strtab_shdr = shdr;
        }
    }

    if (!symtab_shdr || !strtab_shdr) {
        /* No symbol table is acceptable for some objects */
        return true;
    }

    /* Validate symbol table and string table offsets */
    CHECK_OFFSET_SIZE(symtab_shdr->sh_offset, symtab_shdr->sh_size, ctx->size,
                     "Symbol table extends beyond file");
    CHECK_OFFSET_SIZE(strtab_shdr->sh_offset, strtab_shdr->sh_size, ctx->size,
                     "String table extends beyond file");

    /* Get symbol table data */
    const Elf64_Sym* symbols = (const Elf64_Sym*)(ctx->data + symtab_shdr->sh_offset);
    int symbol_count = (int)(symtab_shdr->sh_size / sizeof(Elf64_Sym));
    const char* strtab = (const char*)(ctx->data + strtab_shdr->sh_offset);

    /* Parse symbols (skip null symbol at index 0) */
    for (int i = 1; i < symbol_count; i++) {
        const Elf64_Sym* elf_sym = &symbols[i];

        /* Create new symbol */
        linker_symbol_t* symbol = linker_object_add_symbol(obj);
        if (!symbol) {
            fprintf(stderr, "ELF reader error: Failed to allocate symbol\n");
            return false;
        }

        /*
         * SECURITY FIX: Critical Issue #1 - Buffer Overflow Protection
         * Validate symbol name index is within string table bounds and
         * ensure the string is properly null-terminated.
         */
        if (elf_sym->st_name >= strtab_shdr->sh_size) {
            fprintf(stderr, "ELF reader error: Symbol %d name index %u out of bounds (strtab size: %lu)\n",
                    i, elf_sym->st_name, (uint64_t)strtab_shdr->sh_size);
            return false;
        }

        const char* sym_name = strtab + elf_sym->st_name;

        /* Ensure null-termination within string table bounds */
        size_t max_len = strtab_shdr->sh_size - elf_sym->st_name;
        size_t name_len = strnlen(sym_name, max_len);
        if (name_len == max_len && (max_len == 0 || sym_name[max_len - 1] != '\0')) {
            fprintf(stderr, "ELF reader error: Symbol %d name not null-terminated within string table\n", i);
            return false;
        }

        symbol->name = strdup(sym_name);

        /* Map symbol type and binding */
        unsigned char st_type = elf_sym->st_info & 0xf;
        unsigned char st_bind = elf_sym->st_info >> 4;
        symbol->type = elf_map_symbol_type(st_type);
        symbol->binding = elf_map_symbol_binding(st_bind);

        /*
         * SECURITY FIX: Critical Issue #3 - Use Section Index Mapping
         * Properly map ELF section indices to linker section indices using
         * the mapping table. Handles special section indices (SHN_UNDEF, SHN_ABS, etc.).
         */
        if (elf_sym->st_shndx == SHN_UNDEF) {
            /* Undefined symbol */
            symbol->section_index = -1;
            symbol->is_defined = false;
        } else if (elf_sym->st_shndx == SHN_ABS) {
            /* Absolute symbol - value doesn't need relocation */
            symbol->section_index = -1;
            symbol->is_defined = true;
            /* Keep sym->value as-is (already absolute) */
        } else if (elf_sym->st_shndx == SHN_COMMON) {
            /* Common symbol (uninitialized data, typically goes to BSS) */
            symbol->section_index = -1;
            symbol->is_defined = true;
        } else if (elf_sym->st_shndx >= SHN_LORESERVE) {
            /* Reserved section indices */
            fprintf(stderr, "ELF reader warning: Symbol '%s' uses reserved section index 0x%x\n",
                    symbol->name, elf_sym->st_shndx);
            symbol->section_index = -1;
            symbol->is_defined = false;
        } else if (elf_sym->st_shndx >= ctx->num_elf_sections) {
            /* Section index out of range */
            fprintf(stderr, "ELF reader error: Symbol '%s' section index %u out of range (max %zu)\n",
                    symbol->name, elf_sym->st_shndx, ctx->num_elf_sections);
            return false;
        } else {
            /* Normal section - use the mapping table */
            int mapped_index = ctx->section_index_map[elf_sym->st_shndx];

            if (mapped_index == -1) {
                /*
                 * Symbol references a section we didn't load (e.g., .symtab, .strtab, .rela.*).
                 * This can happen for debugging symbols or metadata.
                 * Mark as undefined since we don't have the section.
                 */
                symbol->section_index = -1;
                symbol->is_defined = false;
            } else {
                symbol->section_index = mapped_index;
                symbol->is_defined = true;
            }
        }

        symbol->value = elf_sym->st_value;
        symbol->size = elf_sym->st_size;
        symbol->final_address = 0; /* Computed during layout */
        symbol->defining_object = 0;
    }

    return true;
}

/**
 * Parse ELF relocations
 */
static bool elf_parse_relocations(linker_object_t* obj, elf_parse_context_t* ctx) {
    const Elf64_Ehdr* ehdr = ctx->ehdr;
    const Elf64_Shdr* shdr_table = ctx->shdr_table;

    /* Iterate through all sections looking for relocation sections */
    for (int i = 0; i < ehdr->e_shnum; i++) {
        const Elf64_Shdr* shdr = &shdr_table[i];

        if (shdr->sh_type != SHT_RELA) {
            continue;
        }

        /* Get target section index */
        int target_section = shdr->sh_info;

        /* Get relocation entries */
        const Elf64_Rela* relocations = (const Elf64_Rela*)(ctx->data + shdr->sh_offset);
        int reloc_count = (int)(shdr->sh_size / sizeof(Elf64_Rela));

        /* Parse each relocation */
        for (int j = 0; j < reloc_count; j++) {
            const Elf64_Rela* elf_rela = &relocations[j];

            /* Create new relocation */
            linker_relocation_t* reloc = linker_object_add_relocation(obj);
            if (!reloc) {
                fprintf(stderr, "ELF reader error: Failed to allocate relocation\n");
                return false;
            }

            reloc->offset = elf_rela->r_offset;
            reloc->addend = elf_rela->r_addend;
            reloc->symbol_index = ELF64_R_SYM(elf_rela->r_info);
            reloc->section_index = target_section - 1; /* Adjust for our indexing */
            reloc->object_index = 0;

            /* Map relocation type */
            uint32_t elf_type = ELF64_R_TYPE(elf_rela->r_info);
            reloc->type = elf_map_relocation_type(elf_type, ehdr->e_machine);
        }
    }

    return true;
}

/**
 * Read an ELF object file and parse it into a linker_object_t structure.
 */
linker_object_t* elf_read_object(const char* filename, const uint8_t* data, size_t size) {
    /* Validate ELF header */
    const Elf64_Ehdr* ehdr = (const Elf64_Ehdr*)data;
    if (!elf_validate_header(ehdr, size)) {
        return NULL;
    }

    /* Create linker object */
    linker_object_t* obj = linker_object_new(filename, PLATFORM_FORMAT_ELF);
    if (!obj) {
        fprintf(stderr, "ELF reader error: Failed to allocate linker object\n");
        return NULL;
    }

    /* Validate section header table */
    if (ehdr->e_shoff == 0 || ehdr->e_shnum == 0) {
        fprintf(stderr, "ELF reader error: No section headers\n");
        linker_object_free(obj);
        return NULL;
    }

    size_t shdr_table_offset = ehdr->e_shoff;
    size_t shdr_table_size = ehdr->e_shnum * sizeof(Elf64_Shdr);
    if (shdr_table_offset + shdr_table_size > size) {
        fprintf(stderr, "ELF reader error: Section header table extends beyond file\n");
        linker_object_free(obj);
        return NULL;
    }

    const Elf64_Shdr* shdr_table = (const Elf64_Shdr*)(data + shdr_table_offset);

    /* Get section header string table */
    if (ehdr->e_shstrndx >= ehdr->e_shnum) {
        fprintf(stderr, "ELF reader error: Invalid section header string table index\n");
        linker_object_free(obj);
        return NULL;
    }

    const Elf64_Shdr* shstrtab_shdr = &shdr_table[ehdr->e_shstrndx];
    if (shstrtab_shdr->sh_offset + shstrtab_shdr->sh_size > size) {
        fprintf(stderr, "ELF reader error: Section header string table extends beyond file\n");
        linker_object_free(obj);
        return NULL;
    }

    const char* shstrtab = (const char*)(data + shstrtab_shdr->sh_offset);

    /*
     * SECURITY FIX: Critical Issue #3 - Section Index Mapping
     * Allocate and initialize section index map to track which ELF sections
     * map to which linker sections (since we skip some sections like .symtab).
     */
    int* section_index_map = (int*)calloc(ehdr->e_shnum, sizeof(int));
    if (!section_index_map) {
        fprintf(stderr, "ELF reader error: Failed to allocate section index map\n");
        linker_object_free(obj);
        return NULL;
    }

    /* Initialize all entries to -1 (unmapped) */
    for (int i = 0; i < ehdr->e_shnum; i++) {
        section_index_map[i] = -1;
    }

    /* Create parse context */
    elf_parse_context_t ctx = {
        .data = data,
        .size = size,
        .ehdr = ehdr,
        .shdr_table = shdr_table,
        .shstrtab = shstrtab,
        .shstrtab_size = shstrtab_shdr->sh_size,
        .section_index_map = section_index_map,
        .num_elf_sections = ehdr->e_shnum
    };

    /* Parse sections */
    if (!elf_parse_sections(obj, &ctx)) {
        free(section_index_map);  /* Cleanup mapping */
        linker_object_free(obj);
        return NULL;
    }

    /* Parse symbols */
    if (!elf_parse_symbols(obj, &ctx)) {
        free(section_index_map);  /* Cleanup mapping */
        linker_object_free(obj);
        return NULL;
    }

    /* Parse relocations */
    if (!elf_parse_relocations(obj, &ctx)) {
        free(section_index_map);  /* Cleanup mapping */
        linker_object_free(obj);
        return NULL;
    }

    /* Store raw data for debugging (optional) */
    obj->raw_data = (uint8_t*)l_mem_alloc(size);
    if (obj->raw_data) {
        memcpy(obj->raw_data, data, size);
        obj->raw_size = size;
    }

    /*
     * SECURITY FIX: Critical Issue #3 - Cleanup Section Mapping
     * Free the temporary section index map now that parsing is complete.
     */
    free(section_index_map);

    return obj;
}

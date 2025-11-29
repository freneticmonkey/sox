#include "elf_writer.h"
#include "../lib/memory.h"
#include "../lib/file.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

elf_builder_t* elf_builder_new(void) {
    elf_builder_t* builder = (elf_builder_t*)l_mem_alloc(sizeof(elf_builder_t));
    builder->data = NULL;
    builder->size = 0;
    builder->capacity = 0;
    builder->sections = NULL;
    builder->section_count = 0;
    builder->section_capacity = 0;
    builder->strtab = NULL;
    builder->strtab_size = 0;
    builder->strtab_capacity = 0;
    builder->symtab = NULL;
    builder->symtab_count = 0;
    builder->symtab_capacity = 0;
    builder->rela = NULL;
    builder->rela_count = 0;
    builder->rela_capacity = 0;

    // Initialize string table with null byte
    builder->strtab_capacity = 256;
    builder->strtab = (char*)l_mem_alloc(builder->strtab_capacity);
    builder->strtab[0] = '\0';
    builder->strtab_size = 1;

    // Initialize symbol table with null symbol
    builder->symtab_capacity = 16;
    builder->symtab = (Elf64_Sym*)l_mem_alloc(sizeof(Elf64_Sym) * builder->symtab_capacity);
    memset(&builder->symtab[0], 0, sizeof(Elf64_Sym));
    builder->symtab_count = 1;

    return builder;
}

void elf_builder_free(elf_builder_t* builder) {
    if (!builder) return;

    if (builder->data) {
        l_mem_free(builder->data, builder->capacity);
    }
    if (builder->sections) {
        l_mem_free(builder->sections, sizeof(Elf64_Shdr) * builder->section_capacity);
    }
    if (builder->strtab) {
        l_mem_free(builder->strtab, builder->strtab_capacity);
    }
    if (builder->symtab) {
        l_mem_free(builder->symtab, sizeof(Elf64_Sym) * builder->symtab_capacity);
    }
    if (builder->rela) {
        l_mem_free(builder->rela, sizeof(Elf64_Rela) * builder->rela_capacity);
    }

    l_mem_free(builder, sizeof(elf_builder_t));
}

static uint32_t add_string(elf_builder_t* builder, const char* str) {
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

int elf_add_section(elf_builder_t* builder, const char* name, uint32_t type,
                    uint64_t flags, const uint8_t* data, size_t size) {
    if (builder->section_capacity < builder->section_count + 1) {
        int old_capacity = builder->section_capacity;
        builder->section_capacity = (old_capacity < 8) ? 8 : old_capacity * 2;
        builder->sections = (Elf64_Shdr*)l_mem_realloc(
            builder->sections,
            sizeof(Elf64_Shdr) * old_capacity,
            sizeof(Elf64_Shdr) * builder->section_capacity
        );
    }

    Elf64_Shdr* shdr = &builder->sections[builder->section_count];
    memset(shdr, 0, sizeof(Elf64_Shdr));

    shdr->sh_name = add_string(builder, name);
    shdr->sh_type = type;
    shdr->sh_flags = flags;
    shdr->sh_addr = 0;
    shdr->sh_offset = 0; // Will be set later
    shdr->sh_size = size;
    shdr->sh_link = 0;
    shdr->sh_info = 0;
    shdr->sh_addralign = (type == SHT_PROGBITS) ? 16 : 1;
    shdr->sh_entsize = (type == SHT_SYMTAB) ? sizeof(Elf64_Sym) : 0;

    return builder->section_count++;
}

int elf_add_symbol(elf_builder_t* builder, const char* name, unsigned char bind,
                   unsigned char type, uint16_t shndx, uint64_t value, uint64_t size) {
    if (builder->symtab_capacity < builder->symtab_count + 1) {
        int old_capacity = builder->symtab_capacity;
        builder->symtab_capacity = (old_capacity < 16) ? 16 : old_capacity * 2;
        builder->symtab = (Elf64_Sym*)l_mem_realloc(
            builder->symtab,
            sizeof(Elf64_Sym) * old_capacity,
            sizeof(Elf64_Sym) * builder->symtab_capacity
        );
    }

    Elf64_Sym* sym = &builder->symtab[builder->symtab_count];
    sym->st_name = add_string(builder, name);
    sym->st_info = ELF64_ST_INFO(bind, type);
    sym->st_other = 0;
    sym->st_shndx = shndx;
    sym->st_value = value;
    sym->st_size = size;

    return builder->symtab_count++;
}

void elf_add_relocation(elf_builder_t* builder, uint64_t offset, uint32_t sym,
                        uint32_t type, int64_t addend) {
    if (builder->rela_capacity < builder->rela_count + 1) {
        int old_capacity = builder->rela_capacity;
        builder->rela_capacity = (old_capacity < 8) ? 8 : old_capacity * 2;
        builder->rela = (Elf64_Rela*)l_mem_realloc(
            builder->rela,
            sizeof(Elf64_Rela) * old_capacity,
            sizeof(Elf64_Rela) * builder->rela_capacity
        );
    }

    Elf64_Rela* rela = &builder->rela[builder->rela_count++];
    rela->r_offset = offset;
    rela->r_info = ELF64_R_INFO(sym, type);
    rela->r_addend = addend;
}

static void write_data(elf_builder_t* builder, const void* data, size_t size) {
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

bool elf_write_file(elf_builder_t* builder, const char* filename, uint16_t machine_type) {
    // Build ELF header
    Elf64_Ehdr ehdr;
    memset(&ehdr, 0, sizeof(ehdr));

    ehdr.e_ident[0] = ELFMAG0;
    ehdr.e_ident[1] = ELFMAG1;
    ehdr.e_ident[2] = ELFMAG2;
    ehdr.e_ident[3] = ELFMAG3;
    ehdr.e_ident[4] = ELFCLASS64;
    ehdr.e_ident[5] = ELFDATA2LSB;
    ehdr.e_ident[6] = EV_CURRENT;

    ehdr.e_type = ET_REL;
    ehdr.e_machine = machine_type;
    ehdr.e_version = EV_CURRENT;
    ehdr.e_entry = 0;
    ehdr.e_phoff = 0;
    ehdr.e_flags = 0;
    ehdr.e_ehsize = sizeof(Elf64_Ehdr);
    ehdr.e_phentsize = 0;
    ehdr.e_phnum = 0;
    ehdr.e_shentsize = sizeof(Elf64_Shdr);

    // Reset builder data
    builder->size = 0;

    // Write ELF header
    write_data(builder, &ehdr, sizeof(ehdr));

    // Section data positions
    size_t* section_data_offsets = (size_t*)malloc(sizeof(size_t) * builder->section_count);

    // Write section data and record offsets
    for (int i = 0; i < builder->section_count; i++) {
        section_data_offsets[i] = builder->size;
        // For now, we don't write actual section data here
        // It would be added by the caller
    }

    // Update section header offsets
    size_t shoff = builder->size;

    // Add null section header
    Elf64_Shdr null_shdr;
    memset(&null_shdr, 0, sizeof(null_shdr));
    write_data(builder, &null_shdr, sizeof(null_shdr));

    // Write section headers
    for (int i = 0; i < builder->section_count; i++) {
        builder->sections[i].sh_offset = section_data_offsets[i];
        write_data(builder, &builder->sections[i], sizeof(Elf64_Shdr));
    }

    // Update ELF header with section info
    ehdr.e_shoff = shoff;
    ehdr.e_shnum = builder->section_count + 1; // +1 for null section
    ehdr.e_shstrndx = 1; // String table is usually section 1

    memcpy(builder->data, &ehdr, sizeof(ehdr));

    // Write to file
    FILE* fp = fopen(filename, "wb");
    if (!fp) {
        free(section_data_offsets);
        return false;
    }

    fwrite(builder->data, 1, builder->size, fp);
    fclose(fp);

    free(section_data_offsets);
    return true;
}

bool elf_create_object_file(const char* filename, const uint8_t* code,
                             size_t code_size, const char* function_name,
                             uint16_t machine_type) {
    elf_builder_t* builder = elf_builder_new();

    // Add .text section
    int text_section = elf_add_section(builder, ".text", SHT_PROGBITS,
                                        SHF_ALLOC | SHF_EXECINSTR, code, code_size);

    // Add .strtab section
    int strtab_section = elf_add_section(builder, ".strtab", SHT_STRTAB,
                                          0, (uint8_t*)builder->strtab, builder->strtab_size);

    // Add .symtab section
    int symtab_section = elf_add_section(builder, ".symtab", SHT_SYMTAB,
                                          0, (uint8_t*)builder->symtab,
                                          builder->symtab_count * sizeof(Elf64_Sym));
    builder->sections[symtab_section].sh_link = strtab_section;
    builder->sections[symtab_section].sh_info = 1;

    // Add function symbol
    elf_add_symbol(builder, function_name, STB_GLOBAL, STT_FUNC,
                   text_section + 1, 0, code_size);

    bool result = elf_write_file(builder, filename, machine_type);

    elf_builder_free(builder);
    return result;
}

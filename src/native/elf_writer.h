#ifndef SOX_ELF_WRITER_H
#define SOX_ELF_WRITER_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

// ELF64 header structures
#define EI_NIDENT 16

typedef struct {
    unsigned char e_ident[EI_NIDENT];
    uint16_t e_type;
    uint16_t e_machine;
    uint32_t e_version;
    uint64_t e_entry;
    uint64_t e_phoff;
    uint64_t e_shoff;
    uint32_t e_flags;
    uint16_t e_ehsize;
    uint16_t e_phentsize;
    uint16_t e_phnum;
    uint16_t e_shentsize;
    uint16_t e_shnum;
    uint16_t e_shstrndx;
} Elf64_Ehdr;

typedef struct {
    uint32_t sh_name;
    uint32_t sh_type;
    uint64_t sh_flags;
    uint64_t sh_addr;
    uint64_t sh_offset;
    uint64_t sh_size;
    uint32_t sh_link;
    uint32_t sh_info;
    uint64_t sh_addralign;
    uint64_t sh_entsize;
} Elf64_Shdr;

typedef struct {
    uint32_t st_name;
    unsigned char st_info;
    unsigned char st_other;
    uint16_t st_shndx;
    uint64_t st_value;
    uint64_t st_size;
} Elf64_Sym;

typedef struct {
    uint64_t r_offset;
    uint64_t r_info;
    int64_t r_addend;
} Elf64_Rela;

// ELF constants
#define ELFMAG0 0x7f
#define ELFMAG1 'E'
#define ELFMAG2 'L'
#define ELFMAG3 'F'

#define ELFCLASS64 2
#define ELFDATA2LSB 1
#define EV_CURRENT 1

#define ET_REL 1        // Relocatable object file
#define EM_X86_64 62    // AMD x86-64

// Section types
#define SHT_NULL 0
#define SHT_PROGBITS 1
#define SHT_SYMTAB 2
#define SHT_STRTAB 3
#define SHT_RELA 4

// Section flags
#define SHF_WRITE 0x1
#define SHF_ALLOC 0x2
#define SHF_EXECINSTR 0x4

// Symbol binding
#define STB_LOCAL 0
#define STB_GLOBAL 1

// Symbol types
#define STT_NOTYPE 0
#define STT_FUNC 2

#define STN_UNDEF 0

// Relocation types (x86-64)
#define R_X86_64_NONE 0
#define R_X86_64_64 1
#define R_X86_64_PC32 2
#define R_X86_64_PLT32 4

#define ELF64_R_SYM(i) ((i) >> 32)
#define ELF64_R_TYPE(i) ((i) & 0xffffffffL)
#define ELF64_R_INFO(s, t) (((uint64_t)(s) << 32) + ((t) & 0xffffffffL))

#define ELF64_ST_INFO(b, t) (((b) << 4) + ((t) & 0xf))

// ELF object file builder
typedef struct {
    // File data
    uint8_t* data;
    size_t size;
    size_t capacity;

    // Section tracking
    Elf64_Shdr* sections;
    int section_count;
    int section_capacity;

    // String table
    char* strtab;
    size_t strtab_size;
    size_t strtab_capacity;

    // Symbol table
    Elf64_Sym* symtab;
    int symtab_count;
    int symtab_capacity;

    // Relocations
    Elf64_Rela* rela;
    int rela_count;
    int rela_capacity;
} elf_builder_t;

// Create ELF builder
elf_builder_t* elf_builder_new(void);
void elf_builder_free(elf_builder_t* builder);

// Add sections
int elf_add_section(elf_builder_t* builder, const char* name, uint32_t type,
                    uint64_t flags, const uint8_t* data, size_t size);

// Add symbols
int elf_add_symbol(elf_builder_t* builder, const char* name, unsigned char bind,
                   unsigned char type, uint16_t shndx, uint64_t value, uint64_t size);

// Add relocation
void elf_add_relocation(elf_builder_t* builder, uint64_t offset, uint32_t sym,
                        uint32_t type, int64_t addend);

// Write ELF file
bool elf_write_file(elf_builder_t* builder, const char* filename);

// High-level API: create object file from code
bool elf_create_object_file(const char* filename, const uint8_t* code,
                             size_t code_size, const char* function_name);

#endif

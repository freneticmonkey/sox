#ifndef SOX_MACHO_WRITER_H
#define SOX_MACHO_WRITER_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

// Mach-O header structures (64-bit)

// Mach-O magic numbers
#define MH_MAGIC_64 0xfeedfacf
#define MH_CIGAM_64 0xcffaedfe

// CPU types
#define CPU_TYPE_X86_64 0x01000007
#define CPU_TYPE_ARM64  0x0100000c

// CPU subtypes
#define CPU_SUBTYPE_X86_64_ALL 3
#define CPU_SUBTYPE_ARM64_ALL  0

// File types
#define MH_OBJECT 0x1

// Flags
#define MH_NOUNDEFS 0x1
#define MH_SUBSECTIONS_VIA_SYMBOLS 0x2000

// Load command types
#define LC_SEGMENT_64 0x19
#define LC_SYMTAB 0x2
#define LC_DYSYMTAB 0xb
#define LC_BUILD_VERSION 0x32

// Section flags
#define S_REGULAR 0x0
#define S_CSTRING_LITERALS 0x2
#define S_ATTR_PURE_INSTRUCTIONS 0x80000000
#define S_ATTR_SOME_INSTRUCTIONS 0x00000400

// Symbol types
#define N_UNDF 0x0
#define N_EXT 0x1
#define N_SECT 0xE

// Platform types
#define PLATFORM_MACOS 1
#define PLATFORM_IOS 2

// Tool types
#define TOOL_CLANG 3

// Mach-O header (64-bit)
typedef struct {
    uint32_t magic;
    uint32_t cputype;
    uint32_t cpusubtype;
    uint32_t filetype;
    uint32_t ncmds;
    uint32_t sizeofcmds;
    uint32_t flags;
    uint32_t reserved;
} mach_header_64_t;

// Load command
typedef struct {
    uint32_t cmd;
    uint32_t cmdsize;
} load_command_t;

// Segment command (64-bit)
typedef struct {
    uint32_t cmd;
    uint32_t cmdsize;
    char segname[16];
    uint64_t vmaddr;
    uint64_t vmsize;
    uint64_t fileoff;
    uint64_t filesize;
    uint32_t maxprot;
    uint32_t initprot;
    uint32_t nsects;
    uint32_t flags;
} segment_command_64_t;

// Section (64-bit)
typedef struct {
    char sectname[16];
    char segname[16];
    uint64_t addr;
    uint64_t size;
    uint32_t offset;
    uint32_t align;
    uint32_t reloff;
    uint32_t nreloc;
    uint32_t flags;
    uint32_t reserved1;
    uint32_t reserved2;
    uint32_t reserved3;
} section_64_t;

// Symbol table command
typedef struct {
    uint32_t cmd;
    uint32_t cmdsize;
    uint32_t symoff;
    uint32_t nsyms;
    uint32_t stroff;
    uint32_t strsize;
} symtab_command_t;

// Dynamic symbol table command
typedef struct {
    uint32_t cmd;
    uint32_t cmdsize;
    uint32_t ilocalsym;
    uint32_t nlocalsym;
    uint32_t iextdefsym;
    uint32_t nextdefsym;
    uint32_t iundefsym;
    uint32_t nundefsym;
    uint32_t tocoff;
    uint32_t ntoc;
    uint32_t modtaboff;
    uint32_t nmodtab;
    uint32_t extrefsymoff;
    uint32_t nextrefsyms;
    uint32_t indirectsymoff;
    uint32_t nindirectsyms;
    uint32_t extreloff;
    uint32_t nextrel;
    uint32_t locreloff;
    uint32_t nlocrel;
} dysymtab_command_t;

// Build version command
typedef struct {
    uint32_t cmd;
    uint32_t cmdsize;
    uint32_t platform;
    uint32_t minos;
    uint32_t sdk;
    uint32_t ntools;
} build_version_command_t;

// Build tool version
typedef struct {
    uint32_t tool;
    uint32_t version;
} build_tool_version_t;

// Symbol table entry (64-bit)
typedef struct {
    uint32_t n_strx;
    uint8_t n_type;
    uint8_t n_sect;
    uint16_t n_desc;
    uint64_t n_value;
} nlist_64_t;

// Relocation entry (Apple's official Mach-O format)
// 8 bytes total: 4-byte address + 4-byte packed info word
// Bit fields are NOT used because they are platform-dependent and unreliable for binary formats
typedef struct {
    int32_t r_address;    // Offset in section where relocation occurs
    uint32_t r_info;      // Packed: bits[0-23]=symbolnum, [24]=pcrel, [25-26]=length, [27]=extern, [28-31]=type
} relocation_info_t;

// Mach-O builder context
typedef struct {
    uint32_t cputype;
    uint32_t cpusubtype;

    // File data
    uint8_t* data;
    size_t size;
    size_t capacity;

    // Sections
    section_64_t* sections;
    int section_count;
    int section_capacity;

    // Section data
    uint8_t** section_data;
    size_t* section_sizes;

    // String table
    char* strtab;
    size_t strtab_size;
    size_t strtab_capacity;

    // Symbol table
    nlist_64_t* symtab;
    int symtab_count;
    int symtab_capacity;

    // Relocations
    relocation_info_t* relocs;
    int reloc_count;
    int reloc_capacity;
} macho_builder_t;

// Create Mach-O builder
macho_builder_t* macho_builder_new(uint32_t cputype, uint32_t cpusubtype);
void macho_builder_free(macho_builder_t* builder);

// Add section
int macho_add_section(macho_builder_t* builder, const char* sectname,
                      const char* segname, uint32_t flags,
                      const uint8_t* data, size_t size, uint32_t align);

// Add symbol
int macho_add_symbol(macho_builder_t* builder, const char* name,
                     uint8_t type, uint8_t sect, uint64_t value);

// Add relocation
void macho_add_relocation(macho_builder_t* builder, int32_t address,
                          uint32_t symbolnum, bool pcrel, uint32_t length,
                          bool external, uint32_t type);

// Write Mach-O file
bool macho_write_file(macho_builder_t* builder, const char* filename);

// High-level API: create object file from code
bool macho_create_object_file(const char* filename, const uint8_t* code,
                               size_t code_size, const char* function_name,
                               uint32_t cputype, uint32_t cpusubtype);

// High-level API: create executable-ready object file (with main entry point)
bool macho_create_executable_object_file(const char* filename, const uint8_t* code,
                                         size_t code_size,
                                         uint32_t cputype, uint32_t cpusubtype);

// Forward declaration - opaque type (actual definition in arm64_encoder.h as arm64_relocation_t)
typedef void* arm64_relocation;

// Forward declaration - opaque type (actual definition in codegen_arm64.h as string_literal_t)
typedef void* string_literal;

// High-level API: create object file with ARM64 relocations
bool macho_create_object_file_with_arm64_relocs(const char* filename, const uint8_t* code,
                                                 size_t code_size, const char* function_name,
                                                 uint32_t cputype, uint32_t cpusubtype,
                                                 const arm64_relocation* relocations,
                                                 int relocation_count);

// High-level API: create executable-ready object file with ARM64 relocations
bool macho_create_executable_object_file_with_arm64_relocs(const char* filename, const uint8_t* code,
                                                            size_t code_size,
                                                            uint32_t cputype, uint32_t cpusubtype,
                                                            const arm64_relocation* relocations,
                                                            int relocation_count);

// High-level API: create object file with ARM64 relocations and string literals
bool macho_create_object_file_with_arm64_relocs_and_strings(const char* filename, const uint8_t* code,
                                                              size_t code_size, const char* function_name,
                                                              uint32_t cputype, uint32_t cpusubtype,
                                                              const arm64_relocation* relocations,
                                                              int relocation_count,
                                                              const string_literal* string_literals,
                                                              int string_literal_count);

// High-level API: create executable-ready object file with ARM64 relocations and string literals
bool macho_create_executable_object_file_with_arm64_relocs_and_strings(const char* filename, const uint8_t* code,
                                                                         size_t code_size,
                                                                         uint32_t cputype, uint32_t cpusubtype,
                                                                         const arm64_relocation* relocations,
                                                                         int relocation_count,
                                                                         const string_literal* string_literals,
                                                                         int string_literal_count);

#endif

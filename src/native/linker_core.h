#ifndef SOX_LINKER_CORE_H
#define SOX_LINKER_CORE_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/*
 * Custom Linker Core Data Structures
 *
 * This module defines the core data structures for the Sox custom linker.
 * The linker processes object files (ELF, Mach-O) and combines them into
 * executable binaries.
 *
 * Phase 1.1: Core data structures and basic API
 */

/* Forward declarations */
typedef struct linker_object_t linker_object_t;
typedef struct linker_section_t linker_section_t;
typedef struct linker_symbol_t linker_symbol_t;
typedef struct linker_relocation_t linker_relocation_t;
typedef struct linker_context_t linker_context_t;

/* Platform-specific object file formats */
typedef enum {
    PLATFORM_FORMAT_UNKNOWN = 0,
    PLATFORM_FORMAT_ELF,      /* Linux ELF64 */
    PLATFORM_FORMAT_MACH_O,   /* macOS Mach-O 64-bit */
    PLATFORM_FORMAT_PE_COFF   /* Windows PE/COFF (future) */
} platform_format_t;

/* Section types (unified across platforms) */
typedef enum {
    SECTION_TYPE_UNKNOWN = 0,
    SECTION_TYPE_TEXT,    /* Executable code (.text) */
    SECTION_TYPE_DATA,    /* Initialized data (.data) */
    SECTION_TYPE_BSS,     /* Uninitialized data (.bss) */
    SECTION_TYPE_RODATA   /* Read-only data (.rodata) */
} section_type_t;

/* Symbol types */
typedef enum {
    SYMBOL_TYPE_NOTYPE = 0,  /* No type specified */
    SYMBOL_TYPE_FUNC,        /* Function symbol */
    SYMBOL_TYPE_OBJECT,      /* Data object symbol */
    SYMBOL_TYPE_SECTION      /* Section symbol */
} symbol_type_t;

/* Symbol binding attributes */
typedef enum {
    SYMBOL_BINDING_LOCAL = 0,  /* Local symbol (not visible outside object) */
    SYMBOL_BINDING_GLOBAL,     /* Global symbol (visible to all objects) */
    SYMBOL_BINDING_WEAK        /* Weak symbol (can be overridden) */
} symbol_binding_t;

/* Relocation types (unified across platforms) */
typedef enum {
    /* None */
    RELOC_NONE = 0,

    /* x86-64 relocations */
    RELOC_X64_64,           /* 64-bit absolute address */
    RELOC_X64_PC32,         /* 32-bit PC-relative */
    RELOC_X64_PLT32,        /* PLT-relative (function calls) */
    RELOC_X64_GOTPCREL,     /* GOT-relative */

    /* ARM64 relocations */
    RELOC_ARM64_ABS64,      /* 64-bit absolute */
    RELOC_ARM64_CALL26,     /* 26-bit PC-relative call (BL) */
    RELOC_ARM64_JUMP26,     /* 26-bit PC-relative jump (B) */
    RELOC_ARM64_ADR_PREL_PG_HI21,   /* Page-relative ADR */
    RELOC_ARM64_ADD_ABS_LO12_NC,    /* Low 12 bits */

    /* Common */
    RELOC_RELATIVE,         /* Relative to load address */
} relocation_type_t;

/* Section representation (unified from different object formats) */
struct linker_section_t {
    char* name;                     /* Section name (".text", ".data", etc.) */
    section_type_t type;            /* Section type */
    uint8_t* data;                  /* Section data */
    size_t size;                    /* Section size in bytes */
    size_t alignment;               /* Required alignment (power of 2) */
    uint64_t vaddr;                 /* Virtual address (assigned during layout) */
    uint32_t flags;                 /* Read/write/execute permissions */
    int object_index;               /* Which object file owns this section */
};

/* Symbol representation (unified from different object formats) */
struct linker_symbol_t {
    char* name;                     /* Symbol name */
    symbol_type_t type;             /* Symbol type (function, object, etc.) */
    symbol_binding_t binding;       /* Symbol binding (local, global, weak) */
    int section_index;              /* Section containing symbol (-1 if undefined) */
    uint64_t value;                 /* Symbol value/offset within section */
    uint64_t size;                  /* Symbol size in bytes */
    uint64_t final_address;         /* Final address (computed during layout) */
    bool is_defined;                /* True if defined in some object */
    int defining_object;            /* Index of object that defines this symbol */
};

/* Relocation representation (unified from different object formats) */
struct linker_relocation_t {
    uint64_t offset;                /* Offset in section where relocation occurs */
    relocation_type_t type;         /* Relocation type */
    int symbol_index;               /* Index into symbol table */
    int64_t addend;                 /* Addend for calculation */
    int section_index;              /* Section being relocated */
    int object_index;               /* Source object file index */
};

/* Unified object file representation */
struct linker_object_t {
    char* filename;                 /* Source filename */
    platform_format_t format;       /* Object file format (ELF, Mach-O, etc.) */

    /* Sections */
    linker_section_t* sections;
    int section_count;
    int section_capacity;

    /* Symbols */
    linker_symbol_t* symbols;
    int symbol_count;
    int symbol_capacity;

    /* Relocations */
    linker_relocation_t* relocations;
    int relocation_count;
    int relocation_capacity;

    /* Raw data (optional, for debugging) */
    uint8_t* raw_data;
    size_t raw_size;
};

/* Global linker context */
struct linker_context_t {
    /* Input objects */
    linker_object_t** objects;
    int object_count;
    int object_capacity;

    /* Symbol resolution (to be implemented in Phase 2) */
    linker_symbol_t* global_symbols;
    int global_symbol_count;
    void* symbol_lookup;            /* Hash table for quick lookup (TBD) */

    /* Layout (to be implemented in Phase 3) */
    linker_section_t* merged_sections;
    int merged_section_count;
    uint64_t base_address;          /* Base load address */
    uint64_t total_size;            /* Total memory footprint */

    /* Output (to be implemented in Phase 5) */
    uint8_t* executable_data;
    size_t executable_size;
    uint64_t entry_point;           /* Address of _start or main */

    /* Target platform */
    platform_format_t target_format;
};

/*
 * Core API Functions
 */

/* Create and destroy linker context */
linker_context_t* linker_context_new(void);
void linker_context_free(linker_context_t* context);

/* Add object file to context */
bool linker_context_add_object(linker_context_t* context, linker_object_t* object);

/* Object file management */
linker_object_t* linker_object_new(const char* filename, platform_format_t format);
void linker_object_free(linker_object_t* object);

/* Section management */
linker_section_t* linker_object_add_section(linker_object_t* object);
void linker_section_init(linker_section_t* section);
void linker_section_free(linker_section_t* section);

/* Symbol management */
linker_symbol_t* linker_object_add_symbol(linker_object_t* object);
void linker_symbol_init(linker_symbol_t* symbol);
void linker_symbol_free(linker_symbol_t* symbol);

/* Relocation management */
linker_relocation_t* linker_object_add_relocation(linker_object_t* object);
void linker_relocation_init(linker_relocation_t* relocation);

/* Utility functions */
const char* platform_format_name(platform_format_t format);
const char* section_type_name(section_type_t type);
const char* symbol_type_name(symbol_type_t type);
const char* symbol_binding_name(symbol_binding_t binding);
const char* relocation_type_name(relocation_type_t type);

#endif

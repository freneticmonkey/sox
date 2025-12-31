#ifndef SOX_MACHO_EXECUTABLE_H
#define SOX_MACHO_EXECUTABLE_H

#include "linker_core.h"
#include "macho_writer.h"
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/*
 * Mach-O Executable Writer
 *
 * This module implements Phase 5.2 of the custom linker: generating executable
 * Mach-O binaries for macOS/ARM64. It takes the linked sections and symbols
 * from the linker context and creates a runnable executable.
 *
 * Key Responsibilities:
 * - Create Mach-O header with MH_EXECUTE filetype
 * - Generate load commands (LC_SEGMENT_64, LC_MAIN, LC_LOAD_DYLINKER)
 * - Write __TEXT segment (__text, __const sections)
 * - Write __DATA segment (__data, __bss sections)
 * - Set entry point to _main symbol
 * - Set executable file permissions (0755)
 *
 * Mach-O Executable Structure:
 * ┌─────────────────────────────────────┐
 * │ Mach-O Header                       │
 * │  - magic = MH_MAGIC_64              │
 * │  - cputype = CPU_TYPE_ARM64         │
 * │  - filetype = MH_EXECUTE            │
 * │  - ncmds = number of load commands  │
 * ├─────────────────────────────────────┤
 * │ Load Commands                       │
 * │  - LC_SEGMENT_64 (__TEXT)           │
 * │  - LC_SEGMENT_64 (__DATA)           │
 * │  - LC_MAIN (entry point)            │
 * │  - LC_LOAD_DYLINKER                 │
 * ├─────────────────────────────────────┤
 * │ __TEXT Segment                      │
 * │  - __text section (code)            │
 * │  - __const section (rodata)         │
 * ├─────────────────────────────────────┤
 * │ __DATA Segment                      │
 * │  - __data section                   │
 * │  - __bss section                    │
 * └─────────────────────────────────────┘
 *
 * Phase 5.2: Mach-O Executable Writer
 */

/* Additional Mach-O constants for executables (extend macho_writer.h) */
#define MH_EXECUTE 0x2              /* Executable file type */
#define MH_PIE 0x200000             /* Position-independent executable */
#define MH_DYLDLINK 0x4             /* Dynamically linked */
#define MH_TWOLEVEL 0x80            /* Two-level namespace */

#define LC_MAIN 0x80000028          /* Entry point command (modern macOS) */
#define LC_LOAD_DYLINKER 0xe        /* Dynamic linker command */
#define LC_LOAD_DYLIB 0xc           /* Load dynamic library */
#define LC_SYMTAB 0x2               /* Symbol table */
#define LC_DYSYMTAB 0xb             /* Dynamic symbol table */
#define LC_UUID 0x1b                /* UUID */
#define LC_BUILD_VERSION 0x32       /* Build version */
#define LC_DYLD_INFO_ONLY 0x80000022 /* Dyld info (compressed) */

#define VM_PROT_NONE 0x00           /* No protection */
#define VM_PROT_READ 0x01           /* Read permission */
#define VM_PROT_WRITE 0x02          /* Write permission */
#define VM_PROT_EXECUTE 0x04        /* Execute permission */

#define SG_READ_ONLY 0x10           /* Read-only data segment */

#define S_ZEROFILL 0x1              /* Zero-filled section (no file content) */
#define S_SYMBOL_STUBS 0x8          /* Section contains symbol stubs */
#define S_NON_LAZY_SYMBOL_POINTERS 0x6 /* Non-lazy symbol pointers */
#define S_THREAD_LOCAL_REGULAR 0x11     /* Thread-local data section */
#define S_THREAD_LOCAL_ZEROFILL 0x12    /* Thread-local bss section */
#define S_THREAD_LOCAL_VARIABLES 0x13   /* Thread-local variable section */

/* Segment names */
#define SEG_TEXT "__TEXT"           /* Text segment name */
#define SEG_DATA "__DATA"           /* Data segment name */
#define SEG_DATA_CONST "__DATA_CONST" /* Read-only data segment */

/* Section names */
#define SECT_TEXT "__text"          /* Code section */
#define SECT_CONST "__const"        /* Read-only data section */
#define SECT_STUBS "__stubs"        /* Symbol stubs section */
#define SECT_DATA "__data"          /* Initialized data section */
#define SECT_BSS "__bss"            /* Uninitialized data section */
#define SECT_GOT "__got"            /* Global offset table section */
#define SECT_TLV "__thread_vars"    /* Thread-local variables section */
#define SECT_TDATA "__thread_data"  /* Thread-local data section */
#define SECT_TBSS "__thread_bss"    /* Thread-local bss section */

/* Default dynamic linker path */
#define DYLD_PATH "/usr/lib/dyld"
#define LIBSYSTEM_PATH "/usr/lib/libSystem.B.dylib"

typedef struct {
    uint32_t name_offset;
    uint32_t timestamp;
    uint32_t current_version;
    uint32_t compatibility_version;
} dylib_t;

typedef struct {
    uint32_t cmd;
    uint32_t cmdsize;
    dylib_t dylib;
} dylib_command_t;

/* Entry point command structure (LC_MAIN) */
typedef struct {
    uint32_t cmd;                   /* LC_MAIN */
    uint32_t cmdsize;               /* Size of this command */
    uint64_t entryoff;              /* Offset of entry point from __TEXT */
    uint64_t stacksize;             /* Stack size (0 = use default) */
} entry_point_command_t;

/* Dynamic linker command structure (LC_LOAD_DYLINKER) */
typedef struct {
    uint32_t cmd;                   /* LC_LOAD_DYLINKER */
    uint32_t cmdsize;               /* Size including path string */
    uint32_t name_offset;           /* Offset to dylinker path (usually 12) */
    /* Followed by null-terminated dylinker path string */
} dylinker_command_t;

/* UUID command structure (LC_UUID) */
typedef struct {
    uint32_t cmd;                   /* LC_UUID */
    uint32_t cmdsize;               /* sizeof(uuid_command_t) */
    uint8_t uuid[16];               /* 128-bit UUID */
} uuid_command_t;

/* Dyld info command structure (LC_DYLD_INFO_ONLY) */
typedef struct {
    uint32_t cmd;
    uint32_t cmdsize;
    uint32_t rebase_off;
    uint32_t rebase_size;
    uint32_t bind_off;
    uint32_t bind_size;
    uint32_t weak_bind_off;
    uint32_t weak_bind_size;
    uint32_t lazy_bind_off;
    uint32_t lazy_bind_size;
    uint32_t export_off;
    uint32_t export_size;
} dyld_info_command_t;

/*
 * Main Mach-O Executable Writer API
 */

/**
 * Write a Mach-O executable file from linker context
 *
 * This is the main function that creates a complete executable binary
 * from the linked sections and symbols in the linker context.
 *
 * Steps:
 * 1. Create mach_header_64 with MH_EXECUTE filetype
 * 2. Create LC_SEGMENT_64 for __TEXT segment
 * 3. Create LC_SEGMENT_64 for __DATA segment
 * 4. Create LC_MAIN entry point command
 * 5. Create LC_LOAD_DYLINKER command
 * 6. Write all structures and section data to file
 * 7. Set file permissions to 0755
 *
 * @param output_path Path to output executable file
 * @param context Linker context with merged sections and symbols
 * @return true on success, false on error
 */
bool macho_write_executable(const char* output_path,
                             linker_context_t* context);

/**
 * Set entry point to _main symbol address
 *
 * macOS uses _main directly as the entry point (no _start wrapper needed).
 * This function looks up the _main symbol in the symbol table and sets
 * context->entry_point to its final address.
 *
 * @param context Linker context with resolved symbols
 * @return true on success, false if _main not found
 */
bool macho_set_entry_point(linker_context_t* context);

/**
 * Calculate total size of load commands
 *
 * Helper function to compute the total size of all load commands,
 * needed for mach_header_64.sizeofcmds field.
 *
 * @param context Linker context with sections
 * @return Total size in bytes of all load commands
 */
uint32_t macho_calculate_load_commands_size(linker_context_t* context);

/**
 * Get section count for a segment
 *
 * Helper function to count how many sections belong to a given segment.
 *
 * @param context Linker context with merged sections
 * @param segment_name Segment name ("__TEXT" or "__DATA")
 * @return Number of sections in the segment
 */
uint32_t macho_get_segment_section_count(linker_context_t* context,
                                          const char* segment_name);

/**
 * Calculate segment size
 *
 * Helper function to calculate the total size of all sections in a segment.
 *
 * @param context Linker context with merged sections
 * @param segment_name Segment name ("__TEXT" or "__DATA")
 * @return Total size in bytes of the segment
 */
uint64_t macho_calculate_segment_size(linker_context_t* context,
                                       const char* segment_name);

#endif

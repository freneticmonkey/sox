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

#define LC_MAIN 0x80000028          /* Entry point command (modern macOS) */
#define LC_LOAD_DYLINKER 0xe        /* Dynamic linker command */
#define LC_SYMTAB 0x2               /* Symbol table */
#define LC_DYSYMTAB 0xb             /* Dynamic symbol table */
#define LC_UUID 0x1b                /* UUID */
#define LC_BUILD_VERSION 0x32       /* Build version */

#define VM_PROT_NONE 0x00           /* No protection */
#define VM_PROT_READ 0x01           /* Read permission */
#define VM_PROT_WRITE 0x02          /* Write permission */
#define VM_PROT_EXECUTE 0x04        /* Execute permission */

/* Segment names */
#define SEG_TEXT "__TEXT"           /* Text segment name */
#define SEG_DATA "__DATA"           /* Data segment name */

/* Section names */
#define SECT_TEXT "__text"          /* Code section */
#define SECT_CONST "__const"        /* Read-only data section */
#define SECT_DATA "__data"          /* Initialized data section */
#define SECT_BSS "__bss"            /* Uninitialized data section */

/* Default dynamic linker path */
#define DYLD_PATH "/usr/lib/dyld"

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

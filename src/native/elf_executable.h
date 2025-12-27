#ifndef SOX_ELF_EXECUTABLE_H
#define SOX_ELF_EXECUTABLE_H

#include <stdint.h>
#include <stdbool.h>
#include "linker_core.h"
#include "elf_writer.h"

/*
 * ELF Executable Generation
 *
 * This module generates executable ELF files from a linked context.
 * It creates program headers (PT_LOAD segments) for code and data,
 * and optionally generates an entry point (_start) function.
 *
 * Phase 5.1: ELF Executable Writer
 */

/* Program header types */
#define PT_NULL    0
#define PT_LOAD    1
#define PT_DYNAMIC 2
#define PT_INTERP  3
#define PT_NOTE    4
#define PT_SHLIB   5
#define PT_PHDR    6

/* Program header flags */
#define PF_X 0x1  /* Execute */
#define PF_W 0x2  /* Write */
#define PF_R 0x4  /* Read */

/* ELF types */
#define ET_EXEC 2     /* Executable file */
#define ET_DYN  3     /* Shared object file / PIE executable */

/* ELF identification indices */
#define EI_MAG0    0  /* File identification */
#define EI_MAG1    1
#define EI_MAG2    2
#define EI_MAG3    3
#define EI_CLASS   4  /* File class */
#define EI_DATA    5  /* Data encoding */
#define EI_VERSION 6  /* File version */
#define EI_OSABI   7  /* OS/ABI identification */
#define EI_ABIVERSION 8  /* ABI version */
#define EI_PAD     9  /* Start of padding bytes */

/* ELF OS/ABI values */
#define ELFOSABI_SYSV 0   /* System V ABI */
#define ELFOSABI_LINUX 3  /* Linux */

/* Program header structure */
typedef struct {
    uint32_t p_type;
    uint32_t p_flags;
    uint64_t p_offset;
    uint64_t p_vaddr;
    uint64_t p_paddr;
    uint64_t p_filesz;
    uint64_t p_memsz;
    uint64_t p_align;
} Elf64_Phdr;

/* Entry point code generation options */
typedef struct {
    bool generate_start;    /* Generate _start entry point */
    bool call_main;         /* Have _start call main() */
    uint16_t machine_type;  /* EM_X86_64 or EM_AARCH64 */
} entry_point_options_t;

/*
 * Write ELF executable file
 *
 * Generates a complete executable ELF file from the linker context.
 * Creates program headers for loadable segments and writes all section data.
 *
 * Parameters:
 *   output_path - Path to output executable file
 *   context     - Linker context with merged sections and symbols
 *
 * Returns:
 *   true on success, false on error
 */
bool elf_write_executable(const char* output_path,
                           linker_context_t* context);

/*
 * Generate entry point code
 *
 * Generates the _start function that serves as the program entry point.
 * The generated code:
 *   1. Clears the frame pointer (rbp/fp)
 *   2. Calls the main function
 *   3. Exits with the return value from main
 *
 * For x86-64:
 *   xor rbp, rbp
 *   call main
 *   mov rdi, rax
 *   mov rax, 60      ; sys_exit
 *   syscall
 *
 * For ARM64:
 *   mov x29, #0
 *   bl main
 *   mov x0, x0       ; exit code already in x0
 *   mov x8, #93      ; sys_exit
 *   svc #0
 *
 * Parameters:
 *   context - Linker context to add entry point code to
 *   options - Entry point generation options
 *
 * Returns:
 *   true on success, false on error
 */
bool elf_generate_entry_point(linker_context_t* context,
                                const entry_point_options_t* options);

/*
 * Get default entry point options
 *
 * Returns default options for entry point generation based on
 * the target architecture.
 *
 * Parameters:
 *   machine_type - EM_X86_64 or EM_AARCH64
 *
 * Returns:
 *   Default entry point options
 */
entry_point_options_t elf_get_default_entry_options(uint16_t machine_type);

/*
 * Calculate executable memory layout
 *
 * Calculates virtual addresses for all sections based on the base address
 * and alignment requirements. Updates section vaddr fields in context.
 *
 * Parameters:
 *   context - Linker context with merged sections
 *
 * Returns:
 *   true on success, false on error
 */
bool elf_calculate_layout(linker_context_t* context);

/*
 * Find section by type
 *
 * Helper function to find a merged section by its type.
 *
 * Parameters:
 *   context - Linker context
 *   type    - Section type to find
 *
 * Returns:
 *   Pointer to section or NULL if not found
 */
linker_section_t* elf_find_section_by_type(linker_context_t* context,
                                             section_type_t type);

#endif

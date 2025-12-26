#ifndef SOX_SECTION_LAYOUT_H
#define SOX_SECTION_LAYOUT_H

#include "linker_core.h"
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/*
 * Section Layout & Address Assignment
 *
 * This module implements Phase 3 of the custom linker: merging sections from
 * multiple object files and assigning virtual addresses to create the final
 * executable memory layout.
 *
 * Key Responsibilities:
 * - Merge sections of the same type (.text, .data, .rodata, .bss)
 * - Handle section alignment requirements
 * - Assign virtual addresses to merged sections
 * - Support platform-specific memory layouts (ELF, Mach-O)
 * - Track section contributions for debugging
 *
 * Phase 3.1: Section Merging
 * Phase 3.2: Virtual Address Assignment
 */

/* Forward declarations */
typedef struct section_layout_t section_layout_t;
typedef struct merged_section_t merged_section_t;
typedef struct section_contribution_t section_contribution_t;

/* Section contribution tracking (which object contributed what data) */
struct section_contribution_t {
    int object_index;               /* Which object file contributed this */
    int section_index;              /* Original section index in object */
    uint64_t offset_in_merged;      /* Offset within merged section */
    size_t size;                    /* Size of contribution */
    section_contribution_t* next;   /* Linked list of contributions */
};

/* Merged section representation */
struct merged_section_t {
    char* name;                     /* Section name (".text", ".data", etc.) */
    section_type_t type;            /* Section type */
    uint8_t* data;                  /* Merged section data */
    size_t size;                    /* Current size in bytes */
    size_t capacity;                /* Allocated capacity */
    size_t alignment;               /* Maximum alignment requirement */
    uint64_t vaddr;                 /* Assigned virtual address */
    uint32_t flags;                 /* Read/write/execute permissions */
    section_contribution_t* contributions; /* List of contributions */
};

/* Section layout context */
struct section_layout_t {
    merged_section_t* sections;     /* Array of merged sections */
    int section_count;              /* Number of merged sections */
    int section_capacity;           /* Allocated capacity */
    uint64_t base_address;          /* Base load address */
    uint64_t total_size;            /* Total memory footprint */
    platform_format_t target_format;/* Target platform format */

    /* Platform-specific page sizes */
    size_t page_size;               /* Page size (4KB for ELF, 16KB for Mach-O) */
};

/*
 * Section Layout API
 */

/* Create and destroy section layout context */
section_layout_t* section_layout_new(uint64_t base_address,
                                     platform_format_t target_format);
void section_layout_free(section_layout_t* layout);

/* Add sections from an object file to the layout */
void section_layout_add_object(section_layout_t* layout,
                               linker_object_t* obj,
                               int object_index);

/* Compute final layout (merge sections and assign addresses) */
void section_layout_compute(section_layout_t* layout);

/* Get a merged section by name */
merged_section_t* section_layout_find_section(section_layout_t* layout,
                                               const char* name);

/* Get a merged section by type */
merged_section_t* section_layout_find_section_by_type(section_layout_t* layout,
                                                       section_type_t type);

/* Get the final virtual address for a symbol */
uint64_t section_layout_get_symbol_address(section_layout_t* layout,
                                            linker_symbol_t* symbol);

/* Get the final address for a section offset */
uint64_t section_layout_get_address(section_layout_t* layout,
                                    int object_index,
                                    int section_index,
                                    uint64_t offset);

/* Print layout information (for debugging) */
void section_layout_print(section_layout_t* layout);

/*
 * Utility Functions
 */

/* Align value to specified alignment (must be power of 2) */
uint64_t align_to(uint64_t value, size_t alignment);

/* Get default base address for platform */
uint64_t get_default_base_address(platform_format_t format);

/* Get page size for platform */
size_t get_page_size(platform_format_t format);

/* Get platform-specific section name */
const char* get_platform_section_name(platform_format_t format,
                                       section_type_t type);

#endif

#include "section_layout.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/*
 * Section Layout & Address Assignment Implementation
 *
 * This module merges sections from multiple object files and assigns virtual
 * addresses to create the final executable memory layout.
 *
 * Phase 3.1: Section Merging
 * - Merge sections of the same type from all objects
 * - Track section contributions for debugging
 * - Handle alignment requirements
 *
 * Phase 3.2: Virtual Address Assignment
 * - Assign virtual addresses based on platform-specific layouts
 * - ELF: 4KB page alignment, base 0x400000
 * - Mach-O: 16KB page alignment, base 0x100000000
 */

/* Default capacity for dynamic arrays */
#define DEFAULT_SECTION_CAPACITY 8
#define DEFAULT_DATA_CAPACITY 4096

/* Helper: grow capacity using standard growth pattern */
static int grow_capacity(int capacity) {
    return capacity < DEFAULT_SECTION_CAPACITY ? DEFAULT_SECTION_CAPACITY : capacity * 2;
}

/* Helper: grow data capacity */
static size_t grow_data_capacity(size_t capacity) {
    return capacity < DEFAULT_DATA_CAPACITY ? DEFAULT_DATA_CAPACITY : capacity * 2;
}

/* Helper: duplicate string (caller must free) */
static char* duplicate_string(const char* str) {
    if (str == NULL) {
        return NULL;
    }
    size_t len = strlen(str);
    char* copy = malloc(len + 1);
    if (copy == NULL) {
        fprintf(stderr, "Section layout error: Failed to allocate memory for string\n");
        return NULL;
    }
    memcpy(copy, str, len + 1);
    return copy;
}

/*
 * Utility Functions
 */

/* Align value to specified alignment (must be power of 2) */
uint64_t align_to(uint64_t value, size_t alignment) {
    if (alignment == 0 || alignment == 1) {
        return value;
    }
    return (value + alignment - 1) & ~(alignment - 1);
}

/* Get default base address for platform */
uint64_t get_default_base_address(platform_format_t format) {
    switch (format) {
        case PLATFORM_FORMAT_ELF:
            return 0x400000;      /* Standard x86-64 ELF base */
        case PLATFORM_FORMAT_MACH_O:
            return 0x100000000;   /* Standard ARM64 Mach-O base */
        case PLATFORM_FORMAT_PE_COFF:
            return 0x400000;      /* Standard Windows PE base */
        default:
            return 0x400000;      /* Default fallback */
    }
}

/* Get page size for platform */
size_t get_page_size(platform_format_t format) {
    switch (format) {
        case PLATFORM_FORMAT_ELF:
            return 4096;          /* 4KB pages for x86-64 */
        case PLATFORM_FORMAT_MACH_O:
            return 16384;         /* 16KB pages for ARM64 */
        case PLATFORM_FORMAT_PE_COFF:
            return 4096;          /* 4KB pages for Windows */
        default:
            return 4096;          /* Default fallback */
    }
}

/* Get platform-specific section name */
const char* get_platform_section_name(platform_format_t format,
                                       section_type_t type) {
    if (format == PLATFORM_FORMAT_MACH_O) {
        /* Mach-O uses __segment,__section naming */
        switch (type) {
            case SECTION_TYPE_TEXT:
                return "__TEXT,__text";
            case SECTION_TYPE_DATA:
                return "__DATA,__data";
            case SECTION_TYPE_BSS:
                return "__DATA,__bss";
            case SECTION_TYPE_RODATA:
                return "__TEXT,__const";
            default:
                return "__TEXT,__unknown";
        }
    } else {
        /* ELF and PE use standard names */
        switch (type) {
            case SECTION_TYPE_TEXT:
                return ".text";
            case SECTION_TYPE_DATA:
                return ".data";
            case SECTION_TYPE_BSS:
                return ".bss";
            case SECTION_TYPE_RODATA:
                return ".rodata";
            default:
                return ".unknown";
        }
    }
}

/*
 * Section Contribution Management
 */

static section_contribution_t* contribution_new(int object_index,
                                                 int section_index,
                                                 uint64_t offset,
                                                 size_t size) {
    section_contribution_t* contrib = malloc(sizeof(section_contribution_t));
    if (contrib == NULL) {
        fprintf(stderr, "Section layout error: Failed to allocate contribution\n");
        return NULL;
    }

    contrib->object_index = object_index;
    contrib->section_index = section_index;
    contrib->offset_in_merged = offset;
    contrib->size = size;
    contrib->next = NULL;

    return contrib;
}

static void contribution_free_list(section_contribution_t* head) {
    while (head != NULL) {
        section_contribution_t* next = head->next;
        free(head);
        head = next;
    }
}

static void contribution_add(merged_section_t* section,
                             section_contribution_t* contrib) {
    if (section->contributions == NULL) {
        section->contributions = contrib;
    } else {
        /* Append to end of list */
        section_contribution_t* curr = section->contributions;
        while (curr->next != NULL) {
            curr = curr->next;
        }
        curr->next = contrib;
    }
}

/*
 * Merged Section Management
 */

static merged_section_t* merged_section_new(const char* name,
                                              section_type_t type,
                                              uint32_t flags) {
    merged_section_t* section = malloc(sizeof(merged_section_t));
    if (section == NULL) {
        fprintf(stderr, "Section layout error: Failed to allocate merged section\n");
        return NULL;
    }

    section->name = duplicate_string(name);
    section->type = type;
    section->flags = flags;
    section->data = NULL;
    section->size = 0;
    section->capacity = 0;
    section->alignment = 1;
    section->vaddr = 0;
    section->contributions = NULL;

    return section;
}

static void merged_section_free(merged_section_t* section) {
    if (section == NULL) {
        return;
    }

    free(section->name);
    free(section->data);
    contribution_free_list(section->contributions);
}

/* Append data to a merged section with alignment */
static bool merged_section_append(merged_section_t* section,
                                   const uint8_t* data,
                                   size_t data_size,
                                   size_t alignment,
                                   int object_index,
                                   int section_index) {
    /* Update maximum alignment requirement */
    if (alignment > section->alignment) {
        section->alignment = alignment;
    }

    /* Align current size */
    uint64_t aligned_offset = align_to(section->size, alignment);
    size_t padding = aligned_offset - section->size;

    /* Calculate new size */
    size_t new_size = aligned_offset + data_size;

    /* Grow capacity if needed */
    if (new_size > section->capacity) {
        size_t new_capacity = section->capacity;
        while (new_capacity < new_size) {
            new_capacity = grow_data_capacity(new_capacity);
        }

        uint8_t* new_data = realloc(section->data, new_capacity);
        if (new_data == NULL) {
            fprintf(stderr, "Section layout error: Failed to grow section data\n");
            return false;
        }

        section->data = new_data;
        section->capacity = new_capacity;
    }

    /* Add padding bytes (zeros) */
    if (padding > 0) {
        memset(section->data + section->size, 0, padding);
    }

    /* Copy data */
    if (data != NULL && data_size > 0) {
        memcpy(section->data + aligned_offset, data, data_size);
    }

    /* Track contribution */
    section_contribution_t* contrib = contribution_new(object_index,
                                                        section_index,
                                                        aligned_offset,
                                                        data_size);
    if (contrib != NULL) {
        contribution_add(section, contrib);
    }

    section->size = new_size;
    return true;
}

/*
 * Section Layout Context Management
 */

section_layout_t* section_layout_new(uint64_t base_address,
                                     platform_format_t target_format) {
    section_layout_t* layout = malloc(sizeof(section_layout_t));
    if (layout == NULL) {
        fprintf(stderr, "Section layout error: Failed to allocate layout context\n");
        return NULL;
    }

    layout->sections = NULL;
    layout->section_count = 0;
    layout->section_capacity = 0;
    layout->base_address = base_address;
    layout->total_size = 0;
    layout->target_format = target_format;
    layout->page_size = get_page_size(target_format);

    /* Use default base address if not specified */
    if (layout->base_address == 0) {
        layout->base_address = get_default_base_address(target_format);
    }

    return layout;
}

void section_layout_free(section_layout_t* layout) {
    if (layout == NULL) {
        return;
    }

    if (layout->sections != NULL) {
        for (int i = 0; i < layout->section_count; i++) {
            merged_section_free(&layout->sections[i]);
        }
        free(layout->sections);
    }

    free(layout);
}

/* Find or create a merged section by name and type */
static merged_section_t* find_or_create_section(section_layout_t* layout,
                                                  const char* name,
                                                  section_type_t type,
                                                  uint32_t flags) {
    /* First, try to find existing section */
    for (int i = 0; i < layout->section_count; i++) {
        if (layout->sections[i].type == type) {
            return &layout->sections[i];
        }
    }

    /* Not found, create new section */
    if (layout->section_count >= layout->section_capacity) {
        int new_capacity = grow_capacity(layout->section_capacity);
        merged_section_t* new_sections = realloc(layout->sections,
                                                  sizeof(merged_section_t) * new_capacity);
        if (new_sections == NULL) {
            fprintf(stderr, "Section layout error: Failed to grow sections array\n");
            return NULL;
        }

        layout->sections = new_sections;
        layout->section_capacity = new_capacity;
    }

    /* Initialize new section */
    merged_section_t* section = &layout->sections[layout->section_count];
    merged_section_t* new_section = merged_section_new(name, type, flags);
    if (new_section == NULL) {
        return NULL;
    }

    memcpy(section, new_section, sizeof(merged_section_t));
    free(new_section);  /* Free the temporary struct, not its contents */

    layout->section_count++;
    return section;
}

/*
 * Section Merging (Phase 3.1)
 */

void section_layout_add_object(section_layout_t* layout,
                               linker_object_t* obj,
                               int object_index) {
    if (layout == NULL || obj == NULL) {
        return;
    }

    /* Process each section in the object file */
    for (int i = 0; i < obj->section_count; i++) {
        linker_section_t* section = &obj->sections[i];

        /* Skip sections with no data (like symbol tables) */
        if (section->type == SECTION_TYPE_UNKNOWN) {
            continue;
        }

        /* Get platform-specific name */
        const char* section_name = get_platform_section_name(layout->target_format,
                                                               section->type);

        /* Find or create merged section */
        merged_section_t* merged = find_or_create_section(layout,
                                                           section_name,
                                                           section->type,
                                                           section->flags);
        if (merged == NULL) {
            fprintf(stderr, "Section layout error: Failed to create merged section\n");
            continue;
        }

        /* Append section data to merged section */
        merged_section_append(merged,
                             section->data,
                             section->size,
                             section->alignment,
                             object_index,
                             i);
    }
}

/*
 * Virtual Address Assignment (Phase 3.2)
 */

/* Compare sections for sorting (text, rodata, data, bss) */
static int compare_sections_for_layout(const void* a, const void* b) {
    const merged_section_t* sec_a = (const merged_section_t*)a;
    const merged_section_t* sec_b = (const merged_section_t*)b;

    /* Order: text (0), rodata (3), data (1), bss (2) */
    static const int order[] = {
        [SECTION_TYPE_UNKNOWN] = 4,
        [SECTION_TYPE_TEXT] = 0,
        [SECTION_TYPE_DATA] = 2,
        [SECTION_TYPE_BSS] = 3,
        [SECTION_TYPE_RODATA] = 1
    };

    int order_a = (sec_a->type < 5) ? order[sec_a->type] : 4;
    int order_b = (sec_b->type < 5) ? order[sec_b->type] : 4;

    return order_a - order_b;
}

void section_layout_compute(section_layout_t* layout) {
    if (layout == NULL || layout->section_count == 0) {
        return;
    }

    /* Sort sections: .text, .rodata, .data, .bss */
    qsort(layout->sections, layout->section_count,
          sizeof(merged_section_t), compare_sections_for_layout);

    /* Start address after headers (approximate) */
    uint64_t addr = layout->base_address + layout->page_size;

    /* Assign virtual addresses to each section */
    for (int i = 0; i < layout->section_count; i++) {
        merged_section_t* section = &layout->sections[i];

        /* Align to page boundary for each section */
        addr = align_to(addr, layout->page_size);

        /* Also respect section's own alignment requirement */
        if (section->alignment > layout->page_size) {
            addr = align_to(addr, section->alignment);
        }

        /* Assign virtual address */
        section->vaddr = addr;

        /* Advance address by section size */
        /* .bss sections take up virtual space but no file space */
        addr += section->size;
    }

    /* Record total memory footprint */
    layout->total_size = addr - layout->base_address;
}

/*
 * Query Functions
 */

merged_section_t* section_layout_find_section(section_layout_t* layout,
                                               const char* name) {
    if (layout == NULL || name == NULL) {
        return NULL;
    }

    for (int i = 0; i < layout->section_count; i++) {
        if (strcmp(layout->sections[i].name, name) == 0) {
            return &layout->sections[i];
        }
    }

    return NULL;
}

merged_section_t* section_layout_find_section_by_type(section_layout_t* layout,
                                                       section_type_t type) {
    if (layout == NULL) {
        return NULL;
    }

    for (int i = 0; i < layout->section_count; i++) {
        if (layout->sections[i].type == type) {
            return &layout->sections[i];
        }
    }

    return NULL;
}

uint64_t section_layout_get_symbol_address(section_layout_t* layout,
                                            linker_symbol_t* symbol) {
    if (layout == NULL || symbol == NULL || !symbol->is_defined) {
        return 0;
    }

    /* Symbol's final address = section's vaddr + symbol's value (offset) */
    /* Note: symbol->section_index refers to the original object file section */
    /* We need to find which merged section contains this symbol's original section */

    /* For now, if symbol has final_address already computed, use that */
    if (symbol->final_address != 0) {
        return symbol->final_address;
    }

    /* Otherwise, this is a simplified version that assumes symbol value */
    /* is already an offset within the correct merged section */
    return symbol->value;
}

uint64_t section_layout_get_address(section_layout_t* layout,
                                    int object_index,
                                    int section_index,
                                    uint64_t offset) {
    if (layout == NULL) {
        return 0;
    }

    /* Find the contribution for this object/section */
    for (int i = 0; i < layout->section_count; i++) {
        merged_section_t* section = &layout->sections[i];
        section_contribution_t* contrib = section->contributions;

        while (contrib != NULL) {
            if (contrib->object_index == object_index &&
                contrib->section_index == section_index) {
                /* Found the contribution */
                return section->vaddr + contrib->offset_in_merged + offset;
            }
            contrib = contrib->next;
        }
    }

    return 0;
}

/*
 * Debug Output
 */

void section_layout_print(section_layout_t* layout) {
    if (layout == NULL) {
        printf("Section layout: NULL\n");
        return;
    }

    printf("Section Layout:\n");
    printf("  Base address: 0x%016llx\n", layout->base_address);
    printf("  Total size: %llu bytes (0x%llx)\n", layout->total_size, layout->total_size);
    printf("  Page size: %zu bytes\n", layout->page_size);
    printf("  Platform: %s\n", platform_format_name(layout->target_format));
    printf("\n");

    printf("Merged Sections:\n");
    for (int i = 0; i < layout->section_count; i++) {
        merged_section_t* section = &layout->sections[i];
        printf("  [%d] %s (%s)\n", i, section->name,
               section_type_name(section->type));
        printf("      Virtual address: 0x%016llx\n", section->vaddr);
        printf("      Size: %zu bytes (0x%zx)\n", section->size, section->size);
        printf("      Alignment: %zu bytes\n", section->alignment);
        printf("      Flags: 0x%08x\n", section->flags);

        /* Print contributions */
        int contrib_count = 0;
        section_contribution_t* contrib = section->contributions;
        while (contrib != NULL) {
            contrib_count++;
            contrib = contrib->next;
        }
        printf("      Contributions: %d\n", contrib_count);

        printf("\n");
    }
}

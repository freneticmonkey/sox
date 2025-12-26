#ifndef SOX_RELOCATION_PROCESSOR_H
#define SOX_RELOCATION_PROCESSOR_H

#include "linker_core.h"
#include "section_layout.h"
#include "symbol_resolver.h"
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/*
 * Relocation Processing
 *
 * This module implements Phase 4 of the custom linker: processing relocations
 * and patching code/data with final addresses. It handles both x86-64 and ARM64
 * relocation types.
 *
 * Key Responsibilities:
 * - Process relocations using resolved symbol addresses
 * - Apply platform-specific relocation formulas
 * - Patch instructions with calculated values
 * - Validate relocation ranges to prevent overflow
 * - Support both absolute and PC-relative relocations
 *
 * Relocation Formula:
 *   S = Symbol address (target)
 *   A = Addend
 *   P = Place (location being patched)
 *
 * Common formulas:
 *   R_X86_64_64:     S + A         (64-bit absolute)
 *   R_X86_64_PC32:   S + A - P     (32-bit PC-relative)
 *   R_AARCH64_ABS64: S + A         (64-bit absolute)
 *   R_AARCH64_CALL26:(S + A - P) >> 2 (26-bit PC-relative)
 *
 * Phase 4.1: Relocation Processor
 */

/* Forward declarations */
typedef struct relocation_processor_t relocation_processor_t;
typedef struct relocation_error_t relocation_error_t;

/* Relocation error types */
typedef enum {
    RELOC_ERROR_NONE = 0,
    RELOC_ERROR_UNDEFINED_SYMBOL,     /* Symbol not defined */
    RELOC_ERROR_RANGE_OVERFLOW,       /* Relocation value out of range */
    RELOC_ERROR_INVALID_TYPE,         /* Unknown relocation type */
    RELOC_ERROR_ALIGNMENT,            /* Alignment violation */
    RELOC_ERROR_INVALID_SECTION,      /* Invalid section index */
    RELOC_ERROR_PATCH_FAILED          /* Instruction patching failed */
} relocation_error_type_t;

/* Relocation error structure */
struct relocation_error_t {
    relocation_error_type_t type;
    char* message;                    /* Detailed error message */
    char* symbol_name;                /* Symbol involved */
    uint64_t offset;                  /* Offset where error occurred */
    int object_index;                 /* Object file index */
    int section_index;                /* Section index */
};

/* Relocation processor state */
struct relocation_processor_t {
    linker_context_t* context;        /* Linker context with objects */
    section_layout_t* layout;         /* Section layout with addresses */
    symbol_resolver_t* symbols;       /* Symbol resolver for lookups */

    /* Error tracking */
    relocation_error_t* errors;
    int error_count;
    int error_capacity;

    /* Statistics */
    int relocations_processed;
    int relocations_skipped;
};

/*
 * Relocation Processor API
 */

/* Create and destroy relocation processor */
relocation_processor_t* relocation_processor_new(linker_context_t* ctx,
                                                   section_layout_t* layout,
                                                   symbol_resolver_t* symbols);
void relocation_processor_free(relocation_processor_t* proc);

/* Process all relocations from all object files */
bool relocation_processor_process_all(relocation_processor_t* proc);

/* Process relocations for a single object file */
bool relocation_processor_process_object(relocation_processor_t* proc,
                                          int object_index);

/* Process a single relocation */
bool relocation_processor_process_one(relocation_processor_t* proc,
                                       linker_relocation_t* reloc,
                                       int object_index);

/* Calculate relocation value based on type */
int64_t relocation_calculate_value(relocation_type_t type,
                                    uint64_t S,  /* Symbol address */
                                    int64_t A,   /* Addend */
                                    uint64_t P); /* Place */

/* Validate relocation value is within range for type */
bool relocation_validate_range(relocation_type_t type, int64_t value);

/* Get error description */
const char* relocation_error_type_name(relocation_error_type_t type);

/* Get errors */
relocation_error_t* relocation_processor_get_errors(relocation_processor_t* proc,
                                                      int* count);
void relocation_processor_clear_errors(relocation_processor_t* proc);

/* Print statistics */
void relocation_processor_print_stats(relocation_processor_t* proc);

#endif

#include "relocation_processor.h"
#include "instruction_patcher.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Initial capacity for error array */
#define INITIAL_ERROR_CAPACITY 16

/* Helper to add error */
static void add_error(relocation_processor_t* proc,
                      relocation_error_type_t type,
                      const char* message,
                      const char* symbol_name,
                      uint64_t offset,
                      int object_index,
                      int section_index) {
    if (!proc) return;

    /* Grow error array if needed */
    if (proc->error_count >= proc->error_capacity) {
        int new_capacity = proc->error_capacity * 2;
        relocation_error_t* new_errors = realloc(proc->errors,
                                                   new_capacity * sizeof(relocation_error_t));
        if (!new_errors) {
            fprintf(stderr, "Error: Failed to grow error array\n");
            return;
        }
        proc->errors = new_errors;
        proc->error_capacity = new_capacity;
    }

    /* Add error */
    relocation_error_t* error = &proc->errors[proc->error_count++];
    error->type = type;
    error->message = message ? strdup(message) : NULL;
    error->symbol_name = symbol_name ? strdup(symbol_name) : NULL;
    error->offset = offset;
    error->object_index = object_index;
    error->section_index = section_index;
}

/* Create relocation processor */
relocation_processor_t* relocation_processor_new(linker_context_t* ctx,
                                                   section_layout_t* layout,
                                                   symbol_resolver_t* symbols) {
    if (!ctx || !layout || !symbols) return NULL;

    relocation_processor_t* proc = calloc(1, sizeof(relocation_processor_t));
    if (!proc) return NULL;

    proc->context = ctx;
    proc->layout = layout;
    proc->symbols = symbols;

    /* Initialize error tracking */
    proc->error_capacity = INITIAL_ERROR_CAPACITY;
    proc->errors = malloc(proc->error_capacity * sizeof(relocation_error_t));
    if (!proc->errors) {
        free(proc);
        return NULL;
    }
    proc->error_count = 0;

    /* Initialize statistics */
    proc->relocations_processed = 0;
    proc->relocations_skipped = 0;

    return proc;
}

/* Free relocation processor */
void relocation_processor_free(relocation_processor_t* proc) {
    if (!proc) return;

    /* Free errors */
    for (int i = 0; i < proc->error_count; i++) {
        free(proc->errors[i].message);
        free(proc->errors[i].symbol_name);
    }
    free(proc->errors);

    free(proc);
}

/* Calculate relocation value based on type */
int64_t relocation_calculate_value(relocation_type_t type,
                                    uint64_t S,  /* Symbol address */
                                    int64_t A,   /* Addend */
                                    uint64_t P)  /* Place */ {
    switch (type) {
        case RELOC_X64_64:
        case RELOC_ARM64_ABS64:
            /* S + A (64-bit absolute) */
            return (int64_t)(S + (uint64_t)A);

        case RELOC_X64_PC32:
        case RELOC_X64_PLT32:
        case RELOC_X64_GOTPCREL:
        case RELOC_ARM64_CALL26:
        case RELOC_ARM64_JUMP26:
            /* S + A - P (PC-relative) */
            return (int64_t)((S + (uint64_t)A) - P);

        case RELOC_ARM64_ADR_PREL_PG_HI21:
            /* For ADRP, return target address (patching function handles page calc) */
            return (int64_t)(S + (uint64_t)A);

        case RELOC_ARM64_ADD_ABS_LO12_NC:
            /* S + A (absolute, low 12 bits extracted during patching) */
            return (int64_t)(S + (uint64_t)A);

        case RELOC_NONE:
            return 0;

        default:
            fprintf(stderr, "Warning: Unknown relocation type for calculation: %d\n", type);
            return 0;
    }
}

/* Validate relocation range */
bool relocation_validate_range(relocation_type_t type, int64_t value) {
    return validate_relocation_range(value, type);
}

/* Process a single relocation */
bool relocation_processor_process_one(relocation_processor_t* proc,
                                       linker_relocation_t* reloc,
                                       int object_index) {
    if (!proc || !reloc) return false;

    /* Skip RELOC_NONE */
    if (reloc->type == RELOC_NONE) {
        proc->relocations_skipped++;
        return true;
    }

    /* Get the object */
    if (object_index < 0 || object_index >= proc->context->object_count) {
        add_error(proc, RELOC_ERROR_INVALID_SECTION,
                  "Invalid object index",
                  NULL, reloc->offset, object_index, reloc->section_index);
        return false;
    }

    linker_object_t* obj = proc->context->objects[object_index];

    /* Get the symbol */
    if (reloc->symbol_index < 0 || reloc->symbol_index >= obj->symbol_count) {
        add_error(proc, RELOC_ERROR_UNDEFINED_SYMBOL,
                  "Invalid symbol index in relocation",
                  NULL, reloc->offset, object_index, reloc->section_index);
        return false;
    }

    linker_symbol_t* symbol = &obj->symbols[reloc->symbol_index];

    /* Look up symbol in global table */
    linker_symbol_t* target_symbol = symbol_resolver_lookup(proc->symbols, symbol->name);
    if (!target_symbol || !target_symbol->is_defined) {
        char msg[256];
        snprintf(msg, sizeof(msg), "Undefined symbol in relocation: %s", symbol->name);
        add_error(proc, RELOC_ERROR_UNDEFINED_SYMBOL,
                  msg, symbol->name, reloc->offset, object_index, reloc->section_index);
        return false;
    }

    /* Get the section being relocated */
    if (reloc->section_index < 0 || reloc->section_index >= obj->section_count) {
        add_error(proc, RELOC_ERROR_INVALID_SECTION,
                  "Invalid section index in relocation",
                  symbol->name, reloc->offset, object_index, reloc->section_index);
        return false;
    }

    linker_section_t* section = &obj->sections[reloc->section_index];

    /* Find the merged section */
    merged_section_t* merged = section_layout_find_section_by_type(proc->layout, section->type);
    if (!merged) {
        add_error(proc, RELOC_ERROR_INVALID_SECTION,
                  "Cannot find merged section for relocation",
                  symbol->name, reloc->offset, object_index, reloc->section_index);
        return false;
    }

    /* Calculate addresses:
     * S = target_symbol.final_address
     * A = relocation.addend
     * P = relocation.offset + section_base_in_merged_section */

    uint64_t S = target_symbol->final_address;
    int64_t A = reloc->addend;

    /* Get the offset of this section within the merged section */
    uint64_t section_base = section_layout_get_address(proc->layout,
                                                         object_index,
                                                         reloc->section_index,
                                                         0);
    uint64_t P = section_base + reloc->offset;

    /* Calculate relocation value */
    int64_t value = relocation_calculate_value(reloc->type, S, A, P);

    /* Validate range */
    if (!relocation_validate_range(reloc->type, value)) {
        char msg[256];
        snprintf(msg, sizeof(msg), "Relocation value out of range for type %s: %lld",
                 relocation_type_name(reloc->type), (long long)value);
        add_error(proc, RELOC_ERROR_RANGE_OVERFLOW,
                  msg, symbol->name, reloc->offset, object_index, reloc->section_index);
        return false;
    }

    /* Find the offset within the merged section */
    uint64_t offset_in_merged = section_layout_get_address(proc->layout,
                                                             object_index,
                                                             reloc->section_index,
                                                             reloc->offset) - merged->vaddr;

    /* Validate offset is within merged section */
    if (offset_in_merged >= merged->size) {
        char msg[256];
        snprintf(msg, sizeof(msg), "Relocation offset out of bounds: %llu >= %zu",
                 (unsigned long long)offset_in_merged, merged->size);
        add_error(proc, RELOC_ERROR_INVALID_SECTION,
                  msg, symbol->name, reloc->offset, object_index, reloc->section_index);
        return false;
    }

    /* Patch the instruction */
    bool success = patch_instruction(merged->data,
                                      merged->size,
                                      offset_in_merged,
                                      value,
                                      reloc->type,
                                      P);

    if (!success) {
        char msg[256];
        snprintf(msg, sizeof(msg), "Failed to patch instruction for relocation type %s",
                 relocation_type_name(reloc->type));
        add_error(proc, RELOC_ERROR_PATCH_FAILED,
                  msg, symbol->name, reloc->offset, object_index, reloc->section_index);
        return false;
    }

    proc->relocations_processed++;
    return true;
}

/* Process relocations for a single object file */
bool relocation_processor_process_object(relocation_processor_t* proc,
                                          int object_index) {
    if (!proc) return false;

    if (object_index < 0 || object_index >= proc->context->object_count) {
        fprintf(stderr, "Error: Invalid object index: %d\n", object_index);
        return false;
    }

    linker_object_t* obj = proc->context->objects[object_index];
    bool all_success = true;

    /* Process all relocations in this object */
    for (int i = 0; i < obj->relocation_count; i++) {
        linker_relocation_t* reloc = &obj->relocations[i];
        if (!relocation_processor_process_one(proc, reloc, object_index)) {
            all_success = false;
            /* Continue processing other relocations to collect all errors */
        }
    }

    return all_success;
}

/* Process all relocations from all object files */
bool relocation_processor_process_all(relocation_processor_t* proc) {
    if (!proc) return false;

    bool all_success = true;

    /* Process each object file */
    for (int i = 0; i < proc->context->object_count; i++) {
        if (!relocation_processor_process_object(proc, i)) {
            all_success = false;
            /* Continue processing to collect all errors */
        }
    }

    return all_success;
}

/* Get error description */
const char* relocation_error_type_name(relocation_error_type_t type) {
    switch (type) {
        case RELOC_ERROR_NONE: return "None";
        case RELOC_ERROR_UNDEFINED_SYMBOL: return "Undefined Symbol";
        case RELOC_ERROR_RANGE_OVERFLOW: return "Range Overflow";
        case RELOC_ERROR_INVALID_TYPE: return "Invalid Type";
        case RELOC_ERROR_ALIGNMENT: return "Alignment Error";
        case RELOC_ERROR_INVALID_SECTION: return "Invalid Section";
        case RELOC_ERROR_PATCH_FAILED: return "Patch Failed";
        default: return "Unknown";
    }
}

/* Get errors */
relocation_error_t* relocation_processor_get_errors(relocation_processor_t* proc,
                                                      int* count) {
    if (!proc || !count) return NULL;
    *count = proc->error_count;
    return proc->errors;
}

/* Clear errors */
void relocation_processor_clear_errors(relocation_processor_t* proc) {
    if (!proc) return;

    for (int i = 0; i < proc->error_count; i++) {
        free(proc->errors[i].message);
        free(proc->errors[i].symbol_name);
    }
    proc->error_count = 0;
}

/* Print statistics */
void relocation_processor_print_stats(relocation_processor_t* proc) {
    if (!proc) return;

    printf("Relocation Statistics:\n");
    printf("  Processed: %d\n", proc->relocations_processed);
    printf("  Skipped:   %d\n", proc->relocations_skipped);
    printf("  Errors:    %d\n", proc->error_count);

    if (proc->error_count > 0) {
        printf("\nErrors:\n");
        for (int i = 0; i < proc->error_count; i++) {
            relocation_error_t* err = &proc->errors[i];
            printf("  [%d] %s: %s\n",
                   i + 1,
                   relocation_error_type_name(err->type),
                   err->message ? err->message : "(no message)");
            if (err->symbol_name) {
                printf("      Symbol: %s\n", err->symbol_name);
            }
            printf("      Object: %d, Section: %d, Offset: 0x%llx\n",
                   err->object_index,
                   err->section_index,
                   (unsigned long long)err->offset);
        }
    }
}

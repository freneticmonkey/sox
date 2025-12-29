#include "linker_core.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/*
 * Custom Linker Core Implementation
 *
 * This module implements the core data structures and basic API for the Sox
 * custom linker. It provides object file representation, section/symbol/relocation
 * management, and the global linker context.
 *
 * Phase 1.1: Core data structures and basic API
 */

/* Default capacity for dynamic arrays */
#define DEFAULT_CAPACITY 8

/* Helper: grow capacity using standard growth pattern */
static int grow_capacity(int capacity) {
    return capacity < DEFAULT_CAPACITY ? DEFAULT_CAPACITY : capacity * 2;
}

/* Helper: duplicate string (caller must free) */
static char* duplicate_string(const char* str) {
    if (str == NULL) {
        return NULL;
    }
    size_t len = strlen(str);
    char* copy = malloc(len + 1);
    if (copy == NULL) {
        fprintf(stderr, "Linker error: Failed to allocate memory for string\n");
        return NULL;
    }
    memcpy(copy, str, len + 1);
    return copy;
}

/*
 * Linker Context Management
 */

linker_context_t* linker_context_new(void) {
    linker_context_t* context = malloc(sizeof(linker_context_t));
    if (context == NULL) {
        fprintf(stderr, "Linker error: Failed to allocate linker context\n");
        return NULL;
    }

    /* Initialize all fields to zero */
    memset(context, 0, sizeof(linker_context_t));

    /* Allocate initial object array */
    context->object_capacity = DEFAULT_CAPACITY;
    context->objects = malloc(sizeof(linker_object_t*) * context->object_capacity);
    if (context->objects == NULL) {
        fprintf(stderr, "Linker error: Failed to allocate object array\n");
        free(context);
        return NULL;
    }

    return context;
}

void linker_context_free(linker_context_t* context) {
    if (context == NULL) {
        return;
    }

    /* Free all object files */
    for (int i = 0; i < context->object_count; i++) {
        linker_object_free(context->objects[i]);
    }
    free(context->objects);

    /* Free global symbols (if allocated) */
    if (context->global_symbols != NULL) {
        for (int i = 0; i < context->global_symbol_count; i++) {
            linker_symbol_free(&context->global_symbols[i]);
        }
        free(context->global_symbols);
    }

    /* Free merged sections (if allocated) */
    if (context->merged_sections != NULL) {
        for (int i = 0; i < context->merged_section_count; i++) {
            linker_section_free(&context->merged_sections[i]);
        }
        free(context->merged_sections);
    }

    /* Free executable data (if allocated) */
    if (context->executable_data != NULL) {
        free(context->executable_data);
    }

    /* Free the context itself */
    free(context);
}

bool linker_context_add_object(linker_context_t* context, linker_object_t* object) {
    if (context == NULL || object == NULL) {
        fprintf(stderr, "Linker error: NULL context or object\n");
        return false;
    }

    /* Grow array if needed */
    if (context->object_count >= context->object_capacity) {
        int new_capacity = grow_capacity(context->object_capacity);
        linker_object_t** new_objects = realloc(context->objects,
            sizeof(linker_object_t*) * new_capacity);
        if (new_objects == NULL) {
            fprintf(stderr, "Linker error: Failed to grow object array\n");
            return false;
        }
        context->objects = new_objects;
        context->object_capacity = new_capacity;
    }

    /* Add object to array */
    context->objects[context->object_count++] = object;

    return true;
}

/*
 * Object File Management
 */

linker_object_t* linker_object_new(const char* filename, platform_format_t format) {
    linker_object_t* object = malloc(sizeof(linker_object_t));
    if (object == NULL) {
        fprintf(stderr, "Linker error: Failed to allocate object file\n");
        return NULL;
    }

    /* Initialize all fields to zero */
    memset(object, 0, sizeof(linker_object_t));

    /* Set filename and format */
    object->filename = duplicate_string(filename);
    object->format = format;

    /* Allocate initial arrays */
    object->section_capacity = DEFAULT_CAPACITY;
    object->sections = malloc(sizeof(linker_section_t) * object->section_capacity);
    if (object->sections == NULL) {
        fprintf(stderr, "Linker error: Failed to allocate section array\n");
        free(object->filename);
        free(object);
        return NULL;
    }

    object->symbol_capacity = DEFAULT_CAPACITY;
    object->symbols = malloc(sizeof(linker_symbol_t) * object->symbol_capacity);
    if (object->symbols == NULL) {
        fprintf(stderr, "Linker error: Failed to allocate symbol array\n");
        free(object->sections);
        free(object->filename);
        free(object);
        return NULL;
    }

    object->relocation_capacity = DEFAULT_CAPACITY;
    object->relocations = malloc(sizeof(linker_relocation_t) * object->relocation_capacity);
    if (object->relocations == NULL) {
        fprintf(stderr, "Linker error: Failed to allocate relocation array\n");
        free(object->symbols);
        free(object->sections);
        free(object->filename);
        free(object);
        return NULL;
    }

    return object;
}

void linker_object_free(linker_object_t* object) {
    if (object == NULL) {
        return;
    }

    /* Free filename */
    free(object->filename);

    /* Free all sections */
    for (int i = 0; i < object->section_count; i++) {
        linker_section_free(&object->sections[i]);
    }
    free(object->sections);

    /* Free all symbols */
    for (int i = 0; i < object->symbol_count; i++) {
        linker_symbol_free(&object->symbols[i]);
    }
    free(object->symbols);

    /* Free relocations */
    free(object->relocations);

    /* Free raw data (if allocated) */
    free(object->raw_data);

    /* Free the object itself */
    free(object);
}

/*
 * Section Management
 */

linker_section_t* linker_object_add_section(linker_object_t* object) {
    if (object == NULL) {
        fprintf(stderr, "Linker error: NULL object in add_section\n");
        return NULL;
    }

    /* Grow array if needed */
    if (object->section_count >= object->section_capacity) {
        int new_capacity = grow_capacity(object->section_capacity);
        linker_section_t* new_sections = realloc(object->sections,
            sizeof(linker_section_t) * new_capacity);
        if (new_sections == NULL) {
            fprintf(stderr, "Linker error: Failed to grow section array\n");
            return NULL;
        }
        object->sections = new_sections;
        object->section_capacity = new_capacity;
    }

    /* Initialize new section */
    linker_section_t* section = &object->sections[object->section_count++];
    linker_section_init(section);

    return section;
}

void linker_section_init(linker_section_t* section) {
    if (section == NULL) {
        return;
    }

    memset(section, 0, sizeof(linker_section_t));
    section->object_index = -1;
}

void linker_section_free(linker_section_t* section) {
    if (section == NULL) {
        return;
    }

    free(section->name);
    free(section->data);

    /* Zero out structure for safety */
    memset(section, 0, sizeof(linker_section_t));
}

/*
 * Symbol Management
 */

linker_symbol_t* linker_object_add_symbol(linker_object_t* object) {
    if (object == NULL) {
        fprintf(stderr, "Linker error: NULL object in add_symbol\n");
        return NULL;
    }

    /* Grow array if needed */
    if (object->symbol_count >= object->symbol_capacity) {
        int new_capacity = grow_capacity(object->symbol_capacity);
        linker_symbol_t* new_symbols = realloc(object->symbols,
            sizeof(linker_symbol_t) * new_capacity);
        if (new_symbols == NULL) {
            fprintf(stderr, "Linker error: Failed to grow symbol array\n");
            return NULL;
        }
        object->symbols = new_symbols;
        object->symbol_capacity = new_capacity;
    }

    /* Initialize new symbol */
    linker_symbol_t* symbol = &object->symbols[object->symbol_count++];
    linker_symbol_init(symbol);

    return symbol;
}

void linker_symbol_init(linker_symbol_t* symbol) {
    if (symbol == NULL) {
        return;
    }

    memset(symbol, 0, sizeof(linker_symbol_t));
    symbol->section_index = -1;
    symbol->defining_object = -1;
}

void linker_symbol_free(linker_symbol_t* symbol) {
    if (symbol == NULL) {
        return;
    }

    free(symbol->name);

    /* Zero out structure for safety */
    memset(symbol, 0, sizeof(linker_symbol_t));
}

/*
 * Relocation Management
 */

linker_relocation_t* linker_object_add_relocation(linker_object_t* object) {
    if (object == NULL) {
        fprintf(stderr, "Linker error: NULL object in add_relocation\n");
        return NULL;
    }

    /* Grow array if needed */
    if (object->relocation_count >= object->relocation_capacity) {
        int new_capacity = grow_capacity(object->relocation_capacity);
        linker_relocation_t* new_relocations = realloc(object->relocations,
            sizeof(linker_relocation_t) * new_capacity);
        if (new_relocations == NULL) {
            fprintf(stderr, "Linker error: Failed to grow relocation array\n");
            return NULL;
        }
        object->relocations = new_relocations;
        object->relocation_capacity = new_capacity;
    }

    /* Initialize new relocation */
    linker_relocation_t* relocation = &object->relocations[object->relocation_count++];
    linker_relocation_init(relocation);

    return relocation;
}

void linker_relocation_init(linker_relocation_t* relocation) {
    if (relocation == NULL) {
        return;
    }

    memset(relocation, 0, sizeof(linker_relocation_t));
    relocation->section_index = -1;
    relocation->symbol_index = -1;
    relocation->object_index = -1;
}

/*
 * Utility Functions
 */

const char* platform_format_name(platform_format_t format) {
    switch (format) {
        case PLATFORM_FORMAT_ELF:      return "ELF";
        case PLATFORM_FORMAT_MACH_O:   return "Mach-O";
        case PLATFORM_FORMAT_PE_COFF:  return "PE/COFF";
        case PLATFORM_FORMAT_UNKNOWN:  return "Unknown";
        default:                       return "Invalid";
    }
}

const char* section_type_name(section_type_t type) {
    switch (type) {
        case SECTION_TYPE_TEXT:    return "TEXT";
        case SECTION_TYPE_DATA:    return "DATA";
        case SECTION_TYPE_BSS:     return "BSS";
        case SECTION_TYPE_RODATA:  return "RODATA";
        case SECTION_TYPE_UNKNOWN: return "Unknown";
        default:                   return "Invalid";
    }
}

const char* symbol_type_name(symbol_type_t type) {
    switch (type) {
        case SYMBOL_TYPE_NOTYPE:  return "NOTYPE";
        case SYMBOL_TYPE_FUNC:    return "FUNC";
        case SYMBOL_TYPE_OBJECT:  return "OBJECT";
        case SYMBOL_TYPE_SECTION: return "SECTION";
        default:                  return "Invalid";
    }
}

const char* symbol_binding_name(symbol_binding_t binding) {
    switch (binding) {
        case SYMBOL_BINDING_LOCAL:  return "LOCAL";
        case SYMBOL_BINDING_GLOBAL: return "GLOBAL";
        case SYMBOL_BINDING_WEAK:   return "WEAK";
        default:                    return "Invalid";
    }
}

const char* relocation_type_name(relocation_type_t type) {
    switch (type) {
        case RELOC_NONE:                       return "NONE";
        case RELOC_X64_64:                     return "X64_64";
        case RELOC_X64_PC32:                   return "X64_PC32";
        case RELOC_X64_PLT32:                  return "X64_PLT32";
        case RELOC_X64_GOTPCREL:               return "X64_GOTPCREL";
        case RELOC_ARM64_ABS64:                return "ARM64_ABS64";
        case RELOC_ARM64_CALL26:               return "ARM64_CALL26";
        case RELOC_ARM64_JUMP26:               return "ARM64_JUMP26";
        case RELOC_ARM64_ADR_PREL_PG_HI21:     return "ARM64_ADR_PREL_PG_HI21";
        case RELOC_ARM64_ADD_ABS_LO12_NC:      return "ARM64_ADD_ABS_LO12_NC";
        case RELOC_RELATIVE:                   return "RELATIVE";
        default:                               return "Invalid";
    }
}

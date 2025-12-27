#include "symbol_resolver.h"
#include "section_layout.h"  /* For symbol address computation */
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/*
 * Symbol Resolution Engine Implementation
 *
 * This module implements a two-phase symbol resolution algorithm:
 *
 * Phase 1: Collect all defined symbols into a global hash table
 *   - Global symbols override weak symbols
 *   - Two global symbols with the same name is an error
 *
 * Phase 2: Resolve all undefined symbols
 *   - Look up each undefined symbol in the global table
 *   - Mark runtime symbols (sox_runtime_*) as external
 *   - Report errors for unresolved non-runtime symbols
 *
 * The hash table provides O(1) average-case symbol lookup.
 */

/* Constants */
#define INITIAL_TABLE_SIZE 256
#define DEFAULT_ERROR_CAPACITY 16
#define MAX_LOAD_FACTOR 0.75

/* Runtime library symbols (Phase 2.2) */
static const char* runtime_symbol_names[] = {
    "sox_runtime_add",
    "sox_runtime_subtract",
    "sox_runtime_multiply",
    "sox_runtime_divide",
    "sox_runtime_negate",
    "sox_runtime_less",
    "sox_runtime_greater",
    "sox_runtime_equal",
    "sox_runtime_not_equal",
    "sox_runtime_print",
    "sox_runtime_println",
    "sox_runtime_alloc",
    "sox_runtime_free",
    "sox_runtime_string_concat",
    "sox_runtime_string_length",
    "sox_runtime_bool_to_string",
    "sox_runtime_number_to_string",
    NULL  /* Sentinel */
};

/*
 * Helper Functions
 */

/* Duplicate a string (caller must free) */
static char* duplicate_string(const char* str) {
    if (str == NULL) {
        return NULL;
    }
    size_t len = strlen(str);
    char* copy = malloc(len + 1);
    if (copy == NULL) {
        fprintf(stderr, "Symbol resolver error: Failed to allocate memory for string\n");
        return NULL;
    }
    memcpy(copy, str, len + 1);
    return copy;
}

/* Hash function for symbol names (FNV-1a) */
uint32_t symbol_hash(const char* name) {
    if (name == NULL) {
        return 0;
    }

    uint32_t hash = 2166136261u;
    for (const char* c = name; *c != '\0'; c++) {
        hash ^= (uint32_t)(*c);
        hash *= 16777619u;
    }
    return hash;
}

/* Check if a symbol is from the system C library */
static bool is_system_library_symbol(const char* name) {
    if (name == NULL) {
        return false;
    }

    /* Common C library functions that should be resolved by dynamic linker */
    static const char* libc_symbols[] = {
        /* Memory management */
        "malloc", "calloc", "realloc", "free", "memcpy", "memmove", "memset",
        "memcmp", "__memcpy_chk", "__memmove_chk", "__memset_chk",

        /* String functions */
        "strlen", "strcmp", "strncmp", "strcpy", "strncpy", "strcat", "strncat",
        "strchr", "strrchr", "strstr", "strdup", "__strcpy_chk", "__strcat_chk",

        /* I/O functions */
        "printf", "fprintf", "sprintf", "snprintf", "vprintf", "vfprintf",
        "puts", "fputs", "fputc", "putchar", "fgets", "fread", "fwrite",
        "fopen", "fclose", "fflush", "fseek", "ftell", "rewind",
        "__sprintf_chk", "__snprintf_chk", "__vsnprintf_chk",

        /* File descriptors */
        "stdin", "stdout", "stderr", "__stdinp", "__stdoutp", "__stderrp",

        /* Other standard functions */
        "exit", "abort", "atexit", "qsort", "atoi", "atof", "strtol", "strtod",
        "isdigit", "isalpha", "isspace", "tolower", "toupper",

        /* Math functions */
        "pow", "sqrt", "sin", "cos", "tan", "exp", "log", "floor", "ceil",

        /* Thread-local storage (macOS) */
        "_tlv_bootstrap",

        NULL  /* Sentinel */
    };

    for (int i = 0; libc_symbols[i] != NULL; i++) {
        if (strcmp(name, libc_symbols[i]) == 0) {
            return true;
        }
    }
    return false;
}

/* Check if a symbol is from the runtime library or system library */
bool is_runtime_symbol(const char* name) {
    if (name == NULL) {
        return false;
    }

    /* Check Sox runtime symbols */
    for (int i = 0; runtime_symbol_names[i] != NULL; i++) {
        if (strcmp(name, runtime_symbol_names[i]) == 0) {
            return true;
        }
    }

    /* Check system library symbols */
    return is_system_library_symbol(name);
}

/*
 * Error Management
 */

/* Add an error to the resolver */
static bool add_error(symbol_resolver_t* resolver,
                      linker_error_type_t type,
                      const char* message,
                      const char* symbol_name,
                      const char* object_file) {
    if (resolver == NULL) {
        return false;
    }

    /* Grow error array if needed */
    if (resolver->error_count >= resolver->error_capacity) {
        int new_capacity = resolver->error_capacity == 0 ?
            DEFAULT_ERROR_CAPACITY :
            resolver->error_capacity * 2;
        linker_error_t* new_errors = realloc(resolver->errors,
                                              new_capacity * sizeof(linker_error_t));
        if (new_errors == NULL) {
            fprintf(stderr, "Symbol resolver error: Failed to allocate error array\n");
            return false;
        }
        resolver->errors = new_errors;
        resolver->error_capacity = new_capacity;
    }

    /* Create error */
    linker_error_t* error = &resolver->errors[resolver->error_count++];
    error->type = type;
    error->message = duplicate_string(message);
    error->symbol_name = duplicate_string(symbol_name);
    error->object_file = duplicate_string(object_file);
    error->line_number = -1;  /* Not used yet */

    return true;
}

/* Free an error structure */
static void free_error(linker_error_t* error) {
    if (error == NULL) {
        return;
    }
    free(error->message);
    free(error->symbol_name);
    free(error->object_file);
}

/* Get error type name */
const char* linker_error_type_name(linker_error_type_t type) {
    switch (type) {
        case LINKER_ERROR_NONE:
            return "None";
        case LINKER_ERROR_UNDEFINED_SYMBOL:
            return "Undefined Symbol";
        case LINKER_ERROR_DUPLICATE_DEFINITION:
            return "Duplicate Definition";
        case LINKER_ERROR_WEAK_SYMBOL_CONFLICT:
            return "Weak Symbol Conflict";
        case LINKER_ERROR_TYPE_MISMATCH:
            return "Type Mismatch";
        case LINKER_ERROR_ALLOCATION_FAILED:
            return "Allocation Failed";
        default:
            return "Unknown Error";
    }
}

/* Get errors from resolver */
linker_error_t* symbol_resolver_get_errors(symbol_resolver_t* resolver, int* count) {
    if (resolver == NULL || count == NULL) {
        return NULL;
    }
    *count = resolver->error_count;
    return resolver->errors;
}

/* Clear all errors */
void symbol_resolver_clear_errors(symbol_resolver_t* resolver) {
    if (resolver == NULL) {
        return;
    }
    for (int i = 0; i < resolver->error_count; i++) {
        free_error(&resolver->errors[i]);
    }
    resolver->error_count = 0;
}

/*
 * Hash Table Implementation
 */

/* Find an entry in the hash table */
symbol_table_entry_t* symbol_table_find(symbol_resolver_t* resolver,
                                        const char* name) {
    if (resolver == NULL || name == NULL || resolver->table == NULL) {
        return NULL;
    }

    uint32_t hash = symbol_hash(name);
    size_t index = hash % resolver->table_size;

    /* Linear probing with chaining */
    symbol_table_entry_t* entry = resolver->table[index];
    while (entry != NULL) {
        if (strcmp(entry->key, name) == 0) {
            return entry;
        }
        entry = entry->next;
    }

    return NULL;
}

/* Resize hash table when load factor exceeds threshold */
static bool symbol_table_resize(symbol_resolver_t* resolver) {
    if (resolver == NULL) {
        return false;
    }

    size_t new_size = resolver->table_size * 2;
    symbol_table_entry_t** new_table = calloc(new_size, sizeof(symbol_table_entry_t*));
    if (new_table == NULL) {
        fprintf(stderr, "Symbol resolver error: Failed to resize hash table\n");
        return false;
    }

    /* Rehash all entries */
    for (size_t i = 0; i < resolver->table_size; i++) {
        symbol_table_entry_t* entry = resolver->table[i];
        while (entry != NULL) {
            symbol_table_entry_t* next = entry->next;

            /* Reinsert into new table */
            uint32_t hash = symbol_hash(entry->key);
            size_t new_index = hash % new_size;

            entry->next = new_table[new_index];
            new_table[new_index] = entry;

            entry = next;
        }
    }

    /* Replace old table */
    free(resolver->table);
    resolver->table = new_table;
    resolver->table_size = new_size;

    return true;
}

/* Insert a symbol into the hash table */
bool symbol_table_insert(symbol_resolver_t* resolver,
                         const char* name,
                         linker_symbol_t* symbol,
                         int object_index) {
    if (resolver == NULL || name == NULL || symbol == NULL) {
        return false;
    }

    /* Check load factor and resize if needed */
    double load_factor = (double)resolver->table_count / (double)resolver->table_size;
    if (load_factor > MAX_LOAD_FACTOR) {
        if (!symbol_table_resize(resolver)) {
            return false;
        }
    }

    /* Create new entry */
    symbol_table_entry_t* entry = malloc(sizeof(symbol_table_entry_t));
    if (entry == NULL) {
        fprintf(stderr, "Symbol resolver error: Failed to allocate hash table entry\n");
        return false;
    }

    entry->key = duplicate_string(name);
    if (entry->key == NULL) {
        free(entry);
        return false;
    }
    entry->symbol = symbol;
    entry->object_index = object_index;

    /* Insert at head of chain */
    uint32_t hash = symbol_hash(name);
    size_t index = hash % resolver->table_size;

    entry->next = resolver->table[index];
    resolver->table[index] = entry;

    resolver->table_count++;

    return true;
}

/*
 * Symbol Resolver API
 */

/* Create new symbol resolver */
symbol_resolver_t* symbol_resolver_new(void) {
    symbol_resolver_t* resolver = malloc(sizeof(symbol_resolver_t));
    if (resolver == NULL) {
        fprintf(stderr, "Symbol resolver error: Failed to allocate resolver\n");
        return NULL;
    }

    /* Initialize hash table */
    resolver->table_size = INITIAL_TABLE_SIZE;
    resolver->table = calloc(resolver->table_size, sizeof(symbol_table_entry_t*));
    if (resolver->table == NULL) {
        fprintf(stderr, "Symbol resolver error: Failed to allocate hash table\n");
        free(resolver);
        return NULL;
    }
    resolver->table_count = 0;

    /* Initialize object tracking */
    resolver->objects = NULL;
    resolver->object_count = 0;

    /* Initialize error tracking */
    resolver->errors = NULL;
    resolver->error_count = 0;
    resolver->error_capacity = 0;

    /* Initialize statistics */
    resolver->defined_count = 0;
    resolver->undefined_count = 0;
    resolver->runtime_count = 0;

    return resolver;
}

/* Free symbol resolver */
void symbol_resolver_free(symbol_resolver_t* resolver) {
    if (resolver == NULL) {
        return;
    }

    /* Free hash table entries */
    if (resolver->table != NULL) {
        for (size_t i = 0; i < resolver->table_size; i++) {
            symbol_table_entry_t* entry = resolver->table[i];
            while (entry != NULL) {
                symbol_table_entry_t* next = entry->next;
                free(entry->key);
                free(entry);
                entry = next;
            }
        }
        free(resolver->table);
    }

    /* Free errors */
    if (resolver->errors != NULL) {
        for (int i = 0; i < resolver->error_count; i++) {
            free_error(&resolver->errors[i]);
        }
        free(resolver->errors);
    }

    /* Free objects array (but not the objects themselves - we don't own them) */
    free(resolver->objects);

    free(resolver);
}

/* Add an object file to the resolver */
void symbol_resolver_add_object(symbol_resolver_t* resolver,
                                 linker_object_t* obj,
                                 int obj_index) {
    if (resolver == NULL || obj == NULL) {
        return;
    }

    /* Grow objects array if needed */
    if (resolver->object_count >= obj_index + 1) {
        /* Array is already large enough */
    } else {
        /* Need to grow array */
        int new_count = obj_index + 1;
        linker_object_t** new_objects = realloc(resolver->objects,
                                                 new_count * sizeof(linker_object_t*));
        if (new_objects == NULL) {
            fprintf(stderr, "Symbol resolver error: Failed to grow objects array\n");
            return;
        }

        /* Initialize new slots to NULL */
        for (int i = resolver->object_count; i < new_count; i++) {
            new_objects[i] = NULL;
        }

        resolver->objects = new_objects;
        resolver->object_count = new_count;
    }

    /* Store object at specified index */
    resolver->objects[obj_index] = obj;
}

/*
 * Two-Phase Symbol Resolution Algorithm
 */

/* Phase 1: Collect all defined symbols into global table */
static bool resolve_phase1_collect_symbols(symbol_resolver_t* resolver,
                                            linker_object_t** objects,
                                            int object_count) {
    if (resolver == NULL || objects == NULL) {
        return false;
    }

    bool success = true;

    /* Iterate over all objects and their symbols */
    for (int obj_idx = 0; obj_idx < object_count; obj_idx++) {
        linker_object_t* obj = objects[obj_idx];
        if (obj == NULL) {
            continue;
        }

        for (int sym_idx = 0; sym_idx < obj->symbol_count; sym_idx++) {
            linker_symbol_t* symbol = &obj->symbols[sym_idx];

            /* Only process global and weak symbols that are defined */
            if (!symbol->is_defined) {
                continue;
            }

            if (symbol->binding != SYMBOL_BINDING_GLOBAL &&
                symbol->binding != SYMBOL_BINDING_WEAK) {
                continue;
            }

            /* Check if symbol already exists in global table */
            symbol_table_entry_t* existing = symbol_table_find(resolver, symbol->name);

            if (existing != NULL) {
                /* Handle duplicate symbol */
                linker_symbol_t* existing_symbol = existing->symbol;

                /* Two global symbols with same name is an error */
                if (existing_symbol->binding == SYMBOL_BINDING_GLOBAL &&
                    symbol->binding == SYMBOL_BINDING_GLOBAL) {
                    char message[512];
                    snprintf(message, sizeof(message),
                            "Duplicate definition of global symbol '%s' in '%s' "
                            "(first defined in object %d)",
                            symbol->name,
                            obj->filename ? obj->filename : "<unknown>",
                            existing->object_index);

                    add_error(resolver,
                             LINKER_ERROR_DUPLICATE_DEFINITION,
                             message,
                             symbol->name,
                             obj->filename);
                    success = false;
                    continue;
                }

                /* Global symbol overrides weak symbol */
                if (symbol->binding == SYMBOL_BINDING_GLOBAL &&
                    existing_symbol->binding == SYMBOL_BINDING_WEAK) {
                    /* Replace weak symbol with global symbol */
                    existing->symbol = symbol;
                    existing->object_index = obj_idx;
                    /* Set defining_object on the new symbol */
                    symbol->defining_object = obj_idx;
                }

                /* Weak symbol doesn't override anything */
                /* (Existing symbol remains, whether global or weak) */

            } else {
                /* New symbol - insert into global table */
                if (!symbol_table_insert(resolver, symbol->name, symbol, obj_idx)) {
                    char message[512];
                    snprintf(message, sizeof(message),
                            "Failed to insert symbol '%s' from '%s' into global table",
                            symbol->name,
                            obj->filename ? obj->filename : "<unknown>");

                    add_error(resolver,
                             LINKER_ERROR_ALLOCATION_FAILED,
                             message,
                             symbol->name,
                             obj->filename);
                    success = false;
                    continue;
                }

                /* CRITICAL: Set defining_object so address computation knows this symbol is defined */
                symbol->defining_object = obj_idx;

                resolver->defined_count++;
            }
        }
    }

    return success;
}

/* Phase 2: Resolve all undefined symbols */
static bool resolve_phase2_resolve_undefined(symbol_resolver_t* resolver,
                                              linker_object_t** objects,
                                              int object_count) {
    if (resolver == NULL || objects == NULL) {
        return false;
    }

    bool success = true;

    /* Iterate over all objects and their undefined symbols */
    for (int obj_idx = 0; obj_idx < object_count; obj_idx++) {
        linker_object_t* obj = objects[obj_idx];
        if (obj == NULL) {
            continue;
        }

        for (int sym_idx = 0; sym_idx < obj->symbol_count; sym_idx++) {
            linker_symbol_t* symbol = &obj->symbols[sym_idx];

            /* Only process undefined symbols */
            if (symbol->is_defined) {
                continue;
            }

            /* Look up symbol in global table */
            symbol_table_entry_t* entry = symbol_table_find(resolver, symbol->name);

            if (entry != NULL) {
                /* Found definition - link to it */
                symbol->defining_object = entry->object_index;

                /* Mark as resolved by updating the symbol */
                /* (We don't modify is_defined, but we record the defining object) */
                /* Note: final_address will be computed later in symbol_resolver_compute_addresses() */

            } else {
                /* Not found - check if it's a runtime symbol */
                if (is_runtime_symbol(symbol->name)) {
                    /* Mark as runtime symbol (external reference) */
                    symbol->defining_object = -1;  /* Special value for runtime */
                    resolver->runtime_count++;
                } else {
                    /* Undefined non-runtime symbol is an error */
                    char message[512];
                    snprintf(message, sizeof(message),
                            "Undefined symbol '%s' referenced in '%s'",
                            symbol->name,
                            obj->filename ? obj->filename : "<unknown>");

                    add_error(resolver,
                             LINKER_ERROR_UNDEFINED_SYMBOL,
                             message,
                             symbol->name,
                             obj->filename);
                    success = false;
                    resolver->undefined_count++;
                }
            }
        }
    }

    return success;
}

/* Main symbol resolution function (two-phase algorithm) */
bool symbol_resolver_resolve(symbol_resolver_t* resolver) {
    if (resolver == NULL || resolver->objects == NULL) {
        return false;
    }

    /* Clear previous errors */
    symbol_resolver_clear_errors(resolver);

    /* Phase 1: Collect all defined symbols */
    bool phase1_success = resolve_phase1_collect_symbols(resolver,
                                                          resolver->objects,
                                                          resolver->object_count);

    /* Phase 2: Resolve all undefined symbols */
    bool phase2_success = resolve_phase2_resolve_undefined(resolver,
                                                            resolver->objects,
                                                            resolver->object_count);

    return phase1_success && phase2_success;
}

/* Look up a symbol by name */
linker_symbol_t* symbol_resolver_lookup(symbol_resolver_t* resolver,
                                        const char* name) {
    if (resolver == NULL || name == NULL) {
        return NULL;
    }

    symbol_table_entry_t* entry = symbol_table_find(resolver, name);
    return entry != NULL ? entry->symbol : NULL;
}

/* Get object index for a symbol */
int symbol_resolver_get_object_index(symbol_resolver_t* resolver,
                                     const char* name) {
    if (resolver == NULL || name == NULL) {
        return -1;
    }

    symbol_table_entry_t* entry = symbol_table_find(resolver, name);
    return entry != NULL ? entry->object_index : -1;
}

/*
 * Helper: Compute final address for a symbol using contribution-based lookup
 *
 * This is the CORRECT way to compute symbol addresses. It:
 * 1. Gets the section from the object file containing the symbol
 * 2. Finds the merged section by TYPE (not index!) - critical for multi-object linking
 * 3. Finds the contribution from that object's section
 * 4. Calculates: merged.vaddr + contribution.offset + symbol.value
 *
 * This works for ALL symbols (global, weak, local) from ALL objects.
 */
static bool compute_symbol_address(linker_symbol_t* symbol,
                                   linker_object_t* obj,
                                   int obj_idx,
                                   section_layout_t* layout,
                                   bool verbose) {
    /* Skip undefined symbols */
    if (!symbol->is_defined) {
        symbol->final_address = 0;
        return true;
    }

    /* Skip runtime/external symbols (defining_object == -1) */
    if (symbol->defining_object == -1) {
        symbol->final_address = 0;  /* External - resolved by dynamic linker */
        if (verbose) {
            fprintf(stderr, "[ADDR] Symbol '%s': EXTERNAL (runtime/system library)\n",
                    symbol->name);
        }
        return true;
    }

    /* Handle absolute symbols (SHN_ABS - section_index == -1) */
    if (symbol->section_index == -1) {
        symbol->final_address = symbol->value;  /* Value IS the address */
        if (verbose) {
            fprintf(stderr, "[ADDR] Symbol '%s': ABSOLUTE, final=0x%llx\n",
                    symbol->name, (unsigned long long)symbol->final_address);
        }
        return true;
    }

    /* Validate section index */
    if (symbol->section_index < 0 || symbol->section_index >= obj->section_count) {
        fprintf(stderr, "Error: Symbol '%s' references invalid section %d (max %d)\n",
                symbol->name, symbol->section_index, obj->section_count - 1);
        return false;
    }

    /* Get section from object file */
    linker_section_t* obj_section = &obj->sections[symbol->section_index];

    /* Find merged section by TYPE (not index!) - this is critical */
    merged_section_t* merged = section_layout_find_section_by_type(layout, obj_section->type);
    if (!merged) {
        /* Skip symbols with unknown section types (like TLV sections) */
        if (obj_section->type == SECTION_TYPE_UNKNOWN) {
            if (verbose) {
                fprintf(stderr, "[ADDR] Symbol '%s': SKIPPED (unknown section type)\n",
                        symbol->name);
            }
            symbol->final_address = 0;  /* Treat as external */
            return true;  /* Not fatal, just skip */
        }
        fprintf(stderr, "Error: No merged section for type %s (symbol '%s')\n",
                section_type_name(obj_section->type), symbol->name);
        return false;
    }

    /* Find contribution from this object's section */
    section_contribution_t* contrib = merged->contributions;
    while (contrib != NULL) {
        if (contrib->object_index == obj_idx &&
            contrib->section_index == symbol->section_index) {

            /* Calculate final address: merged_base + contribution_offset + symbol_value */
            symbol->final_address = merged->vaddr + contrib->offset_in_merged + symbol->value;

            if (verbose) {
                fprintf(stderr, "[ADDR] Symbol '%s': obj=%d, sec=%d(%s), "
                        "merged_vaddr=0x%llx, contrib_offset=0x%llx, sym_value=0x%llx, "
                        "final=0x%llx\n",
                        symbol->name, obj_idx, symbol->section_index,
                        section_type_name(obj_section->type),
                        (unsigned long long)merged->vaddr,
                        (unsigned long long)contrib->offset_in_merged,
                        (unsigned long long)symbol->value,
                        (unsigned long long)symbol->final_address);
            }

            return true;
        }
        contrib = contrib->next;
    }

    /* Contribution not found - this is an error */
    fprintf(stderr, "Error: No contribution found for symbol '%s' in obj %d, section %d\n",
            symbol->name, obj_idx, symbol->section_index);
    return false;
}

/*
 * CRITICAL FIX #6: Compute Final Addresses for All Symbols (Using Contribution Lookup)
 *
 * This function must be called AFTER section layout is complete (Phase 3).
 * It computes the final virtual addresses for all symbols based on:
 *   final_address = merged_section_vaddr + contribution_offset + symbol_value
 *
 * FIXED: Now uses contribution-based lookup instead of direct section_index,
 * which was causing symbols from runtime library to get wrong addresses.
 *
 * This computes addresses for BOTH global/weak AND local symbols.
 */
bool symbol_resolver_compute_addresses(symbol_resolver_t* resolver,
                                        section_layout_t* layout) {
    if (!resolver || !layout) {
        fprintf(stderr, "Symbol resolver error: NULL parameters to compute_addresses\n");
        return false;
    }

    /* Enable verbose mode for debugging (can be controlled via env var later) */
    bool verbose = false;  /* Set to true to enable debug output */

    /* Part 1: Compute addresses for GLOBAL/WEAK symbols in hash table */
    for (size_t i = 0; i < resolver->table_size; i++) {
        symbol_table_entry_t* entry = resolver->table[i];

        while (entry != NULL) {
            linker_symbol_t* sym = entry->symbol;
            int obj_idx = entry->object_index;

            /* Validate object index */
            if (obj_idx < 0 || obj_idx >= resolver->object_count) {
                fprintf(stderr, "Error: Invalid object index %d for symbol '%s'\n",
                        obj_idx, sym->name);
                return false;
            }

            linker_object_t* obj = resolver->objects[obj_idx];

            /* Use helper to compute address */
            if (!compute_symbol_address(sym, obj, obj_idx, layout, verbose)) {
                return false;
            }

            entry = entry->next;
        }
    }

    /* Part 2: Compute addresses for LOCAL symbols (not in hash table) */
    for (int obj_idx = 0; obj_idx < resolver->object_count; obj_idx++) {
        linker_object_t* obj = resolver->objects[obj_idx];
        if (!obj) continue;

        for (int sym_idx = 0; sym_idx < obj->symbol_count; sym_idx++) {
            linker_symbol_t* symbol = &obj->symbols[sym_idx];

            /* Skip if already computed (global/weak from Part 1) */
            if (symbol->final_address != 0) continue;

            /* Only process LOCAL symbols */
            if (symbol->binding != SYMBOL_BINDING_LOCAL) continue;

            /* Use helper to compute address */
            if (!compute_symbol_address(symbol, obj, obj_idx, layout, verbose)) {
                /* Error already printed, but don't fail - continue with other symbols */
            }
        }
    }

    return true;
}

/* Print resolver statistics */
void symbol_resolver_print_stats(symbol_resolver_t* resolver) {
    if (resolver == NULL) {
        return;
    }

    printf("Symbol Resolution Statistics:\n");
    printf("  Total symbols in table: %zu\n", resolver->table_count);
    printf("  Defined symbols:        %d\n", resolver->defined_count);
    printf("  Undefined symbols:      %d\n", resolver->undefined_count);
    printf("  Runtime symbols:        %d\n", resolver->runtime_count);
    printf("  Hash table size:        %zu\n", resolver->table_size);
    printf("  Hash table load factor: %.2f\n",
           (double)resolver->table_count / (double)resolver->table_size);
    printf("  Errors:                 %d\n", resolver->error_count);

    if (resolver->error_count > 0) {
        printf("\nErrors:\n");
        for (int i = 0; i < resolver->error_count; i++) {
            linker_error_t* error = &resolver->errors[i];
            printf("  [%s] %s\n",
                   linker_error_type_name(error->type),
                   error->message ? error->message : "<no message>");
        }
    }
}

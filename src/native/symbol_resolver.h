#ifndef SOX_SYMBOL_RESOLVER_H
#define SOX_SYMBOL_RESOLVER_H

#include "linker_core.h"
#include <stdbool.h>
#include <stddef.h>

/* Forward declaration to avoid circular dependency */
typedef struct section_layout_t section_layout_t;

/*
 * Symbol Resolution Engine
 *
 * This module implements the symbol resolution phase of the custom linker.
 * It builds a global symbol table from all input object files and resolves
 * undefined symbol references.
 *
 * Symbol Resolution Algorithm (Two-Phase):
 *
 * Phase 1: Collect Defined Symbols
 *   - Build global symbol hash table from all object files
 *   - Handle global vs weak symbol precedence
 *   - Detect duplicate definitions (error if two global symbols with same name)
 *   - Global symbols override weak symbols
 *
 * Phase 2: Resolve Undefined Symbols
 *   - For each undefined symbol, look up in global table
 *   - If found, link to definition
 *   - If not found and is runtime symbol, mark as runtime
 *   - If not found and not runtime, report error
 *
 * Phase 2.1: Global Symbol Table
 */

/* Forward declarations */
typedef struct symbol_resolver_t symbol_resolver_t;
typedef struct linker_error_t linker_error_t;
typedef struct symbol_table_entry_t symbol_table_entry_t;

/* Linker error types */
typedef enum {
    LINKER_ERROR_NONE = 0,
    LINKER_ERROR_UNDEFINED_SYMBOL,
    LINKER_ERROR_DUPLICATE_DEFINITION,
    LINKER_ERROR_WEAK_SYMBOL_CONFLICT,
    LINKER_ERROR_TYPE_MISMATCH,
    LINKER_ERROR_ALLOCATION_FAILED
} linker_error_type_t;

/* Linker error structure */
struct linker_error_t {
    linker_error_type_t type;
    char* message;              /* Detailed error message */
    char* symbol_name;          /* Name of problematic symbol */
    char* object_file;          /* Object file where error occurred */
    int line_number;            /* Line number (if available) */
};

/* Symbol table entry for hash table */
struct symbol_table_entry_t {
    char* key;                  /* Symbol name (key) */
    linker_symbol_t* symbol;    /* Pointer to symbol in object file */
    int object_index;           /* Index of object that defines this symbol */
    symbol_table_entry_t* next; /* For collision chaining */
};

/* Symbol resolver state */
struct symbol_resolver_t {
    /* Global symbol hash table */
    symbol_table_entry_t** table;
    size_t table_size;
    size_t table_count;

    /* Object files being resolved */
    linker_object_t** objects;
    int object_count;

    /* Error tracking */
    linker_error_t* errors;
    int error_count;
    int error_capacity;

    /* Statistics */
    int defined_count;
    int undefined_count;
    int runtime_count;

    /* Debug verbosity */
    bool verbose;
};

/*
 * Symbol Resolver API
 */

/* Create and destroy symbol resolver */
symbol_resolver_t* symbol_resolver_new(void);
void symbol_resolver_free(symbol_resolver_t* resolver);

/* Add object file to resolver */
void symbol_resolver_add_object(symbol_resolver_t* resolver,
                                 linker_object_t* obj,
                                 int obj_index);

/* Resolve all symbols (two-phase algorithm) */
bool symbol_resolver_resolve(symbol_resolver_t* resolver);

/* Compute final addresses for all symbols based on section layout
 *
 * This must be called AFTER section layout is complete.
 *
 * @param resolver Symbol resolver with resolved symbols
 * @param layout   Section layout with computed virtual addresses
 * @return true on success, false on error
 */
bool symbol_resolver_compute_addresses(symbol_resolver_t* resolver,
                                        section_layout_t* layout);

/* Look up symbol by name */
linker_symbol_t* symbol_resolver_lookup(symbol_resolver_t* resolver,
                                        const char* name);

/* Get object index for a symbol */
int symbol_resolver_get_object_index(symbol_resolver_t* resolver,
                                     const char* name);

/* Error management */
linker_error_t* symbol_resolver_get_errors(symbol_resolver_t* resolver,
                                           int* count);
void symbol_resolver_clear_errors(symbol_resolver_t* resolver);
const char* linker_error_type_name(linker_error_type_t type);

/* Runtime library symbol detection (Phase 2.2) */
bool is_runtime_symbol(const char* name);

/* Statistics */
void symbol_resolver_print_stats(symbol_resolver_t* resolver);

/* Internal hash table functions (exposed for testing) */
uint32_t symbol_hash(const char* name);
symbol_table_entry_t* symbol_table_find(symbol_resolver_t* resolver,
                                        const char* name);
bool symbol_table_insert(symbol_resolver_t* resolver,
                         const char* name,
                         linker_symbol_t* symbol,
                         int object_index);

#endif

#ifndef SOX_RUNTIME_PRINT_H
#define SOX_RUNTIME_PRINT_H

#include <stddef.h>

// Forward declaration to avoid circular dependency
typedef struct value_t value_t;

// Simple printf-like function
int runtime_printf(const char* format, ...);

// Print a value directly
void runtime_print_value(value_t value);

#endif

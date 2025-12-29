#include "runtime_print.h"
#include "runtime_value.h"
#include "runtime_object.h"
#include <stdio.h>
#include <stdarg.h>
#include <stdbool.h>

/**
 * runtime_printf - Simple printf-like function for output operations
 *
 * A wrapper around vprintf() that provides variable argument formatting
 * for the Sox runtime library. Outputs directly to stdout.
 *
 * @format: printf-style format string
 * @...: variable arguments matching the format string
 *
 * Returns: number of characters printed, or negative on error
 */
int runtime_printf(const char* format, ...) {
    va_list args;
    va_start(args, format);
    int result = vprintf(format, args);
    va_end(args);
    return result;
}

/**
 * runtime_print_value - Print a Sox value to stdout
 *
 * Prints a value_t in a human-readable format based on its type:
 * - BOOL: "true" or "false"
 * - NIL: "nil"
 * - NUMBER: numeric representation (using %g format)
 * - OBJ: delegates to runtime_print_object() for complex objects
 *
 * @value: the value to print
 *
 * This function is implemented in runtime_value.c and declared here
 * for convenience and to match the header file interface.
 */
// Note: runtime_print_value is implemented in runtime_value.c

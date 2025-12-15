#ifndef SOX_RUNTIME_OPS_H
#define SOX_RUNTIME_OPS_H

/**
 * Runtime Library Operations for ARM64 Register Pair Operations
 *
 * These functions handle arithmetic and comparison operations on value_t
 * (16-byte composite types). They are designed to be called from native
 * code generated for ARM64 targets.
 *
 * ARM64 Calling Convention:
 * - Binary operations: left value_t in X0:X1, right value_t in X2:X3
 * - Unary operations: operand value_t in X0:X1
 * - Return value: value_t in X0:X1
 */

#include "runtime_value.h"

#ifdef SOX_RUNTIME_SHARED
  #ifdef SOX_RUNTIME_BUILD
    #ifdef _WIN32
      #define SOX_OPS_API __declspec(dllexport)
    #else
      #define SOX_OPS_API __attribute__((visibility("default")))
    #endif
  #else
    #ifdef _WIN32
      #define SOX_OPS_API __declspec(dllimport)
    #else
      #define SOX_OPS_API
    #endif
  #endif
#else
  #define SOX_OPS_API
#endif

/* ====================================================================
 * Arithmetic Operations
 * All binary operations take 2 value_t args (X0:X1 and X2:X3)
 * and return value_t in X0:X1
 * ==================================================================== */

/**
 * Add two values.
 * For numbers: returns NUMBER_VAL(left + right)
 * For strings: returns concatenated string (string + string)
 * For invalid types: prints error and returns NIL_VAL
 */
SOX_OPS_API value_t sox_add(value_t left, value_t right);

/**
 * Subtract two numbers.
 * Returns NUMBER_VAL(left - right)
 * For invalid types: prints error and returns NIL_VAL
 */
SOX_OPS_API value_t sox_sub(value_t left, value_t right);

/**
 * Multiply two numbers.
 * Returns NUMBER_VAL(left * right)
 * For invalid types: prints error and returns NIL_VAL
 */
SOX_OPS_API value_t sox_mul(value_t left, value_t right);

/**
 * Divide two numbers.
 * Returns NUMBER_VAL(left / right)
 * For division by zero: prints error and returns NIL_VAL
 * For invalid types: prints error and returns NIL_VAL
 */
SOX_OPS_API value_t sox_div(value_t left, value_t right);

/**
 * Negate a number.
 * Single argument in X0:X1
 * Returns NUMBER_VAL(-operand)
 * For invalid types: prints error and returns NIL_VAL
 */
SOX_OPS_API value_t sox_neg(value_t operand);

/* ====================================================================
 * Comparison Operations
 * All binary operations take 2 value_t args (X0:X1 and X2:X3)
 * and return boolean value_t in X0:X1
 * ==================================================================== */

/**
 * Check if two values are equal.
 * Returns BOOL_VAL(left == right)
 * Uses value equality semantics (type and value must match)
 */
SOX_OPS_API value_t sox_eq(value_t left, value_t right);

/**
 * Check if two values are not equal.
 * Returns BOOL_VAL(left != right)
 */
SOX_OPS_API value_t sox_ne(value_t left, value_t right);

/**
 * Check if left is less than right.
 * Returns BOOL_VAL(left < right)
 * For non-numbers: returns BOOL_VAL(false)
 */
SOX_OPS_API value_t sox_lt(value_t left, value_t right);

/**
 * Check if left is less than or equal to right.
 * Returns BOOL_VAL(left <= right)
 * For non-numbers: returns BOOL_VAL(false)
 */
SOX_OPS_API value_t sox_le(value_t left, value_t right);

/**
 * Check if left is greater than right.
 * Returns BOOL_VAL(left > right)
 * For non-numbers: returns BOOL_VAL(false)
 */
SOX_OPS_API value_t sox_gt(value_t left, value_t right);

/**
 * Check if left is greater than or equal to right.
 * Returns BOOL_VAL(left >= right)
 * For non-numbers: returns BOOL_VAL(false)
 */
SOX_OPS_API value_t sox_ge(value_t left, value_t right);

/* ====================================================================
 * Logical Operations
 * Single argument in X0:X1, returns boolean value_t in X0:X1
 * ==================================================================== */

/**
 * Logical NOT operation.
 * Returns BOOL_VAL(true) if operand is falsy (nil or false)
 * Returns BOOL_VAL(false) otherwise
 */
SOX_OPS_API value_t sox_not(value_t operand);

#endif /* SOX_RUNTIME_OPS_H */

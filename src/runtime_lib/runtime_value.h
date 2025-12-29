#ifndef SOX_RUNTIME_VALUE_H
#define SOX_RUNTIME_VALUE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct runtime_obj_t runtime_obj_t;
typedef struct runtime_obj_string_t runtime_obj_string_t;

typedef enum {
    VAL_BOOL,
    VAL_NIL,
    VAL_NUMBER,
    VAL_OBJ,
} ValueType;

typedef struct value_t {
    ValueType type;
    union as {
        double number;
        bool boolean;
        runtime_obj_t *obj;
    } as;
} value_t;

#define BOOL_VAL(value)   ((value_t){VAL_BOOL,  {.boolean = value}})
#define NIL_VAL           ((value_t){VAL_NIL,   {.number = 0}})
#define NUMBER_VAL(value) ((value_t){VAL_NUMBER,{.number = value}})
#define OBJ_VAL(object)   ((value_t){VAL_OBJ,   {.obj = (runtime_obj_t*)object}})

#define AS_BOOL(value)    ((value).as.boolean)
#define AS_NUMBER(value)  ((value).as.number)
#define AS_OBJ(value)     ((value).as.obj)

#define IS_BOOL(value)    ((value).type == VAL_BOOL)
#define IS_NIL(value)     ((value).type == VAL_NIL)
#define IS_NUMBER(value)  ((value).type == VAL_NUMBER)
#define IS_OBJ(value)     ((value).type == VAL_OBJ)

typedef struct {
    size_t capacity;
    size_t count;
    value_t* values;
} value_array_t;

void runtime_init_value_array(value_array_t* array);
void runtime_write_value_array(value_array_t* array, value_t value);
void runtime_write_value_array_front(value_array_t* array, value_t value);

void runtime_free_value_array(value_array_t* array);

bool runtime_values_equal(value_t a, value_t b);

void runtime_print_value(value_t value);

#endif

#ifndef SOX_VALUE_H
#define SOX_VALUE_H

#include "common.h"

typedef struct obj_t obj_t;
typedef struct obj_string_t obj_string_t;

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
        obj_t *obj;
    } as;
} value_t;

#define BOOL_VAL(value)   ((value_t){VAL_BOOL,  {.boolean = value}})
#define NIL_VAL           ((value_t){VAL_NIL,   {.number = 0}})
#define NUMBER_VAL(value) ((value_t){VAL_NUMBER,{.number = value}})
#define OBJ_VAL(object)   ((value_t){VAL_OBJ,   {.obj = (obj_t*)object}})

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

void l_init_value_array(value_array_t* array);
void l_write_value_array(value_array_t* array, value_t value);
void l_write_value_array_front(value_array_t* array, value_t value);

void l_free_value_array(value_array_t* array);

bool l_values_equal(value_t a, value_t b);

void l_print_value(value_t value);

#endif
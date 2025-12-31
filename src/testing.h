#ifndef SOX_TESTING_H
#define SOX_TESTING_H

#include <stdbool.h>

#include "object.h"

typedef struct test_state_t {
    const char* test_name;
    int failure_count;
    bool fatal_triggered;
} test_state_t;

obj_table_t* l_create_test_context(test_state_t* state);

#endif

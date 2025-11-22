#ifndef SOX_WAT_GENERATOR_H
#define SOX_WAT_GENERATOR_H

#include "object.h"
#include "vm_config.h"

typedef enum WatErrorCode {
    WAT_OK,
    WAT_ERROR,
    WAT_ERROR_FILE_NOT_OPEN,
    WAT_ERROR_FILE_NOT_WRITTEN,
    WAT_ERROR_UNSUPPORTED_OPCODE,
    WAT_ERROR_INVALID_VALUE_TYPE,
} WatErrorCode;

typedef struct wat_generator_t wat_generator_t;

const char * l_wat_get_error_string(WatErrorCode error);

wat_generator_t * l_wat_new(const char * filename_source);
void l_wat_del(wat_generator_t* generator);

WatErrorCode l_wat_generate_from_function(wat_generator_t* generator, obj_function_t* function);
WatErrorCode l_wat_write_to_file(wat_generator_t* generator);

#endif
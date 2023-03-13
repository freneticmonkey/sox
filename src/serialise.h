#ifndef SOX_SERIALISE_H
#define SOX_SERIALISE_H

#include "object.h"

// typedef obj_function_t obj_function_t;
// typedef obj_string_t obj_string_t;
// typedef value_t value_t;
// typedef value_array_t value_array_t;
// typedef table_t table_t;

typedef FILE FILE;

typedef enum SerialiseErrorCode {
    SERIALISE_OK,
    SERIALISE_ERROR,
    SERIALISE_ERROR_FILE_NOT_FOUND,
    SERIALISE_ERROR_FILE_NOT_OPEN,
    SERIALISE_ERROR_FILE_NOT_CLOSED,
    SERIALISE_ERROR_FILE_NOT_READ,
    SERIALISE_ERROR_FILE_NOT_WRITTEN,
    SERIALISE_ERROR_BUFFER_NOT_INITIALISED,
} SerialiseErrorCode;

const char * l_serialise_get_error_string(SerialiseErrorCode error);

typedef struct serialiser_buf_t serialiser_buf_t;

typedef struct {
    serialiser_buf_t * buffer;
    SerialiseErrorCode error;
    FILE * file;

    // VM State
    int global_offset;
    int string_offset;
    int flush_offset;
} serialiser_t;

serialiser_t * l_serialise_new(const char * filename_source);
void l_serialise_del(serialiser_t* serialiser);
void l_serialise_flush(serialiser_t* serialiser);

void l_serialise_vm_set_init_state(serialiser_t* serialiser);
void l_serialise_vm(serialiser_t* serialiser);

void l_serialise_obj(serialiser_t* serialiser, obj_t* object);
void l_serialise_table(serialiser_t* serialiser, table_t* table);
void l_serialise_table_offset(serialiser_t* serialiser, table_t* table, int offset);
void l_serialise_value(serialiser_t* serialiser, value_t* value);

#endif
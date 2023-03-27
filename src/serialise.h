#ifndef SOX_SERIALISE_H
#define SOX_SERIALISE_H

#include "object.h"

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

    SERIALISE_ERROR_SOX_VERSION_MISMATCH,
    SERIALISE_ERROR_SOURCE_FILENAME_MISMATCH,
    SERIALISE_ERROR_SOURCE_HASH_MISMATCH,
    SERIALISE_ERROR_UNKNOWN_OBJECT_TYPE,

} SerialiseErrorCode;

typedef enum SerialisationMode {
    SERIALISE_MODE_READ,
    SERIALISE_MODE_WRITE,
} SerialisationMode;

const char * l_serialise_get_error_string(SerialiseErrorCode error);

typedef struct serialiser_buf_t serialiser_buf_t;

typedef struct {
    serialiser_buf_t * buffer;
    SerialiseErrorCode error;
    SerialisationMode mode;
    FILE * file;

    // VM State
    int global_offset;
    int string_offset;
    size_t flush_offset;
} serialiser_t;

serialiser_t * l_serialise_new(const char * filename_source, const char * source, SerialisationMode mode);
void l_serialise_del(serialiser_t* serialiser);

void l_serialise_flush(serialiser_t* serialiser);

void l_serialise_vm_set_init_state(serialiser_t* serialiser);
void l_serialise_vm(serialiser_t* serialiser);

void l_serialise_obj(serialiser_t* serialiser, obj_t* object);
void l_serialise_table(serialiser_t* serialiser, table_t* table);
void l_serialise_table_offset(serialiser_t* serialiser, table_t* table, int offset);
void l_serialise_value(serialiser_t* serialiser, value_t* value);


void l_serialise_int(serialiser_t* serialiser, int value);
void l_serialise_ints(serialiser_t* serialiser, int *values, size_t count);
void l_serialise_uint8(serialiser_t* serialiser, uint8_t value);
void l_serialise_uint8s(serialiser_t* serialiser, uint8_t *values, size_t count);
void l_serialise_uint32(serialiser_t* serialiser, uint32_t value);
void l_serialise_uintptr(serialiser_t* serialiser, uintptr_t value);
void l_serialise_long(serialiser_t* serialiser, size_t value);

void l_serialise_double(serialiser_t* serialiser, double value);
void l_serialise_bool(serialiser_t* serialiser, bool value);
void l_serialise_char(serialiser_t* serialiser, const char *chars);

void l_serialise_rewind(serialiser_t* serialiser);

obj_closure_t * l_deserialise_vm(serialiser_t* serialiser);
void            l_deserialise_vm_set_init_state(serialiser_t* serialiser, obj_closure_t * entry_point);

obj_t * l_deserialise_obj(serialiser_t* serialiser);
void    l_deserialise_table(serialiser_t* serialiser, table_t* table);
value_t l_deserialise_value(serialiser_t* serialiser);

int       l_deserialise_int(serialiser_t* serialiser);
int *     l_deserialise_ints(serialiser_t* serialiser);
uint8_t   l_deserialise_uint8(serialiser_t* serialiser);
uint8_t * l_deserialise_uint8s(serialiser_t* serialiser);
uint32_t  l_deserialise_uint32(serialiser_t* serialiser);
uintptr_t l_deserialise_uintptr(serialiser_t* serialiser);
size_t    l_deserialise_long(serialiser_t* serialiser);

double l_deserialise_double(serialiser_t* serialiser);
bool   l_deserialise_bool(serialiser_t* serialiser);
char * l_deserialise_char(serialiser_t* serialiser);


#endif

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "serialise.h"
#include "lib/memory.h"
#include "lib/string.h"
#include "version.h"

#define SERIALISATION_VERSION 1


#if defined(SERIALISE_DEBUG)
#define INDENT_SIZE 4
int indent = 0;
#define INDENT() for (int i = 0; i < indent; i++) printf(" ");
#define BLOCK_START(block_name) INDENT(); printf("" block_name " {\n"); indent+=INDENT_SIZE; 
#define BLOCK_END() indent-=INDENT_SIZE; INDENT(); printf("}\n");
#endif


const char * l_serialise_get_error_string(SerialiseErrorCode error) {
    switch (error) {
        case SERIALISE_OK: return "No error.";
        case SERIALISE_ERROR: return "An error occurred.";
        case SERIALISE_ERROR_FILE_NOT_FOUND: return "File not found.";
        case SERIALISE_ERROR_FILE_NOT_OPEN: return "File not open.";
        case SERIALISE_ERROR_FILE_NOT_CLOSED: return "File not closed.";
        case SERIALISE_ERROR_FILE_NOT_READ: return "File not read.";
        case SERIALISE_ERROR_FILE_NOT_WRITTEN: return "File not written.";
        case SERIALISE_ERROR_BUFFER_NOT_INITIALISED: return "Buffer not initialised.";
        case SERIALISE_ERROR_SOX_VERSION_MISMATCH: return "SOX version mismatch.";
        case SERIALISE_ERROR_SOURCE_FILENAME_MISMATCH: return "Source filename mismatch.";
        case SERIALISE_ERROR_SOURCE_HASH_MISMATCH: return "SOX file hash mismatch.";
        case SERIALISE_ERROR_UNKNOWN_OBJECT_TYPE: return "Unknown object type.";
        default: return "Unknown error.";
    }
}

typedef struct serialiser_buf_t {
    size_t count;
    size_t capacity;
    size_t offset;
    uint8_t* bytes;
} serialiser_buf_t;

serialiser_buf_t * _serialise_buf_new() {

    serialiser_buf_t * buffer = ALLOCATE(serialiser_buf_t, 1);

    buffer->count = 0;
    buffer->capacity = 0;
    buffer->bytes = NULL;

    return buffer;
}

bool _serialise_buf_is_empty(serialiser_buf_t* buffer) {
    return buffer->count == 0;
}

bool _serialise_buf_is_eof(serialiser_buf_t* buffer) {
    return buffer->offset >= buffer->count;
}

void _serialise_buf_free(serialiser_buf_t* buffer) {
    if (buffer->bytes != NULL && buffer->capacity > 0) {
        FREE_ARRAY(uint8_t, buffer->bytes, buffer->capacity);
    }
    FREE(serialiser_buf_t, buffer);
}

void _serialise_buf_write(serialiser_buf_t* buffer, const void* bytes, size_t count) {
    if (buffer->capacity < buffer->count + count) {
        size_t new_capacity = GROW_CAPACITY(buffer->capacity);
        buffer->bytes = GROW_ARRAY(uint8_t, buffer->bytes, buffer->capacity, new_capacity);
        buffer->capacity = new_capacity;
    }

    memcpy(&buffer->bytes[buffer->count], bytes, count);
    buffer->count += count;
}

void _serialise_buf_read_file(serialiser_buf_t* buffer, FILE* file) {
    fseek(file, 0, SEEK_END);
    size_t size = ftell(file);
    rewind(file);

    buffer->bytes = ALLOCATE(uint8_t, size);
    buffer->capacity = size;
    buffer->count = size;

    fread(buffer->bytes, size, 1, file);
}


#if defined(SERIALISE_DEBUG)
#define WRITE_DEBUG(type, size) \
    INDENT(); printf("writing <" type "> (%lu bytes) at offset %lu\n", size, buffer->count - size);
#else
#define WRITE_DEBUG(type, size)
#endif

void _serialise_buf_write_long(serialiser_buf_t* buffer, size_t value) {
    _serialise_buf_write(buffer, &value, sizeof(size_t));
    WRITE_DEBUG("long", sizeof(size_t))
}

void _serialise_buf_write_bytes(serialiser_buf_t* buffer, const void* bytes, size_t count) {
    _serialise_buf_write_long(buffer, count);
    _serialise_buf_write(buffer, bytes, count);
}

void _serialise_buf_write_int(serialiser_buf_t* buffer, int value) {
    _serialise_buf_write_bytes(buffer, &value, sizeof(int));
    WRITE_DEBUG("int", sizeof(int))
}

void _serialise_buf_write_ints(serialiser_buf_t* buffer, int *values, size_t count) {
    _serialise_buf_write_bytes(buffer, values, sizeof(int) * count);
    WRITE_DEBUG("int *", sizeof(int) * count)
}

void _serialise_buf_write_uint8(serialiser_buf_t* buffer, uint8_t value) {
    _serialise_buf_write_bytes(buffer, &value, sizeof(uint8_t));
    WRITE_DEBUG("uint8", sizeof(uint8_t))
}

void _serialise_buf_write_uint8s(serialiser_buf_t* buffer, uint8_t *values, size_t count) {
    _serialise_buf_write_bytes(buffer, values, sizeof(uint8_t) * count);
    WRITE_DEBUG("uint8 *", sizeof(uint8_t) * count)
}

void _serialise_buf_write_uint32(serialiser_buf_t* buffer, uint32_t value) {
    _serialise_buf_write_bytes(buffer, &value, sizeof(uint32_t));
    WRITE_DEBUG("uint32", sizeof(uint32_t))
}

void _serialise_buf_write_uintptr(serialiser_buf_t* buffer, uintptr_t value) {

#if defined(LINK_DEBUGGING)
    printf("serialising ptr: %lu\n", value);
#endif

    _serialise_buf_write_bytes(buffer, &value, sizeof(uintptr_t));
    WRITE_DEBUG("uintptr", sizeof(uintptr_t))
}

void _serialise_buf_write_double(serialiser_buf_t* buffer, double value) {
    _serialise_buf_write_bytes(buffer, &value, sizeof(double));
    WRITE_DEBUG("double", sizeof(double))
}

void _serialise_buf_write_bool(serialiser_buf_t* buffer, bool value) {
    _serialise_buf_write(buffer, &value, sizeof(bool));
    WRITE_DEBUG("bool", sizeof(bool))
}

void _serialise_buf_write_string(serialiser_buf_t* buffer, obj_string_t* string) {
    _serialise_buf_write_uint32(buffer, string->hash);
    _serialise_buf_write_bytes(buffer, string->chars, sizeof(char) * string->length);
    WRITE_DEBUG("string", sizeof(char) * string->length)
#if defined(SERIALISE_DEBUG)
    printf("\t - %s\n", string->chars);
#endif

}

void _serialise_buf_write_string_char(serialiser_buf_t* buffer, const char *string) {
    _serialise_buf_write_bytes(buffer, string, sizeof(char) * strlen(string));
    WRITE_DEBUG("string", sizeof(char) * strlen(string))
#if defined(SERIALISE_DEBUG)
    printf("\t - %s\n", string);
#endif
}

void _serialise_buf_write_file(serialiser_buf_t* buffer, FILE* file) {
    fwrite(buffer->bytes, sizeof(uint8_t), buffer->count, file);
}

void _serialise_value_array(serialiser_t* serialiser, value_array_t* array) {

#if defined(SERIALISE_DEBUG)
    BLOCK_START("Write Value Array")
#endif

    _serialise_buf_write_int(serialiser->buffer, array->count);
    for (int i = 0; i < array->count; i++) {
        l_serialise_value(serialiser, &array->values[i]);
    }
#if defined(SERIALISE_DEBUG)
    BLOCK_END()
#endif
}

void _serialise_chunk(serialiser_t* serialiser, chunk_t* chunk) {

#if defined(SERIALISE_DEBUG)
    BLOCK_START("Write CHUNK")
#endif
    _serialise_buf_write_int(serialiser->buffer, chunk->count);
    _serialise_buf_write_int(serialiser->buffer, chunk->capacity);
    _serialise_buf_write_bytes(serialiser->buffer, chunk->code, chunk->count);
    _serialise_buf_write_ints(serialiser->buffer, chunk->lines, chunk->count);
    _serialise_value_array(serialiser, &chunk->constants);
#if defined(SERIALISE_DEBUG)
    BLOCK_END()
#endif
}

void _serialise_ptr(serialiser_t* serialiser, void* ptr) {
    _serialise_buf_write_uintptr(serialiser->buffer, (uintptr_t)ptr);
}

void _serialise_string_ptr(serialiser_t* serialiser, obj_string_t* string) {

    // if the string being serialised is NULL, then write a hash of 0 to indicate that
    // there is no string
    if (string == NULL)
        _serialise_buf_write_uint32(serialiser->buffer, 0);
    else
        _serialise_buf_write_uint32(serialiser->buffer, string->hash);
}

void _serialise_write_object(serialiser_t* serialiser, obj_t* object) {

#if defined(SERIALISE_DEBUG)
    switch (object->type)
    {
        case OBJ_BOUND_METHOD: {
            BLOCK_START("Write BOUND_METHOD - NYI");
            break;
        }
        case OBJ_CLASS: {
            BLOCK_START("Write CLASS");
            break;
        }
        case OBJ_CLOSURE: {
            BLOCK_START("Write CLOSURE");
            break;
        }
        case OBJ_FUNCTION: {
            BLOCK_START("Write FUNCTION");
            break;
        }
        case OBJ_INSTANCE: {
            BLOCK_START("Write INSTANCE");
            break;
        }
        case OBJ_NATIVE: {
            BLOCK_START("Write NATIVE");
            break;
        }
        case OBJ_STRING: {
            BLOCK_START("Write STRING");
            break;
        }
        case OBJ_UPVALUE: {
            BLOCK_START("Write UPVALUE - NYI");
            break;
        }
        case OBJ_TABLE: {
            BLOCK_START("Write TABLE");
            break;
        }
        case OBJ_ERROR: {
            BLOCK_START("Write ERROR");
            break;
        }
        default: {
            BLOCK_START("Write UNKNOWN - Error");
            break;
        }
    }
#endif

    // write the uintptr_t of the object so that it can be tracked when deserialised
    _serialise_ptr(serialiser, object);

    switch (object->type) {

        case OBJ_BOUND_METHOD: {
            // _serialise_buf_write_int(serialiser->buffer, OBJ_BOUND_METHOD);
            
            // obj_bound_method_t* bound_method = (obj_bound_method_t*)object;
            // _serialise_ptr(serialiser->buffer, (obj_t*)bound_method->receiver);
            // _serialise_ptr(serialiser->buffer, (obj_t*)bound_method->method);
            break;
        }

        case OBJ_CLASS: {
            _serialise_buf_write_int(serialiser->buffer, OBJ_CLASS);
            
            obj_class_t* class = (obj_class_t*)object;
            _serialise_string_ptr(serialiser, class->name);
            l_serialise_table(serialiser, &class->methods);
            break;
        }

        case OBJ_CLOSURE: {

            _serialise_buf_write_int(serialiser->buffer, OBJ_CLOSURE);
            
            obj_closure_t* closure = (obj_closure_t*)object;

            _serialise_buf_write_int(serialiser->buffer, closure->upvalue_count);
            for (int i = 0; i < closure->upvalue_count; i++) {
                _serialise_write_object(serialiser, (obj_t*)closure->upvalues[i]);
            }

            _serialise_ptr(serialiser, closure->function);
            break;
        }
        case OBJ_FUNCTION: {
            _serialise_buf_write_int(serialiser->buffer, OBJ_FUNCTION);

            obj_function_t* function = (obj_function_t*)object;
            _serialise_buf_write_int(serialiser->buffer, function->arity);
            _serialise_buf_write_int(serialiser->buffer, function->upvalue_count);
            _serialise_chunk(serialiser, &function->chunk);
            _serialise_string_ptr(serialiser, function->name);                

            break;
        }
        case OBJ_INSTANCE: {
            _serialise_buf_write_int(serialiser->buffer, OBJ_INSTANCE);

            obj_instance_t* instance = (obj_instance_t*)object;
            _serialise_ptr(serialiser, instance->klass);
            // _serialise_write_object(serialiser->buffer, (obj_t*)instance->klass);

            l_serialise_table(serialiser, &instance->fields);
            break;
        }
        case OBJ_NATIVE: {
            _serialise_buf_write_int(serialiser->buffer, OBJ_NATIVE);
            // get the name of the native function using its pointer
            const char *name = l_allocate_track_get_native_name(((obj_native_t*)object)->function);
            
            // write the native function name
            _serialise_buf_write_bytes(serialiser->buffer, name, sizeof(char) * strlen(name));

            // obj_native_t* native = (obj_native_t*)object;
            // _serialise_buf_write_int(buffer, native->arity);
            break;
        }
        case OBJ_STRING: {
            _serialise_buf_write_int(serialiser->buffer, OBJ_STRING);

            obj_string_t* string = (obj_string_t*)object;
            _serialise_buf_write_string(serialiser->buffer, string);
            break;
        }
        case OBJ_UPVALUE: {
            // _serialise_buf_write_int(serialiser->buffer, OBJ_UPVALUE);

            // obj_upvalue_t* upvalue = (obj_upvalue_t*)object;
            // _serialise_ptr(serialiser, upvalue->location);
            break;
        }
        case OBJ_TABLE: {
            _serialise_buf_write_int(serialiser->buffer, OBJ_TABLE);

            obj_table_t* table = (obj_table_t*)object;
            l_serialise_table(serialiser, &table->table);
            break;
        }
        case OBJ_ERROR:
            break;
        default:
            serialiser->error = SERIALISE_ERROR_UNKNOWN_OBJECT_TYPE;
            break;
    }
#if defined(SERIALISE_DEBUG)
    // enabling this will cause the serialiser to flush the buffer after every object
    // this will also cause an error state if used with unit tests and memory only serialisation
    // l_serialise_flush(serialiser);
    BLOCK_END();
#endif
}

void _serialise_write_header(serialiser_t* serialiser, const char * filename_source, const char * source);
void _serialise_read_header(serialiser_t* serialiser, const char * filename_source, const char * source);

serialiser_t * l_serialise_new(const char * filename_source, const char * source, SerialisationMode mode) {

    // generate a bytecode filename from the source filename
    char filename_bytecode[256];
    sprintf(&filename_bytecode[0], "%s.sbc", filename_source);

    serialiser_t * serialiser = ALLOCATE(serialiser_t, 1);
    serialiser->mode = mode;
    serialiser->buffer = _serialise_buf_new();
    serialiser->error = SERIALISE_OK;
    serialiser->global_offset = 0;
    serialiser->string_offset = 0;
    serialiser->flush_offset = 0;

    // NOTE: NULL values are used in unit testing
    // if the filename and source isn't null, then setup the serialiser's file pointer
    // and process the serialisation header
    if ( filename_source != NULL && source != NULL) {
        const char * file_mode = (serialiser->mode == SERIALISE_MODE_WRITE) ? "wb" : "rb";

        FILE * file = fopen(&filename_bytecode[0], file_mode);

        if (file == NULL) {
            fprintf(stderr, "Could not open a bytecode \"%s\" for writing.", filename_bytecode);
            serialiser->error = SERIALISE_ERROR_FILE_NOT_FOUND;
            return serialiser;
        }

        serialiser->file = file;

        if (mode == SERIALISE_MODE_WRITE) {
            
            // write the header
            _serialise_write_header(serialiser, filename_source, source);
            
        } else {

            // read the bytecode file into the buffer
            _serialise_buf_read_file(serialiser->buffer, serialiser->file);

            // read the header from the bytecode file
            _serialise_read_header(serialiser, filename_source, source);

        }
    }
    
    return serialiser;
}

void l_serialise_del(serialiser_t* serialiser) {
    if (serialiser->buffer != NULL) {
        _serialise_buf_free(serialiser->buffer);
    }
    if (serialiser->file != NULL) {
        fclose(serialiser->file);
    } else {
        serialiser->error = SERIALISE_ERROR_FILE_NOT_CLOSED;
        return;
    }
    FREE(serialiser_t, serialiser);
}

void l_serialise_flush(serialiser_t* serialiser) {
    if (serialiser->file == NULL) {
        serialiser->error = SERIALISE_ERROR_FILE_NOT_OPEN;
        return;
    }
    if (serialiser->buffer == NULL) {
        serialiser->error = SERIALISE_ERROR_BUFFER_NOT_INITIALISED;
        return;
    }
    if (serialiser->buffer->count == 0) {
        return;
    }
    
    size_t flush_length = serialiser->buffer->count - serialiser->flush_offset;

    fwrite(&serialiser->buffer->bytes[serialiser->flush_offset], sizeof(uint8_t), flush_length, serialiser->file);
    serialiser->flush_offset = serialiser->buffer->count;
    
    fflush(serialiser->file);
}

void l_serialise_obj(serialiser_t* serialiser, obj_t* object) {
    _serialise_write_object(serialiser, object);
}

void l_serialise_table(serialiser_t* serialiser, table_t* table) {

#if defined(SERIALISE_DEBUG)
    BLOCK_START("Write TABLE");
#endif

    // determine how many entries need to be serialised by counting the number of non-null keys
    int count = 0;
    for (int i = 0; i < table->capacity; i++) {
        entry_t* entry = &table->entries[i];
        if (entry->key == NULL) {
            continue;
        }
        count++;
    }
    
    // write the count
    _serialise_buf_write_int(serialiser->buffer, count);

#if defined(SERIALISE_DEBUG)
    BLOCK_START("keys");
#endif
    for (int i = 0; i < table->capacity; i++) {
        entry_t* entry = &table->entries[i];

#if defined(SERIALISE_DEBUG)
        printf("\t>\n");
#endif

        if (entry->key == NULL) {
#if defined(SERIALISE_DEBUG)
        printf("\tskipping\n\t<\n");
#endif
          continue;
        }

        _serialise_buf_write_string(serialiser->buffer, entry->key);
        l_serialise_value(serialiser, &entry->value);

#if defined(SERIALISE_DEBUG)
        printf("\t<\n");
#endif
    }

#if defined(SERIALISE_DEBUG)
    BLOCK_END();
    BLOCK_END();
#endif
}

void l_serialise_table_offset(serialiser_t* serialiser, table_t* table, int offset) {
    _serialise_buf_write_int(serialiser->buffer, table->count);
    for (int i = offset; i < table->capacity; i++) {
        entry_t* entry = &table->entries[i];
        if (entry->key == NULL) {
            continue;
        }
        _serialise_buf_write_string(serialiser->buffer, entry->key);
        l_serialise_value(serialiser, &entry->value);
    }
}

void l_serialise_value(serialiser_t* serialiser, value_t * value) {

    _serialise_buf_write_int(serialiser->buffer, value->type);

#if defined(SERIALISE_DEBUG)
    BLOCK_START("Write VALUE");
    switch (value->type) {
        case VAL_BOOL:
            BLOCK_START("bool");
            break;
        case VAL_NIL:

            BLOCK_START("nil");
            break;
        case VAL_NUMBER:
            BLOCK_START("number");
            break;
        case VAL_OBJ:
            BLOCK_START("object");
            break;
        default:
            BLOCK_START("unknown");
            break;
    }
#endif


    switch (value->type) {
        case VAL_BOOL:
            _serialise_buf_write_bool(serialiser->buffer, AS_BOOL(*value));
            break;
        case VAL_NIL:
            break;
        case VAL_NUMBER:
            _serialise_buf_write_double(serialiser->buffer, AS_NUMBER(*value));
            break;
        case VAL_OBJ:
            //_serialise_write_object(serialiser, AS_OBJ(*value));
            _serialise_ptr(serialiser, AS_OBJ(*value));
            break;
        default:
            break;
    }

#if defined(SERIALISE_DEBUG)
    BLOCK_END();
    BLOCK_END();
#endif

}

const void* _serialise_buf_read(serialiser_buf_t* buffer, size_t length);
int _serialise_buf_read_int(serialiser_buf_t* buffer);
size_t _serialise_buf_read_long(serialiser_buf_t* buffer);
obj_t * _serialise_read_object(serialiser_t* serialiser);
void _serialise_read_ptr(serialiser_t* serialiser, obj_t ** ptr);

#if defined(SERIALISE_DEBUG)
#define READ_DEBUG(type, expected_size, actual_size) \
    INDENT(); \
    printf("reading <" type "> (expected: %lu actual: %lu bytes) at offset %lu\n", expected_size, actual_size, buffer->offset - actual_size);
#else
#define READ_DEBUG(type, expected_size, actual_size)
#endif

void _read_pod_type(serialiser_buf_t* buffer, const char * type, void* value, size_t length) {
    size_t read_length = _serialise_buf_read_long(buffer);
    
#if defined(SERIALISE_DEBUG)
    INDENT();
    printf("reading <%s> (expected: %lu actual: %lu bytes) at offset %lu\n", type, length, read_length, buffer->offset - read_length);
#endif

    if (read_length != length) {
        fprintf(stderr, "Serialiser buffer data error while reading <%s>.", type);
        exit(1);
    }

    memcpy(value, _serialise_buf_read(buffer, length), length);
}

const void* _serialise_buf_read(serialiser_buf_t* buffer, size_t length) {
    const void* result = &buffer->bytes[buffer->offset];
    if (buffer->offset + length > buffer->count) {
        fprintf(stderr, "Serialiser buffer read out of bounds.");
        exit(1);
    }
    buffer->offset += length;
    return result;
}

const void* _serialise_buf_read_bytes(serialiser_buf_t* buffer) {
    size_t length = _serialise_buf_read_long(buffer);
    if (length == 0) {
        return NULL;
    }
    uint8_t *bytes = ALLOCATE(uint8_t, length + 1);
    memcpy(bytes, _serialise_buf_read(buffer, length), length);
    return bytes;
}

int _serialise_buf_read_int(serialiser_buf_t* buffer) {
    int value;
    _read_pod_type(buffer, "int", &value, sizeof(int));
    return value;
}

size_t _serialise_buf_read_long(serialiser_buf_t* buffer) {
    size_t value;
    memcpy(&value, _serialise_buf_read(buffer, sizeof(size_t)), sizeof(size_t));
    READ_DEBUG("long", sizeof(size_t), sizeof(size_t))
    return value;
}

int * _serialise_buf_read_ints(serialiser_buf_t* buffer) {
    size_t bytes_length = _serialise_buf_read_long(buffer);
    
    if (bytes_length == 0) {
        READ_DEBUG("int * (empty)", bytes_length, bytes_length)
        return NULL;
    }

    // convert from bytes to length
    size_t length = bytes_length / sizeof(int);

    int *value = ALLOCATE(int, length + 1);
    memcpy(value, _serialise_buf_read(buffer, bytes_length), sizeof(int) * length);
    READ_DEBUG("int *", bytes_length, bytes_length)

    return value;
}

uint8_t _serialise_buf_read_uint8(serialiser_buf_t* buffer) {
    uint8_t value;
    _read_pod_type(buffer, "uint8_t", &value, sizeof(uint8_t));
    return value;
}

uint8_t* _serialise_buf_read_uint8s(serialiser_buf_t* buffer) {
    size_t bytes_length = _serialise_buf_read_long(buffer);
    
    if (bytes_length == 0) {
        READ_DEBUG("uint8 * (empty)", bytes_length, bytes_length)
        return NULL;
    }

    // convert from bytes to length
    size_t length = bytes_length / sizeof(uint8_t);

    uint8_t *value = ALLOCATE(uint8_t, length + 1);
    memcpy(value, _serialise_buf_read(buffer, bytes_length), sizeof(uint8_t) * length);

    READ_DEBUG("uint8 *", bytes_length, bytes_length)

    return value;
}

uint32_t _serialise_buf_read_uint32(serialiser_buf_t* buffer) {
    uint32_t value;
    _read_pod_type(buffer, "uint32_t", &value, sizeof(uint32_t));
    return value;
    
}

uintptr_t _serialise_buf_read_uintptr(serialiser_buf_t* buffer) {
    uintptr_t value;
    _read_pod_type(buffer, "uintptr_t", &value, sizeof(uintptr_t));
    return value;
}

double _serialise_buf_read_double(serialiser_buf_t* buffer) {
    double value;
    _read_pod_type(buffer, "double", &value, sizeof(double));
    return value;
}

obj_string_t * _serialise_buf_read_string(serialiser_buf_t* buffer) {

    _serialise_buf_read_uint32(buffer); // unused
    size_t length = _serialise_buf_read_long(buffer);

    if (length == 0) {
        READ_DEBUG("string (NULL)", length, length)
        return NULL;
    }

    // read the bytes from the file
    char * chars = (char*)_serialise_buf_read(buffer, length);

    READ_DEBUG("string", length, length)
#if defined(SERIALISE_DEBUG)
    INDENT();
    printf(" - %s\n", chars);
#endif
    // convert the length into characters from bytes
    length /= sizeof(char);

    // allocate a new string buffer
    char * copy_chars = ALLOCATE(char, length + 1);
    memcpy(copy_chars, chars, length);
    
    // insert the string into the strings table, will be freed if a) already exists or b) when the vm is freed
    obj_string_t * str = l_take_string(copy_chars, length);

    // register the string
    l_allocate_track_string_register(str->hash, str);

    return str;
}

char * _serialise_buf_read_string_char(serialiser_buf_t* buffer) {

    size_t bytes_length = _serialise_buf_read_long(buffer);

    if (bytes_length == 0) {
        READ_DEBUG("char * (empty)", bytes_length, bytes_length)
        return NULL;
    }

    char * chars = (char*)_serialise_buf_read(buffer, bytes_length);

    READ_DEBUG("char *", bytes_length, bytes_length)
#if defined(SERIALISE_DEBUG)
    printf("\t - %s\n", chars);
#endif

    // convert the length into characters from bytes
    size_t length = bytes_length / sizeof(char);

    // allocate a new string buffer
    char * copy_chars = ALLOCATE(char, length + 1);
    memcpy(copy_chars, chars, length);

    // return the pointer to the chars, up to the receiver to delete
    return copy_chars;
}

bool _serialise_buf_read_bool(serialiser_buf_t* buffer) {
    bool * read = (bool *)_serialise_buf_read(buffer, sizeof(bool));
    READ_DEBUG("bool", sizeof(bool), sizeof(bool))
    return *read == 1;
}

// read a value from the serialiser buffer
// the value is passed by reference and will be updated, so that the caller
// can provide a value for an object pointer to be corrected linked into
// at the end of deserilisation
void l_deserialise_value(serialiser_t* serialiser, value_t *value) {
    int type = _serialise_buf_read_int(serialiser->buffer);
#if defined(SERIALISE_DEBUG)
    BLOCK_START("Read VALUE: ");
    switch (type) {
        case VAL_BOOL:
            BLOCK_START("BOOL");
            break;
        case VAL_NIL:
            BLOCK_START("NIL");
            break;
        case VAL_NUMBER:
            BLOCK_START("NUMBER");
            break;
        case VAL_OBJ:
            BLOCK_START("OBJECT");
            break;
        default:
            BLOCK_START("UNKNOWN");
            break;
    }
#endif

    value_t read_value;
    switch (type) {
        case VAL_BOOL: {
            read_value = BOOL_VAL(_serialise_buf_read_bool(serialiser->buffer));
            value->type = VAL_BOOL;
            value->as.boolean = read_value.as.boolean;
            break;
        }
        case VAL_NUMBER: {
            read_value = NUMBER_VAL(_serialise_buf_read_double(serialiser->buffer));
            value->type = VAL_NUMBER;
            value->as.number = read_value.as.number;
            break;
        }
        case VAL_OBJ:
        {
            value->type = VAL_OBJ;
            obj_t ** ptr = &value->as.obj;
            _serialise_read_ptr(serialiser, ptr);
            break;
        }
        case VAL_NIL:
        // TODO: is this the correct default?
        default:
            value->type = VAL_NIL;
            value->as.boolean = false;
            break;
    }

#if defined(SERIALISE_DEBUG)
   BLOCK_END()
   BLOCK_END()
#endif
}

void _serialise_read_value_array(serialiser_t* serialiser, value_array_t * array) {

#if defined(SERIALISE_DEBUG)
    BLOCK_START("Read VALUE ARRAY: ");
#endif

    int count = _serialise_buf_read_int(serialiser->buffer);
    array->values = ALLOCATE(value_t, count);
    array->capacity = count;
    array->count = count;
    for (int i = 0; i < count; i++) {
        l_deserialise_value(serialiser, &array->values[i]);
    }

#if defined(SERIALISE_DEBUG)
   BLOCK_END()
#endif
}

void _serialise_read_chunk(serialiser_t* serialiser, chunk_t * chunk) {

#if defined(SERIALISE_DEBUG)
    BLOCK_START("Read CHUNK: ");
#endif

    chunk->count = _serialise_buf_read_int(serialiser->buffer);
    chunk->capacity = _serialise_buf_read_int(serialiser->buffer);
    chunk->code = (uint8_t*)_serialise_buf_read_bytes(serialiser->buffer);
    chunk->lines = _serialise_buf_read_ints(serialiser->buffer);
    _serialise_read_value_array(serialiser, &chunk->constants);
#if defined(SERIALISE_DEBUG)
   BLOCK_END()
#endif
}

void _serialise_read_ptr(serialiser_t* serialiser, obj_t ** ptr) {
    // register an interest in the name object when it's eventually created
    uintptr_t ptr_id = _serialise_buf_read_uintptr(serialiser->buffer);
    l_allocate_track_target_register(ptr_id, ptr);
}

void _serialise_read_str_ptr(serialiser_t* serialiser, obj_string_t ** ptr) {

    // register an interest in the string object when it's eventually created using its hash
    uint32_t hash = _serialise_buf_read_uint32(serialiser->buffer);

    // if the hash is zero, then the string pointer was NULL when serialised
    if (hash == 0) {
        *ptr = NULL;
        return;
    }
    l_allocate_track_string_target_register(hash, ptr);
}

void l_deserialise_table(serialiser_t* serialiser, table_t* table) {

#if defined(SERIALISE_DEBUG)
    BLOCK_START("Read TABLE {\n");
#endif

    int count = _serialise_buf_read_int(serialiser->buffer);

#if defined(SERIALISE_DEBUG)
    BLOCK_START("keys: {\n");
#endif
    for (int i = 0; i < count; i++) {
#if defined(SERIALISE_DEBUG)
        INDENT()
        printf(">\n");
#endif
        obj_string_t* key = _serialise_buf_read_string(serialiser->buffer);

        // TODO: This will totally break if the table contains a value that is a pointer to an object
        // e.g. a string. :'(
        value_t value;
        l_deserialise_value(serialiser, &value);

#if defined(SERIALISE_DEBUG)
        INDENT()
        printf("<\n");
#endif
        l_table_set(table, key, value);
    }

#if defined(SERIALISE_DEBUG)
    BLOCK_END()
    BLOCK_END()
#endif
}

obj_t * _serialise_read_object(serialiser_t* serialiser) {

    obj_t * result = NULL;

#if defined(SERIALISE_DEBUG)
    BLOCK_START("Read OBJECT");
#endif

    // read the uintptr_t id of the object
    uintptr_t obj_id = _serialise_buf_read_uintptr(serialiser->buffer);

    int type = _serialise_buf_read_int(serialiser->buffer);

#if defined(SERIALISE_DEBUG)
    switch (type)
    {
        case OBJ_BOUND_METHOD: {
            BLOCK_START("Read BOUND_METHOD - NYI");
            break;
        }
        case OBJ_CLASS: {
            BLOCK_START("Read CLASS");
            break;
        }
        case OBJ_CLOSURE: {
            BLOCK_START("Read CLOSURE");
            break;
        }
        case OBJ_FUNCTION: {
            BLOCK_START("Read FUNCTION");
            break;
        }
        case OBJ_INSTANCE: {
            BLOCK_START("Read INSTANCE");
            break;
        }
        case OBJ_NATIVE: {
            BLOCK_START("Read NATIVE");
            break;
        }
        case OBJ_STRING: {
            BLOCK_START("Read STRING");
            break;
        }
        case OBJ_UPVALUE: {
            BLOCK_START("Read UPVALUE - NYI");
            break;
        }
        case OBJ_TABLE: {
            BLOCK_START("Read TABLE");
            break;
        }
        case OBJ_ERROR: {
            BLOCK_START("Read ERROR");
            break;
        }
        default: {
            BLOCK_START("Read UNKNOWN - Error");
            break;
        }
    }
#endif

    switch (type) {
        case OBJ_BOUND_METHOD: {
            break;
        }
        case OBJ_CLASS: {
            // create a new class object without registering the name
            obj_class_t* klass = l_new_class(NULL);

            // register an interest in the name object when it's eventually created
            _serialise_read_str_ptr(serialiser, &klass->name);

            // deserialise the methods table
            l_deserialise_table(serialiser, &klass->methods);

            result = (obj_t*)klass;

            break;
        }
        case OBJ_CLOSURE: {
            // read the number of upvalues
            int upvalue_count = _serialise_buf_read_int(serialiser->buffer);

            // create a closure to store them
            obj_closure_t * closure = l_new_closure_empty(upvalue_count);
            
            // read their values
            for (int i = 0; i < upvalue_count; i++) {
                closure->upvalues[i] =(obj_upvalue_t*) _serialise_read_object(serialiser);
            }

            // track the pointer for the function the closure contains
            _serialise_read_ptr(serialiser, (obj_t **)&closure->function);

            result = (obj_t*)closure;
            break;
            
        }
        case OBJ_FUNCTION: {
            // create the new function object
            obj_function_t * func = l_new_function();

            // setup internals
            func->arity = _serialise_buf_read_int(serialiser->buffer);
            func->upvalue_count = _serialise_buf_read_int(serialiser->buffer);
            
            // read the function's bytecode
            _serialise_read_chunk(serialiser, &func->chunk);

            // register for the function name when it's created
            _serialise_read_str_ptr(serialiser, &func->name);

            result = (obj_t*)func;
            break;
        }
        case OBJ_INSTANCE: {
            obj_instance_t * instance = l_new_instance(NULL);

            // register an interest in the class object when it's eventually created
            _serialise_read_ptr(serialiser, (obj_t **)&instance->klass);

            // deserialise the fields table
            l_deserialise_table(serialiser, &instance->fields);

            result = (obj_t*)instance;
            break;
        }
        case OBJ_NATIVE: {
            char * func_name = (char *)_serialise_buf_read_string_char(serialiser->buffer);

            native_func_t * native_func = l_allocate_track_get_native_ptr(func_name);
            FREE(char, func_name);

            obj_native_t * native = l_new_native(*native_func);

            result = (obj_t*)native;

            break;
        }
        case OBJ_STRING: {
            obj_string_t * string = _serialise_buf_read_string(serialiser->buffer);

            result = (obj_t *)string;

            break;
        }
        case OBJ_UPVALUE: {
            break;
        }
        case OBJ_TABLE: {
            obj_table_t * table = l_new_table();
            l_deserialise_table(serialiser, &table->table);

            result = (obj_t *)table;
            break;
        }
        case OBJ_ERROR:
            break;
        default:
            break;
    }

#if defined(SERIALISE_DEBUG)
    BLOCK_END()
    BLOCK_END()
#endif

    // register the object with the id
    l_allocate_track_register(obj_id, (void *)(result));

    return result;
}



// Header
//

void _serialise_write_header(serialiser_t* serialiser, const char * filename_source, const char * source) {

#if defined(SERIALISE_DEBUG)
    BLOCK_START("Write Header:");
#endif

    // serialisation version
    _serialise_buf_write_int(serialiser->buffer, SERIALISATION_VERSION);
    // sox version
    _serialise_buf_write_string_char(serialiser->buffer, VERSION);
    // source filename
    _serialise_buf_write_string_char(serialiser->buffer, filename_source);
    // source hash
    uint32_t source_hash = l_hash_string(source, strlen(source));
    _serialise_buf_write_uint32(serialiser->buffer, source_hash);
    
#if defined(SERIALISE_DEBUG)
    BLOCK_END();
#endif

    // flush the header
    l_serialise_flush(serialiser);
}

void _serialise_read_header(serialiser_t* serialiser, const char * filename_source, const char * source) {

#if defined(SERIALISE_DEBUG)
    BLOCK_START("Read Header:");
#endif
    
    // read the serialisation version
    int serialisation_version = _serialise_buf_read_int(serialiser->buffer);
    if (serialisation_version != SERIALISATION_VERSION) {
        fprintf(stderr, "Serialisation version mismatch. Expected %d, got %d\n", SERIALISATION_VERSION, serialisation_version);
        serialiser->error = 1;
        return;
    }

    // read the sox version
    char * sox_version = (char *)_serialise_buf_read_string_char(serialiser->buffer);
    printf("sox version: %s\n", sox_version);
    if (strcmp(sox_version, VERSION) != 0) {
        fprintf(stderr, "Serialisation version mismatch. Expected %s, got %s\n", VERSION, sox_version);
        serialiser->error = SERIALISE_ERROR_SOX_VERSION_MISMATCH;
        FREE(char, sox_version);
        return;
    }
    FREE(char, sox_version);

    // read the source filename
    char * source_filename = (char *)_serialise_buf_read_string_char(serialiser->buffer);
    printf("source filename: %s\n", source_filename);

    if (strcmp(source_filename, filename_source) != 0) {
        fprintf(stderr, "Serialisation source filename mismatch. Expected %s, got %s\n", filename_source, source_filename);
        serialiser->error = SERIALISE_ERROR_SOURCE_FILENAME_MISMATCH;
        FREE(char, source_filename);
        return;
    }
    FREE(char, source_filename);

    // read the source hash
    uint32_t source_hash = _serialise_buf_read_uint32(serialiser->buffer);
    printf("source hash: %u\n", source_hash);

    uint32_t source_hash_expected = l_hash_string(source, strlen(source));

    if (source_hash != source_hash_expected) {
        fprintf(stderr, "Serialisation source hash mismatch. Expected %u, got %u\n", source_hash_expected, source_hash);
        serialiser->error = SERIALISE_ERROR_SOURCE_HASH_MISMATCH;
        return;
    }

#if defined(SERIALISE_DEBUG)
    BLOCK_END();
#endif
}


// VM
//


void l_serialise_vm_set_init_state(serialiser_t* serialiser) {
    serialiser->global_offset = vm.globals.count;
    serialiser->string_offset = vm.strings.count;
}

void l_serialise_vm(serialiser_t* serialiser) {

    // TODO: 
    // Will walking the interpreted structs from the closure entrypoint cover all code?
    // Pointers are used as IDs all over the place, this will not serialise..

    // l_serialise_table_offset(serialiser, &vm.globals, serialiser->global_offset);
    // l_serialise_table_offset(serialiser, &vm.strings, serialiser->string_offset);
    
#if defined(SERIALISE_DEBUG)
    BLOCK_START("Write VM START");
#endif

    // l_serialise_value(serialiser, vm.stack_top);
    obj_t * obj = l_get_objects();

    int objects_serialised = 0;

    // count the vm objects
    while (obj != NULL) {
        objects_serialised++;
        obj = obj->next;
    }

    // write the count to the serialised buffer
    _serialise_buf_write_int(serialiser->buffer, objects_serialised);

    // reset the obj pointer to the start of the list
    obj = l_get_objects();

    // let's walk the vm objects and serialise them all.
    while (obj != NULL) {
        l_serialise_obj(serialiser, obj);
        obj = obj->next;
    }

    printf("serialised %d objects\n", objects_serialised);

    // serialise the closure pointer on the top of the stack
    value_t * closure = vm.stack_top-1;
    _serialise_ptr(serialiser, AS_CLOSURE(*closure)->function);

    printf("serialisation complete.\n");

#if defined(SERIALISE_DEBUG)
    BLOCK_END();
#endif

}

void l_serialise_rewind(serialiser_t* serialiser) {
    serialiser->buffer->offset = 0;
}

obj_closure_t * l_deserialise_vm(serialiser_t* serialiser) {


#if defined(SERIALISE_DEBUG)
    BLOCK_START("Read VM START");
#endif

    // read the number of objects
    int objects_serialised_count = _serialise_buf_read_int(serialiser->buffer);

    int objects_deserialised = 0;

    // read each of the objects in the serialised buffer until the end of the buffer is reached
    for ( int i = 0; i < objects_serialised_count; i++ ) {
        _serialise_read_object(serialiser);
        
        if (serialiser->error != SERIALISE_OK) {
            break;
        }
        objects_deserialised++;
    }

    printf("deserialised %d objects\n", objects_deserialised);

    // push a the closure pointer onto the top of the stack
    obj_closure_t *closure = l_new_closure_empty(0);

    // setup the tracking for the closure function to the stack value
    _serialise_read_ptr(serialiser, (obj_t **)&closure->function);


#if defined(SERIALISE_DEBUG)
    BLOCK_END();
#endif


    return closure;
}

void l_deserialise_vm_set_init_state(serialiser_t* serialiser, obj_closure_t * entry_point) {
    l_set_entry_point(entry_point);
}

obj_t * l_deserialise_obj(serialiser_t* serialiser) {
    return _serialise_read_object(serialiser);
}


// public serialisation methods

void l_serialise_int(serialiser_t* serialiser, int value) {
    _serialise_buf_write_int(serialiser->buffer, value);
}

int l_deserialise_int(serialiser_t* serialiser) {
    return _serialise_buf_read_int(serialiser->buffer);
}

void l_serialise_ints(serialiser_t* serialiser, int * values, size_t count) {
    _serialise_buf_write_ints(serialiser->buffer, values, count);
}

int * l_deserialise_ints(serialiser_t* serialiser) {
    return _serialise_buf_read_ints(serialiser->buffer);
}

void l_serialise_uint8(serialiser_t* serialiser, uint8_t value) {
    _serialise_buf_write_uint8(serialiser->buffer, value);
}

uint8_t l_deserialise_uint8(serialiser_t* serialiser) {
    return _serialise_buf_read_uint8(serialiser->buffer);
}

void l_serialise_uint8s(serialiser_t* serialiser, uint8_t * values, size_t count) {
    _serialise_buf_write_uint8s(serialiser->buffer, values, count);
}

uint8_t * l_deserialise_uint8s(serialiser_t* serialiser) {
    return _serialise_buf_read_uint8s(serialiser->buffer);
}

void l_serialise_uint32(serialiser_t* serialiser, uint32_t value) {
    _serialise_buf_write_uint32(serialiser->buffer, value);
}

uint32_t l_deserialise_uint32(serialiser_t* serialiser) {
    return _serialise_buf_read_uint32(serialiser->buffer);
}

void l_serialise_uintptr(serialiser_t* serialiser, uintptr_t value) {
    _serialise_buf_write_uintptr(serialiser->buffer, value);
}

uintptr_t l_deserialise_uintptr(serialiser_t* serialiser) {
    return _serialise_buf_read_uintptr(serialiser->buffer);
}

void l_serialise_long(serialiser_t* serialiser, size_t value) {
    _serialise_buf_write_long(serialiser->buffer, value);
}

size_t l_deserialise_long(serialiser_t* serialiser) {
    return _serialise_buf_read_long(serialiser->buffer);
}

void l_serialise_double(serialiser_t* serialiser, double value) {
    _serialise_buf_write_double(serialiser->buffer, value);
}

double l_deserialise_double(serialiser_t* serialiser) {
    return _serialise_buf_read_double(serialiser->buffer);
}

void l_serialise_bool(serialiser_t* serialiser, bool value) {
    _serialise_buf_write_bool(serialiser->buffer, value);
}

bool l_deserialise_bool(serialiser_t* serialiser) {
    return _serialise_buf_read_bool(serialiser->buffer);
}

void l_serialise_char(serialiser_t* serialiser, const char * value) {
    _serialise_buf_write_string_char(serialiser->buffer, value);
}

char * l_deserialise_char(serialiser_t* serialiser) {
    return _serialise_buf_read_string_char(serialiser->buffer);
}
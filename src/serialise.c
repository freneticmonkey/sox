
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

//#include "object.h"
#include "serialise.h"
#include "lib/memory.h"
#include "lib/string.h"
#include "version.h"

#define SERIALISATION_VERSION 1

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

void _serialise_buf_write_int(serialiser_buf_t* buffer, int value) {
    _serialise_buf_write(buffer, &value, sizeof(int));
}

void _serialise_buf_write_bytes(serialiser_buf_t* buffer, const void* bytes, int count) {
    _serialise_buf_write_int(buffer, count);
    _serialise_buf_write(buffer, bytes, count);
}

void _serialise_buf_write_ints(serialiser_buf_t* buffer, int *values, int count) {
    _serialise_buf_write_bytes(buffer, values, sizeof(int) * count);
}

void _serialise_buf_write_uint8(serialiser_buf_t* buffer, int value) {
    _serialise_buf_write_bytes(buffer, &value, sizeof(uint8_t));
}

void _serialise_buf_write_uint8s(serialiser_buf_t* buffer, uint8_t *values, int count) {
    _serialise_buf_write_bytes(buffer, values, sizeof(uint8_t) * count);
}

void _serialise_buf_write_uint32(serialiser_buf_t* buffer, uint32_t value) {
    _serialise_buf_write_bytes(buffer, &value, sizeof(uint32_t));
}

void _serialise_buf_write_uintptr(serialiser_buf_t* buffer, uintptr_t value) {

#if defined(LINK_DEBUGGING)
    printf("serialising ptr: %lu\n", value);
#endif

    _serialise_buf_write_bytes(buffer, &value, sizeof(uintptr_t));
}

void _serialise_buf_write_double(serialiser_buf_t* buffer, double value) {
    _serialise_buf_write_bytes(buffer, &value, sizeof(double));
}

void _serialise_buf_write_bool(serialiser_buf_t* buffer, bool value) {
    _serialise_buf_write(buffer, &value, sizeof(bool));
}

void _serialise_buf_write_string(serialiser_buf_t* buffer, obj_string_t* string) {
    _serialise_buf_write_uint8(buffer, string->hash);
    _serialise_buf_write_bytes(buffer, string->chars, sizeof(char) * string->length);
}

void _serialise_buf_write_string_char(serialiser_buf_t* buffer, const char *string) {
    _serialise_buf_write_bytes(buffer, string, sizeof(char) * strlen(string));
}

void _serialise_buf_write_file(serialiser_buf_t* buffer, FILE* file) {
    fwrite(buffer->bytes, sizeof(uint8_t), buffer->count, file);
}

void _serialise_value_array(serialiser_t* serialiser, value_array_t* array) {
    _serialise_buf_write_int(serialiser->buffer, array->count);
    for (int i = 0; i < array->count; i++) {
        l_serialise_value(serialiser, &array->values[i]);
    }
}

void _serialise_chunk(serialiser_t* serialiser, chunk_t* chunk) {
    _serialise_buf_write_int(serialiser->buffer, chunk->count);
    _serialise_buf_write_int(serialiser->buffer, chunk->capacity);
    _serialise_buf_write_bytes(serialiser->buffer, chunk->code, chunk->count);
    _serialise_buf_write_ints(serialiser->buffer, chunk->lines, chunk->count);
    _serialise_value_array(serialiser, &chunk->constants);
}

void _serialise_ptr(serialiser_t* serialiser, void* ptr) {
    _serialise_buf_write_uintptr(serialiser->buffer, (uintptr_t)ptr);
}

void _serialise_string_ptr(serialiser_t* serialiser, obj_string_t* string) {
    _serialise_buf_write_uint32(serialiser->buffer, string->hash);
}

void _serialise_write_object(serialiser_t* serialiser, obj_t* object) {

    printf("{\n");
    // write the uintptr_t of the object so that it can be tracked when deserialised
    _serialise_ptr(serialiser, object);

    switch (object->type) {

        case OBJ_BOUND_METHOD: {
            printf("Serialising bound method?\n");
            // _serialise_buf_write_int(serialiser->buffer, OBJ_BOUND_METHOD);
            
            // obj_bound_method_t* bound_method = (obj_bound_method_t*)object;
            // _serialise_ptr(serialiser->buffer, (obj_t*)bound_method->receiver);
            // _serialise_ptr(serialiser->buffer, (obj_t*)bound_method->method);
            break;
        }

        case OBJ_CLASS: {
            printf("Serialising class\n");
            _serialise_buf_write_int(serialiser->buffer, OBJ_CLASS);
            
            obj_class_t* class = (obj_class_t*)object;
            _serialise_string_ptr(serialiser, class->name);
            l_serialise_table(serialiser, &class->methods);
            break;
        }

        case OBJ_CLOSURE: {
            printf("Serialising closure\n");

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
            printf("Serialising function\n");
            _serialise_buf_write_int(serialiser->buffer, OBJ_FUNCTION);

            obj_function_t* function = (obj_function_t*)object;
            _serialise_buf_write_int(serialiser->buffer, function->arity);
            _serialise_buf_write_int(serialiser->buffer, function->upvalue_count);
            _serialise_chunk(serialiser, &function->chunk);
            _serialise_string_ptr(serialiser, function->name);
            break;
        }
        case OBJ_INSTANCE: {
            printf("Serialising instance\n");
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
            printf("serialising string\n");
            _serialise_buf_write_int(serialiser->buffer, OBJ_STRING);

            obj_string_t* string = (obj_string_t*)object;
            _serialise_buf_write_string(serialiser->buffer, string);
            break;
        }
        case OBJ_UPVALUE: {
            printf("serialising upvalue?\n");
            // _serialise_buf_write_int(serialiser->buffer, OBJ_UPVALUE);

            // obj_upvalue_t* upvalue = (obj_upvalue_t*)object;
            // _serialise_ptr(serialiser, upvalue->location);
            break;
        }
        case OBJ_TABLE: {
            printf("serialising table\n");
            _serialise_buf_write_int(serialiser->buffer, OBJ_TABLE);

            obj_table_t* table = (obj_table_t*)object;
            l_serialise_table(serialiser, &table->table);
            break;
        }
        case OBJ_ERROR:
            printf("serialising error\n");
            break;
        default:
            printf("serialising - ignoring unknown\n");
            serialiser->error = SERIALISE_ERROR_UNKNOWN_OBJECT_TYPE;
            break;
    }

    l_serialise_flush(serialiser);
    printf("}\n");
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
    
    int flush_length = serialiser->buffer->count - serialiser->flush_offset;

    fwrite(&serialiser->buffer->bytes[serialiser->flush_offset], sizeof(uint8_t), flush_length, serialiser->file);
    serialiser->flush_offset = serialiser->buffer->count;
    
    fflush(serialiser->file);
}

void l_serialise_obj(serialiser_t* serialiser, obj_t* object) {
    _serialise_write_object(serialiser, object);
}

void l_serialise_table(serialiser_t* serialiser, table_t* table) {
    _serialise_buf_write_int(serialiser->buffer, table->count);
    for (int i = 0; i < table->capacity; i++) {
        entry_t* entry = &table->entries[i];
        if (entry->key == NULL) {
            continue;
        }
        _serialise_buf_write_string(serialiser->buffer, entry->key);
        l_serialise_value(serialiser, &entry->value);
    }
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
}

int _serialise_buf_read_int(serialiser_buf_t* buffer);
obj_t * _serialise_read_object(serialiser_t* serialiser);
void _serialise_read_ptr(serialiser_t* serialiser, void ** ptr);

#define READ_POD_TYPE(type) \
    type value; \
    memcpy(&value, _serialise_buf_read_bytes(buffer), sizeof(type)); \
    return value;

const void* _serialise_buf_read(serialiser_buf_t* buffer, int length) {
    const void* result = &buffer->bytes[buffer->offset];
    if (buffer->offset + length > buffer->count) {
        fprintf(stderr, "Serialiser buffer read out of bounds.");
        exit(1);
    }
    buffer->offset += length;
    return result;
}

const void* _serialise_buf_read_bytes(serialiser_buf_t* buffer) {
    int length = _serialise_buf_read_int(buffer);
    uint8_t *bytes = ALLOCATE(uint8_t, length + 1);
    memcpy(bytes, _serialise_buf_read(buffer, length), length);
    return bytes;
}

int _serialise_buf_read_int(serialiser_buf_t* buffer) {
    int *read = _serialise_buf_read(buffer, sizeof(int));
    return *read;
}

int * _serialise_buf_read_ints(serialiser_buf_t* buffer) {
    int bytes_length = _serialise_buf_read_int(buffer);
    
    // convert from bytes to length
    int length = bytes_length / sizeof(int);

    int *value = ALLOCATE(int, length + 1);
    memcpy(value, _serialise_buf_read(buffer, bytes_length), sizeof(int) * length);
    return value;
}

uint8_t _serialise_buf_read_uint8(serialiser_buf_t* buffer) {
    READ_POD_TYPE(uint8_t)
}

uint8_t* _serialise_buf_read_uint8s(serialiser_buf_t* buffer) {
    int bytes_length = _serialise_buf_read_int(buffer);
    
    // convert from bytes to length
    int length = bytes_length / sizeof(uint8_t);

    uint8_t *value = ALLOCATE(uint8_t, length + 1);
    memcpy(value, _serialise_buf_read(buffer, bytes_length), sizeof(uint8_t) * length);
    return value;
}

uint32_t _serialise_buf_read_uint32(serialiser_buf_t* buffer) {
    READ_POD_TYPE(uint32_t)
}

uintptr_t _serialise_buf_read_uintptr(serialiser_buf_t* buffer) {
    READ_POD_TYPE(uintptr_t)
}

double _serialise_buf_read_double(serialiser_buf_t* buffer) {
    READ_POD_TYPE(double)
}

obj_string_t * _serialise_buf_read_string(serialiser_buf_t* buffer) {

    uint8_t hash = _serialise_buf_read_uint8(buffer);
    int length = _serialise_buf_read_int(buffer);
    // read the bytes from the file
    char * chars = (char*)_serialise_buf_read(buffer, length);

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

const char * _serialise_buf_read_string_char(serialiser_buf_t* buffer) {

    int bytes_length = _serialise_buf_read_int(buffer);
    char * chars = (char*)_serialise_buf_read(buffer, bytes_length);

    // convert the length into characters from bytes
    int length = bytes_length / sizeof(char);

    // allocate a new string buffer
    char * copy_chars = ALLOCATE(char, length + 1);
    memcpy(copy_chars, chars, length);

    // return the pointer to the chars, up to the receiver to delete
    return copy_chars;
}

bool _serialise_buf_read_bool(serialiser_buf_t* buffer) {
    return _serialise_buf_read(buffer, sizeof(bool)) == 1;
}

value_t l_deserialise_value(serialiser_t* serialiser) {
    int type = _serialise_buf_read_int(serialiser->buffer);
    switch (type) {
        case VAL_BOOL:
            return BOOL_VAL(_serialise_buf_read_bool(serialiser->buffer));
        case VAL_NIL:
            return NIL_VAL;
        case VAL_NUMBER:
            return NUMBER_VAL(_serialise_buf_read_double(serialiser->buffer));
        case VAL_OBJ:
        {
            value_t obj;
            _serialise_read_ptr(serialiser, (void**)&obj.as.obj);
            return obj;
        }
        default:
            break;
    }
}

void _serialise_read_value_array(serialiser_t* serialiser, value_array_t * array) {
    int count = _serialise_buf_read_int(serialiser->buffer);
    array->values = ALLOCATE(value_t, count);
    array->capacity = count;
    array->count = count;
    for (int i = 0; i < count; i++) {
        array->values[i] = l_deserialise_value(serialiser);
    }
}

void _serialise_read_chunk(serialiser_t* serialiser, chunk_t * chunk) {
    chunk->count = _serialise_buf_read_int(serialiser->buffer);
    chunk->capacity = _serialise_buf_read_int(serialiser->buffer);
    chunk->code = _serialise_buf_read_bytes(serialiser->buffer);
    chunk->lines = _serialise_buf_read_ints(serialiser->buffer);
    _serialise_read_value_array(serialiser, &chunk->constants);
}

void _serialise_read_ptr(serialiser_t* serialiser, void ** ptr) {
    // register an interest in the name object when it's eventually created
    uintptr_t ptr_id = _serialise_buf_read_uintptr(serialiser->buffer);
    l_allocate_track_target_register(ptr_id, ptr);
}

void _serialise_read_str_ptr(serialiser_t* serialiser, obj_string_t ** ptr) {

    // register an interest in the string object when it's eventually created using its hash
    uint32_t hash = _serialise_buf_read_uint32(serialiser->buffer);
    l_allocate_track_string_target_register(hash, ptr);
}

void l_deserialise_table(serialiser_t* serialiser, table_t* table) {
    int count = _serialise_buf_read_int(serialiser->buffer);
    for (int i = 0; i < count; i++) {
        obj_string_t* key = _serialise_buf_read_string(serialiser->buffer);
        value_t value = l_deserialise_value(serialiser);
        l_table_set(table, key, value);
    }
}

obj_t * _serialise_read_object(serialiser_t* serialiser) {

    obj_t * result = NULL;

    // read the uintptr_t id of the object
    uintptr_t obj_id = _serialise_buf_read_uintptr(serialiser->buffer);

    int type = _serialise_buf_read_int(serialiser->buffer);
    switch (type) {
        case OBJ_BOUND_METHOD:
            printf("read bound method\n");
            // _serialise_read_bound_method(serialiser);
            break;
        case OBJ_CLASS:
            printf("read class\n");
            // _serialise_read_class(serialiser);

            // create a new class object without registering the name
            obj_class_t* klass = l_new_class(NULL);

            // register an interest in the name object when it's eventually created
            _serialise_read_str_ptr(serialiser, &klass->name);

            // deserialise the methods table
            l_deserialise_table(serialiser, &klass->methods);

            result = (obj_t*)klass;

            break;
        case OBJ_CLOSURE:
            printf("read closure\n");
            
            // read the number of upvalues
            int upvalue_count = _serialise_buf_read_int(serialiser->buffer);

            // create a closure to store them
            obj_closure_t * closure = l_new_closure_empty(upvalue_count);
            
            // read their values
            for (int i = 0; i < upvalue_count; i++) {
                closure->upvalues[i] = _serialise_read_object(serialiser);
            }

            // track the pointer for the function the closure contains
            _serialise_read_ptr(serialiser, (void**)&closure->function);

            result = (obj_t*)closure;
            break;
            
        case OBJ_FUNCTION:
            printf("read function\n");
            
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
        case OBJ_INSTANCE:
            printf("read instance\n");
            
            obj_instance_t * instance = l_new_instance(NULL);

            // register an interest in the class object when it's eventually created
            _serialise_read_ptr(serialiser, (void**)&instance->klass);

            // deserialise the fields table
            l_deserialise_table(serialiser, &instance->fields);

            result = (obj_t*)instance;
            break;
        case OBJ_NATIVE:
            printf("read native\n");
            
            char * func_name = _serialise_buf_read_string_char(serialiser->buffer);

            native_func_t * native_func = l_allocate_track_get_native_ptr(func_name);
            FREE(char, func_name);

            obj_native_t * native = l_new_native(native_func);

            result = (obj_t*)native;

            break;
        case OBJ_STRING:
            printf("read string\n");
            
            obj_string_t * string = _serialise_buf_read_string(serialiser->buffer);

            result = (obj_t *)string;

            break;
        case OBJ_UPVALUE:
            printf("read upvalue\n");
            // _serialise_read_upvalue(serialiser);
            break;

        case OBJ_TABLE:
            printf("read table\n");
            obj_table_t * table = l_new_table();
            l_deserialise_table(serialiser, table);

            result = (obj_t *)table;
            break;
        case OBJ_ERROR:
            printf("read error\n");
            break;
        default:
            break;
    }

    // register the object with the id
    l_allocate_track_register(obj_id, result);

    return result;
}



// Header
//

void _serialise_write_header(serialiser_t* serialiser, const char * filename_source, const char * source) {
    // serialisation version
    _serialise_buf_write_int(serialiser->buffer, SERIALISATION_VERSION);
    // sox version
    _serialise_buf_write_string_char(serialiser->buffer, VERSION);
    // source filename
    _serialise_buf_write_string_char(serialiser->buffer, filename_source);
    // source hash
    uint32_t source_hash = l_hash_string(source, (int)strlen(source));
    _serialise_buf_write_uint32(serialiser->buffer, source_hash);
    
    // flush the header
    l_serialise_flush(serialiser);
}

void _serialise_read_header(serialiser_t* serialiser, const char * filename_source, const char * source) {

    // read the serialisation version
    int serialisation_version = _serialise_buf_read_int(serialiser->buffer);
    if (serialisation_version != SERIALISATION_VERSION) {
        fprintf(stderr, "Serialisation version mismatch. Expected %d, got %d\n", SERIALISATION_VERSION, serialisation_version);
        serialiser->error = 1;
        return;
    }

    // read the sox version
    char * sox_version = _serialise_buf_read_string_char(serialiser->buffer);
    printf("sox version: %s\n", sox_version);
    if (strcmp(sox_version, VERSION) != 0) {
        fprintf(stderr, "Serialisation version mismatch. Expected %s, got %s\n", VERSION, sox_version);
        serialiser->error = SERIALISE_ERROR_SOX_VERSION_MISMATCH;
        FREE(char, sox_version);
        return;
    }
    FREE(char, sox_version);

    // read the source filename
    char * source_filename = _serialise_buf_read_string_char(serialiser->buffer);
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

    uint32_t source_hash_expected = l_hash_string(source, (int)strlen(source));

    if (source_hash != source_hash_expected) {
        fprintf(stderr, "Serialisation source hash mismatch. Expected %u, got %u\n", source_hash_expected, source_hash);
        serialiser->error = SERIALISE_ERROR_SOURCE_HASH_MISMATCH;
        return;
    }
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
}

void l_serialise_rewind(serialiser_t* serialiser) {
    serialiser->buffer->offset = 0;
}

obj_closure_t * l_deserialise_vm(serialiser_t* serialiser) {

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
    _serialise_read_ptr(serialiser, (void**)&closure->function);

    return closure;
}

void l_deserialise_vm_set_init_state(serialiser_t* serialiser, obj_closure_t * entry_point) {
    l_push(OBJ_VAL(entry_point));
}

obj_t * l_deserialise_obj(serialiser_t* serialiser) {
    return _serialise_read_object(serialiser);
}
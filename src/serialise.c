
#include <stdio.h>
#include <string.h>

//#include "object.h"
#include "serialise.h"
#include "lib/memory.h"
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
        default: return "Unknown error.";
    }
}

typedef struct serialiser_buf_t {
    size_t count;
    size_t capacity;
    uint8_t* bytes;
} serialiser_buf_t;

serialiser_buf_t * _serialise_buf_new() {

    serialiser_buf_t * buffer = ALLOCATE(serialiser_buf_t, 1);

    buffer->count = 0;
    buffer->capacity = 0;
    buffer->bytes = NULL;

    return buffer;
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

void _serialise_buf_write_int(serialiser_buf_t* buffer, int value) {
    _serialise_buf_write(buffer, &value, sizeof(int));
}

void _serialise_buf_write_uintptr(serialiser_buf_t* buffer, uintptr_t value) {
    _serialise_buf_write(buffer, &value, sizeof(uintptr_t));
}

void _serialise_buf_write_bytes(serialiser_buf_t* buffer, const void* bytes, int count) {
    _serialise_buf_write_int(buffer, count);
    _serialise_buf_write(buffer, bytes, count);
}

void _serialise_buf_write_ints(serialiser_buf_t* buffer, int *values, int count) {
    _serialise_buf_write_bytes(buffer, values, count);
}

void _serialise_buf_write_uint8(serialiser_buf_t* buffer, int value) {
    _serialise_buf_write(buffer, &value, sizeof(uint8_t));
}

void _serialise_buf_write_uint8s(serialiser_buf_t* buffer, uint8_t *values, int count) {
    _serialise_buf_write_bytes(buffer, values, count);
}

void _serialise_buf_write_double(serialiser_buf_t* buffer, double value) {
    _serialise_buf_write(buffer, &value, sizeof(double));
}

void _serialise_buf_write_bool(serialiser_buf_t* buffer, bool value) {
    _serialise_buf_write(buffer, &value, sizeof(bool));
}

void _serialise_buf_write_string(serialiser_buf_t* buffer, obj_string_t* string) {
    _serialise_buf_write_uint8(buffer, string->hash);
    _serialise_buf_write_bytes(buffer, string->chars, string->length);
}

void _serialise_buf_write_string_char(serialiser_buf_t* buffer, const char *string) {
    size_t string_len = strlen(string);
    _serialise_buf_write_int(buffer, (int)string_len);
    _serialise_buf_write_bytes(buffer, string, string_len);
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

void _serialise_write_object(serialiser_t* serialiser, obj_t* object) {
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
            _serialise_ptr(serialiser, class->name);
            l_serialise_table(serialiser, &class->methods);
            break;
        }

        case OBJ_CLOSURE: {
            printf("Serialising closure\n");

            _serialise_buf_write_int(serialiser->buffer, OBJ_CLOSURE);
            
            obj_closure_t* closure = (obj_closure_t*)object;
            _serialise_ptr(serialiser, closure->function);

            _serialise_buf_write_int(serialiser->buffer, closure->upvalue_count);
            for (int i = 0; i < closure->upvalue_count; i++) {

                _serialise_write_object(serialiser, (obj_t*)closure->upvalues[i]);
            }
            break;
        }
        case OBJ_FUNCTION: {
            printf("Serialising function\n");
            _serialise_buf_write_int(serialiser->buffer, OBJ_FUNCTION);

            obj_function_t* function = (obj_function_t*)object;
            _serialise_buf_write_int(serialiser->buffer, function->arity);
            _serialise_buf_write_int(serialiser->buffer, function->upvalue_count);
            _serialise_chunk(serialiser, &function->chunk);
            _serialise_ptr(serialiser, function->name);
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
            _serialise_buf_write_bytes(serialiser->buffer, name, (int)strlen(name));

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
        default:
            break;
    }

    l_serialise_flush(serialiser);
}

serialiser_t * l_serialise_new(const char * filename_source) {

    // generate a bytecode filename from the source filename
    char filename_bytecode[256];
    sprintf(&filename_bytecode[0], "%s.sbc", filename_source);
    
    FILE * file = fopen(&filename_bytecode[0], "wb");

    if (file == NULL) {
        fprintf(stderr, "Could not open file \"%s\" for writing.", filename_bytecode);
        return NULL;
    }

    serialiser_t * serialiser = ALLOCATE(serialiser_t, 1);
    
    serialiser->buffer = NULL;
    serialiser->file = file;
    serialiser->error = 0;
    serialiser->global_offset = 0;
    serialiser->string_offset = 0;
    serialiser->flush_offset = 0;

    serialiser->buffer = _serialise_buf_new();

    // write the header

    // serialisation version
    _serialise_buf_write_int(serialiser->buffer, SERIALISATION_VERSION);
    // sox version
    _serialise_buf_write_string_char(serialiser->buffer, VERSION);
    // source filename
    _serialise_buf_write_string_char(serialiser->buffer, filename_source);
    // source hash
    _serialise_buf_write_string_char(serialiser->buffer, "<source_hash>");
    
    // flush the header
    l_serialise_flush(serialiser);

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
    obj_t * obj = vm.objects;

    // let's walk the vm objects and serialise them all.
    while (obj != NULL) {
        l_serialise_obj(serialiser, obj);
        obj = obj->next;
    }
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
            _serialise_write_object(serialiser, AS_OBJ(*value));
            break;
        default:
            break;
    }
}

// void l_deserialise_chunk(chunk_t* chunk, FILE * file) {
//     // Read the chunk from file
//     int capacity = 0;

//     // the count can be read into the chunk directly
//     fread(&chunk->count, sizeof(int), 1, file);

//     // read the capacity into a separate variable
//     fread(&capacity, sizeof(int), 1, file);
    
//     // ensure there is enough space in the chunk for the code and line data
//     if (capacity < chunk->count + 1) {
//         int oldCapacity = chunk->capacity;
//         chunk->capacity = GROW_CAPACITY(oldCapacity);
//         chunk->code = GROW_ARRAY(uint8_t, chunk->code, oldCapacity, capacity);
//         chunk->lines = GROW_ARRAY(int, chunk->lines, oldCapacity, capacity);
//     }

//     // now read the code and line data into the chunk
//     fread(&chunk->code, sizeof(uint8_t), chunk->count, file);
//     fread(&chunk->lines, sizeof(int), chunk->count, file);

//     // now deserialise the constants
//     l_deserialise_value_array(&chunk->constants, file);
// }



// void l_serialise_value_array(value_array_t* array, FILE * file) {
//     fwrite(&array->count, sizeof(int), 1, file);
//     fwrite(&array->capacity, sizeof(int), 1, file);
//     fwrite(&array->values, sizeof(value_t), array->count, file);
// }

// void l_deserialise_value_array(value_array_t* array, FILE * file) {
//     fread(&array->count, sizeof(int), 1, file);
//     fread(&array->capacity, sizeof(int), 1, file);
//     array->values = GROW_ARRAY(value_t, array->values, 0, array->capacity);
//     fread(&array->values, sizeof(value_t), array->count, file);
// }


// void l_serialise_bytecode(const obj_function_t* function, const char * filename) {
//     FILE * file = fopen(filename, "wb");
//     if (file == NULL) {
//         fprintf(stderr, "Could not open file \"%s\" for writing.", filename);
//         return;
//     }

//     fwrite(&function->arity, sizeof(int), 1, file);
//     fwrite(&function->upvalue_count, sizeof(int), 1, file);

//     l_serialise_chunk(&function->chunk, file);
// }

// void l_deserialise_bytecode(obj_function_t* function, const char * filename) {
//     FILE * file = fopen(filename, "rb");
//     if (file == NULL) {
//         fprintf(stderr, "Could not open file \"%s\" for reading.", filename);
//         return;
//     }

//     fread(&function->arity, sizeof(int), 1, file);
//     fread(&function->upvalue_count, sizeof(int), 1, file);

//     l_deserialise_chunk(&function->chunk, file);
// }
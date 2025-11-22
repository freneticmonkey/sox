#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "wat_generator.h"
#include "lib/memory.h"
#include "chunk.h"
#include "value.h"

typedef struct wat_generator_t {
    char* filename_source;
    char* filename_wat;
    char* output_buffer;
    size_t buffer_size;
    size_t buffer_capacity;
    WatErrorCode error;
} wat_generator_t;

const char * l_wat_get_error_string(WatErrorCode error) {
    switch (error) {
        case WAT_OK: return "No error.";
        case WAT_ERROR: return "An error occurred.";
        case WAT_ERROR_FILE_NOT_OPEN: return "File could not be opened.";
        case WAT_ERROR_FILE_NOT_WRITTEN: return "File could not be written.";
        case WAT_ERROR_UNSUPPORTED_OPCODE: return "Unsupported opcode for WAT generation.";
        case WAT_ERROR_INVALID_VALUE_TYPE: return "Invalid value type for WAT generation.";
        default: return "Unknown error.";
    }
}

static void _wat_append(wat_generator_t* generator, const char* text) {
    size_t text_len = strlen(text);
    size_t needed_capacity = generator->buffer_size + text_len + 1;
    
    if (needed_capacity > generator->buffer_capacity) {
        generator->buffer_capacity = needed_capacity * 2;
        generator->output_buffer = (char*)realloc(generator->output_buffer, generator->buffer_capacity);
    }
    
    strcat(generator->output_buffer + generator->buffer_size, text);
    generator->buffer_size += text_len;
}

static void _wat_append_int(wat_generator_t* generator, int value) {
    char buffer[32];
    sprintf(buffer, "%d", value);
    _wat_append(generator, buffer);
}

static void _wat_append_double(wat_generator_t* generator, double value) {
    char buffer[64];
    sprintf(buffer, "%.6f", value);
    _wat_append(generator, buffer);
}

wat_generator_t * l_wat_new(const char * filename_source) {
    wat_generator_t* generator = (wat_generator_t*)malloc(sizeof(wat_generator_t));
    
    // Create WAT filename from source filename
    size_t source_len = strlen(filename_source);
    generator->filename_source = (char*)malloc(source_len + 1);
    strcpy(generator->filename_source, filename_source);
    
    generator->filename_wat = (char*)malloc(source_len + 5); // +4 for ".wat" +1 for null terminator
    strcpy(generator->filename_wat, filename_source);
    strcat(generator->filename_wat, ".wat");
    
    // Initialize output buffer
    generator->buffer_capacity = 1024;
    generator->output_buffer = (char*)malloc(generator->buffer_capacity);
    generator->output_buffer[0] = '\0';
    generator->buffer_size = 0;
    generator->error = WAT_OK;
    
    return generator;
}

void l_wat_del(wat_generator_t* generator) {
    if (generator) {
        free(generator->filename_source);
        free(generator->filename_wat);
        free(generator->output_buffer);
        free(generator);
    }
}

static WatErrorCode _wat_generate_module_header(wat_generator_t* generator) {
    _wat_append(generator, "(module\n");
    
    // Import print function from host environment
    _wat_append(generator, "  (import \"env\" \"print_f64\" (func $print_f64 (param f64)))\n");
    _wat_append(generator, "  (import \"env\" \"print_str\" (func $print_str (param i32 i32)))\n");
    
    // Define memory for string storage
    _wat_append(generator, "  (memory (export \"memory\") 1)\n");
    
    return WAT_OK;
}

static WatErrorCode _wat_generate_value(wat_generator_t* generator, value_t value) {
    if (IS_BOOL(value)) {
        _wat_append(generator, "    f64.const ");
        _wat_append_double(generator, AS_BOOL(value) ? 1.0 : 0.0);
        _wat_append(generator, "\n");
    } else if (IS_NIL(value)) {
        _wat_append(generator, "    f64.const 0.0\n");
    } else if (IS_NUMBER(value)) {
        _wat_append(generator, "    f64.const ");
        _wat_append_double(generator, AS_NUMBER(value));
        _wat_append(generator, "\n");
    } else if (IS_STRING(value)) {
        // For now, we'll store strings as constants and print them via imported function
        obj_string_t* string = AS_STRING(value);
        _wat_append(generator, "    ;; String constant: \"");
        _wat_append(generator, string->chars);
        _wat_append(generator, "\"\n");
        _wat_append(generator, "    i32.const 0  ;; string pointer placeholder\n");
        _wat_append(generator, "    i32.const ");
        _wat_append_int(generator, string->length);
        _wat_append(generator, "  ;; string length\n");
    } else {
        generator->error = WAT_ERROR_INVALID_VALUE_TYPE;
        return WAT_ERROR_INVALID_VALUE_TYPE;
    }
    
    return WAT_OK;
}

static WatErrorCode _wat_generate_instruction(wat_generator_t* generator, chunk_t* chunk, int* ip) {
    uint8_t instruction = chunk->code[*ip];
    
    switch (instruction) {
        case OP_CONSTANT: {
            (*ip)++;
            uint8_t constant_index = chunk->code[*ip];
            value_t constant = chunk->constants.values[constant_index];
            return _wat_generate_value(generator, constant);
        }
        case OP_NIL:
            _wat_append(generator, "    f64.const 0.0\n");
            break;
        case OP_TRUE:
            _wat_append(generator, "    f64.const 1.0\n");
            break;
        case OP_FALSE:
            _wat_append(generator, "    f64.const 0.0\n");
            break;
        case OP_ADD:
            _wat_append(generator, "    f64.add\n");
            break;
        case OP_SUBTRACT:
            _wat_append(generator, "    f64.sub\n");
            break;
        case OP_MULTIPLY:
            _wat_append(generator, "    f64.mul\n");
            break;
        case OP_DIVIDE:
            _wat_append(generator, "    f64.div\n");
            break;
        case OP_NEGATE:
            _wat_append(generator, "    f64.neg\n");
            break;
        case OP_PRINT:
            _wat_append(generator, "    call $print_f64\n");
            break;
        case OP_POP:
            _wat_append(generator, "    drop\n");
            break;
        case OP_RETURN:
            _wat_append(generator, "    return\n");
            break;
        default:
            generator->error = WAT_ERROR_UNSUPPORTED_OPCODE;
            return WAT_ERROR_UNSUPPORTED_OPCODE;
    }
    
    return WAT_OK;
}

WatErrorCode l_wat_generate_from_function(wat_generator_t* generator, obj_function_t* function) {
    WatErrorCode result;
    
    // Generate module header
    result = _wat_generate_module_header(generator);
    if (result != WAT_OK) return result;
    
    // Start main function
    _wat_append(generator, "  (func (export \"main\")\n");
    
    // Generate instructions
    chunk_t* chunk = &function->chunk;
    int ip = 0;
    
    while (ip < chunk->count) {
        result = _wat_generate_instruction(generator, chunk, &ip);
        if (result != WAT_OK) return result;
        ip++;
    }
    
    // End function and module
    _wat_append(generator, "  )\n");
    _wat_append(generator, ")\n");
    
    return WAT_OK;
}

WatErrorCode l_wat_write_to_file(wat_generator_t* generator) {
    FILE* file = fopen(generator->filename_wat, "w");
    if (!file) {
        generator->error = WAT_ERROR_FILE_NOT_OPEN;
        return WAT_ERROR_FILE_NOT_OPEN;
    }
    
    size_t written = fwrite(generator->output_buffer, 1, generator->buffer_size, file);
    fclose(file);
    
    if (written != generator->buffer_size) {
        generator->error = WAT_ERROR_FILE_NOT_WRITTEN;
        return WAT_ERROR_FILE_NOT_WRITTEN;
    }
    
    return WAT_OK;
}
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "wasm_generator.h"
#include "lib/memory.h"
#include "chunk.h"
#include "value.h"

typedef struct wasm_generator_t {
    char* filename_source;
    char* filename_wasm;
    uint8_t* output_buffer;
    size_t buffer_size;
    size_t buffer_capacity;
    WasmErrorCode error;
} wasm_generator_t;

const char * l_wasm_get_error_string(WasmErrorCode error) {
    switch (error) {
        case WASM_OK: return "No error.";
        case WASM_ERROR: return "An error occurred.";
        case WASM_ERROR_FILE_NOT_OPEN: return "File could not be opened.";
        case WASM_ERROR_FILE_NOT_WRITTEN: return "File could not be written.";
        case WASM_ERROR_UNSUPPORTED_OPCODE: return "Unsupported opcode for WASM generation.";
        case WASM_ERROR_INVALID_VALUE_TYPE: return "Invalid value type for WASM generation.";
        default: return "Unknown error.";
    }
}

static void _wasm_append_byte(wasm_generator_t* generator, uint8_t byte) {
    if (generator->buffer_size + 1 > generator->buffer_capacity) {
        generator->buffer_capacity = (generator->buffer_capacity == 0) ? 1024 : generator->buffer_capacity * 2;
        generator->output_buffer = (uint8_t*)realloc(generator->output_buffer, generator->buffer_capacity);
    }

    generator->output_buffer[generator->buffer_size++] = byte;
}

static void _wasm_append_bytes(wasm_generator_t* generator, const uint8_t* bytes, size_t count) {
    for (size_t i = 0; i < count; i++) {
        _wasm_append_byte(generator, bytes[i]);
    }
}

static void _wasm_append_leb128_u32(wasm_generator_t* generator, uint32_t value) {
    while (value >= 0x80) {
        _wasm_append_byte(generator, (uint8_t)(value | 0x80));
        value >>= 7;
    }
    _wasm_append_byte(generator, (uint8_t)value);
}

static void _wasm_append_f64(wasm_generator_t* generator, double value) {
    union { double d; uint64_t i; } u;
    u.d = value;

    // Little-endian encoding
    for (int i = 0; i < 8; i++) {
        _wasm_append_byte(generator, (uint8_t)(u.i >> (i * 8)));
    }
}

wasm_generator_t * l_wasm_new(const char * filename_source) {
    wasm_generator_t* generator = (wasm_generator_t*)malloc(sizeof(wasm_generator_t));
    if (generator == NULL) {
        return NULL;
    }

    // Create WASM filename from source filename
    size_t source_len = strlen(filename_source);
    generator->filename_source = (char*)malloc(source_len + 1);
    if (generator->filename_source == NULL) {
        free(generator);
        return NULL;
    }
    strcpy(generator->filename_source, filename_source);

    // Use snprintf to prevent buffer overflow when creating .wasm filename
    size_t wasm_filename_size = source_len + 6; // +5 for ".wasm" +1 for null terminator
    generator->filename_wasm = (char*)malloc(wasm_filename_size);
    if (generator->filename_wasm == NULL) {
        free(generator->filename_source);
        free(generator);
        return NULL;
    }
    snprintf(generator->filename_wasm, wasm_filename_size, "%s.wasm", filename_source);

    // Initialize output buffer
    generator->buffer_capacity = 0;
    generator->output_buffer = NULL;
    generator->buffer_size = 0;
    generator->error = WASM_OK;

    return generator;
}

void l_wasm_del(wasm_generator_t* generator) {
    if (generator) {
        free(generator->filename_source);
        free(generator->filename_wasm);
        free(generator->output_buffer);
        free(generator);
    }
}

static WasmErrorCode _wasm_generate_module_header(wasm_generator_t* generator) {
    // WASM magic number
    const uint8_t magic[] = {0x00, 0x61, 0x73, 0x6D};
    _wasm_append_bytes(generator, magic, 4);

    // WASM version
    const uint8_t version[] = {0x01, 0x00, 0x00, 0x00};
    _wasm_append_bytes(generator, version, 4);

    return WASM_OK;
}

// Helper function to encode and store a LEB128 value at a specific position
static WasmErrorCode _wasm_encode_leb128_at(wasm_generator_t* generator, size_t pos, uint32_t value) {
    uint8_t bytes[5]; // Max 5 bytes for 32-bit LEB128
    int byte_count = 0;

    do {
        bytes[byte_count] = (uint8_t)(value | 0x80);
        value >>= 7;
        if (value == 0) {
            bytes[byte_count] &= 0x7F;
        }
        byte_count++;
    } while (value > 0 && byte_count < 5);

    // Check if we have enough space
    if (pos + byte_count > generator->buffer_capacity) {
        return WASM_ERROR;
    }

    // Write bytes
    for (int i = 0; i < byte_count; i++) {
        generator->output_buffer[pos + i] = bytes[i];
    }

    return WASM_OK;
}

static WasmErrorCode _wasm_generate_type_section(wasm_generator_t* generator) {
    // Type section ID
    _wasm_append_byte(generator, 0x01);

    // Section size placeholder - save position for later update
    size_t size_pos = generator->buffer_size;
    _wasm_append_leb128_u32(generator, 0); // Placeholder, will be updated
    size_t content_start = generator->buffer_size;

    // Number of types
    _wasm_append_leb128_u32(generator, 2);

    // Type 0: (f64) -> () - for print function
    _wasm_append_byte(generator, 0x60); // func type
    _wasm_append_leb128_u32(generator, 1); // param count
    _wasm_append_byte(generator, 0x7C); // f64
    _wasm_append_leb128_u32(generator, 0); // result count

    // Type 1: () -> () - for main function
    _wasm_append_byte(generator, 0x60); // func type
    _wasm_append_leb128_u32(generator, 0); // param count
    _wasm_append_leb128_u32(generator, 0); // result count

    // Update section size with proper LEB128 encoding
    size_t content_size = generator->buffer_size - content_start;
    return _wasm_encode_leb128_at(generator, size_pos, content_size);
}

static WasmErrorCode _wasm_generate_import_section(wasm_generator_t* generator) {
    // Import section ID
    _wasm_append_byte(generator, 0x02);

    // Section size placeholder
    size_t size_pos = generator->buffer_size;
    _wasm_append_leb128_u32(generator, 0); // Placeholder, will be updated
    size_t content_start = generator->buffer_size;

    // Number of imports
    _wasm_append_leb128_u32(generator, 1);

    // Import: env.print_f64
    _wasm_append_leb128_u32(generator, 3); // module name length
    _wasm_append_bytes(generator, (const uint8_t*)"env", 3);
    _wasm_append_leb128_u32(generator, 9); // field name length (print_f64 is 9 chars)
    _wasm_append_bytes(generator, (const uint8_t*)"print_f64", 9);
    _wasm_append_byte(generator, 0x00); // import kind: function
    _wasm_append_leb128_u32(generator, 0); // type index

    // Update section size with proper LEB128 encoding
    size_t content_size = generator->buffer_size - content_start;
    return _wasm_encode_leb128_at(generator, size_pos, content_size);
}

WasmErrorCode l_wasm_generate_from_function(wasm_generator_t* generator, obj_function_t* function) {
    WasmErrorCode result;

    // Generate WASM module header
    result = _wasm_generate_module_header(generator);
    if (result != WASM_OK) return result;

    // Generate type section
    result = _wasm_generate_type_section(generator);
    if (result != WASM_OK) return result;

    // Generate import section
    result = _wasm_generate_import_section(generator);
    if (result != WASM_OK) return result;

    // For now, we'll create a minimal WASM file
    // A full implementation would need function, export, and code sections

    return WASM_OK;
}

WasmErrorCode l_wasm_write_to_file(wasm_generator_t* generator) {
    FILE* file = fopen(generator->filename_wasm, "wb");
    if (!file) {
        generator->error = WASM_ERROR_FILE_NOT_OPEN;
        return WASM_ERROR_FILE_NOT_OPEN;
    }

    size_t written = fwrite(generator->output_buffer, 1, generator->buffer_size, file);
    fclose(file);

    if (written != generator->buffer_size) {
        generator->error = WASM_ERROR_FILE_NOT_WRITTEN;
        return WASM_ERROR_FILE_NOT_WRITTEN;
    }

    return WASM_OK;
}

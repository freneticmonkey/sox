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
        uint8_t* new_buffer = (uint8_t*)realloc(generator->output_buffer, generator->buffer_capacity);
        if (new_buffer == NULL) {
            generator->error = WASM_ERROR;
            return;
        }
        generator->output_buffer = new_buffer;
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
    memcpy(generator->filename_source, filename_source, source_len + 1);

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

// Helper function to reserve space for section size placeholder
// Returns the position where size will be written, reserves 5 bytes
static size_t _wasm_reserve_section_size_placeholder(wasm_generator_t* generator) {
    size_t pos = generator->buffer_size;
    // Reserve 5 bytes max for LEB128 u32 (worst case: 0xFFFFFFFF)
    for (int i = 0; i < 5; i++) {
        _wasm_append_byte(generator, 0x00);
    }
    return pos;
}

// Helper function to encode and store a LEB128 value at a specific position
// Also returns the actual number of bytes used
static WasmErrorCode _wasm_encode_leb128_at(wasm_generator_t* generator, size_t pos, uint32_t value, int* bytes_used) {
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
    if (pos + byte_count > generator->buffer_size) {
        return WASM_ERROR;
    }

    // Write bytes
    for (int i = 0; i < byte_count; i++) {
        generator->output_buffer[pos + i] = bytes[i];
    }

    if (bytes_used) {
        *bytes_used = byte_count;
    }

    return WASM_OK;
}

static WasmErrorCode _wasm_generate_type_section(wasm_generator_t* generator) {
    // Type section ID
    _wasm_append_byte(generator, 0x01);
    if (generator->error != WASM_OK) return generator->error;

    // Section size placeholder - reserves 5 bytes for LEB128
    size_t size_pos = _wasm_reserve_section_size_placeholder(generator);
    if (generator->error != WASM_OK) return generator->error;

    size_t content_start = generator->buffer_size;

    // Number of types
    _wasm_append_leb128_u32(generator, 2);
    if (generator->error != WASM_OK) return generator->error;

    // Type 0: (f64) -> () - for print function
    _wasm_append_byte(generator, 0x60); // func type
    if (generator->error != WASM_OK) return generator->error;

    _wasm_append_leb128_u32(generator, 1); // param count
    if (generator->error != WASM_OK) return generator->error;

    _wasm_append_byte(generator, 0x7C); // f64
    if (generator->error != WASM_OK) return generator->error;

    _wasm_append_leb128_u32(generator, 0); // result count
    if (generator->error != WASM_OK) return generator->error;

    // Type 1: () -> () - for main function
    _wasm_append_byte(generator, 0x60); // func type
    if (generator->error != WASM_OK) return generator->error;

    _wasm_append_leb128_u32(generator, 0); // param count
    if (generator->error != WASM_OK) return generator->error;

    _wasm_append_leb128_u32(generator, 0); // result count
    if (generator->error != WASM_OK) return generator->error;

    // Update section size with proper LEB128 encoding
    size_t content_size = generator->buffer_size - content_start;
    return _wasm_encode_leb128_at(generator, size_pos, content_size, NULL);
}

static WasmErrorCode _wasm_generate_import_section(wasm_generator_t* generator) {
    // Import section ID
    _wasm_append_byte(generator, 0x02);
    if (generator->error != WASM_OK) return generator->error;

    // Section size placeholder - reserves 5 bytes for LEB128
    size_t size_pos = _wasm_reserve_section_size_placeholder(generator);
    if (generator->error != WASM_OK) return generator->error;

    size_t content_start = generator->buffer_size;

    // Number of imports
    _wasm_append_leb128_u32(generator, 1);
    if (generator->error != WASM_OK) return generator->error;

    // Import: env.print_f64
    _wasm_append_leb128_u32(generator, 3); // module name length
    if (generator->error != WASM_OK) return generator->error;

    _wasm_append_bytes(generator, (const uint8_t*)"env", 3);
    if (generator->error != WASM_OK) return generator->error;

    _wasm_append_leb128_u32(generator, 9); // field name length (print_f64 is 9 chars)
    if (generator->error != WASM_OK) return generator->error;

    _wasm_append_bytes(generator, (const uint8_t*)"print_f64", 9);
    if (generator->error != WASM_OK) return generator->error;

    _wasm_append_byte(generator, 0x00); // import kind: function
    if (generator->error != WASM_OK) return generator->error;

    _wasm_append_leb128_u32(generator, 0); // type index
    if (generator->error != WASM_OK) return generator->error;

    // Update section size with proper LEB128 encoding
    size_t content_size = generator->buffer_size - content_start;
    return _wasm_encode_leb128_at(generator, size_pos, content_size, NULL);
}

static WasmErrorCode _wasm_generate_function_section(wasm_generator_t* generator) {
    // Function section ID
    _wasm_append_byte(generator, 0x03);
    if (generator->error != WASM_OK) return generator->error;

    // Section size placeholder - reserves 5 bytes for LEB128
    size_t size_pos = _wasm_reserve_section_size_placeholder(generator);
    if (generator->error != WASM_OK) return generator->error;

    size_t content_start = generator->buffer_size;

    // Number of function declarations (1 = main function)
    _wasm_append_leb128_u32(generator, 1);
    if (generator->error != WASM_OK) return generator->error;

    // Function 0: main() uses type 1 (no params, no returns)
    _wasm_append_leb128_u32(generator, 1); // type index
    if (generator->error != WASM_OK) return generator->error;

    // Update section size
    size_t content_size = generator->buffer_size - content_start;
    return _wasm_encode_leb128_at(generator, size_pos, content_size, NULL);
}

static WasmErrorCode _wasm_generate_export_section(wasm_generator_t* generator) {
    // Export section ID
    _wasm_append_byte(generator, 0x07);
    if (generator->error != WASM_OK) return generator->error;

    // Section size placeholder - reserves 5 bytes for LEB128
    size_t size_pos = _wasm_reserve_section_size_placeholder(generator);
    if (generator->error != WASM_OK) return generator->error;

    size_t content_start = generator->buffer_size;

    // Number of exports
    _wasm_append_leb128_u32(generator, 1);
    if (generator->error != WASM_OK) return generator->error;

    // Export: "main" function
    _wasm_append_leb128_u32(generator, 4); // name length
    if (generator->error != WASM_OK) return generator->error;

    _wasm_append_bytes(generator, (const uint8_t*)"main", 4);
    if (generator->error != WASM_OK) return generator->error;

    _wasm_append_byte(generator, 0x00); // export kind: function
    if (generator->error != WASM_OK) return generator->error;

    _wasm_append_leb128_u32(generator, 1); // function index (1 = our main, 0 is imported print_f64)
    if (generator->error != WASM_OK) return generator->error;

    // Update section size
    size_t content_size = generator->buffer_size - content_start;
    return _wasm_encode_leb128_at(generator, size_pos, content_size, NULL);
}

// Helper macro to check buffer space before write operations
#define TEMP_BODY_RESERVE(size) do { \
    if (temp_body_size + (size) > temp_body_capacity) { \
        generator->error = WASM_ERROR; \
        if (temp_body != NULL) free(temp_body); \
        return WASM_ERROR; \
    } \
} while(0)

static WasmErrorCode _wasm_generate_code_section(wasm_generator_t* generator, obj_function_t* function) {
    // Code section ID
    _wasm_append_byte(generator, 0x0A);
    if (generator->error != WASM_OK) return generator->error;

    // Section size placeholder - reserves 5 bytes for LEB128
    size_t size_pos = _wasm_reserve_section_size_placeholder(generator);
    if (generator->error != WASM_OK) return generator->error;

    size_t content_start = generator->buffer_size;

    // Number of function bodies (1 = main)
    _wasm_append_leb128_u32(generator, 1);
    if (generator->error != WASM_OK) return generator->error;

    // Dynamically allocate temp buffer to avoid fixed-size overflow
    size_t temp_body_capacity = 8192; // Initial capacity
    uint8_t* temp_body = (uint8_t*)malloc(temp_body_capacity);
    if (temp_body == NULL) {
        generator->error = WASM_ERROR;
        return WASM_ERROR;
    }

    size_t temp_body_size = 0;

    // Manually build function body
    // Local variables count (0 for now - we don't use locals)
    TEMP_BODY_RESERVE(1);
    temp_body[temp_body_size++] = 0x00; // 0 local variable groups

    // Generate instructions from bytecode
    chunk_t* chunk = &function->chunk;
    int ip = 0;

    while (ip < chunk->count) {
        // Bounds check before reading instruction

        uint8_t instruction = chunk->code[ip];

        switch (instruction) {
            case OP_CONSTANT: {
                ip++;
                // Bounds check before reading operand
                if (ip >= chunk->count) {
                    generator->error = WASM_ERROR;
                    free(temp_body);
                    return WASM_ERROR;
                }

                uint8_t constant_index = chunk->code[ip];
                // Bounds check on constant array access
                if (constant_index >= chunk->constants.count) {
                    generator->error = WASM_ERROR;
                    free(temp_body);
                    return WASM_ERROR;
                }

                value_t constant = chunk->constants.values[constant_index];

                if (IS_NUMBER(constant)) {
                    TEMP_BODY_RESERVE(9); // 1 byte opcode + 8 bytes f64
                    temp_body[temp_body_size++] = 0x44; // f64.const
                    union { double d; uint8_t bytes[8]; } u;
                    u.d = AS_NUMBER(constant);
                    for (int i = 0; i < 8; i++) {
                        temp_body[temp_body_size++] = u.bytes[i];
                    }
                } else if (IS_BOOL(constant)) {
                    TEMP_BODY_RESERVE(9);
                    temp_body[temp_body_size++] = 0x44; // f64.const
                    union { double d; uint8_t bytes[8]; } u;
                    u.d = AS_BOOL(constant) ? 1.0 : 0.0;
                    for (int i = 0; i < 8; i++) {
                        temp_body[temp_body_size++] = u.bytes[i];
                    }
                } else if (IS_NIL(constant)) {
                    TEMP_BODY_RESERVE(9);
                    temp_body[temp_body_size++] = 0x44; // f64.const
                    union { double d; uint8_t bytes[8]; } u;
                    u.d = 0.0;
                    for (int i = 0; i < 8; i++) {
                        temp_body[temp_body_size++] = u.bytes[i];
                    }
                }
                break;
            }
            case OP_NIL: {
                TEMP_BODY_RESERVE(9);
                temp_body[temp_body_size++] = 0x44;
                union { double d; uint8_t bytes[8]; } u;
                u.d = 0.0;
                for (int i = 0; i < 8; i++) {
                    temp_body[temp_body_size++] = u.bytes[i];
                }
                break;
            }
            case OP_TRUE: {
                TEMP_BODY_RESERVE(9);
                temp_body[temp_body_size++] = 0x44; // f64.const
                union { double d; uint8_t bytes[8]; } u;
                u.d = 1.0;
                for (int i = 0; i < 8; i++) {
                    temp_body[temp_body_size++] = u.bytes[i];
                }
                break;
            }
            case OP_FALSE: {
                TEMP_BODY_RESERVE(9);
                temp_body[temp_body_size++] = 0x44;
                union { double d; uint8_t bytes[8]; } u;
                u.d = 0.0;
                for (int i = 0; i < 8; i++) {
                    temp_body[temp_body_size++] = u.bytes[i];
                }
                break;
            }
            case OP_ADD:
                TEMP_BODY_RESERVE(1);
                temp_body[temp_body_size++] = 0xA0;
                break;
            case OP_SUBTRACT:
                TEMP_BODY_RESERVE(1);
                temp_body[temp_body_size++] = 0xA1;
                break;
            case OP_MULTIPLY:
                TEMP_BODY_RESERVE(1);
                temp_body[temp_body_size++] = 0xA2;
                break;
            case OP_DIVIDE:
                TEMP_BODY_RESERVE(1);
                temp_body[temp_body_size++] = 0xA3;
                break;
            case OP_NEGATE:
                TEMP_BODY_RESERVE(1);
                temp_body[temp_body_size++] = 0x9A;
                break;
            case OP_PRINT:
                TEMP_BODY_RESERVE(2);
                temp_body[temp_body_size++] = 0x10; // call
                temp_body[temp_body_size++] = 0x00; // function index 0
                break;
            case OP_POP:
                TEMP_BODY_RESERVE(1);
                temp_body[temp_body_size++] = 0x1A;
                break;
            case OP_RETURN:
                TEMP_BODY_RESERVE(1);
                temp_body[temp_body_size++] = 0x0F;
                break;
        }
        ip++;
    }

    // Add implicit return if not already present
    if (chunk->count > 0 && chunk->code[chunk->count - 1] == OP_RETURN) {
        // Already has return, don't add another
    } else {
        TEMP_BODY_RESERVE(1);
        temp_body[temp_body_size++] = 0x0F; // return
    }

    // Verify output buffer is valid before writing
    if (generator->output_buffer == NULL && generator->buffer_capacity > 0) {
        generator->error = WASM_ERROR;
        free(temp_body);
        return WASM_ERROR;
    }

    // Now write the function body with correct size
    _wasm_append_leb128_u32(generator, temp_body_size);
    if (generator->error != WASM_OK) {
        free(temp_body);
        return generator->error;
    }

    _wasm_append_bytes(generator, temp_body, temp_body_size);
    if (generator->error != WASM_OK) {
        free(temp_body);
        return generator->error;
    }

    // Clean up temp buffer
    free(temp_body);

    // Update section size
    size_t content_size = generator->buffer_size - content_start;
    return _wasm_encode_leb128_at(generator, size_pos, content_size, NULL);
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

    // Generate function section
    result = _wasm_generate_function_section(generator);
    if (result != WASM_OK) return result;

    // Generate export section
    result = _wasm_generate_export_section(generator);
    if (result != WASM_OK) return result;

    // Generate code section (function bodies)
    result = _wasm_generate_code_section(generator, function);
    if (result != WASM_OK) return result;

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

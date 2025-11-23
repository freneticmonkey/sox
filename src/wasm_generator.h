#ifndef SOX_WASM_GENERATOR_H
#define SOX_WASM_GENERATOR_H

#include "object.h"
#include "vm_config.h"

typedef enum WasmErrorCode {
    WASM_OK,
    WASM_ERROR,
    WASM_ERROR_FILE_NOT_OPEN,
    WASM_ERROR_FILE_NOT_WRITTEN,
    WASM_ERROR_UNSUPPORTED_OPCODE,
    WASM_ERROR_INVALID_VALUE_TYPE,
} WasmErrorCode;

typedef struct wasm_generator_t wasm_generator_t;

const char * l_wasm_get_error_string(WasmErrorCode error);

wasm_generator_t * l_wasm_new(const char * filename_source);
void l_wasm_del(wasm_generator_t* generator);

WasmErrorCode l_wasm_generate_from_function(wasm_generator_t* generator, obj_function_t* function);
WasmErrorCode l_wasm_write_to_file(wasm_generator_t* generator);

#endif

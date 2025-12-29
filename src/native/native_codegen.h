#ifndef SOX_NATIVE_CODEGEN_H
#define SOX_NATIVE_CODEGEN_H

#include "../object.h"
#include <stdbool.h>

// Main entry point for native code generation
// Takes a compiled Sox closure and generates a native executable

typedef struct {
    const char* output_file;      // Output executable/object file path
    const char* target_arch;      // Target architecture ("x86_64", "arm64")
    const char* target_os;        // Target OS ("linux", "macos", "windows")
    bool emit_object;             // Emit object file instead of executable
    bool debug_output;            // Enable debug output
    int optimization_level;       // 0-3
} native_codegen_options_t;

// Generate native code from a Sox closure
bool native_codegen_generate(obj_closure_t* closure, const native_codegen_options_t* options);

// Helper: generate object file only
bool native_codegen_generate_object(obj_closure_t* closure, const char* output_file);

// Helper: generate and link executable
bool native_codegen_generate_executable(obj_closure_t* closure, const char* output_file);

#endif

#ifndef SOX_ARG_PARSER_H
#define SOX_ARG_PARSER_H

#include <stdbool.h>

// Command-line arguments structure with all available options
typedef struct {
    // Input file
    char* input_file;

    // Output options
    bool show_version;
    bool show_help;
    bool run_tests;

    // Bytecode options
    bool enable_serialisation;
    bool suppress_print;

    // WebAssembly options
    bool enable_wasm_output;
    bool enable_wat_output;

    // Native code generation options
    bool enable_native_output;
    char* native_output_file;
    char* native_target_arch;  // "x86_64", "arm64", "aarch64"
    char* native_target_os;    // "linux", "macos", "darwin", "windows"
    bool native_emit_object;
    bool native_debug_output;
    int native_optimization_level;  // 0-3
    bool use_custom_linker;    // Use custom linker instead of system linker

    // Benchmarking options
    bool enable_benchmarks;
    double benchmark_time_seconds;
    char* benchmark_filter;

    // Internal
    int argc;
    const char** argv;
} sox_args_t;

// Parse command-line arguments
// Returns true if parsing succeeded, false if there was an error
bool parse_arguments(int argc, const char* argv[], sox_args_t* args);

// Print help/usage information
void print_help(const char* program_name);

// Print version information
void print_version(void);

// Free allocated argument memory
void free_arguments(sox_args_t* args);

#endif

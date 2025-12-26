#ifndef SOX_LINKER_H
#define SOX_LINKER_H

#include <stdbool.h>

// Supported linker types
typedef enum {
    LINKER_SYSTEM,    // System linker (ld, ld64, link.exe)
    LINKER_GCC,       // GCC wrapper
    LINKER_CLANG,     // Clang wrapper
    LINKER_NONE       // No linker found
} linker_type_t;

// Linker mode - Phase 6.1: Main Linker API
typedef enum {
    LINKER_MODE_SYSTEM,     // Use system linker (current default)
    LINKER_MODE_CUSTOM,     // Use custom linker (when implemented)
    LINKER_MODE_AUTO        // Auto-select based on complexity
} linker_mode_t;

// Platform information
typedef struct {
    const char* os;      // "linux", "macos", "windows"
    const char* arch;    // "x86_64", "arm64"
} platform_t;

// Linker information
typedef struct {
    linker_type_t type;
    const char* name;      // "ld", "gcc", "clang", etc.
    const char* path;      // Full path to linker (if found)
    bool available;        // Whether this linker is available
} linker_info_t;

// Linker options for invocation
typedef struct {
    // Input/Output files
    const char* input_file;      // Primary object file to link
    const char** input_files;    // Array of input files (for multi-object linking)
    int input_file_count;        // Number of input files in array
    const char* output_file;     // Executable output file

    // Target platform
    const char* target_os;       // "linux", "macos", "windows"
    const char* target_arch;     // "x86_64", "arm64"

    // Linking behavior
    bool link_runtime;           // Link with sox runtime library
    bool verbose;                // Print linker commands

    // Phase 6.1: Enhanced options
    linker_mode_t mode;          // Which linker to use
    bool verbose_linking;        // Print detailed linking information
    bool keep_objects;           // Keep intermediate object files
} linker_options_t;

// Detect available linkers for a target platform
// Returns array of linker_info_t, must be freed with linker_free_list()
linker_info_t* linker_detect_available(const char* target_os, const char* target_arch, int* count);

// Get the best available linker for the target
linker_info_t linker_get_preferred(const char* target_os, const char* target_arch);

// Check if a specific linker is available
bool linker_is_available(const char* linker_name);

// Get the full path to a linker if available
const char* linker_get_path(const char* linker_name);

// Link an object file into an executable
// Returns 0 on success, non-zero on failure
int linker_invoke(linker_info_t linker, const linker_options_t* options);

// Get the current platform (OS and architecture)
platform_t linker_get_current_platform(void);

// Free linker list allocated by linker_detect_available
void linker_free_list(linker_info_t* list);

// Phase 6.1: Main Linker API - Unified linking entry point
// This function selects between system and custom linker based on mode
int linker_link(const linker_options_t* options);

// Custom linker implementation (stub - falls back to system linker for now)
// Will orchestrate Phases 1-5 when custom linker is fully implemented
int linker_link_custom(const linker_options_t* options);

// Helper: Determine if a link job is simple enough for custom linker
// Returns true if the custom linker can handle this job
bool linker_is_simple_link_job(const linker_options_t* options);

#endif

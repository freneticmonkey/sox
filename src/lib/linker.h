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
    const char* input_file;      // Object file to link
    const char* output_file;     // Executable output file
    const char* target_os;       // "linux", "macos", "windows"
    const char* target_arch;     // "x86_64", "arm64"
    bool link_runtime;           // Link with sox runtime library
    bool verbose;                // Print linker commands
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

#endif

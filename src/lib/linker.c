#include "linker.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/utsname.h>

// Custom linker headers
#include "../native/linker_core.h"
#include "../native/object_reader.h"
#include "../native/symbol_resolver.h"
#include "../native/section_layout.h"
#include "../native/relocation_processor.h"
#include "../native/elf_executable.h"
#include "../native/macho_executable.h"
#include "../native/archive_reader.h"

#define MAX_LINKERS 5
#define MAX_CMD_LEN 1024

// Get the current platform (public function, implementation follows)
platform_t linker_get_current_platform(void);

// Internal implementation of platform detection
static platform_t _get_current_platform_impl(void) {
    struct utsname info;
    uname(&info);

    platform_t platform = {0};

    // Detect OS (case-insensitive comparison)
    if (strstr(info.sysname, "Linux") || strstr(info.sysname, "linux")) {
        platform.os = "linux";
    } else if (strstr(info.sysname, "Darwin") || strstr(info.sysname, "darwin")) {
        platform.os = "macos";
    } else if (strstr(info.sysname, "Windows") || strstr(info.sysname, "windows") ||
               strstr(info.sysname, "MINGW") || strstr(info.sysname, "mingw")) {
        platform.os = "windows";
    } else {
        platform.os = "unknown";
    }

    // Detect architecture
    if (strcmp(info.machine, "x86_64") == 0 || strcmp(info.machine, "amd64") == 0) {
        platform.arch = "x86_64";
    } else if (strcmp(info.machine, "aarch64") == 0 || strcmp(info.machine, "arm64") == 0) {
        platform.arch = "arm64";
    } else {
        platform.arch = "unknown";
    }

    return platform;
}

// Check if we're cross-compiling
static bool _is_cross_compilation(const char* target_os, const char* target_arch) {
    platform_t current = _get_current_platform_impl();

    bool os_mismatch = strcmp(current.os, target_os) != 0;
    bool arch_mismatch = strcmp(current.arch, target_arch) != 0;

    return os_mismatch || arch_mismatch;
}

// Try to find a cross-compiler for the target
static char* _find_cross_compiler(const char* target_os, const char* target_arch) {
    static char cross_compiler[256];

    // Build the cross-compiler prefix
    const char* prefix = NULL;

    if (strcmp(target_os, "linux") == 0) {
        if (strcmp(target_arch, "x86_64") == 0) {
            prefix = "x86_64-linux-gnu";
        } else if (strcmp(target_arch, "arm64") == 0) {
            prefix = "aarch64-linux-gnu";
        }
    }

    if (prefix == NULL) {
        return NULL;
    }

    // Try different compiler names
    const char* compilers[] = {"gcc", "clang"};

    for (int i = 0; i < 2; i++) {
        snprintf(cross_compiler, sizeof(cross_compiler), "%s-%s", prefix, compilers[i]);
        if (linker_is_available(cross_compiler)) {
            return cross_compiler;
        }
    }

    return NULL;
}

// Helper: Check if a file exists and is executable
static bool _file_is_executable(const char* path) {
    struct stat st;
    if (stat(path, &st) != 0) {
        return false;
    }
    return (st.st_mode & S_IXUSR) != 0;
}

// Helper: Search for a command in PATH
static char* _find_in_path(const char* command) {
    static char full_path[512];
    const char* path_env = getenv("PATH");

    if (path_env == NULL) {
        return NULL;
    }

    // Check if it's an absolute path first
    if (command[0] == '/' && _file_is_executable(command)) {
        return (char*)command;
    }

    // Search in PATH directories
    char path_copy[2048];
    strncpy(path_copy, path_env, sizeof(path_copy) - 1);
    path_copy[sizeof(path_copy) - 1] = '\0';

    char* dir = strtok(path_copy, ":");
    while (dir != NULL) {
        snprintf(full_path, sizeof(full_path), "%s/%s", dir, command);
        if (_file_is_executable(full_path)) {
            return full_path;
        }
        dir = strtok(NULL, ":");
    }

    return NULL;
}

// Detect available linkers for a target platform
linker_info_t* linker_detect_available(const char* target_os, const char* target_arch, int* count) {
    linker_info_t* linkers = (linker_info_t*)malloc(sizeof(linker_info_t) * MAX_LINKERS);
    *count = 0;

    if (linkers == NULL) {
        return NULL;
    }

    // List of linkers to try, in order of preference per platform
    const char* candidates[3];
    int num_candidates = 3;

    // For macOS, prefer clang then gcc
    if (strcmp(target_os, "macos") == 0 || strcmp(target_os, "darwin") == 0) {
        candidates[0] = "clang";
        candidates[1] = "gcc";
        candidates[2] = "ld";
    } else {
        // For Linux/Unix, prefer gcc then clang then ld
        candidates[0] = "gcc";
        candidates[1] = "clang";
        candidates[2] = "ld";
    }

    // Detect each candidate
    for (int i = 0; i < num_candidates && *count < MAX_LINKERS; i++) {
        char* path = _find_in_path(candidates[i]);
        if (path != NULL) {
            // Make a copy of the path for storage
            static char path_copies[MAX_LINKERS][512];
            strncpy(path_copies[*count], path, sizeof(path_copies[*count]) - 1);
            path_copies[*count][sizeof(path_copies[*count]) - 1] = '\0';

            linkers[*count].name = candidates[i];
            linkers[*count].path = path_copies[*count];
            linkers[*count].available = true;

            if (strcmp(candidates[i], "ld") == 0) {
                linkers[*count].type = LINKER_SYSTEM;
            } else if (strcmp(candidates[i], "gcc") == 0) {
                linkers[*count].type = LINKER_GCC;
            } else if (strcmp(candidates[i], "clang") == 0) {
                linkers[*count].type = LINKER_CLANG;
            }

            (*count)++;
        }
    }

    // If no linkers found, mark as none available
    if (*count == 0) {
        linkers[0].type = LINKER_NONE;
        linkers[0].name = "none";
        linkers[0].path = NULL;
        linkers[0].available = false;
        *count = 1;
    }

    return linkers;
}

// Get the best available linker for the target
linker_info_t linker_get_preferred(const char* target_os, const char* target_arch) {
    int count = 0;
    linker_info_t* linkers = linker_detect_available(target_os, target_arch, &count);

    if (count == 0 || linkers == NULL) {
        return (linker_info_t){.type = LINKER_NONE, .name = "none", .available = false};
    }

    // Return first available (which is preferred due to ordering)
    linker_info_t preferred = linkers[0];

    // Make a copy of the path since we're freeing the array
    if (preferred.path != NULL) {
        static char path_copy[512];
        strncpy(path_copy, preferred.path, sizeof(path_copy) - 1);
        path_copy[sizeof(path_copy) - 1] = '\0';
        preferred.path = path_copy;
    }

    free(linkers);
    return preferred;
}

// Check if a specific linker is available
bool linker_is_available(const char* linker_name) {
    char* path = _find_in_path(linker_name);
    return path != NULL;
}

// Get the full path to a linker if available
const char* linker_get_path(const char* linker_name) {
    return _find_in_path(linker_name);
}

// Build linker command for Linux/Unix systems
static int _link_linux(linker_info_t linker, const linker_options_t* options) {
    char cmd[MAX_CMD_LEN];

    if (linker.type == LINKER_GCC || linker.type == LINKER_CLANG) {
        // Using compiler wrapper - much simpler, handles all the complexity
        snprintf(cmd, sizeof(cmd), "%s %s -o %s",
                 linker.path,
                 options->input_file,
                 options->output_file);

        // Add runtime library if needed
        if (options->link_runtime) {
            strncat(cmd, " -L./build -lsox_runtime", sizeof(cmd) - strlen(cmd) - 1);
        }
    } else {
        // Using system linker (ld) - more complex, platform-specific
        const char* ld_script = "";

        // Determine dynamic linker path based on architecture
        if (strcmp(options->target_arch, "arm64") == 0 || strcmp(options->target_arch, "aarch64") == 0) {
            ld_script = "-dynamic-linker /lib/ld-linux-aarch64.so.1";
        } else {
            // x86_64
            ld_script = "-dynamic-linker /lib64/ld-linux-x86-64.so.2";
        }

        snprintf(cmd, sizeof(cmd),
                 "%s %s -pie -o %s %s -L/lib -L/usr/lib -L/lib64 -L/usr/lib64 -lc",
                 linker.path,
                 ld_script,
                 options->output_file,
                 options->input_file);

        // Add runtime library if needed
        if (options->link_runtime) {
            strncat(cmd, " -L./build -lsox_runtime", sizeof(cmd) - strlen(cmd) - 1);
        }
    }

    if (options->verbose) {
        printf("Linking command: %s\n", cmd);
    }

    // Execute the linker
    int result = system(cmd);
    return result == 0 ? 0 : 1;
}

// Build linker command for macOS systems
static int _link_macos(linker_info_t linker, const linker_options_t* options) {
    char cmd[MAX_CMD_LEN];
    char cwd[512];

    if (getcwd(cwd, sizeof(cwd)) == NULL) {
        strcpy(cwd, "<unknown>");
    }
    fprintf(stderr, "[LINKER] macOS linking from: %s\n", cwd);

    if (linker.type == LINKER_CLANG || linker.type == LINKER_GCC) {
        // Using compiler wrapper (clang or gcc) - simplest and most reliable
        snprintf(cmd, sizeof(cmd), "%s %s -o %s",
                 linker.path,
                 options->input_file,
                 options->output_file);

        // Add runtime library if needed
        if (options->link_runtime) {
            // Try to find the runtime library
            // Common locations to search for libsox_runtime.a
            const char* candidates[] = {
                "./build",
                "/Users/scott/development/projects/sox/build",
                NULL
            };

            char link_cmd[256] = "";
            struct stat st;
            for (int i = 0; candidates[i] != NULL; i++) {
                char lib_path[512];
                snprintf(lib_path, sizeof(lib_path), "%s/libsox_runtime.a", candidates[i]);
                if (stat(lib_path, &st) == 0) {
                    snprintf(link_cmd, sizeof(link_cmd), " -L%s -lsox_runtime", candidates[i]);
                    fprintf(stderr, "[LINKER] Found runtime library at: %s\n", lib_path);
                    break;
                }
            }

            if (strlen(link_cmd) == 0) {
                // Fallback to -L./build if we can't find it
                strcpy(link_cmd, " -L./build -lsox_runtime");
                fprintf(stderr, "[LINKER] Using fallback path for runtime library\n");
            }

            fprintf(stderr, "[LINKER] Adding runtime library: %s\n", link_cmd);
            strncat(cmd, link_cmd, sizeof(cmd) - strlen(cmd) - 1);
        } else {
            fprintf(stderr, "[LINKER] NOT adding runtime library (link_runtime=false)\n");
        }

        fprintf(stderr, "[LINKER] Full command: %s\n", cmd);
    } else {
        // Using ld64 (system linker for macOS) - more complex
        const char* arch_flag = "arm64";
        if (strcmp(options->target_arch, "x86_64") == 0) {
            arch_flag = "x86_64";
        }

        snprintf(cmd, sizeof(cmd),
                 "%s -lSystem -arch %s -pie -o %s %s",
                 linker.path,
                 arch_flag,
                 options->output_file,
                 options->input_file);

        // Add runtime library if needed
        if (options->link_runtime) {
            strncat(cmd, " -L./build -lsox_runtime", sizeof(cmd) - strlen(cmd) - 1);
        }
    }

    if (options->verbose) {
        printf("Linking command: %s\n", cmd);
    }

    // Execute the linker
    int result = system(cmd);
    return result == 0 ? 0 : 1;
}

// Link an object file into an executable
int linker_invoke(linker_info_t linker, const linker_options_t* options) {
    if (!linker.available) {
        fprintf(stderr, "Error: No linker available\n");
        return 1;
    }

    if (options->input_file == NULL || options->output_file == NULL) {
        fprintf(stderr, "Error: Input and output files must be specified\n");
        return 1;
    }

    // Check for cross-compilation
    if (_is_cross_compilation(options->target_os, options->target_arch)) {
        platform_t current = _get_current_platform_impl();
        fprintf(stderr, "\n");
        fprintf(stderr, "Warning: Cross-compilation detected\n");
        fprintf(stderr, "  Current platform: %s-%s\n", current.os, current.arch);
        fprintf(stderr, "  Target platform:  %s-%s\n", options->target_os, options->target_arch);
        fprintf(stderr, "\n");

        // Try to find a cross-compiler
        char* cross_compiler = _find_cross_compiler(options->target_os, options->target_arch);
        if (cross_compiler != NULL) {
            printf("Found cross-compiler: %s\n", cross_compiler);
            linker.path = cross_compiler;
        } else {
            fprintf(stderr, "Error: No cross-compiler found for %s-%s\n",
                    options->target_os, options->target_arch);
            fprintf(stderr, "\n");
            fprintf(stderr, "To compile for %s, you need one of:\n", options->target_os);

            if (strcmp(options->target_os, "linux") == 0) {
                if (strcmp(options->target_arch, "x86_64") == 0) {
                    fprintf(stderr, "  - x86_64-linux-gnu-gcc\n");
                    fprintf(stderr, "  - x86_64-linux-gnu-clang\n");
                } else if (strcmp(options->target_arch, "arm64") == 0) {
                    fprintf(stderr, "  - aarch64-linux-gnu-gcc\n");
                    fprintf(stderr, "  - aarch64-linux-gnu-clang\n");
                }
            }

            fprintf(stderr, "\n");
            fprintf(stderr, "Alternatively, use --native-obj to generate just the object file:\n");
            fprintf(stderr, "  sox script.sox --native --native-obj --native-out script.o\n");
            fprintf(stderr, "\n");
            return 1;
        }
    }

    if (options->verbose) {
        printf("Linking %s (%s) into executable %s\n",
               options->input_file,
               linker.name,
               options->output_file);
    }

    // Route to platform-specific linker
    if (strcmp(options->target_os, "macos") == 0 || strcmp(options->target_os, "darwin") == 0) {
        return _link_macos(linker, options);
    } else {
        // Default to Linux/Unix linking
        return _link_linux(linker, options);
    }
}

// Get the current platform (public wrapper)
platform_t linker_get_current_platform(void) {
    return _get_current_platform_impl();
}

// Free linker list allocated by linker_detect_available
void linker_free_list(linker_info_t* list) {
    if (list != NULL) {
        free(list);
    }
}

// ============================================================================
// Phase 6.1: Main Linker API - Integration & Mode Selection
// ============================================================================

// Helper: Determine if a link job is simple enough for custom linker
bool linker_is_simple_link_job(const linker_options_t* options) {
    // For initial integration, consider jobs simple if:
    // - Single input file
    // - Linking runtime library
    // - Standard target (linux/macos x86_64/arm64)

    bool single_file = (options->input_file != NULL &&
                       (options->input_files == NULL || options->input_file_count == 0));

    bool supported_platform = (
        (strcmp(options->target_os, "linux") == 0 ||
         strcmp(options->target_os, "macos") == 0) &&
        (strcmp(options->target_arch, "x86_64") == 0 ||
         strcmp(options->target_arch, "arm64") == 0)
    );

    return single_file && supported_platform;
}

// Custom linker implementation - Phase 6.1 Integration Layer
// This orchestrates all 5 phases of the custom linker
int linker_link_custom(const linker_options_t* options) {
    if (options->verbose_linking || options->verbose) {
        fprintf(stderr, "[CUSTOM LINKER] Starting custom linking process\n");
        fprintf(stderr, "[CUSTOM LINKER] Input: %s\n", options->input_file);
        fprintf(stderr, "[CUSTOM LINKER] Output: %s\n", options->output_file);
        fprintf(stderr, "[CUSTOM LINKER] Target: %s-%s\n", options->target_os, options->target_arch);
    }

    // Phase 1: Create linker context and read object file
    if (options->verbose_linking || options->verbose) {
        fprintf(stderr, "[CUSTOM LINKER] Phase 1: Reading object file...\n");
    }

    linker_context_t* context = linker_context_new();
    if (!context) {
        fprintf(stderr, "Error: Failed to create linker context\n");
        return 1;
    }

    // Set target format
    if (strcmp(options->target_os, "linux") == 0) {
        context->target_format = PLATFORM_FORMAT_ELF;
    } else if (strcmp(options->target_os, "macos") == 0) {
        context->target_format = PLATFORM_FORMAT_MACH_O;
    } else {
        fprintf(stderr, "Error: Unsupported target OS: %s\n", options->target_os);
        linker_context_free(context);
        return 1;
    }

    linker_object_t* obj = linker_read_object(options->input_file);
    if (!obj) {
        fprintf(stderr, "Error: Failed to read object file: %s\n", options->input_file);
        linker_context_free(context);
        return 1;
    }

    if (!linker_context_add_object(context, obj)) {
        fprintf(stderr, "Error: Failed to add object to context\n");
        linker_object_free(obj);
        linker_context_free(context);
        return 1;
    }

    // Phase 1.5: Extract runtime library if needed
    if (options->link_runtime) {
        if (options->verbose_linking || options->verbose) {
            fprintf(stderr, "[CUSTOM LINKER] Phase 1.5: Extracting runtime library...\n");
        }

        // Search for runtime library in common locations
        // Try architecture-specific archives first (lipo-extracted), then universal binaries
        const char* candidates[] = {
            "./build/libsox_runtime_arm64.a",   /* ARM64-specific (macOS) */
            "./build/libsox_runtime_aarch64.a", /* ARM64-specific (Linux) */
            "./build/libsox_runtime_x86_64.a",  /* x86_64-specific */
            "./build/libsox_runtime.a",
            "./build/x64/Debug/libsox_runtime.a",
            "./build/x64/Release/libsox_runtime.a",
            "./bin/x64/Debug/libsox_runtime.a",
            "./bin/x64/Release/libsox_runtime.a",
            NULL
        };

        const char* runtime_path = NULL;
        struct stat st;
        for (int i = 0; candidates[i] != NULL; i++) {
            if (stat(candidates[i], &st) == 0) {
                runtime_path = candidates[i];
                if (options->verbose_linking || options->verbose) {
                    fprintf(stderr, "[CUSTOM LINKER] Found runtime library: %s\n", runtime_path);
                }
                break;
            }
        }

        if (!runtime_path) {
            fprintf(stderr, "Error: Runtime library not found (searched standard locations)\n");
            linker_context_free(context);
            return 1;
        }

        // Extract runtime objects from archive
        if (!archive_extract_objects(runtime_path, context, options->verbose_linking || options->verbose)) {
            fprintf(stderr, "Error: Failed to extract runtime library objects\n");
            linker_context_free(context);
            return 1;
        }
    }

    // Phase 2: Symbol resolution
    if (options->verbose_linking || options->verbose) {
        fprintf(stderr, "[CUSTOM LINKER] Phase 2: Resolving symbols...\n");
    }

    symbol_resolver_t* resolver = symbol_resolver_new();
    if (!resolver) {
        fprintf(stderr, "Error: Failed to create symbol resolver\n");
        linker_context_free(context);
        return 1;
    }

    // Add all objects to the symbol resolver
    for (int i = 0; i < context->object_count; i++) {
        symbol_resolver_add_object(resolver, context->objects[i], i);
    }

    if (!symbol_resolver_resolve(resolver)) {
        fprintf(stderr, "Error: Symbol resolution failed\n");
        int error_count;
        linker_error_t* errors = symbol_resolver_get_errors(resolver, &error_count);
        for (int i = 0; i < error_count; i++) {
            fprintf(stderr, "  %s: %s\n",
                    linker_error_type_name(errors[i].type),
                    errors[i].message ? errors[i].message : "Unknown error");
        }
        symbol_resolver_free(resolver);
        linker_context_free(context);
        return 1;
    }

    // Phase 3: Section layout
    if (options->verbose_linking || options->verbose) {
        fprintf(stderr, "[CUSTOM LINKER] Phase 3: Computing section layout...\n");
    }

    uint64_t base_address = get_default_base_address(context->target_format);
    section_layout_t* layout = section_layout_new(base_address, context->target_format);
    if (!layout) {
        fprintf(stderr, "Error: Failed to create section layout\n");
        symbol_resolver_free(resolver);
        linker_context_free(context);
        return 1;
    }

    // Add all objects to the layout
    for (int i = 0; i < context->object_count; i++) {
        section_layout_add_object(layout, context->objects[i], i);
    }

    section_layout_compute(layout);

    // Compute final symbol addresses based on layout
    if (!symbol_resolver_compute_addresses(resolver, layout)) {
        fprintf(stderr, "Error: Failed to compute symbol addresses\n");
        section_layout_free(layout);
        symbol_resolver_free(resolver);
        linker_context_free(context);
        return 1;
    }

    // Phase 4: Process relocations
    if (options->verbose_linking || options->verbose) {
        fprintf(stderr, "[CUSTOM LINKER] Phase 4: Processing relocations...\n");
    }

    relocation_processor_t* reloc_proc = relocation_processor_new(context, layout, resolver);
    if (!reloc_proc) {
        fprintf(stderr, "Error: Failed to create relocation processor\n");
        section_layout_free(layout);
        symbol_resolver_free(resolver);
        linker_context_free(context);
        return 1;
    }

    if (!relocation_processor_process_all(reloc_proc)) {
        fprintf(stderr, "Error: Relocation processing failed\n");
        int error_count;
        relocation_error_t* errors = relocation_processor_get_errors(reloc_proc, &error_count);
        for (int i = 0; i < error_count; i++) {
            fprintf(stderr, "  %s: %s\n",
                    relocation_error_type_name(errors[i].type),
                    errors[i].message ? errors[i].message : "Unknown error");
        }
        relocation_processor_free(reloc_proc);
        section_layout_free(layout);
        symbol_resolver_free(resolver);
        linker_context_free(context);
        return 1;
    }

    // Phase 5: Generate executable
    if (options->verbose_linking || options->verbose) {
        fprintf(stderr, "[CUSTOM LINKER] Phase 5: Generating executable...\n");
    }

    // Convert merged_section_t to linker_section_t for executable generation
    context->merged_sections = (linker_section_t*)malloc(layout->section_count * sizeof(linker_section_t));
    if (!context->merged_sections) {
        fprintf(stderr, "Error: Failed to allocate merged sections\n");
        relocation_processor_free(reloc_proc);
        section_layout_free(layout);
        symbol_resolver_free(resolver);
        linker_context_free(context);
        return 1;
    }

    context->merged_section_count = layout->section_count;
    for (int i = 0; i < layout->section_count; i++) {
        merged_section_t* src = &layout->sections[i];
        linker_section_t* dst = &context->merged_sections[i];

        dst->name = src->name ? strdup(src->name) : NULL;
        dst->type = src->type;
        dst->data = src->data;
        dst->size = src->size;
        dst->alignment = src->alignment;
        dst->vaddr = src->vaddr;
        dst->flags = src->flags;
        dst->object_index = 0;  // Merged from multiple objects

        // Transfer ownership: prevent double-free when both layout and context are freed
        src->data = NULL;
        src->name = NULL;
    }

    context->base_address = layout->base_address;
    context->total_size = layout->total_size;

    // Find entry point symbol
    linker_symbol_t* entry_sym = symbol_resolver_lookup(resolver, "_main");
    if (!entry_sym && context->target_format == PLATFORM_FORMAT_ELF) {
        entry_sym = symbol_resolver_lookup(resolver, "_start");
    }
    if (entry_sym && entry_sym->is_defined) {
        context->entry_point = entry_sym->final_address;
        if (options->verbose_linking || options->verbose) {
            fprintf(stderr, "[CUSTOM LINKER] Entry point: _main at 0x%llx\n",
                    (unsigned long long)context->entry_point);
        }
    } else {
        if (!entry_sym) {
            fprintf(stderr, "Warning: Entry point symbol '_main' not found\n");
        } else {
            fprintf(stderr, "Warning: Entry point symbol '_main' found but not defined (is_defined=%d, final_address=0x%llx)\n",
                    entry_sym->is_defined, (unsigned long long)entry_sym->final_address);
        }
        if (options->verbose_linking || options->verbose) {
            fprintf(stderr, "[CUSTOM LINKER] Using default entry point: 0x%llx\n",
                    (unsigned long long)context->base_address);
        }
        /* Default to start of __text section */
        context->entry_point = context->base_address;
    }

    bool success = false;
    if (context->target_format == PLATFORM_FORMAT_ELF) {
        success = elf_write_executable(options->output_file, context);
    } else if (context->target_format == PLATFORM_FORMAT_MACH_O) {
        success = macho_write_executable(options->output_file, context);
    }

    // Clean up
    fprintf(stderr, "[LINKER-CLEANUP] Starting cleanup...\n");
    fprintf(stderr, "[LINKER-CLEANUP] Freeing relocation processor...\n");
    relocation_processor_free(reloc_proc);
    fprintf(stderr, "[LINKER-CLEANUP] Freeing section layout...\n");
    section_layout_free(layout);
    fprintf(stderr, "[LINKER-CLEANUP] Freeing symbol resolver...\n");
    symbol_resolver_free(resolver);
    fprintf(stderr, "[LINKER-CLEANUP] Freeing linker context...\n");
    linker_context_free(context);
    fprintf(stderr, "[LINKER-CLEANUP] Cleanup complete\n");

    if (success) {
        if (options->verbose_linking || options->verbose) {
            fprintf(stderr, "[CUSTOM LINKER] Successfully linked: %s\n", options->output_file);
        }
        return 0;
    }

    fprintf(stderr, "Error: Failed to generate executable\n");
    return 1;
}

// Main linking entry point - Phase 6.1 API
// Selects between system and custom linker based on mode
int linker_link(const linker_options_t* options) {
    if (options == NULL) {
        fprintf(stderr, "Error: NULL options provided to linker_link\n");
        return 1;
    }

    if (options->output_file == NULL) {
        fprintf(stderr, "Error: No output file specified\n");
        return 1;
    }

    // Validate we have at least one input file
    bool has_input = false;
    if (options->input_file != NULL) {
        has_input = true;
    } else if (options->input_files != NULL && options->input_file_count > 0) {
        has_input = true;
    }

    if (!has_input) {
        fprintf(stderr, "Error: No input files specified\n");
        return 1;
    }

    if (options->verbose_linking || options->verbose) {
        fprintf(stderr, "\n=== Sox Linker ===\n");
        fprintf(stderr, "Mode: ");
        switch (options->mode) {
            case LINKER_MODE_SYSTEM:
                fprintf(stderr, "SYSTEM (using external linker)\n");
                break;
            case LINKER_MODE_CUSTOM:
                fprintf(stderr, "CUSTOM (using sox internal linker)\n");
                break;
            case LINKER_MODE_AUTO:
                fprintf(stderr, "AUTO (will auto-select)\n");
                break;
        }
        fprintf(stderr, "Target: %s-%s\n", options->target_os, options->target_arch);
        fprintf(stderr, "Output: %s\n", options->output_file);
    }

    // Route to appropriate linker based on mode
    if (options->mode == LINKER_MODE_SYSTEM) {
        // Explicit request for system linker
        if (options->verbose_linking || options->verbose) {
            fprintf(stderr, "Using system linker (explicit)\n");
        }
        linker_info_t linker = linker_get_preferred(options->target_os, options->target_arch);
        return linker_invoke(linker, options);

    } else if (options->mode == LINKER_MODE_CUSTOM) {
        // Explicit request for custom linker
        if (options->verbose_linking || options->verbose) {
            fprintf(stderr, "Using custom linker (explicit)\n");
        }
        return linker_link_custom(options);

    } else {
        // Auto mode: use custom for simple cases, system for complex
        if (linker_is_simple_link_job(options)) {
            if (options->verbose_linking || options->verbose) {
                fprintf(stderr, "Using custom linker (auto-selected)\n");
            }
            return linker_link_custom(options);
        } else {
            if (options->verbose_linking || options->verbose) {
                fprintf(stderr, "Using system linker (auto-selected)\n");
            }
            linker_info_t linker = linker_get_preferred(options->target_os, options->target_arch);
            return linker_invoke(linker, options);
        }
    }
}

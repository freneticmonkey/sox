#include "linker.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/utsname.h>

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

    if (linker.type == LINKER_CLANG || linker.type == LINKER_GCC) {
        // Using compiler wrapper (clang or gcc) - simplest and most reliable
        snprintf(cmd, sizeof(cmd), "%s %s -o %s",
                 linker.path,
                 options->input_file,
                 options->output_file);

        // Add runtime library if needed
        if (options->link_runtime) {
            strncat(cmd, " -L./build -lsox_runtime", sizeof(cmd) - strlen(cmd) - 1);
        }
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

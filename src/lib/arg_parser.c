#include "arg_parser.h"
#include "linker.h"
#include "../version.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "argtable3.h"

#define MAX_STRING 256

void print_help(const char* program_name) {
    // Define argtable for help display
    struct arg_file* input = arg_file0(NULL, NULL, "FILE", "Input Sox source or bytecode file");
    struct arg_lit* help = arg_lit0("h", "help", "Display this help message");
    struct arg_lit* version = arg_lit0("v", "version", "Display version information");

    struct arg_lit* serialise = arg_lit0(NULL, "serialise", "Enable bytecode serialization (cache compiled bytecode)");
    struct arg_lit* suppress = arg_lit0(NULL, "suppress-print", "Suppress print output (useful for testing)");

    struct arg_lit* wasm = arg_lit0(NULL, "wasm", "Generate WebAssembly binary output (.wasm)");
    struct arg_lit* wat = arg_lit0(NULL, "wat", "Generate WebAssembly text format output (.wat)");

    struct arg_lit* native = arg_lit0(NULL, "native", "Enable native code generation (default: generate executable)");
    struct arg_str* native_out = arg_str0(NULL, "native-out", "FILE", "Output executable or object file");
    struct arg_str* native_arch = arg_str0(NULL, "native-arch", "ARCH", "Target architecture: x86_64, arm64 (default: current platform)");
    struct arg_str* native_os = arg_str0(NULL, "native-os", "OS", "Target OS: linux, macos, windows (default: current platform)");
    struct arg_lit* native_obj = arg_lit0(NULL, "native-obj", "Emit object file (skip linking)");
    struct arg_lit* native_debug = arg_lit0(NULL, "native-debug", "Enable debug output during native code generation");
    struct arg_int* native_opt = arg_int0(NULL, "native-opt", "LEVEL", "Optimization level 0-3 (default: 0)");

    struct arg_end* end = arg_end(20);

    void* argtable[] = { help, version, input, serialise, suppress, wasm, wat,
                         native, native_out, native_arch, native_os, native_obj,
                         native_debug, native_opt, end };

    printf("Sox %s\n", VERSION);
    printf("A bytecode-based virtual machine interpreter for a toy programming language\n");
    printf("Commit: %s | Branch: %s | Build time: %s\n\n", COMMIT, BRANCH, BUILD_TIME);

    printf("\nUsage: %s", program_name);
    arg_print_syntax(stdout, argtable, "\n");
    printf("\n");

    printf("Options:\n");
    arg_print_glossary(stdout, argtable, "  %-30s %s\n");

    printf("\n");
    printf("Examples:\n");
    printf("  %s script.sox                   Run a Sox script\n", program_name);
    printf("  %s script.sox --wasm            Generate WebAssembly output\n", program_name);
    printf("  %s script.sox --native          Generate native executable (current platform)\n", program_name);
    printf("  %s script.sox --native --native-obj\n", program_name);
    printf("                                  Generate object file instead of executable\n");
    printf("  %s script.sox --native --native-arch arm64 --native-os macos\n", program_name);
    printf("                                  Cross-compile to ARM64 macOS\n");
    printf("  %s script.sox --serialise       Cache compiled bytecode\n", program_name);
    printf("\nFor more information, visit: https://github.com/freneticmonkey/sox\n");

    // Cleanup
    arg_freetable(argtable, sizeof(argtable)/sizeof(argtable[0]));
}

void print_version(void) {
    printf("Sox %s\n", VERSION);
    printf("Commit: %s\n", COMMIT);
    printf("Branch: %s\n", BRANCH);
    printf("Build time: %s\n", BUILD_TIME);
}

bool parse_arguments(int argc, const char* argv[], sox_args_t* args) {
    // Initialize args structure
    memset(args, 0, sizeof(sox_args_t));
    args->argc = argc;
    args->argv = argv;

    // Set defaults based on current platform
    platform_t current_platform = linker_get_current_platform();
    args->native_target_arch = (char*)current_platform.arch;
    args->native_target_os = (char*)current_platform.os;
    args->native_emit_object = true;
    args->native_optimization_level = 0;

    // Define argtable
    struct arg_file* input = arg_file0(NULL, NULL, "FILE", "Input Sox source or bytecode file");
    struct arg_lit* help = arg_lit0("h", "help", "Display help message");
    struct arg_lit* version = arg_lit0("v", "version", "Display version information");

    struct arg_lit* serialise = arg_lit0(NULL, "serialise", "Enable bytecode serialization");
    struct arg_lit* suppress = arg_lit0(NULL, "suppress-print", "Suppress print output");

    struct arg_lit* wasm = arg_lit0(NULL, "wasm", "Generate WebAssembly binary");
    struct arg_lit* wat = arg_lit0(NULL, "wat", "Generate WebAssembly text format");

    struct arg_lit* native = arg_lit0(NULL, "native", "Enable native code generation");
    struct arg_str* native_out = arg_str0(NULL, "native-out", "FILE", "Native output file");
    struct arg_str* native_arch = arg_str0(NULL, "native-arch", "ARCH", "Target arch (x86_64, arm64)");
    struct arg_str* native_os = arg_str0(NULL, "native-os", "OS", "Target OS (linux, macos, windows)");
    struct arg_lit* native_obj = arg_lit0(NULL, "native-obj", "Emit object file");
    struct arg_lit* native_debug = arg_lit0(NULL, "native-debug", "Debug output");
    struct arg_int* native_opt = arg_int0(NULL, "native-opt", "LEVEL", "Optimization level 0-3");

    struct arg_end* end = arg_end(20);

    void* argtable[] = { help, version, input, serialise, suppress, wasm, wat,
                         native, native_out, native_arch, native_os, native_obj,
                         native_debug, native_opt, end };

    // Parse arguments
    int nerrors = arg_parse(argc, (char**)argv, argtable);

    // Check for help request
    if (help->count > 0) {
        args->show_help = true;
        arg_freetable(argtable, sizeof(argtable)/sizeof(argtable[0]));
        return true;
    }

    // Check for version request
    if (version->count > 0) {
        args->show_version = true;
        arg_freetable(argtable, sizeof(argtable)/sizeof(argtable[0]));
        return true;
    }

    // If no arguments at all, show help
    if (argc == 1) {
        args->show_help = true;
        arg_freetable(argtable, sizeof(argtable)/sizeof(argtable[0]));
        return true;
    }

    // Handle parsing errors
    if (nerrors > 0) {
        fprintf(stderr, "Error: Invalid arguments\n");
        arg_print_errors(stderr, end, "sox");
        fprintf(stderr, "\nUse 'sox --help' for usage information.\n");
        arg_freetable(argtable, sizeof(argtable)/sizeof(argtable[0]));
        return false;
    }

    // Extract input file
    if (input->count > 0) {
        args->input_file = (char*)input->filename[0];
    }

    // Extract options
    args->enable_serialisation = (serialise->count > 0);
    args->suppress_print = (suppress->count > 0);
    args->enable_wasm_output = (wasm->count > 0);
    args->enable_wat_output = (wat->count > 0);

    // Native code generation options
    if (native->count > 0) {
        args->enable_native_output = true;

        // Determine if we're emitting object or executable (defaults to executable)
        bool will_emit_object = (native_obj->count > 0);

        // Native output file
        if (native_out->count > 0) {
            args->native_output_file = (char*)native_out->sval[0];
        } else if (args->input_file) {
            // Generate default output filename from input
            static char default_output[MAX_STRING];
            if (will_emit_object) {
                snprintf(default_output, sizeof(default_output), "%s.o", args->input_file);
            } else {
                // For executables, use input filename without extension
                const char* last_dot = strrchr(args->input_file, '.');
                if (last_dot != NULL) {
                    snprintf(default_output, sizeof(default_output), "%.*s",
                            (int)(last_dot - args->input_file), args->input_file);
                } else {
                    snprintf(default_output, sizeof(default_output), "%s", args->input_file);
                }
            }
            args->native_output_file = default_output;
        }

        // Native target architecture
        if (native_arch->count > 0) {
            const char* arch = native_arch->sval[0];
            if (strcmp(arch, "x86_64") == 0 || strcmp(arch, "x64") == 0) {
                args->native_target_arch = "x86_64";
            } else if (strcmp(arch, "arm64") == 0 || strcmp(arch, "aarch64") == 0) {
                args->native_target_arch = "arm64";
            } else {
                fprintf(stderr, "Error: Unknown architecture '%s'. Use: x86_64, arm64\n", arch);
                arg_freetable(argtable, sizeof(argtable)/sizeof(argtable[0]));
                return false;
            }
        }

        // Native target OS
        if (native_os->count > 0) {
            const char* os = native_os->sval[0];
            if (strcmp(os, "linux") == 0) {
                args->native_target_os = "linux";
            } else if (strcmp(os, "macos") == 0 || strcmp(os, "darwin") == 0) {
                args->native_target_os = "macos";
            } else if (strcmp(os, "windows") == 0 || strcmp(os, "win32") == 0) {
                args->native_target_os = "windows";
            } else {
                fprintf(stderr, "Error: Unknown OS '%s'. Use: linux, macos, windows\n", os);
                arg_freetable(argtable, sizeof(argtable)/sizeof(argtable[0]));
                return false;
            }
        }

        // Default to executable (emit_object = false) unless --native-obj is specified
        args->native_emit_object = (native_obj->count > 0);
        args->native_debug_output = (native_debug->count > 0);

        if (native_opt->count > 0) {
            int opt_level = native_opt->ival[0];
            if (opt_level < 0 || opt_level > 3) {
                fprintf(stderr, "Error: Optimization level must be 0-3, got %d\n", opt_level);
                arg_freetable(argtable, sizeof(argtable)/sizeof(argtable[0]));
                return false;
            }
            args->native_optimization_level = opt_level;
        }
    }

    arg_freetable(argtable, sizeof(argtable)/sizeof(argtable[0]));
    return true;
}

void free_arguments(sox_args_t* args) {
    // The string pointers are borrowed from argv, so we don't free them
    // Just clear the structure
    memset(args, 0, sizeof(sox_args_t));
}

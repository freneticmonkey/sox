#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "common.h"
#include "chunk.h"
#include "version.h"
#include "vm.h"
#include "lib/debug.h"
#include "lib/file.h"
#include "lib/arg_parser.h"

static void _repl() {
    char line[1024];
    for (;;) {
        printf("> ");

        if (!fgets(line, sizeof(line), stdin)) {
            printf("\n");
            break;
        }

        l_interpret(line);
    }
}

int main(int argc, const char* argv[]) {
    fprintf(stderr, "[MAIN] Entered main(), argc=%d\n", argc);

    // Parse command-line arguments
    sox_args_t args;
    if (!parse_arguments(argc, argv, &args)) {
        fprintf(stderr, "Error parsing arguments\n");
        return 1;
    }

    // Handle help request
    if (args.show_help) {
        print_help(argv[0]);
        return 0;
    }

    // Handle version request
    if (args.show_version) {
        print_version();
        return 0;
    }

    // If no input file, start REPL
    if (!args.input_file) {
        printf("Starting sox %s ...\ncommit: %s\nbranch: %s\n",
            VERSION,
            COMMIT,
            BRANCH
        );

        vm_config_t config;
        l_init_vmconfig(&config, argc, argv);

        // Copy over parsed arguments to config
        config.enable_serialisation = args.enable_serialisation;
        config.suppress_print = args.suppress_print;
        config.enable_wasm_output = args.enable_wasm_output;
        config.enable_wat_output = args.enable_wat_output;
        config.enable_native_output = args.enable_native_output;
        config.native_output_file = args.native_output_file;
        config.native_target_arch = args.native_target_arch;
        config.native_target_os = args.native_target_os;
        config.native_emit_object = args.native_emit_object;
        config.native_debug_output = args.native_debug_output;
        config.native_optimization_level = args.native_optimization_level;
        config.use_custom_linker = args.use_custom_linker;

        l_init_memory();
        l_init_vm(&config);
        _repl();
        l_free_vm();
        l_free_memory();
    } else {
        // Run file with parsed arguments
        vm_config_t config;
        l_init_vmconfig(&config, argc, argv);

        // Copy over parsed arguments to config
        config.enable_serialisation = args.enable_serialisation;
        config.suppress_print = args.suppress_print;
        config.enable_wasm_output = args.enable_wasm_output;
        config.enable_wat_output = args.enable_wat_output;
        config.enable_native_output = args.enable_native_output;
        config.native_output_file = args.native_output_file;
        config.native_target_arch = args.native_target_arch;
        config.native_target_os = args.native_target_os;
        config.native_emit_object = args.native_emit_object;
        config.native_debug_output = args.native_debug_output;
        config.native_optimization_level = args.native_optimization_level;
        config.use_custom_linker = args.use_custom_linker;

        int status = l_run_file(&config);

        l_free_vmconfig(&config);

        if (status != 0)
            exit(status);
    }

    printf("Exiting sox ...\n");
    return 0;
}
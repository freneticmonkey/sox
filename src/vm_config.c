#include "vm_config.h"

#include <string.h>

#include "lib/memory.h"

vm_args_t l_default_args() {
    return (vm_args_t) {
        .argc = 0,
    };
}

vm_args_t l_parse_args(int argc, const char *argv[]) {

    vm_args_t result = {
        .argc = argc,
        // .argv = NULL,
    };
    // result.argv = NULL;

    // if there are arguments to parse
    if ( argc > 0 ) {

        // copy the args into the result
        for (int i = 0; i < argc; i++) {
            // memcpy the each arg string
            result.argv[i] = ALLOCATE(char, sizeof(argv[i]));
            size_t length = strlen(argv[i]) * sizeof(char);
            memccpy(result.argv[i], argv[i], 0, length);
        }
    }

    return result;
}

void l_free_args(vm_args_t *args) {
    if ( args->argc > 0 ) {
        for (int i = 0; i < args->argc; i++) {
            FREE(char, args->argv[i]);
        }
    }
}

vm_config_t l_default_vmconfig() {
    return (vm_config_t){
        .enable_serialisation = false,
        .suppress_print = false,
        .args = l_default_args(),
    };
}

void l_init_vmconfig(vm_config_t *config, int argc, const char *argv[]) {
    
    config->enable_serialisation = false;
    config->suppress_print = false;
    config->args = l_parse_args(argc, argv);

    // check the args for flags
    if (config->args.argc > 2) {
    for (int i = 2; i < config->args.argc; i++) {

        // if the arg is null or doesn't start with a dash
        if ( config->args.argv[i] == NULL || config->args.argv[i][0] != '-' ) {
            continue;
        }

        if (strcmp(config->args.argv[i], "--serialise") == 0) {
            config->enable_serialisation = true;
        }
        if (strcmp(config->args.argv[i], "--suppress-print") == 0) {
            config->suppress_print = true;
        }
    }
}

}

void l_free_vmconfig(vm_config_t *config) {
    l_free_args(&config->args);
}
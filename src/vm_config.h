#ifndef _VM_CONFIG_H_
#define _VM_CONFIG_H_

#include <stdbool.h>

#define MAX_ARGS 64

typedef struct vm_args_t {
    // cli args
    int argc;
    char * argv[MAX_ARGS];
} vm_args_t;

typedef struct {
    // additional config
    bool enable_serialisation;

    // for unit testing
    bool suppress_print;

    // WASM/WAT output options
    bool enable_wasm_output;
    bool enable_wat_output;

    // Native code generation options
    bool enable_native_output;
    char* native_output_file;
    char* native_target_arch;      // "x86_64", "arm64"
    char* native_target_os;        // "linux", "macos", "windows"
    bool native_emit_object;
    bool native_debug_output;
    int native_optimization_level;

    vm_args_t args;
} vm_config_t;

vm_args_t l_default_args();
vm_args_t l_parse_args(int argc, const char *argv[]);
void      l_free_args(vm_args_t *args);

vm_config_t l_default_vmconfig();
void        l_init_vmconfig(vm_config_t *config, int argc, const char *argv[]);
void        l_free_vmconfig(vm_config_t *config);

#endif
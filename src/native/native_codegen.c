#include "native_codegen.h"
#include "ir_builder.h"
#include "codegen.h"
#include "codegen_arm64.h"
#include "elf_writer.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

bool native_codegen_generate(obj_closure_t* closure, const native_codegen_options_t* options) {
    if (!closure || !options) {
        fprintf(stderr, "Error: Invalid arguments to native_codegen_generate\n");
        return false;
    }

    printf("Native Code Generation\n");
    printf("======================\n");
    printf("Output file: %s\n", options->output_file);
    printf("Target: %s-%s\n", options->target_arch, options->target_os);
    printf("\n");

    // Step 1: Build IR from bytecode
    printf("[1/4] Building IR from bytecode...\n");
    ir_module_t* module = ir_builder_build_module(closure);
    if (!module) {
        fprintf(stderr, "Error: Failed to build IR module\n");
        return false;
    }

    if (options->debug_output) {
        printf("\nGenerated IR:\n");
        ir_module_print(module);
    }

    // Step 2: Generate native code based on architecture
    size_t code_size;
    uint8_t* code = NULL;
    uint16_t machine_type;
    bool is_arm64 = (strcmp(options->target_arch, "arm64") == 0 ||
                     strcmp(options->target_arch, "aarch64") == 0);

    if (is_arm64) {
        printf("[2/4] Generating ARM64 machine code...\n");
        codegen_arm64_context_t* codegen = codegen_arm64_new(module);
        if (!codegen_arm64_generate(codegen)) {
            fprintf(stderr, "Error: Failed to generate ARM64 code\n");
            codegen_arm64_free(codegen);
            ir_module_free(module);
            return false;
        }

        if (options->debug_output) {
            printf("\nGenerated machine code:\n");
            codegen_arm64_print(codegen);
        }

        code = codegen_arm64_get_code(codegen, &code_size);
        machine_type = EM_AARCH64;
        codegen_arm64_free(codegen);
    } else {
        printf("[2/4] Generating x86-64 machine code...\n");
        codegen_context_t* codegen = codegen_new(module);
        if (!codegen_generate(codegen)) {
            fprintf(stderr, "Error: Failed to generate x86-64 code\n");
            codegen_free(codegen);
            ir_module_free(module);
            return false;
        }

        if (options->debug_output) {
            printf("\nGenerated machine code:\n");
            codegen_print(codegen);
        }

        code = codegen_get_code(codegen, &code_size);
        machine_type = EM_X86_64;
        codegen_free(codegen);
    }

    printf("Generated %zu bytes of machine code\n", code_size);

    // Step 4: Write output file
    printf("[3/4] Writing output file...\n");
    bool success = false;

    if (options->emit_object) {
        // Generate object file
        const char* func_name = closure->function->name ?
                                closure->function->name->chars : "sox_main";
        success = elf_create_object_file(options->output_file, code, code_size,
                                          func_name, machine_type);
    } else {
        // For executables, we'd need to link with runtime library
        fprintf(stderr, "Warning: Executable generation not yet implemented, generating object file\n");
        char obj_file[256];
        snprintf(obj_file, sizeof(obj_file), "%s.o", options->output_file);
        const char* func_name = closure->function->name ?
                                closure->function->name->chars : "sox_main";
        success = elf_create_object_file(obj_file, code, code_size,
                                          func_name, machine_type);

        if (success) {
            printf("Generated object file: %s\n", obj_file);
            printf("\nTo create executable, link with:\n");
            printf("  gcc %s -o %s -L/path/to/sox/runtime -lsox_runtime\n",
                   obj_file, options->output_file);
        }
    }

    if (success) {
        printf("[4/4] Done!\n");
        printf("\nSuccessfully generated: %s\n", options->output_file);
    } else {
        fprintf(stderr, "Error: Failed to write output file\n");
    }

    // Cleanup
    ir_module_free(module);

    return success;
}

bool native_codegen_generate_object(obj_closure_t* closure, const char* output_file) {
    native_codegen_options_t options = {
        .output_file = output_file,
        .target_arch = "x86_64",
        .target_os = "linux",
        .emit_object = true,
        .debug_output = false,
        .optimization_level = 0
    };
    return native_codegen_generate(closure, &options);
}

bool native_codegen_generate_executable(obj_closure_t* closure, const char* output_file) {
    native_codegen_options_t options = {
        .output_file = output_file,
        .target_arch = "x86_64",
        .target_os = "linux",
        .emit_object = false,
        .debug_output = false,
        .optimization_level = 0
    };
    return native_codegen_generate(closure, &options);
}

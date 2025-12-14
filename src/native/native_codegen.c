#include "native_codegen.h"
#include "ir_builder.h"
#include "codegen.h"
#include "codegen_arm64.h"
#include "elf_writer.h"
#include "macho_writer.h"
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

    // Keep codegen contexts alive until after writing (code and relocations point into them)
    codegen_arm64_context_t* codegen_arm64 = NULL;
    codegen_context_t* codegen_x64 = NULL;
    codegen_relocation_t* relocations_x64 = NULL;
    int relocation_count_x64 = 0;
    arm64_relocation_t* relocations_arm64 = NULL;
    int relocation_count_arm64 = 0;

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

        // Extract relocations before freeing codegen context
        fprintf(stderr, "[NATIVE-CODEGEN] About to extract ARM64 relocations...\n");
        relocations_arm64 = codegen_arm64_get_relocations(codegen, &relocation_count_arm64);
        fprintf(stderr, "[NATIVE-CODEGEN] Extracted relocations_arm64=%p, count=%d\n", relocations_arm64, relocation_count_arm64);

        machine_type = EM_AARCH64;
        // Don't free codegen yet - code and relocations point into it
        codegen_arm64 = codegen;
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

        // Extract relocations before freeing codegen context
        int reloc_count;
        codegen_relocation_t* relocs = codegen_get_relocations(codegen, &reloc_count);

        // Store for later use (need to keep valid until ELF writing)
        relocations_x64 = relocs;
        relocation_count_x64 = reloc_count;

        machine_type = EM_X86_64;
        // Don't free codegen yet - relocations point into it
        codegen_x64 = codegen;
    }

    printf("Generated %zu bytes of machine code\n", code_size);

    // Step 4: Write output file
    printf("[3/4] Writing output file...\n");
    bool success = false;
    bool use_macho = (strcmp(options->target_os, "macos") == 0 ||
                      strcmp(options->target_os, "darwin") == 0);

    const char* func_name = closure->function->name ?
                            closure->function->name->chars : "sox_main";

    if (options->emit_object) {
        // Generate object file with script symbol
        if (use_macho) {
            // Determine CPU type for Mach-O
            uint32_t cputype, cpusubtype;
            if (is_arm64) {
                cputype = CPU_TYPE_ARM64;
                cpusubtype = CPU_SUBTYPE_ARM64_ALL;
                // Use relocation-aware Mach-O writer for ARM64
                fprintf(stderr, "[NATIVE-CODEGEN] Calling macho_create_object_file_with_arm64_relocs: file=%s, code_size=%zu, relocations_arm64=%p, reloc_count=%d\n",
                       options->output_file, code_size, relocations_arm64, relocation_count_arm64);
                success = macho_create_object_file_with_arm64_relocs(options->output_file, code, code_size,
                                                                     func_name, cputype, cpusubtype,
                                                                     relocations_arm64, relocation_count_arm64);
                fprintf(stderr, "[NATIVE-CODEGEN] macho_create_object_file_with_arm64_relocs returned: %s\n", success ? "true" : "false");
            } else {
                cputype = CPU_TYPE_X86_64;
                cpusubtype = CPU_SUBTYPE_X86_64_ALL;
                success = macho_create_object_file(options->output_file, code, code_size,
                                                    func_name, cputype, cpusubtype);
            }
        } else {
            // Use relocation-aware ELF writer for x86-64
            if (!is_arm64 && relocation_count_x64 > 0) {
                success = elf_create_object_file_with_relocations(options->output_file, code, code_size,
                                                                  func_name, machine_type,
                                                                  relocations_x64, relocation_count_x64);
            } else {
                success = elf_create_object_file(options->output_file, code, code_size,
                                                  func_name, machine_type);
            }
        }
    } else {
        // Generate executable-ready object file with main entry point
        // Note: output_file should already be set to a temp file (.tmp.o) by file.c
        if (use_macho) {
            uint32_t cputype, cpusubtype;
            if (is_arm64) {
                cputype = CPU_TYPE_ARM64;
                cpusubtype = CPU_SUBTYPE_ARM64_ALL;
                // Use relocation-aware Mach-O writer for ARM64
                fprintf(stderr, "[NATIVE-CODEGEN] Calling macho_create_executable_object_file_with_arm64_relocs: file=%s, code_size=%zu, relocations_arm64=%p, reloc_count=%d\n",
                       options->output_file, code_size, relocations_arm64, relocation_count_arm64);
                success = macho_create_executable_object_file_with_arm64_relocs(options->output_file, code, code_size,
                                                                                 cputype, cpusubtype,
                                                                                 relocations_arm64, relocation_count_arm64);
                fprintf(stderr, "[NATIVE-CODEGEN] macho_create_executable_object_file_with_arm64_relocs returned: %s\n", success ? "true" : "false");
            } else {
                cputype = CPU_TYPE_X86_64;
                cpusubtype = CPU_SUBTYPE_X86_64_ALL;
                success = macho_create_executable_object_file(options->output_file, code, code_size,
                                                              cputype, cpusubtype);
            }
        } else {
            // Use relocation-aware ELF writer for x86-64
            if (!is_arm64 && relocation_count_x64 > 0) {
                success = elf_create_executable_object_file_with_relocations(options->output_file, code, code_size,
                                                                             machine_type,
                                                                             relocations_x64, relocation_count_x64);
            } else {
                success = elf_create_executable_object_file(options->output_file, code, code_size,
                                                            machine_type);
            }
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

    // Free ARM64 codegen context if it was used
    if (codegen_arm64) {
        codegen_arm64_free(codegen_arm64);
    }

    // Free x86-64 codegen context if it was used
    if (codegen_x64) {
        codegen_free(codegen_x64);
    }

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

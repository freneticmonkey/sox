#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#include <direct.h>
#else
#include <dirent.h>
#include <sys/types.h>
#endif

#include "lib/file.h"
#include "lib/print.h"
#include "lib/linker.h"
#include "serialise.h"
#include "vm.h"
#include "wat_generator.h"
#include "wasm_generator.h"
#include "compiler.h"
#include "native/native_codegen.h"

char* l_read_file(const char* path) {
    FILE* file = fopen(path, "rb");
    if (file == NULL) {
        fprintf(stderr, "Could not open file \"%s\".\n", path);
        return NULL;
    }

    fseek(file, 0L, SEEK_END);
    size_t fileSize = ftell(file);
    rewind(file);

    char* buffer = (char*)malloc(fileSize + 1);
    if (buffer == NULL) {
        fprintf(stderr, "Not enough memory to read \"%s\".\n", path);
        fclose(file);
        return NULL;
    }
    size_t bytesRead = fread(buffer, sizeof(char), fileSize, file);
    if (bytesRead < fileSize) {
        fprintf(stderr, "Could not read file \"%s\".\n", path);
        free(buffer);
        fclose(file);
        return NULL;
    }
    buffer[bytesRead] = '\0';

    fclose(file);
    return buffer;
}

bool l_file_exists(const char* path) {
    FILE* file = fopen(path, "rb");
    if (file == NULL) {
        return false;
    }
    fclose(file);
    return true;
}

int _deserialise_bytecode(vm_config_t *config, const char* path, const char* source) {

    l_init_memory();

    // Check for existing bytecode
    serialiser_t * serialiser = l_serialise_new(path, source, SERIALISE_MODE_READ);

    if ( serialiser->error != SERIALISE_OK) {

        switch ( serialiser->error ) {
            case SERIALISE_ERROR_SOURCE_HASH_MISMATCH:
                printf("source change detected. recompiling");
                break;
            case SERIALISE_ERROR_SOX_VERSION_MISMATCH:
                printf("Bytecode version for an old Sox version. recompiling");
                break;
            default:
                break;
        }
        return 1;
    }

    // deserialise the bytecode file

    // start tracking allocations
    l_allocate_track_init();

    // initialise the vm
    l_init_vm(config);

    // deserialise the objects
    obj_closure_t * entry_point = l_deserialise_vm(serialiser);

    // link the objects
    l_allocate_track_link_targets();

    printf("linking complete\n");

    // free tracking allocations
    l_allocate_track_free();

    // set the entry point for the VM
    // this must be done after the link step
    l_set_entry_point(entry_point);

    // free the reader serialiser
    l_serialise_del(serialiser);

    // run the vm
    InterpretResult result = l_run();

    //clean up
    l_free_vm();

    l_free_memory();

    if (result == INTERPRET_COMPILE_ERROR)
        return 65;
    if (result == INTERPRET_RUNTIME_ERROR)
        return 70;

    return 0;
}

int _interpret_serialise_bytecode(vm_config_t *config, const char* path, const char* source) {
    // setup to serialise the interpreted bytecode

    l_init_memory();

    // setup the writer serialiser
    serialiser_t * serialiser = l_serialise_new(path, source, SERIALISE_MODE_WRITE);

    l_init_vm(config);

    l_serialise_vm_set_init_state(serialiser);

    // interpret the source
    InterpretResult result = l_interpret(source);


    if (result == INTERPRET_COMPILE_ERROR)
        return 65;

    // serialise the vm state
    l_serialise_vm(serialiser);

    // flush to the file and free the serialiser
    l_serialise_flush(serialiser);
    l_serialise_finalise(serialiser);

    l_serialise_del(serialiser);

    // run the vm
    result = l_run();

    //clean up
    l_free_vm();

    l_free_memory();

    if (result == INTERPRET_COMPILE_ERROR)
        return 65;

    if (result == INTERPRET_RUNTIME_ERROR)
        return 70;

    return 0;
}

int _generate_wasm_wat(vm_config_t *config, const char* path, const char* source) {
    l_init_memory();

    // Initialize VM for compilation
    l_init_vm(config);

    // Compile the source to get the function
    obj_function_t* function = l_compile(source);

    if (function == NULL) {
        l_free_vm();
        l_free_memory();
        return 65; // Compile error
    }

    // Generate WAT file if requested
    if (config->enable_wat_output) {
        wat_generator_t* wat_gen = l_wat_new(path);
        if (wat_gen == NULL) {
            l_free_vm();
            l_free_memory();
            return 70; // Runtime error
        }

        WatErrorCode wat_result = l_wat_generate_from_function(wat_gen, function);

        if (wat_result == WAT_OK) {
            wat_result = l_wat_write_to_file(wat_gen);
            if (wat_result == WAT_OK) {
                printf("WAT file generated: %s.wat\n", path);
            } else {
                fprintf(stderr, "WAT generation error: %s\n", l_wat_get_error_string(wat_result));
            }
        } else {
            fprintf(stderr, "WAT generation error: %s\n", l_wat_get_error_string(wat_result));
        }

        l_wat_del(wat_gen);
    }

    // Generate WASM file if requested
    if (config->enable_wasm_output) {
        wasm_generator_t* wasm_gen = l_wasm_new(path);
        if (wasm_gen == NULL) {
            l_free_vm();
            l_free_memory();
            return 70; // Runtime error
        }

        WasmErrorCode wasm_result = l_wasm_generate_from_function(wasm_gen, function);

        if (wasm_result == WASM_OK) {
            wasm_result = l_wasm_write_to_file(wasm_gen);
            if (wasm_result == WASM_OK) {
                printf("WASM file generated: %s.wasm\n", path);
            } else {
                fprintf(stderr, "WASM generation error: %s\n", l_wasm_get_error_string(wasm_result));
            }
        } else {
            fprintf(stderr, "WASM generation error: %s\n", l_wasm_get_error_string(wasm_result));
        }

        l_wasm_del(wasm_gen);
    }

    // Cleanup: free VM and memory before returning (Phase 1.3 fix)
    l_free_vm();
    l_free_memory();
    return 0;
}

int _generate_native(vm_config_t *config, const char* path, const char* source) {
    l_init_memory();

    // Initialize VM for compilation
    l_init_vm(config);

    // Compile the source to get the function
    obj_function_t* function = l_compile(source);

    if (function == NULL) {
        l_free_vm();
        l_free_memory();
        return 65; // Compile error
    }

    // Create closure for native code generation
    // Simply wrap the compiled function in a closure structure
    obj_closure_t closure_obj;
    closure_obj.obj.type = OBJ_CLOSURE;
    closure_obj.function = function;
    closure_obj.upvalue_count = 0;

    obj_closure_t* closure = &closure_obj;

    // Determine output file
    const char* final_output = config->native_output_file;
    if (final_output == NULL) {
        // Generate default output filename
        static char default_output[256];
        if (config->native_emit_object) {
            snprintf(default_output, sizeof(default_output), "%s.o", path);
        } else {
            // For executables, strip the .sox extension
            const char* last_dot = strrchr(path, '.');
            if (last_dot != NULL && strcmp(last_dot, ".sox") == 0) {
                snprintf(default_output, sizeof(default_output), "%.*s",
                        (int)(last_dot - path), path);
            } else {
                snprintf(default_output, sizeof(default_output), "%s", path);
            }
        }
        final_output = default_output;
    }

    // If we need to generate an executable, use a temp object file
    const char* object_file = final_output;
    char temp_object[512] = {0};

    if (!config->native_emit_object) {
        // We'll generate a temporary object file and then link it
        // Check if the path + suffix will fit in the buffer
        size_t final_len = strlen(final_output);
        const char* suffix = ".tmp.o";
        size_t suffix_len = strlen(suffix);

        if (final_len + suffix_len + 1 > sizeof(temp_object)) {
            fprintf(stderr, "Error: Output path too long (%zu chars). Maximum path length is %zu chars.\n",
                    final_len, sizeof(temp_object) - suffix_len - 1);
            l_free_vm();
            l_free_memory();
            return 70;  // Runtime error
        }

        snprintf(temp_object, sizeof(temp_object), "%s%s", final_output, suffix);
        object_file = temp_object;
    }

    // Create native codegen options from config
    native_codegen_options_t options = {
        .output_file = object_file,
        .target_arch = config->native_target_arch,
        .target_os = config->native_target_os,
        .emit_object = config->native_emit_object,  // True for object files, false for linking
        .debug_output = config->native_debug_output,
        .optimization_level = config->native_optimization_level
    };

    // Generate native code
    bool success = native_codegen_generate(closure, &options);

    // Cleanup VM before linking
    l_free_vm();
    l_free_memory();

    if (!success) {
        return 70;  // 70 = runtime error
    }

    // If we only need the object file, we're done
    if (config->native_emit_object) {
        return 0;
    }

    // Otherwise, try to link the object file into an executable
    printf("[4/4] Linking object file into executable...\n");

    // Prepare linker options
    linker_options_t linker_opts = {
        .input_file = object_file,
        .output_file = final_output,
        .target_os = config->native_target_os,
        .target_arch = config->native_target_arch,
        .link_runtime = true,  // Link with libsox_runtime for runtime functions
        .verbose = config->native_debug_output,
        .mode = config->use_custom_linker ? LINKER_MODE_CUSTOM : LINKER_MODE_SYSTEM
    };

    // Use unified linker API (auto-selects system/custom based on mode)
    int link_result = linker_link(&linker_opts);

    // Clean up temporary object file
    if (link_result == 0) {
        remove(object_file);
        printf("Successfully linked executable: %s\n", final_output);
    } else {
        fprintf(stderr, "Error: Linking failed. Keeping object file: %s\n", object_file);
    }
    return link_result;
}

int _interpret_run(vm_config_t *config, const char* source) {

    InterpretResult result;

    l_init_memory();

    // start the VM and interpret the source
    l_init_vm(config);

    // interpret the source
    result = l_interpret(source);

    if (result == INTERPRET_COMPILE_ERROR) {
        l_free_vm();
        l_free_memory();
        return 65;
    }

    // run the vm
    result = l_run();

    //clean up
    l_free_vm();

    l_free_memory();

    if (result == INTERPRET_COMPILE_ERROR)
        return 65;
    if (result == INTERPRET_RUNTIME_ERROR)
        return 70;

    return 0;
}

int l_run_file(vm_config_t *config) {
    if (config->args.argc < 2) {
        fprintf(stderr, "Usage: sox [path] [optional: --serialise]\n");
        return 64;
    }
    const char* path = config->args.argv[1];

    if (path == NULL) {
        fprintf(stderr, "Usage: sox [path] [optional: --serialise]\n");
        return 64;
    }

    char* source = l_read_file(path);

    if (source == NULL) {
        printf("Could not read file: %s\n", path);
        return 74;
    }

    if ( source == NULL )
        return 74;

    if (config->suppress_print) {
        l_print_enable_suppress();
    }

    InterpretResult result;

    // Check if we need to generate native code
    if (config->enable_native_output) {
        int native_result = _generate_native(config, path, source);
        if (native_result != 0) {
            free(source);
            return native_result;
        }

        // If only generating native code, don't run the program
        free(source);
        return 0;
    }

    // Check if we only need to generate WASM/WAT without running
    if (config->enable_wasm_output || config->enable_wat_output) {
        int wasm_result = _generate_wasm_wat(config, path, source);
        if (wasm_result != 0) {
            free(source);
            return wasm_result;
        }

        // If only generating WASM/WAT, don't run the program unless serialization is also enabled
        if (!config->enable_serialisation) {
            free(source);
            return 0;
        }
    }

    if (config->enable_serialisation == false) {
        result = _interpret_run(config, source);

    } else {
        char filename_bytecode[256];
        snprintf(filename_bytecode, sizeof(filename_bytecode), "%s.sbc", path);

        if (l_file_exists(filename_bytecode)) {
            result = _deserialise_bytecode(config, path, source);
        } else {
            result = _interpret_serialise_bytecode(config, path, source);
        }

    }

    // clean up the source code
    free(source);

    return result;
}

bool l_file_delete(const char * path) {
    if (l_file_exists(path) == false)
        return false;

    return remove(path) == 0;
}

// Module path resolution
// Tries to find the module file by:
// 1. Direct path
// 2. Path with .sox extension
// 3. Relative paths
// Returns allocated string that must be freed, or NULL if not found
char* l_resolve_module_path(const char* module_path) {
    char resolved[4096];

    // Try direct path
    if (l_file_exists(module_path)) {
        return strdup(module_path);
    }

    // Try with .sox extension
    snprintf(resolved, sizeof(resolved), "%s.sox", module_path);
    if (l_file_exists(resolved)) {
        return strdup(resolved);
    }

    // Try in current directory with .sox extension
    snprintf(resolved, sizeof(resolved), "./%s.sox", module_path);
    if (l_file_exists(resolved)) {
        return strdup(resolved);
    }

    // Try in current directory without extension
    snprintf(resolved, sizeof(resolved), "./%s", module_path);
    if (l_file_exists(resolved)) {
        return strdup(resolved);
    }

    // TODO: Add search paths (module_paths array in VM)

    return NULL;  // Not found
}

static int _compare_strings(const void* a, const void* b) {
    return strcmp(*(const char**)a, *(const char**)b);
}

#ifdef _WIN32
char** l_scan_directory(const char* dir_path, const char* extension, int* count) {
    *count = 0;
    
    char search_path[MAX_PATH];
    snprintf(search_path, sizeof(search_path), "%s/*%s", dir_path, extension);
    
    WIN32_FIND_DATAA find_data;
    HANDLE hFind = FindFirstFileA(search_path, &find_data);
    
    if (hFind == INVALID_HANDLE_VALUE) {
        return NULL;
    }
    
    // First pass: count files
    int file_count = 0;
    do {
        if (!(find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
            file_count++;
        }
    } while (FindNextFileA(hFind, &find_data));
    
    if (file_count == 0) {
        FindClose(hFind);
        return NULL;
    }
    
    // Allocate array (+1 for NULL terminator)
    char** files = (char**)malloc((file_count + 1) * sizeof(char*));
    if (!files) {
        FindClose(hFind);
        return NULL;
    }
    
    // Second pass: collect filenames
    hFind = FindFirstFileA(search_path, &find_data);
    int index = 0;
    do {
        if (!(find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
            size_t path_len = strlen(dir_path) + strlen(find_data.cFileName) + 2;
            files[index] = (char*)malloc(path_len);
            if (files[index]) {
                snprintf(files[index], path_len, "%s/%s", dir_path, find_data.cFileName);
                index++;
            }
        }
    } while (FindNextFileA(hFind, &find_data) && index < file_count);
    
    FindClose(hFind);
    
    // Sort alphabetically for consistent ordering
    qsort(files, index, sizeof(char*), _compare_strings);
    
    files[index] = NULL;
    *count = index;
    
    return files;
}
#else
char** l_scan_directory(const char* dir_path, const char* extension, int* count) {
    *count = 0;
    
    DIR* dir = opendir(dir_path);
    if (!dir) {
        return NULL;
    }
    
    size_t ext_len = strlen(extension);
    
    // First pass: count matching files
    int file_count = 0;
    struct dirent* entry;
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_type == DT_REG) {
            size_t name_len = strlen(entry->d_name);
            if (name_len >= ext_len && 
                strcmp(entry->d_name + name_len - ext_len, extension) == 0) {
                file_count++;
            }
        }
    }
    
    if (file_count == 0) {
        closedir(dir);
        return NULL;
    }
    
    // Allocate array (+1 for NULL terminator)
    char** files = (char**)malloc((file_count + 1) * sizeof(char*));
    if (!files) {
        closedir(dir);
        return NULL;
    }
    
    // Second pass: collect filenames
    rewinddir(dir);
    int index = 0;
    while ((entry = readdir(dir)) != NULL && index < file_count) {
        if (entry->d_type == DT_REG) {
            size_t name_len = strlen(entry->d_name);
            if (name_len >= ext_len && 
                strcmp(entry->d_name + name_len - ext_len, extension) == 0) {
                size_t path_len = strlen(dir_path) + strlen(entry->d_name) + 2;
                files[index] = (char*)malloc(path_len);
                if (files[index]) {
                    snprintf(files[index], path_len, "%s/%s", dir_path, entry->d_name);
                    index++;
                }
            }
        }
    }
    
    closedir(dir);
    
    // Sort alphabetically for consistent ordering
    qsort(files, index, sizeof(char*), _compare_strings);
    
    files[index] = NULL;
    *count = index;
    
    return files;
}
#endif

void l_free_file_list(char** files, int count) {
    if (!files) return;
    
    for (int i = 0; i < count; i++) {
        free(files[i]);
    }
    free(files);
}

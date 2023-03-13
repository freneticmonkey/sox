#include <stdio.h>
#include <stdlib.h>

#include "lib/file.h"
#include "serialise.h"
#include "vm.h"

static char* _read_file(const char* path) {
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

int l_run_file(int argc, const char* argv[]) {
    const char* path = argv[1];
    char* source = _read_file(path);

    if ( source == NULL )
        return 74;

    l_init_vm();

    // serialise the interpreted bytecode
    // TODO: once deserialisation is implemented, an existing bytecode file
    // should be checked for before interpreting. Also the hash of the source needs 
    // to be embedded within the bytecode file so that it can be invalidated when the source changes
    
    // setup serialisation
    serialiser_t * serialiser = l_serialise_new(path);

    l_serialise_vm_set_init_state(serialiser);

    // interpret the source
    InterpretResult result = l_interpret(source);
    free(source); 

    if (result == INTERPRET_COMPILE_ERROR) 
            return 65;


    // serialise the vm state
    l_serialise_vm(serialiser);

    // flush to the file and free the serialiser
    // l_serialise_flush(serialiser);
    l_serialise_del(serialiser);

    // run the vm
    result = l_run();

    if (result == INTERPRET_COMPILE_ERROR) 
        return 65;
    if (result == INTERPRET_RUNTIME_ERROR) 
        return 70;

    l_free_vm();

    return 0;
}
#include <stdio.h>
#include <stdlib.h>

#include "lib/file.h"
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

    InterpretResult result = l_interpret(source);
    free(source); 

    if (result == INTERPRET_COMPILE_ERROR) 
        return 65;
    if (result == INTERPRET_RUNTIME_ERROR) 
        return 70;

    return 0;
}
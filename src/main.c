#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "common.h"
#include "chunk.h"
#include "version.h"
#include "vm.h"
#include "lib/debug.h"
#include "lib/file.h"


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
    printf("Starting lox %s ...\ncommit: %s\nbranch: %s\n", 
        VERSION,
        COMMIT,
        BRANCH
    );

    l_init_vm();

    if (argc == 1) {
        _repl();
    } else if (argc == 2) {
        int status = l_run_file(argv[1]);
        if (status != 0)
            exit(status);
    } else {
        fprintf(stderr, "Usage: lox [path]\n");
        exit(64);
    }

    l_free_vm();
    
    printf("Exiting lox ...\n");
    return 0;
}
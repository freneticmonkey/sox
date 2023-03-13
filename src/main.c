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

    if (argc == 1) {
        printf("Starting sox %s ...\ncommit: %s\nbranch: %s\n", 
            VERSION,
            COMMIT,
            BRANCH
        );
        l_init_vm();
        _repl();
        l_free_vm();
    } else if (argc >= 2) {
        if ( argc == 2 && (
            (strncmp(argv[1], "help", 4) == 0) || 
            (strncmp(argv[1], "h", 4) == 0) ) ) {
      
            printf("Sox %s\ncommit: %s\nbranch: %s\nbuild time: %s\n", 
                VERSION,
                COMMIT,
                BRANCH,
                BUILD_TIME
            );
            fprintf(stderr, "Usage: sox [path]\n");
            exit(64);
        }

        int status = l_run_file(argc, argv);
        if (status != 0)
            exit(status);
    }

    
    printf("Exiting sox ...\n");
    return 0;
}
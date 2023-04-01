#ifndef LIB_FILE_H
#define LIB_FILE_H

#include <stdbool.h>

int l_run_file(int argc, const char* argv[]);

bool l_file_exists(const char * path);
bool l_file_delete(const char * path);

#endif
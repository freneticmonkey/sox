#ifndef SOX_COMPILER_H
#define SOX_COMPILER_H

#include "object.h"
#include "vm.h"

#include "chunk.h"

obj_function_t* l_compile(const char* source);

void l_mark_compiler_roots();
#endif
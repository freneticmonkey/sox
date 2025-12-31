#ifndef SOX_COMPILER_H
#define SOX_COMPILER_H

#include "object.h"
#include "vm.h"

#include "chunk.h"

obj_function_t* l_compile(const char* source);
obj_function_t* l_compile_with_options(const char* source, bool skip_main);
obj_function_t* l_compile_module(const char* source);

void l_mark_compiler_roots();
#endif

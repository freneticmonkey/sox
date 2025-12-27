#ifndef SOX_LINKER_TEST_H
#define SOX_LINKER_TEST_H

#include <munit/munit.h>

/* Setup function for linker tests suite */
MunitSuite l_linker_test_setup(void);

/* Exported suite declarations for individual linker test modules */
extern const MunitSuite linker_core_suite;
extern const MunitSuite object_reader_suite;
extern const MunitSuite elf_reader_suite;
extern const MunitSuite macho_reader_test_suite;
extern const MunitSuite symbol_resolver_suite;

/* Get section_layout test suite */
const MunitSuite* get_section_layout_test_suite(void);

/* Get relocation_processor test suite */
const MunitSuite* get_relocation_processor_test_suite(void);

#endif

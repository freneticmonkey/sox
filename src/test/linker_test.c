/*
 * Linker Tests - Combined Suite
 *
 * This module provides a unified test suite for all linker-related tests,
 * combining both linker_core and object_reader test suites.
 */

#include "test/linker_test.h"
#include "../../ext/munit/munit.h"

/* External suite references */
extern const MunitSuite linker_core_suite;
extern const MunitSuite object_reader_suite;
extern const MunitSuite elf_reader_suite;
extern const MunitSuite macho_reader_test_suite;
extern const MunitSuite symbol_resolver_suite;
extern const MunitSuite macho_executable_suite;

/* Get section_layout test suite */
extern const MunitSuite* get_section_layout_test_suite(void);

/* Get relocation_processor test suite */
extern const MunitSuite* get_relocation_processor_test_suite(void);

/* Setup function for the combined linker test suite */
MunitSuite l_linker_test_setup(void) {
    /* Sub-suites array */
    static MunitSuite linker_suites[] = {
        {(char*)"", NULL, NULL, 0, MUNIT_SUITE_OPTION_NONE}, /* Placeholder - will be set below */
        {(char*)"", NULL, NULL, 0, MUNIT_SUITE_OPTION_NONE}, /* Placeholder - will be set below */
        {(char*)"", NULL, NULL, 0, MUNIT_SUITE_OPTION_NONE}, /* Placeholder - will be set below */
        {(char*)"", NULL, NULL, 0, MUNIT_SUITE_OPTION_NONE}, /* Placeholder - will be set below */
        {(char*)"", NULL, NULL, 0, MUNIT_SUITE_OPTION_NONE}, /* Placeholder - will be set below */
        {(char*)"", NULL, NULL, 0, MUNIT_SUITE_OPTION_NONE}, /* Placeholder - will be set below */
        {(char*)"", NULL, NULL, 0, MUNIT_SUITE_OPTION_NONE}, /* Placeholder - will be set below */
        {(char*)"", NULL, NULL, 0, MUNIT_SUITE_OPTION_NONE}, /* Placeholder - will be set below */
        {NULL, NULL, NULL, 0, MUNIT_SUITE_OPTION_NONE}  /* Terminator */
    };

    /* Initialize sub-suites */
    linker_suites[0] = linker_core_suite;
    linker_suites[1] = object_reader_suite;
    linker_suites[2] = elf_reader_suite;
    linker_suites[3] = macho_reader_test_suite;
    linker_suites[4] = symbol_resolver_suite;
    linker_suites[5] = *get_section_layout_test_suite();
    linker_suites[6] = *get_relocation_processor_test_suite();
    linker_suites[7] = macho_executable_suite;

    /* Return combined suite */
    return (MunitSuite) {
        .prefix = (char*)"linker/",
        .tests = NULL,  /* No tests at this level */
        .suites = linker_suites,
        .iterations = 1,
        .options = MUNIT_SUITE_OPTION_NONE
    };
}

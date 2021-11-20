#include "chunk.h"
#include "vm.h"

#include "lib/debug.h"
#include "lib/file.h"

#include "test/scripts_test.h"

static MunitResult _test_nyi(const MunitParameter params[], void *user_data)
{
	(void)params;
	(void)user_data;
	munit_log(MUNIT_LOG_WARNING , "test NYI");
	return MUNIT_FAIL;
}

static MunitResult _run_file(const MunitParameter params[], void *user_data)
{
	(void)user_data;
	
    char* filename = params[0].value;

    l_init_vm();

    munit_logf(MUNIT_LOG_WARNING , "running script: %s", filename);
    int status = l_run_file(filename);

    l_free_vm();

    munit_assert_int(status, == , 0);

	return MUNIT_OK;
}



MunitSuite l_scripts_test_setup() {

    static char* files[] = {
        "src/test/scripts/super.sox",
        "src/test/scripts/classes.sox",
        "src/test/scripts/closure.sox",
        "src/test/scripts/control.sox",
        "src/test/scripts/funcs.sox",
        "src/test/scripts/defer.sox",
        "src/test/scripts/globals.sox",
        NULL,
    };

    static MunitParameterEnum params[] = {
        {"files", files},
        NULL,
    };

    static MunitTest bytecode_suite_tests[] = {
        {
            .name = (char *)"run files", 
            .test = _run_file, 
            .setup = NULL, 
            .tear_down = NULL, 
            .options = MUNIT_TEST_OPTION_NONE,
            .parameters = params,
        },

        // END
        {NULL, NULL, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL}
    };

    return (MunitSuite) {
        .prefix = (char *)"scripts/",
        .tests = bytecode_suite_tests,
        .suites = NULL,
        .iterations = 1,
        .options = MUNIT_SUITE_OPTION_NONE
    };
}

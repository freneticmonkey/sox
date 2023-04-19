#include "chunk.h"
#include "vm.h"
#include "test/vm_test.h"

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

static MunitResult _run_vm(const MunitParameter params[], void *user_data)
{
	(void)user_data;
    
    vm_config_t config = {
        .suppress_print = true
    };

    l_init_memory();
    l_init_vm(config);

    
    l_free_vm();
    l_free_memory();

	return MUNIT_OK;
}

static MunitResult _run_vm_multiple_times(const MunitParameter params[], void *user_data)
{
	(void)user_data;

    vm_config_t config = {
        .suppress_print = true
    };

    l_init_memory();
    l_init_vm(config);
    
    l_free_vm();
    l_free_memory();

    l_init_memory();
    l_init_vm(config);
    
    l_free_vm();
    l_free_memory();

	return MUNIT_OK;
}

MunitSuite l_vm_test_setup() {

    static MunitTest bytecode_suite_tests[] = {
        {
            .name = (char *)"vm_lifecyle", 
            .test = _run_vm, 
            .setup = NULL, 
            .tear_down = NULL, 
            .options = MUNIT_TEST_OPTION_NONE,
            .parameters = NULL,
        },
        {
            .name = (char *)"vm_multiple_lifecyle", 
            .test = _run_vm, 
            .setup = NULL, 
            .tear_down = NULL, 
            .options = MUNIT_TEST_OPTION_NONE,
            .parameters = NULL,
        },

        // END
        {NULL, NULL, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL}
    };

    return (MunitSuite) {
        .prefix = (char *)"vm/",
        .tests = bytecode_suite_tests,
        .suites = NULL,
        .iterations = 1,
        .options = MUNIT_SUITE_OPTION_NONE
    };
}

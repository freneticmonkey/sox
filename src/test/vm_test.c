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
    l_init_vm(&config);

    
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
    l_init_vm(&config);

    l_free_vm();
    l_free_memory();

    l_init_memory();
    l_init_vm(&config);

    l_free_vm();
    l_free_memory();

	return MUNIT_OK;
}

// Test invalid operators
static MunitResult _test_invalid_add(const MunitParameter params[], void *user_data)
{
	(void)params;
	(void)user_data;

    vm_config_t config = {
        .suppress_print = true
    };

    l_init_memory();
    l_init_vm(&config);

    // Test adding incompatible types (e.g., string + number without proper conversion)
    const char* source = "var x = \"hello\" + true;";
    InterpretResult result = l_interpret(source);

    // If compilation succeeded, run the code to catch runtime errors
    if (result == INTERPRET_OK) {
        result = l_run();
    }

    l_free_vm();
    l_free_memory();

    // Expect either compile or runtime error
    munit_assert_true(result == INTERPRET_COMPILE_ERROR || result == INTERPRET_RUNTIME_ERROR);

	return MUNIT_OK;
}

static MunitResult _test_invalid_subtract(const MunitParameter params[], void *user_data)
{
	(void)params;
	(void)user_data;

    vm_config_t config = {
        .suppress_print = true
    };

    l_init_memory();
    l_init_vm(&config);

    // Test subtracting strings
    const char* source = "var x = \"hello\" - \"world\";";
    InterpretResult result = l_interpret(source);

    // If compilation succeeded, run the code to catch runtime errors
    if (result == INTERPRET_OK) {
        result = l_run();
    }

    l_free_vm();
    l_free_memory();

    // Expect either compile or runtime error
    munit_assert_true(result == INTERPRET_COMPILE_ERROR || result == INTERPRET_RUNTIME_ERROR);

	return MUNIT_OK;
}

static MunitResult _test_invalid_multiply(const MunitParameter params[], void *user_data)
{
	(void)params;
	(void)user_data;

    vm_config_t config = {
        .suppress_print = true
    };

    l_init_memory();
    l_init_vm(&config);

    // Test multiplying strings
    const char* source = "var x = \"hello\" * \"world\";";
    InterpretResult result = l_interpret(source);

    // If compilation succeeded, run the code to catch runtime errors
    if (result == INTERPRET_OK) {
        result = l_run();
    }

    l_free_vm();
    l_free_memory();

    // Expect either compile or runtime error
    munit_assert_true(result == INTERPRET_COMPILE_ERROR || result == INTERPRET_RUNTIME_ERROR);

	return MUNIT_OK;
}

static MunitResult _test_invalid_divide(const MunitParameter params[], void *user_data)
{
	(void)params;
	(void)user_data;

    vm_config_t config = {
        .suppress_print = true
    };

    l_init_memory();
    l_init_vm(&config);

    // Test dividing strings
    const char* source = "var x = \"hello\" / \"world\";";
    InterpretResult result = l_interpret(source);

    // If compilation succeeded, run the code to catch runtime errors
    if (result == INTERPRET_OK) {
        result = l_run();
    }

    l_free_vm();
    l_free_memory();

    // Expect either compile or runtime error
    munit_assert_true(result == INTERPRET_COMPILE_ERROR || result == INTERPRET_RUNTIME_ERROR);

	return MUNIT_OK;
}

// Test invalid assignment
static MunitResult _test_invalid_assignment(const MunitParameter params[], void *user_data)
{
	(void)params;
	(void)user_data;

    vm_config_t config = {
        .suppress_print = true
    };

    l_init_memory();
    l_init_vm(&config);

    // Test assigning to an expression (not an lvalue)
    const char* source = "3 * 4 = 12;";
    InterpretResult result = l_interpret(source);

    l_free_vm();
    l_free_memory();

    // Expect compile error
    munit_assert_int(result, ==, INTERPRET_COMPILE_ERROR);

	return MUNIT_OK;
}

// Test duplicate variable definition
static MunitResult _test_duplicate_var_local(const MunitParameter params[], void *user_data)
{
	(void)params;
	(void)user_data;

    vm_config_t config = {
        .suppress_print = true
    };

    l_init_memory();
    l_init_vm(&config);

    // Test duplicate local variable in same scope
    const char* source = "{ var x = 1; var x = 2; }";
    InterpretResult result = l_interpret(source);

    l_free_vm();
    l_free_memory();

    // Expect compile error
    munit_assert_int(result, ==, INTERPRET_COMPILE_ERROR);

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
        {
            .name = (char *)"invalid_add",
            .test = _test_invalid_add,
            .setup = NULL,
            .tear_down = NULL,
            .options = MUNIT_TEST_OPTION_NONE,
            .parameters = NULL,
        },
        {
            .name = (char *)"invalid_subtract",
            .test = _test_invalid_subtract,
            .setup = NULL,
            .tear_down = NULL,
            .options = MUNIT_TEST_OPTION_NONE,
            .parameters = NULL,
        },
        {
            .name = (char *)"invalid_multiply",
            .test = _test_invalid_multiply,
            .setup = NULL,
            .tear_down = NULL,
            .options = MUNIT_TEST_OPTION_NONE,
            .parameters = NULL,
        },
        {
            .name = (char *)"invalid_divide",
            .test = _test_invalid_divide,
            .setup = NULL,
            .tear_down = NULL,
            .options = MUNIT_TEST_OPTION_NONE,
            .parameters = NULL,
        },
        {
            .name = (char *)"invalid_assignment",
            .test = _test_invalid_assignment,
            .setup = NULL,
            .tear_down = NULL,
            .options = MUNIT_TEST_OPTION_NONE,
            .parameters = NULL,
        },
        {
            .name = (char *)"duplicate_var_local",
            .test = _test_duplicate_var_local,
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

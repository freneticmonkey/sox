#include "chunk.h"
#include "vm.h"

#include "lib/debug.h"
#include "lib/file.h"
#include "lib/print.h"

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
    char* output = NULL;

    // if an output capture file exists, then read it and enable output capture
    char filename_capture[256];
    sprintf(&filename_capture[0], "%s.out", filename);

    if (l_file_exists(&filename_capture[0])) {
        output = l_print_enable_capture();
    } else {
        munit_logf(MUNIT_LOG_WARNING , "no output capture file detected: %s. script output will NOT be validated.", filename_capture);
    }

    munit_logf(MUNIT_LOG_INFO , "running script: %s", filename);
    vm_config_t config = {
        .enable_serialisation = false,
        .suppress_print = true,
        .args = l_parse_args(
            2, 
            (const char *[]){
                "sox",
                filename
            }
        )
    };

    int status = l_run_file(&config);

    munit_assert_int(status, == , 0);

    if (output != NULL) {
        char* expected = l_read_file(&filename_capture[0]);
        munit_assert_string_equal(output, expected);

        free(expected);
        free(output);       
    }    

	return MUNIT_OK;
}

MunitSuite l_scripts_test_setup() {

    static char* files[] = {
        "src/test/scripts/argtest.sox",
        "src/test/scripts/array.sox",
        "src/test/scripts/classes.sox",
        "src/test/scripts/closure.sox",
        "src/test/scripts/control.sox",
        "src/test/scripts/defer.sox",
        "src/test/scripts/file_parsing.sox",
        "src/test/scripts/funcs.sox",
        "src/test/scripts/globals.sox",
        "src/test/scripts/hello.sox",
        "src/test/scripts/locals.sox",
        "src/test/scripts/loops.sox",
        "src/test/scripts/main.sox",
        "src/test/scripts/math_ops.sox",
        "src/test/scripts/optional_semi.sox",
        "src/test/scripts/scope_aliasing.sox",
        "src/test/scripts/string_ops.sox",
        "src/test/scripts/super.sox",
        "src/test/scripts/switch.sox",
        "src/test/scripts/syscall.sox",
        "src/test/scripts/table.sox",
        NULL,
    };

    static MunitParameterEnum params[] = {
        {"files", files},
        NULL,
    };

    static MunitTest bytecode_suite_tests[] = {
        {
            .name = (char *)"run_files", 
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

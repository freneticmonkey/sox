#include "chunk.h"
#include "vm.h"
#include "lib/debug.h"

#include "test/bytecode_test.h"

static MunitResult _test_nyi(const MunitParameter params[], void *user_data)
{
	(void)params;
	(void)user_data;
	munit_log(MUNIT_LOG_WARNING , "test NYI");
	return MUNIT_FAIL;
}

static MunitResult _chunk_compare(const MunitParameter params[], void *user_data)
{
	(void)params;
	(void)user_data;
	// munit_log(MUNIT_LOG_WARNING , "test NYI");

    vm_config_t config = {
        .suppress_print = true
    };

    l_init_memory();
    l_init_vm(config);

    chunk_t chunk;
    l_init_chunk(&chunk);

    int constant = l_add_constant(&chunk, NUMBER_VAL(1.2));
    l_write_chunk(&chunk, OP_CONSTANT, 1);
    l_write_chunk(&chunk, constant, 1);
    l_write_chunk(&chunk, OP_RETURN, 1);
    // l_dissassemble_chunk(&chunk, "test chunk");

    munit_assert_int(chunk.count, == , 3);
    munit_assert_int(chunk.capacity, == , 8);
    munit_assert_int(chunk.constants.count, == , 1);

    munit_assert_int(chunk.code[0], == , OP_CONSTANT);
    munit_assert_int(chunk.code[2], == , OP_RETURN);

    l_free_chunk(&chunk);

    l_free_vm();
    l_free_memory();

	return MUNIT_OK;
}

MunitSuite l_bytecode_test_setup() {

    static MunitTest bytecode_suite_tests[] = {
        {
            .name = (char *)"basic_chunk_compare", 
            .test = _chunk_compare, 
            .setup = NULL, 
            .tear_down = NULL, 
            .options = MUNIT_TEST_OPTION_NONE,
            .parameters = NULL
        },

        // END
        {NULL, NULL, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL}
    };

    return (MunitSuite) {
        .prefix = (char *)"bytecode/",
        .tests = bytecode_suite_tests,
        .suites = NULL,
        .iterations = 1,
        .options = MUNIT_SUITE_OPTION_NONE
    };
}

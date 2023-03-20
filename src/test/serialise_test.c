#include "chunk.h"
#include "vm.h"
#include "lib/debug.h"
#include "serialise.h"

#include "test/serialise_test.h"

static MunitResult _test_nyi(const MunitParameter params[], void *user_data)
{
	(void)params;
	(void)user_data;
	munit_log(MUNIT_LOG_WARNING , "test NYI\n");
	return MUNIT_FAIL;
}

static MunitResult _serialise_memory_cleanup(const MunitParameter params[], void *user_data)
{
	(void)params;
	(void)user_data;

	vm_config_t config = {
        .suppress_print = true
    };
    l_init_memory();
    l_init_vm(config);

    printf("\nSerialiser init\n");

    // init the serialiser
    serialiser_t * serialiser = l_serialise_new(NULL, NULL, SERIALISE_MODE_WRITE);

    printf("VM reset\n");
    // now restart the the vm
    l_free_vm();

    l_free_memory(); // <-- WHY IS THIS REQUIRED??
    l_init_memory();

    l_init_vm(config);
    
    printf("Serialiser cleanup\n");
    // cleanup the serialiser
    l_serialise_del(serialiser);

    printf("VM cleanup\n");
    // cleanup the vm
    l_free_vm();
    l_free_memory();

    return MUNIT_OK;
}

// test creating a function with a bytecode chunk, serialising it, deserialising it, and comparing the two
static MunitResult _serialise_function(const MunitParameter params[], void *user_data)
{
    (void)params;
    (void)user_data;
    // munit_log(MUNIT_LOG_WARNING , "test NYI");
    vm_config_t config = {
        .suppress_print = true
    };
    l_init_memory();
    l_init_vm(config);

    obj_function_t * func = l_new_function();

    chunk_t * chunk = &func->chunk;

    int constant = l_add_constant(chunk, NUMBER_VAL(1.2));
    l_write_chunk(chunk, OP_CONSTANT, 1);
    l_write_chunk(chunk, constant, 1);
    l_write_chunk(chunk, OP_RETURN, 1);
    // l_dissassemble_chunk(&chunk, "test chunk");

    munit_assert_int(chunk->count, == , 3);
    munit_assert_int(chunk->capacity, == , 8);
    munit_assert_int(chunk->constants.count, == , 1);

    munit_assert_int(chunk->code[0], == , OP_CONSTANT);
    munit_assert_int(chunk->code[2], == , OP_RETURN);

    // init the serialiser
    serialiser_t * serialiser = l_serialise_new(NULL, NULL, SERIALISE_MODE_WRITE);

    // serialise the function including the chunk
    l_serialise_obj(serialiser, func);

    // rewrind the buffer and start deserialising
    l_serialise_rewind(serialiser);

    // deserialise the chunk
    obj_t * obj = l_deserialise_obj(serialiser);

    obj_function_t * deserialised_func = (obj_function_t *)obj;

    chunk_t * deserialised_chunk = &deserialised_func->chunk;

    // compare the two chunks
    munit_assert_int(chunk->count, == , deserialised_chunk->count);
    munit_assert_int(chunk->capacity, == , deserialised_chunk->capacity);
    munit_assert_int(chunk->constants.count, == , deserialised_chunk->constants.count);

    munit_assert_int(chunk->code[0], == , deserialised_chunk->code[0]);
    munit_assert_int(chunk->code[2], == , deserialised_chunk->code[2]);

    l_serialise_del(serialiser);

    l_free_vm();
    l_free_memory();

    return MUNIT_OK;
}

// test creating a closure containing a function with a bytecode chunk, serialising it, deserialising it, and comparing the two
static MunitResult _serialise_closure(const MunitParameter params[], void *user_data)
{
    (void)params;
    (void)user_data;
    
    vm_config_t config = {
        .suppress_print = true
    };

    l_init_memory();
    l_init_vm(config);

    obj_function_t * func = l_new_function();

    obj_closure_t * closure = l_new_closure(func);

    chunk_t * chunk = &func->chunk;

    int constant = l_add_constant(chunk, NUMBER_VAL(1.2));
    l_write_chunk(chunk, OP_CONSTANT, 1);
    l_write_chunk(chunk, constant, 1);
    l_write_chunk(chunk, OP_RETURN, 1);

    munit_assert_int(chunk->count, == , 3);
    munit_assert_int(chunk->capacity, == , 8);
    munit_assert_int(chunk->constants.count, == , 1);

    munit_assert_int(chunk->code[0], == , OP_CONSTANT);
    munit_assert_int(chunk->code[2], == , OP_RETURN);

    // store local values for the items that we want to compare after the serialisation process
    int count = chunk->count;
    int capacity = chunk->capacity;
    int constants_count = chunk->constants.count;

    uint8_t code0 = chunk->code[0];
    uint8_t code2 = chunk->code[2];

    // init the serialiser
    serialiser_t * serialiser = l_serialise_new(NULL, NULL, SERIALISE_MODE_WRITE);

    // serialise the function including the chunk
    l_serialise_obj(serialiser, func);

    // serialise the closure
    l_serialise_obj(serialiser, closure);

    // now restart the the vm
    l_free_vm();

    // enable memory tracking so that the deserialised objects can be linked
    l_allocate_track_init();

    l_init_vm(config);

    // rewrind the buffer and start deserialising
    l_serialise_rewind(serialiser);

    // deserialise the function
    obj_t * obj = l_deserialise_obj(serialiser);
    obj_function_t * deserialised_func = (obj_function_t *)obj;

    // deserialise the closure
    obj_t * obj2 = l_deserialise_obj(serialiser);
    obj_closure_t * deserialised_closure = (obj_closure_t *)obj2;

    // link the objects
    l_allocate_track_link_targets();

    // free tracking allocations
    l_allocate_track_free();

    // now access the function via the closure
    obj_function_t * closure_func = deserialised_closure->function;

    // access the function's chunk and validate that it passed through the serialisation process correctly
    chunk_t * deserialised_chunk = &closure_func->chunk;

    // compare the two chunks
    munit_assert_int(count, == , deserialised_chunk->count);
    munit_assert_int(capacity, == , deserialised_chunk->capacity);
    munit_assert_int(constants_count, == , deserialised_chunk->constants.count);

    munit_assert_int(code0, == , deserialised_chunk->code[0]);
    munit_assert_int(code2, == , deserialised_chunk->code[2]);

    l_serialise_del(serialiser);

    l_free_vm();
    l_free_memory();

    return MUNIT_OK;
}

MunitSuite l_serialise_test_setup() {

    static MunitTest serialisation_suite_tests[] = {
        {
            .name = (char *)"serialise_memory_cleanup", 
            .test = _serialise_memory_cleanup, 
            .setup = NULL, 
            .tear_down = NULL, 
            .options = MUNIT_TEST_OPTION_NONE,
            .parameters = NULL
        },
        {
            .name = (char *)"serialise_function", 
            .test = _serialise_function, 
            .setup = NULL, 
            .tear_down = NULL, 
            .options = MUNIT_TEST_OPTION_NONE,
            .parameters = NULL
        },
        {
            .name = (char *)"serialise_closure", 
            .test = _serialise_closure, 
            .setup = NULL, 
            .tear_down = NULL, 
            .options = MUNIT_TEST_OPTION_NONE,
            .parameters = NULL
        },

        // END
        {NULL, NULL, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL}
    };

    return (MunitSuite) {
        .prefix = (char *)"serialisation/",
        .tests = serialisation_suite_tests,
        .suites = NULL,
        .iterations = 1,
        .options = MUNIT_SUITE_OPTION_NONE
    };
}

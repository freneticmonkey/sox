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


// This test validates that the memory management is independent of the vm
// the serialiser, and the vm can be initialised and cleaned up multiple times
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

// test creating a class, serialising it, deserialising it, and comparing the names of the class to ensure that 
// the allocation tracking and pointer linking post deserialisation is working
static MunitResult _serialise_test_string_linking(const MunitParameter params[], void *user_data)
{
    (void)params;
    (void)user_data;
    // munit_log(MUNIT_LOG_WARNING , "test NYI");
    vm_config_t config = {
        .suppress_print = true
    };
    l_init_memory();
    l_init_vm(config);

    obj_class_t * class = l_new_class(l_new_string("test_class"));

    munit_assert_string_equal(class->name->chars, "test_class");

    // init the serialiser
    serialiser_t * serialiser = l_serialise_new(NULL, NULL, SERIALISE_MODE_WRITE);    

    // serialise the class
    l_serialise_obj(serialiser, class);

    // serialise the vm string table
    l_serialise_table(serialiser, &vm.strings);

    // now restart the the vm
    l_free_vm();

    // enable memory tracking so that the deserialised objects can be linked
    l_allocate_track_init();

    // re-initialise the vm
    l_init_vm(config);

    // rewrind the buffer and start deserialising
    l_serialise_rewind(serialiser);

    // deserialise the class
    obj_t * obj = l_deserialise_obj(serialiser);
    obj_class_t * deserialised_class = (obj_class_t *)obj;

    // deserialise the VM string table
    l_deserialise_table(serialiser, &vm.strings);

    // link the objects
    l_allocate_track_link_targets();

    // free tracking allocations
    l_allocate_track_free();

    // now check that the class name has been linked correctly, it should not be NULL
    munit_assert_ptr_not_null(deserialised_class->name);

    // test that the class name is correct
    munit_assert_string_equal(deserialised_class->name->chars, "test_class");

    // cleanup the serialiser
    l_serialise_del(serialiser);

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

    munit_assert_int(chunk->count, == , 3);
    munit_assert_int(chunk->capacity, == , 8);
    munit_assert_int(chunk->constants.count, == , 1);

    munit_assert_int(chunk->code[0], == , OP_CONSTANT);
    munit_assert_int(chunk->code[2], == , OP_RETURN);

    // init the serialiser
    serialiser_t * serialiser = l_serialise_new(NULL, NULL, SERIALISE_MODE_WRITE);

    // serialise the function including the chunk
    l_serialise_obj(serialiser, (obj_t*)func);

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

    // test that the closures function is not NULL
    munit_assert_ptr_not_null(closure_func);

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

// test creating a closure containing a function with a bytecode chunk, serialising it, deserialising it, and comparing the two

// test creating a class, and instance of that class, serialising it, deserialising it, and comparing the two
static MunitResult _serialise_instance(const MunitParameter params[], void *user_data)
{
    (void)params;
    (void)user_data;
    
    vm_config_t config = {
        .suppress_print = true
    };

    l_init_memory();
    l_init_vm(config);

    obj_class_t * klass = l_new_class(l_new_string("test_class"));

    obj_instance_t * instance = l_new_instance(klass);

    // init the serialiser
    serialiser_t * serialiser = l_serialise_new(NULL, NULL, SERIALISE_MODE_WRITE);

    // serialise the class
    l_serialise_obj(serialiser, klass);

    // serialise the instance
    l_serialise_obj(serialiser, instance);

    // now restart the the vm
    l_free_vm();

    // enable memory tracking so that the deserialised objects can be linked
    l_allocate_track_init();

    l_init_vm(config);

    // rewrind the buffer and start deserialising
    l_serialise_rewind(serialiser);

    // deserialise the class
    obj_t * obj = l_deserialise_obj(serialiser);
    obj_class_t * deserialised_class = (obj_class_t *)obj;

    // deserialise the instance
    obj_t * obj2 = l_deserialise_obj(serialiser);
    obj_instance_t * deserialised_instance = (obj_instance_t *)obj2;

    // link the objects
    l_allocate_track_link_targets();

    // free tracking allocations
    l_allocate_track_free();

    // compare the two instances
    munit_assert_int(deserialised_instance->fields.count, == , instance->fields.count);

    l_serialise_del(serialiser);

    l_free_vm();
    l_free_memory();

    return MUNIT_OK;
}

// test creating a class, serialising it, deserialising it, and comparing the two
static MunitResult _serialise_class(const MunitParameter params[], void *user_data)
{
    (void)params;
    (void)user_data;
    
    vm_config_t config = {
        .suppress_print = true
    };

    l_init_memory();
    l_init_vm(config);

    obj_class_t * klass = l_new_class(l_new_string("test_class"));

    // init the serialiser
    serialiser_t * serialiser = l_serialise_new(NULL, NULL, SERIALISE_MODE_WRITE);

    // serialise the class
    l_serialise_obj(serialiser, klass);

    // now restart the the vm
    l_free_vm();

    // enable memory tracking so that the deserialised objects can be linked
    l_allocate_track_init();

    l_init_vm(config);

    // rewrind the buffer and start deserialising
    l_serialise_rewind(serialiser);

    // deserialise the class
    obj_t * obj = l_deserialise_obj(serialiser);
    obj_class_t * deserialised_klass = (obj_class_t *)obj;

    // link the objects
    l_allocate_track_link_targets();

    // free tracking allocations
    l_allocate_track_free();

    // compare the two classes
    munit_assert_int(klass->methods.count, == , deserialised_klass->methods.count);
    // munit_assert_int(klass->name, == , deserialised_klass->name);
    munit_assert_memory_equal(klass->name->length, klass->name->chars, deserialised_klass->name->chars);

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
            .name = (char *)"serialise_test_string_linking", 
            .test = _serialise_test_string_linking, 
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
        {
            .name = (char *)"serialise_instance", 
            .test = _serialise_instance, 
            .setup = NULL, 
            .tear_down = NULL, 
            .options = MUNIT_TEST_OPTION_NONE,
            .parameters = NULL
        },
        {
            .name = (char *)"serialise_class", 
            .test = _serialise_class, 
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

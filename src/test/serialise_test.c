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

    // printf("\nSerialiser init\n");

    // init the serialiser
    serialiser_t * serialiser = l_serialise_new(NULL, NULL, SERIALISE_MODE_WRITE);

    // printf("VM reset\n");
    // now restart the the vm
    l_free_vm();

    l_init_vm(config);
    
    // printf("Serialiser cleanup\n");
    // cleanup the serialiser
    l_serialise_del(serialiser);

    // printf("VM cleanup\n");
    // cleanup the vm
    l_free_vm();
    l_free_memory();

    return MUNIT_OK;
}

// test int serialisation and deserialisation
static MunitResult _serialise_int(const MunitParameter params[], void *user_data)
{
    (void)params;
    (void)user_data;

    l_init_memory();

    // init the serialiser
    serialiser_t * serialiser = l_serialise_new(NULL, NULL, SERIALISE_MODE_WRITE);

    // create a very large int 
    int number = 912345678;

    // serialise the number
    l_serialise_int(serialiser, number);

    // rewrind the buffer and start deserialising
    l_serialise_rewind(serialiser);

    // deserialise the number
    int deserialised_number = l_deserialise_int(serialiser);

    // check that the number is the same
    munit_assert_int(number, ==, deserialised_number);

    // cleanup the serialiser
    l_serialise_del(serialiser);

    l_free_memory();

    return MUNIT_OK;
}

// test int array serialisation and deserialisation
static MunitResult _serialise_int_array(const MunitParameter params[], void *user_data)
{
    (void)params;
    (void)user_data;

    l_init_memory();

    // init the serialiser
    serialiser_t * serialiser = l_serialise_new(NULL, NULL, SERIALISE_MODE_WRITE);

    int length = 16;

    // create a int array of length 16
    int array[16] = {
        0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15
    };
    
    // serialise the array
    l_serialise_ints(serialiser, &array[0], length);

    // rewrind the buffer and start deserialising
    l_serialise_rewind(serialiser);

    // deserialise the array
    int * deserialised_array = l_deserialise_ints(serialiser);

    // check that the array is the same
    for (int i = 0; i < length; i++)
    {
        munit_assert_int(array[i], ==, deserialised_array[i]);
    }

    // cleanup the serialiser
    l_serialise_del(serialiser);

    l_free_memory();

    return MUNIT_OK;
}

// test uint8 serialisation and deserialisation
static MunitResult _serialise_uint8(const MunitParameter params[], void *user_data)
{
    (void)params;
    (void)user_data;

    l_init_memory();

    // init the serialiser
    serialiser_t * serialiser = l_serialise_new(NULL, NULL, SERIALISE_MODE_WRITE);

    // create a very large random uint8 
    uint8_t number = 'a';

    // serialise the number
    l_serialise_uint8(serialiser, number);

    // rewrind the buffer and start deserialising
    l_serialise_rewind(serialiser);

    // deserialise the number
    uint8_t deserialised_number = l_deserialise_uint8(serialiser);

    // check that the number is the same
    munit_assert_uint8(number, ==, deserialised_number);

    // cleanup the serialiser
    l_serialise_del(serialiser);

    l_free_memory();

    return MUNIT_OK;
}

// test uint8 array serialisation and deserialisation
static MunitResult _serialise_uint8_array(const MunitParameter params[], void *user_data)
{
    (void)params;
    (void)user_data;

    l_init_memory();

    // init the serialiser
    serialiser_t * serialiser = l_serialise_new(NULL, NULL, SERIALISE_MODE_WRITE);

    int length = 16;

    // create a uint8 array of length 16
    uint8_t array[16] = {
        0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15
    };
    
    // serialise the array
    l_serialise_uint8s(serialiser, &array[0], length);

    // rewrind the buffer and start deserialising
    l_serialise_rewind(serialiser);

    // deserialise the array
    uint8_t * deserialised_array = l_deserialise_uint8s(serialiser);

    // check that the array is the same
    for (int i = 0; i < length; i++)
    {
        munit_assert_uint8(array[i], ==, deserialised_array[i]);
    }

    // cleanup the serialiser
    l_serialise_del(serialiser);

    l_free_memory();

    return MUNIT_OK;
}

// test uint32 serialisation and deserialisation
static MunitResult _serialise_uint32(const MunitParameter params[], void *user_data)
{
    (void)params;
    (void)user_data;

    l_init_memory();

    // init the serialiser
    serialiser_t * serialiser = l_serialise_new(NULL, NULL, SERIALISE_MODE_WRITE);

    // create a very large uint32 
    uint32_t number = 912345678;

    // serialise the number
    l_serialise_uint32(serialiser, number);

    // rewrind the buffer and start deserialising
    l_serialise_rewind(serialiser);

    // deserialise the number
    uint32_t deserialised_number = l_deserialise_uint32(serialiser);

    // check that the number is the same
    munit_assert_uint32(number, ==, deserialised_number);

    // cleanup the serialiser
    l_serialise_del(serialiser);

    l_free_memory();

    return MUNIT_OK;
}

// test uintptr serialisation and deserialisation
static MunitResult _serialise_uintptr(const MunitParameter params[], void *user_data)
{
    (void)params;
    (void)user_data;

    l_init_memory();

    // init the serialiser
    serialiser_t * serialiser = l_serialise_new(NULL, NULL, SERIALISE_MODE_WRITE);

    // create a very large uintptr 
    uintptr_t number = 91234567890;

    // serialise the number
    l_serialise_uintptr(serialiser, number);

    // rewrind the buffer and start deserialising
    l_serialise_rewind(serialiser);

    // deserialise the number
    uintptr_t deserialised_number = l_deserialise_uintptr(serialiser);

    // check that the number is the same
    munit_assert_size(number, ==, deserialised_number);

    // cleanup the serialiser
    l_serialise_del(serialiser);

    l_free_memory();

    return MUNIT_OK;
}

// test long serialisation and deserialisation
static MunitResult _serialise_long(const MunitParameter params[], void *user_data)
{
    (void)params;
    (void)user_data;

    l_init_memory();

    // init the serialiser
    serialiser_t * serialiser = l_serialise_new(NULL, NULL, SERIALISE_MODE_WRITE);

    // create a very large random size_t 
    size_t size = 91234567890;

    // serialise the size
    l_serialise_long(serialiser, size);

    // rewrind the buffer and start deserialising
    l_serialise_rewind(serialiser);

    // deserialise the size
    size_t deserialised_size = l_deserialise_long(serialiser);

    // check that the size is the same
    munit_assert_size(size, ==, deserialised_size);

    // cleanup the serialiser
    l_serialise_del(serialiser);

    l_free_memory();

    return MUNIT_OK;
}

// test double serialisation and deserialisation
static MunitResult _serialise_double(const MunitParameter params[], void *user_data)
{
    (void)params;
    (void)user_data;

    l_init_memory();

    // init the serialiser
    serialiser_t * serialiser = l_serialise_new(NULL, NULL, SERIALISE_MODE_WRITE);

    // create a random double 
    double number = 91234567890.123456789;

    // serialise the number
    l_serialise_double(serialiser, number);

    // rewrind the buffer and start deserialising
    l_serialise_rewind(serialiser);

    // deserialise the number
    double deserialised_number = l_deserialise_double(serialiser);

    // check that the number is the same
    munit_assert_double(number, ==, deserialised_number);

    // cleanup the serialiser
    l_serialise_del(serialiser);

    l_free_memory();

    return MUNIT_OK;
}

// test bool serialisation and deserialisation
static MunitResult _serialise_bool(const MunitParameter params[], void *user_data)
{
    (void)params;
    (void)user_data;

    l_init_memory();

    // init the serialiser
    serialiser_t * serialiser = l_serialise_new(NULL, NULL, SERIALISE_MODE_WRITE);

    // create a random bool 
    bool boolean = true;

    // serialise the bool
    l_serialise_bool(serialiser, boolean);

    // rewrind the buffer and start deserialising
    l_serialise_rewind(serialiser);

    // deserialise the bool
    bool deserialised_bool = l_deserialise_bool(serialiser);

    // check that the bool is the same
    munit_assert_true(deserialised_bool);

    // cleanup the serialiser
    l_serialise_del(serialiser);

    l_free_memory();

    return MUNIT_OK;
}

// test char * serialisation and deserialisation
static MunitResult _serialise_char(const MunitParameter params[], void *user_data)
{
    (void)params;
    (void)user_data;

    l_init_memory();

    // init the serialiser
    serialiser_t * serialiser = l_serialise_new(NULL, NULL, SERIALISE_MODE_WRITE);

    // create a random char * 
    char * string = "test string";

    // serialise the string
    l_serialise_char(serialiser, string);

    // rewrind the buffer and start deserialising
    l_serialise_rewind(serialiser);

    // deserialise the string
    char * deserialised_string = l_deserialise_char(serialiser);

    // check that the string is the same
    munit_assert_string_equal(string, deserialised_string);

    // cleanup the serialiser
    l_serialise_del(serialiser);

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
    l_serialise_obj(serialiser, (obj_t*)class);

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
    func->name = l_new_string("test_func");

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
    
    // serialise the vm string table
    l_serialise_table(serialiser, &vm.strings);

    // now restart the the vm
    l_free_vm();
    l_init_vm(config);

    // rewrind the buffer and start deserialising
    l_serialise_rewind(serialiser);

    // deserialise the chunk
    obj_t * obj = l_deserialise_obj(serialiser);

    // deserialise the VM string table
    l_deserialise_table(serialiser, &vm.strings);

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
    func->name = l_new_string("test_func");

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
    l_serialise_obj(serialiser, (obj_t*)func);

    // serialise the closure
    l_serialise_obj(serialiser, (obj_t*)closure);
    
    // serialise the vm string table
    l_serialise_table(serialiser, &vm.strings);

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

    // deserialise the VM string table
    l_deserialise_table(serialiser, &vm.strings);

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

    // verify that the function was deserialised correctly

    // test that the function name is not NULL
    munit_assert_ptr_not_null(deserialised_func->name);

    munit_assert_memory_equal(func->name->length, func->name->chars, deserialised_func->name->chars);

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
    l_serialise_obj(serialiser, (obj_t*)klass);

    // serialise the instance
    l_serialise_obj(serialiser, (obj_t*)instance);
    
    // serialise the vm string table
    l_serialise_table(serialiser, &vm.strings);

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

    // deserialise the VM string table
    l_deserialise_table(serialiser, &vm.strings);

    // link the objects
    l_allocate_track_link_targets();

    // free tracking allocations
    l_allocate_track_free();

    // compare the two instances
    munit_assert_int(deserialised_instance->fields.count, == , instance->fields.count);

    // munit_assert_int(klass->name, == , deserialised_klass->name);
    munit_assert_memory_equal(klass->name->length, klass->name->chars, deserialised_class->name->chars);

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
    l_serialise_obj(serialiser, (obj_t*)klass);
    
    // serialise the vm string table
    l_serialise_table(serialiser, &vm.strings);

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

    // deserialise the VM string table
    l_deserialise_table(serialiser, &vm.strings);

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

// test creating a table, serialising it, deserialising it, and comparing the two
static MunitResult _serialise_table(const MunitParameter params[], void *user_data)
{
    (void)params;
    (void)user_data;
    
    vm_config_t config = {
        .suppress_print = true
    };

    l_init_memory();
    l_init_vm(config);



    // init the serialiser
    serialiser_t * serialiser = l_serialise_new(NULL, NULL, SERIALISE_MODE_WRITE);

    // serialise the vm string table
    l_serialise_table(serialiser, &vm.strings);

    // now restart the the vm
    l_free_vm();

    // enable memory tracking so that the deserialised objects can be linked
    l_allocate_track_init();

    l_init_vm(config);

    // rewrind the buffer and start deserialising
    l_serialise_rewind(serialiser);

    // deserialise the VM string table
    l_deserialise_table(serialiser, &vm.strings);

    // link the objects
    l_allocate_track_link_targets();

    // free tracking allocations
    l_allocate_track_free();

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
            .name = (char *)"serialise_int", 
            .test = _serialise_int, 
            .setup = NULL, 
            .tear_down = NULL, 
            .options = MUNIT_TEST_OPTION_NONE,
            .parameters = NULL
        },
        {
            .name = (char *)"serialise_int_array", 
            .test = _serialise_int_array, 
            .setup = NULL, 
            .tear_down = NULL, 
            .options = MUNIT_TEST_OPTION_NONE,
            .parameters = NULL
        },
        {
            .name = (char *)"serialise_uint8", 
            .test = _serialise_uint8, 
            .setup = NULL, 
            .tear_down = NULL, 
            .options = MUNIT_TEST_OPTION_NONE,
            .parameters = NULL
        },
        {
            .name = (char *)"serialise_uint8_array", 
            .test = _serialise_uint8_array, 
            .setup = NULL, 
            .tear_down = NULL, 
            .options = MUNIT_TEST_OPTION_NONE,
            .parameters = NULL
        },
        {
            .name = (char *)"serialise_uint32", 
            .test = _serialise_uint32, 
            .setup = NULL, 
            .tear_down = NULL, 
            .options = MUNIT_TEST_OPTION_NONE,
            .parameters = NULL
        },
        {
            .name = (char *)"serialise_uintptr", 
            .test = _serialise_uintptr, 
            .setup = NULL, 
            .tear_down = NULL, 
            .options = MUNIT_TEST_OPTION_NONE,
            .parameters = NULL
        },
        {
            .name = (char *)"serialise_long", 
            .test = _serialise_long, 
            .setup = NULL, 
            .tear_down = NULL, 
            .options = MUNIT_TEST_OPTION_NONE,
            .parameters = NULL
        },
        {
            .name = (char *)"serialise_double", 
            .test = _serialise_double, 
            .setup = NULL, 
            .tear_down = NULL, 
            .options = MUNIT_TEST_OPTION_NONE,
            .parameters = NULL
        },
        {
            .name = (char *)"serialise_bool", 
            .test = _serialise_bool, 
            .setup = NULL, 
            .tear_down = NULL, 
            .options = MUNIT_TEST_OPTION_NONE,
            .parameters = NULL
        },
        {
            .name = (char *)"serialise_char", 
            .test = _serialise_char, 
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
        {
            .name = (char *)"serialise_table", 
            .test = _serialise_table, 
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

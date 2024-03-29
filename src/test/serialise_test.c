#include "chunk.h"
#include "vm.h"
#include "serialise.h"
#include "lib/debug.h"
#include "lib/file.h"
#include "lib/print.h"
#include "lib/string.h"

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
    l_init_vm(&config);

    // printf("\nSerialiser init\n");

    // init the serialiser
    serialiser_t * serialiser = l_serialise_new(NULL, NULL, SERIALISE_MODE_WRITE);

    // printf("VM reset\n");
    // now restart the the vm
    l_free_vm();

    l_init_vm(&config);
    
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
    l_init_vm(&config);

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
    
    // cleanup the tracking memory
    l_allocate_track_free();

    // enable memory tracking so that the deserialised objects can be linked
    l_allocate_track_init();

    // re-initialise the vm
    l_init_vm(&config);

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
    l_init_vm(&config);

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
    l_init_vm(&config);

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
    l_init_vm(&config);

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
    
    // cleanup the tracking memory
    l_allocate_track_free();

    // enable memory tracking so that the deserialised objects can be linked
    l_allocate_track_init();

    l_init_vm(&config);

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
    l_init_vm(&config);

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

    l_init_vm(&config);

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
    l_init_vm(&config);

    obj_class_t * klass = l_new_class(l_new_string("test_class"));

    // init the serialiser
    serialiser_t * serialiser = l_serialise_new(NULL, NULL, SERIALISE_MODE_WRITE);

    // serialise the class
    l_serialise_obj(serialiser, (obj_t*)klass);
    
    // serialise the vm string table
    l_serialise_table(serialiser, &vm.strings);

    // now restart the the vm
    l_free_vm();
    
    // cleanup the tracking memory
    l_allocate_track_free();

    // enable memory tracking so that the deserialised objects can be linked
    l_allocate_track_init();

    l_init_vm(&config);

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
    l_init_vm(&config);

    // init the serialiser
    serialiser_t * serialiser = l_serialise_new(NULL, NULL, SERIALISE_MODE_WRITE);

    // serialise the vm string table
    l_serialise_table(serialiser, &vm.strings);

    // now restart the the vm
    l_free_vm();
    
    // cleanup the tracking memory
    l_allocate_track_free();

    // enable memory tracking so that the deserialised objects can be linked
    l_allocate_track_init();

    l_init_vm(&config);

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

// test creating a table, insert a string value, serialising it, deserialising it, and test that the table value is correct
static MunitResult _serialise_table_pointer(const MunitParameter params[], void *user_data)
{
    (void)params;
    (void)user_data;
    
    vm_config_t config = {
        .suppress_print = true
    };

    l_init_memory();
    l_init_vm(&config);

    table_t table;
    l_init_table(&table);

    // create a string key and value to insert into the table
    obj_string_t * key = l_new_string("test_key");
    obj_string_t * str_value = l_new_string("test_value");

    value_t value = OBJ_VAL(str_value);

    // insert the key and value into the table
    l_table_set(&table, key, value);
    
    // init the serialiser
    serialiser_t * serialiser = l_serialise_new(NULL, NULL, SERIALISE_MODE_WRITE);

    // serialise the vm
    l_serialise_vm(serialiser);

    // serialise the test table
    l_serialise_table(serialiser, &table);

    // now restart the the vm
    l_free_vm();
    
    // cleanup the tracking memory
    l_allocate_track_free();

    // enable memory tracking so that the deserialised objects can be linked
    l_allocate_track_init();

    l_init_vm(&config);

    // rewrind the buffer and start deserialising
    l_serialise_rewind(serialiser);

    // deserialise the VM
    l_deserialise_vm(serialiser);

    // now deserialise the test table
    table_t deserialised_table;
    l_init_table(&deserialised_table);

    // deserialise the VM
    l_deserialise_table(serialiser, &deserialised_table);

    // link the objects
    l_allocate_track_link_targets();

    // free tracking allocations
    l_allocate_track_free();


    // now get the value from the deserialised table
    // it is important to use a newly allocated string here so that
    // is is pointing to the correct value in the strings table.
    // if it is not, then the table lookup fails as it uses pointer values
    // and the 'key' pointer created at the start of this test will not be
    // in the new VM's strings table, and therefore be unknown to the table
    obj_string_t * find_key = l_new_string("test_key");
    
    value_t d_value;
    bool found = l_table_get(&deserialised_table, find_key, &d_value);

    munit_assert_true(found);
    munit_assert_int(d_value.type, == , VAL_OBJ);
    munit_assert_int(d_value.as.obj->type, == , OBJ_STRING);

    obj_string_t * d_str = (obj_string_t *)d_value.as.obj;
    munit_assert_not_null(d_str->chars);
    munit_assert_int(d_str->length, == , str_value->length);
    munit_assert_memory_equal(d_str->length, d_str->chars, str_value->chars);

    l_serialise_del(serialiser);

    l_free_vm();
    l_free_memory();

    return MUNIT_OK;
}

// test creating a vm, interpreting source, serialising it, deserialising it, running the VM and testing for an error
static MunitResult _serialise_vm(const MunitParameter params[], void *user_data)
{
    (void)params;
    (void)user_data;
    
    const char * source = "print(\"hello world\")";    
    
    vm_config_t config = {
        .suppress_print = false
    };

    l_init_memory();
    l_init_vm(&config);

    // init the serialiser
    serialiser_t * serialiser = l_serialise_new(NULL, NULL, SERIALISE_MODE_WRITE);

    // setup the VM serialisation
    l_serialise_vm_set_init_state(serialiser);

    // interpret the source
    InterpretResult result = l_interpret(source);
    
    if (result == INTERPRET_COMPILE_ERROR) 
        return 65;

    // serialise the vm
    l_serialise_vm(serialiser);

    // now restart the the vm
    l_free_vm();
    
    // cleanup the tracking memory
    l_allocate_track_free();

    // enable memory tracking so that the deserialised objects can be linked
    l_allocate_track_init();

    l_init_vm(&config);

    // rewrind the buffer and start deserialising
    l_serialise_rewind(serialiser);

    // deserialise the VM
    obj_closure_t * closure = l_deserialise_vm(serialiser);

    // link the objects
    l_allocate_track_link_targets();

    // free tracking allocations
    l_allocate_track_free();

    // set the deserialised closure as the entry point in the VM
    l_deserialise_vm_set_init_state(serialiser, closure);

    // run the deserialised VM
    const char * args[] = {""};
    result = l_run(0, args);

    munit_assert_int(result, != , INTERPRET_RUNTIME_ERROR);

    l_serialise_del(serialiser);

    l_free_vm();
    l_free_memory();

    return MUNIT_OK;
}

// run each of the test scripts through serialisation and execute the deserialised VM
static MunitResult _serialise_run_files(const MunitParameter params[], void *fixture)
{
	int count = 0;

    // get the filename of the bytecode file, if we need it
    char * filename_bytecode = (char *)fixture;

    char* filename = params[0].value;

    char* output = NULL;

    // if an output capture file exists, then read it and enable output capture
    char filename_capture[256];
    sprintf(&filename_capture[0], "%s.out", filename);

    if (l_file_exists(&filename_capture[0])) {
        output = l_print_enable_capture();
    }

    munit_logf(MUNIT_LOG_WARNING , "running script: %s", filename);
    
    vm_config_t config = {
        .enable_serialisation = true,
        .suppress_print = true,
        .args = l_parse_args(
            2, 
            (const char *[]){
                "sox",
                filename,
            }
        )
    };
    int status = l_run_file(
        &config
    );


    if (status != 0) {
        munit_logf(MUNIT_LOG_WARNING , "failed to run script: %s", filename);
    }

    munit_assert_int(status, == , 0);

    if (output != NULL) {
        char* expected = l_read_file(&filename_capture[0]);

        size_t output_hash = l_hash_string(output, strlen(output));
        size_t expected_hash = l_hash_string(expected, strlen(expected));

        if (output_hash != expected_hash) {
            munit_logf(MUNIT_LOG_WARNING , "file: %s", filename);
            munit_logf(MUNIT_LOG_WARNING , "output: [%s] expected: [%s]", output, expected);
        }
        
        munit_assert_llong(strlen(output), ==, strlen(expected));

        munit_assert_llong(output_hash, == , expected_hash);
        munit_assert_string_equal(output, expected);

        free(expected);
        free(output);       
    }  
    
    count++;
        

	return MUNIT_OK;
}

static void * _serialise_run_files_setup(const MunitParameter params[], void * user_data) {

    char* filename = params[0].value;
    char filename_bytecode[256];

    // cleanup serialisation file first
    sprintf(&filename_bytecode[0], "%s.sbc", filename);
    if ( l_file_exists(&filename_bytecode[0]) ) {
        bool result = l_file_delete(&filename_bytecode[0]);
        munit_assert_true(result);
    }

    // allocate a temporary char * for the serialisation filename and return it
    char * filename_serialise = malloc(sizeof(char) * 256);
    sprintf(filename_serialise, "%s.sbc", filename);

    return filename_serialise;
}

static void _serialise_run_files_tear_down(void * fixture) {

    char * filename_bytecode = (char *)fixture;
    
    if (filename_bytecode != NULL) {

        // cleanup serialisation file
        if ( l_file_exists(&filename_bytecode[0]) ) {
            bool result = l_file_delete(&filename_bytecode[0]);
            munit_assert_true(result);
        }
        free(filename_bytecode);
    } else {
        munit_log(MUNIT_LOG_WARNING , "filename_bytecode is NULL");
    }
}

MunitSuite l_serialise_test_setup() {

    static char* files[] = {
        "src/test/scripts/argtest.sox",
        "src/test/scripts/array.sox",
        "src/test/scripts/classes.sox",
        "src/test/scripts/closure.sox",
        "src/test/scripts/control.sox",
        "src/test/scripts/defer.sox",
        "src/test/scripts/funcs.sox",
        "src/test/scripts/globals.sox",
        "src/test/scripts/hello.sox",
        "src/test/scripts/loops.sox",
        "src/test/scripts/main.sox",
        "src/test/scripts/optional_semi.sox",
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
        {
            .name = (char *)"serialise_table_pointer",
            .test = _serialise_table_pointer,
            .setup = NULL, 
            .tear_down = NULL, 
            .options = MUNIT_TEST_OPTION_NONE,
            .parameters = NULL
        },
        {
            .name = (char *)"serialise_vm",
            .test = _serialise_vm,
            .setup = NULL, 
            .tear_down = NULL, 
            .options = MUNIT_TEST_OPTION_NONE,
            .parameters = NULL
        },
        {
            .name = (char *)"serialise_run_files",
            .test = _serialise_run_files,
            .setup = _serialise_run_files_setup, 
            .tear_down = _serialise_run_files_tear_down, 
            .options = MUNIT_TEST_OPTION_NONE,
            .parameters = params,
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
